#include "debugger/debugger.h"
#include "debugger_internal.h"
#include "simulator.h"       // Simulator, Breakpoint, sim_step, STACK_BASE, STACK_TOP
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <inttypes.h>

/* ── 测试模式：暴露 static 函数 ─────────────────────────────────── */
#ifdef DEBUGGER_TEST
#define STATIC_OR_EXTERN
#else
#define STATIC_OR_EXTERN static
#endif

/* ================================================================
 * debugger.c — 交互式调试器 REPL 实现
 *
 * 提示符: (rvsim)
 * 命令缩写: 全匹配或首字母 (b→break, c→continue, s→step, q→quit)
 * ================================================================
 */

#define MAX_CMD_LEN  256
#define MAX_ARGS     8
/* EBREAK_INSTR 定义在 breakpoint.c:22，此处通过 debugger_internal.h 共享 */

/* ── 寄存器 ABI 名称表 (x0-x31) ──────────────────────────────── */
static const char *reg_abi_names[] = {
    "zero", "ra",  "sp",  "gp",  "tp",
    "t0",   "t1",  "t2",
    "s0",   "s1",
    "a0",   "a1",  "a2",  "a3",  "a4",  "a5",  "a6",  "a7",
    "s2",   "s3",  "s4",  "s5",  "s6",  "s7",  "s8",  "s9",
    "s10",  "s11",
    "t3",   "t4",  "t5",  "t6"
};

/* ── 寄存器名称解析: 返回索引 (0-31, 32=pc), 失败 -1 ────────── */
STATIC_OR_EXTERN int parse_reg_name(const char *name)
{
    if (name[0] == '$') name++;
    if (strcmp(name, "pc") == 0) return 32;

    for (int i = 0; i < 32; i++)
        if (strcmp(name, reg_abi_names[i]) == 0) return i;

    if (name[0] == 'x' || name[0] == 'X') {
        int n = atoi(name + 1);
        if (n >= 0 && n <= 31) return n;
    }

    char *end;
    long n = strtol(name, &end, 10);
    if (*end == '\0' && n >= 0 && n <= 31) return (int)n;

    return -1;
}

/* ── 地址解析: hex / dec / $reg / $reg+offset ────────────────── */
STATIC_OR_EXTERN bool parse_addr(const char *str, struct Simulator *sim, uint32_t *addr)
{
    if (!str || !*str) return false;

    if (str[0] == '$') {
        int reg = parse_reg_name(str);
        if (reg >= 0 && reg <= 31) { *addr = sim->cpu.regs[reg]; return true; }
        if (reg == 32) { *addr = sim->cpu.pc; return true; }

        char reg_part[32] = {0}, op;
        int offset = 0;
        if (sscanf(str, "$%31[^ +-] %c %i", reg_part, &op, &offset) == 3) {
            reg = parse_reg_name(reg_part);
            if (reg < 0 || reg > 32) return false;
            uint32_t base = (reg == 32) ? sim->cpu.pc : sim->cpu.regs[reg];
            *addr = (op == '+') ? base + (uint32_t)offset : base - (uint32_t)offset;
            return true;
        }
        return false;
    }

    char *end;
    *addr = (uint32_t)strtoul(str, &end, 0);
    return (*end == '\0');
}

STATIC_OR_EXTERN uint32_t get_reg_value(const struct Simulator *sim, int idx)
{
    if (idx == 32) return sim->cpu.pc;
    if (idx >= 0 && idx <= 31) return sim->cpu.regs[idx];
    return 0;
}

/* ================================================================
 * 状态查看
 * ================================================================ */

void debugger_print_registers(const struct Simulator *sim)
{
    printf("Registers:\n");
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 4; col++) {
            int r = row + col * 8;
            if (r >= 32) break;
            printf("  x%-2d (%4s) = 0x%08x", r, reg_abi_names[r], sim->cpu.regs[r]);
            if (col < 3) printf("  ");
        }
        printf("\n");
    }
    printf("  PC          = 0x%08x\n", sim->cpu.pc);
    printf("  mstatus     = 0x%08x\n", sim->cpu.mstatus);
    printf("  mtvec       = 0x%08x\n", sim->cpu.mtvec);
    printf("  mepc        = 0x%08x\n", sim->cpu.mepc);
    printf("  mcause      = 0x%08x\n", sim->cpu.mcause);
    printf("  mtval       = 0x%08x\n", sim->cpu.mtval);
}

void debugger_examine_memory(struct Simulator *sim, uint32_t vaddr,
                             int count, char format, char unit)
{
    if (count  <= 0) count  = 16;
    if (format == 0) format = 'x';
    if (unit   == 0) unit   = 'w';

    int unit_size;
    switch (unit) {
        case 'b': unit_size = 1; break;
        case 'h': unit_size = 2; break;
        case 'w': unit_size = 4; break;
        default:  fprintf(stderr, "Unknown unit size '%c'\n", unit); return;
    }

    int items_per_line = (unit_size == 1) ? 16 : (unit_size == 2) ? 8 : 4;

    for (int i = 0; i < count; i++) {
        if (i % items_per_line == 0)
            printf("0x%08x: ", vaddr + i * unit_size);

        ExceptionType exc = EXC_NONE;
        uint32_t val = 0;

        switch (unit) {
            case 'b': { uint8_t b; mmu_read_8(&sim->mmu, &sim->pmem, vaddr + i, &b, sim->cpu.priv, &exc); val = b; break; }
            case 'h': { uint16_t h; mmu_read_16(&sim->mmu, &sim->pmem, vaddr + i * 2, &h, sim->cpu.priv, &exc); val = h; break; }
            default:  mmu_read_32(&sim->mmu, &sim->pmem, vaddr + i * 4, &val, sim->cpu.priv, &exc); break;
        }

        switch (format) {
            case 'd': printf("%-11d",  (int32_t)val);  break;
            case 'u': printf("%-11u",  val);           break;
            case 'o': printf("%-11o",  val);           break;
            default:  printf("0x%0*x  ", unit_size * 2, val); break;
        }

        if ((i + 1) % items_per_line == 0 || i == count - 1) printf("\n");
    }
}

void debugger_print_backtrace(struct Simulator *sim)
{
    uint32_t fp = sim->cpu.regs[8];
    uint32_t pc = sim->cpu.pc;

    printf("Backtrace:\n");
    printf("#0  PC = 0x%08x", pc);

    {
        ExceptionType exc = EXC_NONE;
        uint32_t instr;
        if (mmu_read_32(&sim->mmu, &sim->pmem, pc, &instr, sim->cpu.priv, &exc)) {
            char disasm[128];
            cpu_disasm(instr, pc, disasm, sizeof(disasm));
            printf("  <%s>\n", disasm);
        } else { printf("\n"); }
    }

    int depth = 0;
    while (fp != 0
           && fp >= STACK_BASE && fp < STACK_TOP
           && depth < 64)
    {
        ExceptionType exc = EXC_NONE;
        uint32_t ra, prev_fp;
        if (!mmu_read_32(&sim->mmu, &sim->pmem, fp + 4, &ra, sim->cpu.priv, &exc)) break;
        if (!mmu_read_32(&sim->mmu, &sim->pmem, fp, &prev_fp, sim->cpu.priv, &exc)) break;

        depth++;
        printf("#%d  fp=0x%08x  ra=0x%08x", depth, fp, ra);

        if (ra >= 4) {
            uint32_t call_instr;
            if (mmu_read_32(&sim->mmu, &sim->pmem, ra - 4, &call_instr, sim->cpu.priv, &exc)) {
                char disasm[128];
                cpu_disasm(call_instr, ra - 4, disasm, sizeof(disasm));
                printf("  <%s>\n", disasm);
            } else { printf("\n"); }
        } else { printf("\n"); }
        fp = prev_fp;
    }
}

/* ================================================================
 * 执行控制
 * ================================================================ */

void debugger_step(struct Simulator *sim)
{
    uint32_t pc_before = sim->cpu.pc;
    bool at_bp = false;
    int bp_i = -1;

    /* ── 流水线模式 ────────────────────────────────────────
     *
     * 断点恢复必须先于 sim_step：若 CPU 已停止且 mepc 命中某个
     * 断点（说明上一次是因断点 trap 停下的），需要先恢复原始指令、
     * 推进到该指令完成（WB）、再重新插入 ebreak，然后返回停止状态。
     *
     * 正常情况：每个 Step = 1 个时钟周期。
     */
    if (sim->cpu_model == MODEL_PIPELINE) {

        /* ── ① 先检查是否停在断点处（恢复后再 step）───────── */
        if (!sim->cpu.running) {
            uint32_t trapped_pc = sim->cpu.mepc;
            for (int i = 0; i < sim->bp_count; i++) {
                if (sim->breakpoints[i].enabled &&
                    sim->breakpoints[i].addr == trapped_pc) {
                    bp_i = i;
                    at_bp = true;
                    break;
                }
            }
            if (at_bp) {
                /* 恢复原始指令 → 推进到指令完成 → 重新插入 ebreak */
                ExceptionType exc = EXC_NONE;
                mmu_write_32(&sim->mmu, &sim->pmem,
                             sim->breakpoints[bp_i].addr,
                             sim->breakpoints[bp_i].original_instr,
                             sim->cpu.priv, &exc);
                sim->cpu.running = true;
                uint64_t instr_before = sim->instr_count;
                int max_cycles = 100;
                while (sim->cpu.running && sim->instr_count == instr_before
                       && max_cycles-- > 0) {
                    sim_step(sim);
                }
                if (max_cycles < 0) {
                    fprintf(stderr, "Warning: breakpoint recovery exceeded cycle limit\n");
                    sim->cpu.running = false;
                }
                mmu_write_32(&sim->mmu, &sim->pmem,
                             sim->breakpoints[bp_i].addr,
                             EBREAK_INSTR, sim->cpu.priv, &exc);
                sim->cpu.running = false;
            }
        }

        /* ── ② 正常推进 1 个时钟周期 ─────────────────────── */
        if (!at_bp) {
            if (!sim->cpu.running) {
                sim->cpu.running = true;
            }
            sim_step(sim);
        }

        /* ── ③ 反汇编显示 ────────────────────────────────── */
        {
            ExceptionType exc2 = EXC_NONE;
            uint32_t instr;
            char disasm[128] = {0};
            uint32_t show_pc = at_bp ? sim->breakpoints[bp_i].addr : pc_before;
            if (mmu_read_32(&sim->mmu, &sim->pmem, show_pc, &instr,
                            sim->cpu.priv, &exc2))
                cpu_disasm(instr, show_pc, disasm, sizeof(disasm));
            printf("0x%08x: %08x  %s [pipeline, 1 cycle]\n",
                   show_pc, instr, disasm);
        }
        return;
    }

    /* ── 单周期 & 多周期模式 ────────────────────────────────
     *
     * 确保 CPU 在运行状态：sim_step() 入口检查 !cpu->running
     * 时直接返回，因此各调用方必须预先设置 running=true。
     */

    if (!sim->cpu.running) {
        sim->cpu.running = true;
    }

    for (int i = 0; i < sim->bp_count; i++) {
        if (sim->breakpoints[i].enabled && sim->breakpoints[i].addr == sim->cpu.pc) {
            at_bp = true; bp_i = i; break;
        }
    }

    if (at_bp) {
        /* 多周期优化：仅在 MC_IF（新指令开始）恢复原始指令，
         * 中间 FSM 状态保持原始指令在内存中（不会取指到该地址）。 */
        bool is_mc_start = (sim->cpu_model == MODEL_MULTI_CYCLE)
                           ? (sim->mc.state == 0 /* MC_IF */) : true;
        if (is_mc_start || sim->cpu_model == MODEL_SINGLE_CYCLE) {
            ExceptionType exc = EXC_NONE;
            mmu_write_32(&sim->mmu, &sim->pmem, sim->breakpoints[bp_i].addr,
                         sim->breakpoints[bp_i].original_instr,
                         sim->cpu.priv, &exc);
        }
    } else {
        sim->single_step = true;
    }

    uint64_t instr_before = sim->instr_count;
    sim_step(sim);

    if (at_bp) {
        /* 多周期：仅在指令完成（instr_count 递增，状态回到 MC_IF）后重新插入 ebreak */
        bool instr_done = (sim->cpu_model == MODEL_MULTI_CYCLE)
                          ? (sim->instr_count > instr_before)
                          : true;
        if (instr_done) {
            ExceptionType exc = EXC_NONE;
            mmu_write_32(&sim->mmu, &sim->pmem, sim->breakpoints[bp_i].addr,
                         EBREAK_INSTR, sim->cpu.priv, &exc);
        }
    }

    {
        ExceptionType exc = EXC_NONE;
        uint32_t instr;
        char disasm[128] = {0};
        if (mmu_read_32(&sim->mmu, &sim->pmem, pc_before, &instr, sim->cpu.priv, &exc))
            cpu_disasm(instr, pc_before, disasm, sizeof(disasm));
        printf("0x%08x: %08x  %s\n", pc_before, instr, disasm);
    }
}

void debugger_continue(struct Simulator *sim)
{
    bool at_bp = false;
    int bp_i = -1;
    for (int i = 0; i < sim->bp_count; i++) {
        if (sim->breakpoints[i].enabled && sim->breakpoints[i].addr == sim->cpu.pc) {
            at_bp = true; bp_i = i; break;
        }
    }

    /* ── 流水线模式：特殊断点处理 ────────────────────────────
     *
     * 如果停在断点处（cpu.running=false, pipeline empty），
     * 需要先恢复原始指令 + 推进到指令完成，再重新插入 ebreak。
     */
    if (at_bp && sim->cpu_model == MODEL_PIPELINE) {
        ExceptionType exc = EXC_NONE;
        /* 恢复原始指令 */
        mmu_write_32(&sim->mmu, &sim->pmem, sim->breakpoints[bp_i].addr,
                     sim->breakpoints[bp_i].original_instr,
                     sim->cpu.priv, &exc);
        /* 推进直到该指令完成（到达 WB），最多 100 周期 */
        sim->cpu.running = true;
        uint64_t instr_before = sim->instr_count;
        int max_cycles_c = 100;
        while (sim->cpu.running && sim->instr_count == instr_before
               && max_cycles_c-- > 0) {
            sim_step(sim);
        }
        /* 重新插入 ebreak */
        mmu_write_32(&sim->mmu, &sim->pmem, sim->breakpoints[bp_i].addr,
                     EBREAK_INSTR, sim->cpu.priv, &exc);
    } else if (at_bp) {
        /* 单周期/多周期：恢复 → 执行整条指令 → 重新插入 */
        ExceptionType exc = EXC_NONE;
        mmu_write_32(&sim->mmu, &sim->pmem, sim->breakpoints[bp_i].addr,
                     sim->breakpoints[bp_i].original_instr,
                     sim->cpu.priv, &exc);
        /* 确保 CPU 在运行状态（同 debugger_step 的修正） */
        sim->cpu.running = true;
        sim_step(sim);
        /* 多周期：持续推进直到断点处指令完整执行，最多 100 周期 */
        if (sim->cpu_model == MODEL_MULTI_CYCLE) {
            uint64_t instr_before_bp = sim->instr_count;
            int max_cycles_mc = 100;
            while (sim->cpu.running && sim->instr_count == instr_before_bp
                   && max_cycles_mc-- > 0) {
                sim_step(sim);
            }
        }
        mmu_write_32(&sim->mmu, &sim->pmem, sim->breakpoints[bp_i].addr,
                     EBREAK_INSTR, sim->cpu.priv, &exc);
    }

    printf("Continuing...\n");
    sim->cpu.running = true;
    sim->single_step = false;
    while (sim->cpu.running) {
        sim_step(sim);
        if (sim->single_step) { sim->single_step = false; break; }
    }
    if (!sim->cpu.running) {
        /* 流水线模式下断点命中后 PC 被设为 mtvec，实际地址在 mepc */
        uint32_t bp_addr = (sim->cpu_model == MODEL_PIPELINE)
                           ? sim->cpu.mepc : sim->cpu.pc;
        printf("Hit breakpoint at 0x%08x\n", bp_addr);
    }
}

/* ================================================================
 * REPL 主循环
 * ================================================================ */

void debugger_run(struct Simulator *sim)
{
    char line[MAX_CMD_LEN];
    char *argv[MAX_ARGS];

    sim->debug_mode = true;
    printf("RISC-V Simulator Debugger\n");
    printf("Loaded program. Type 'help' for a list of commands.\n\n");
    debugger_print_registers(sim);

    while (sim->debug_mode) {
        printf("\n(rvsim) ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) { printf("\n"); break; }

        int argc = 0;
        char *tok = strtok(line, " \t\r\n");
        while (tok && argc < MAX_ARGS) { argv[argc++] = tok; tok = strtok(NULL, " \t\r\n"); }
        if (argc == 0) continue;

        const char *cmd = argv[0];

        /* help */
        if (strcmp(cmd, "h") == 0 || strcmp(cmd, "help") == 0) {
            printf("Commands:\n");
            printf("  b/break  <addr>       Set breakpoint\n");
            printf("  d/delete <n>          Delete breakpoint\n");
            printf("  info breakpoints      List all breakpoints\n");
            printf("  info registers        Show all registers + PC\n");
            printf("  s/step / si/stepi     Single step\n");
            printf("  si/stepi <n>          Step n instructions\n");
            printf("  c/continue            Continue until breakpoint or exit\n");
            printf("  x/<N><F><U> <addr>    Examine memory\n");
            printf("  p/print <reg>         Print register value\n");
            printf("  bt/backtrace          Print call stack\n");
            printf("  disas [addr]          Disassemble\n");
            printf("  q/quit                Quit simulator\n");
        }
        /* quit */
        else if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
            for (int i = 0; i < sim->bp_count; i++) {
                if (sim->breakpoints[i].enabled) {
                    ExceptionType exc = EXC_NONE;
                    mmu_write_32(&sim->mmu, &sim->pmem, sim->breakpoints[i].addr,
                                 sim->breakpoints[i].original_instr, sim->cpu.priv, &exc);
                }
            }
            printf("Exiting...\n");
            sim->debug_mode = false;
            break;
        }
        /* break */
        else if ((strcmp(cmd, "b") == 0 || strcmp(cmd, "break") == 0) && argc >= 2) {
            uint32_t addr;
            if (parse_addr(argv[1], sim, &addr))
                debugger_add_breakpoint(sim, addr);
            else
                fprintf(stderr, "Error: Invalid address '%s'\n", argv[1]);
        }
        /* delete */
        else if ((strcmp(cmd, "d") == 0 || strcmp(cmd, "delete") == 0) && argc >= 2) {
            debugger_del_breakpoint(sim, atoi(argv[1]));
        }
        /* info */
        else if (strcmp(cmd, "i") == 0 || strcmp(cmd, "info") == 0) {
            if (argc >= 2) {
                if (strcmp(argv[1], "r") == 0 || strcmp(argv[1], "registers") == 0)
                    debugger_print_registers(sim);
                else if (strcmp(argv[1], "b") == 0 || strcmp(argv[1], "break") == 0
                         || strcmp(argv[1], "breakpoints") == 0)
                    debugger_list_breakpoints(sim);
                else
                    fprintf(stderr, "Unknown info subcommand '%s'\n", argv[1]);
            } else {
                fprintf(stderr, "Usage: info <registers|breakpoints>\n");
            }
        }
        /* step / stepi */
        else if (strcmp(cmd, "s") == 0 || strcmp(cmd, "step") == 0) {
            debugger_step(sim);
            debugger_print_registers(sim);
        }
        else if (strcmp(cmd, "si") == 0 || strcmp(cmd, "stepi") == 0) {
            int n = (argc >= 2) ? atoi(argv[1]) : 1;
            if (n <= 0) n = 1;
            for (int i = 0; i < n && sim->cpu.running; i++) debugger_step(sim);
            debugger_print_registers(sim);
        }
        /* continue */
        else if (strcmp(cmd, "c") == 0 || strcmp(cmd, "continue") == 0) {
            debugger_continue(sim);
            debugger_print_registers(sim);
        }
        /* print */
        else if (strcmp(cmd, "p") == 0 || strcmp(cmd, "print") == 0) {
            if (argc >= 2) {
                int reg = parse_reg_name(argv[1]);
                if (reg >= 0) {
                    uint32_t val = get_reg_value(sim, reg);
                    const char *name = (reg == 32) ? "pc" : reg_abi_names[reg];
                    printf("  $%s = 0x%08x (%d)\n", name, val, val);
                } else {
                    fprintf(stderr, "Unknown register '%s'\n", argv[1]);
                }
            } else {
                fprintf(stderr, "Usage: print <reg>\n");
            }
        }
        /* x (examine memory) */
        else if (strcmp(cmd, "x") == 0) {
            if (argc < 2) { fprintf(stderr, "Usage: x/<N><F><U> <addr>\n"); continue; }

            int count = 16; char format = 'x', unit = 'w'; char *addr_str = NULL;

            if (argv[1][0] == '/') {
                const char *p = argv[1] + 1;
                if (isdigit((unsigned char)*p)) { count = atoi(p); while (isdigit((unsigned char)*p)) p++; }
                if (*p && strchr("xduotc", *p)) { format = *p; p++; }
                if (*p && strchr("bhw", *p))   { unit = *p;   p++; }
                if (argc >= 3) addr_str = argv[2];
            } else {
                addr_str = argv[1];
            }

            if (addr_str) {
                uint32_t addr;
                if (parse_addr(addr_str, sim, &addr))
                    debugger_examine_memory(sim, addr, count, format, unit);
                else
                    fprintf(stderr, "Error: Invalid address '%s'\n", addr_str);
            } else {
                fprintf(stderr, "Error: Missing address\n");
            }
        }
        /* backtrace */
        else if (strcmp(cmd, "bt") == 0 || strcmp(cmd, "backtrace") == 0) {
            debugger_print_backtrace(sim);
        }
        /* disassemble */
        else if (strcmp(cmd, "disas") == 0 || strcmp(cmd, "disassemble") == 0) {
            uint32_t addr = sim->cpu.pc;
            if (argc >= 2 && !parse_addr(argv[1], sim, &addr))
                { fprintf(stderr, "Error: Invalid address\n"); continue; }
            for (int i = 0; i < 8; i++) {
                ExceptionType exc = EXC_NONE;
                uint32_t instr;
                if (!mmu_read_32(&sim->mmu, &sim->pmem, addr + i * 4, &instr, sim->cpu.priv, &exc)) break;
                char disasm[128];
                cpu_disasm(instr, addr + i * 4, disasm, sizeof(disasm));
                printf("  0x%08x: %08x  %s\n", addr + i * 4, instr, disasm);
            }
        }
        /* unknown */
        else {
            printf("Unknown command: %s (type 'help')\n", cmd);
        }
    }
    sim->debug_mode = false;
}
