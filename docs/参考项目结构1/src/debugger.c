#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

/* ============================================================
 * debugger.c — 交互式调试器 (REPL)
 *
 * ─── 你需要实现的内容 ─────────────────────────────────────
 *
 * 1. debugger_run(dbg) — 主 REPL 循环
 *    循环读取用户命令 → 解析 → 执行
 *
 * 2. 支持的命令 (及缩写)：
 *
 *   ┌──────────────────────────────────────────────────────────┐
 *   │ 命令                      │ 说明                        │
 *   ├──────────────────────────────────────────────────────────┤
 *   │ break <addr>    / b <addr>│ 在指定地址设置断点          │
 *   │ delete <id>     / d <id>  │ 删除指定编号的断点          │
 *   │ info break      / i b     │ 列出所有断点               │
 *   │ step            / s       │ 单步执行一条指令            │
 *   │ stepi <n>       / si <n>  │ 执行 n 条指令               │
 *   │ continue        / c       │ 继续运行直到断点/退出       │
 *   │ info registers  / i r     │ 显示所有寄存器              │
 *   │ x/<N><F><U> <addr>        │ 查看内存 (examine)         │
 *   │ print <expr>    / p <expr>│ 计算表达式 (如 $x10+4)      │
 *   │ backtrace       / bt      │ 打印调用栈 (可选)          │
 *   │ disassemble <a> / disas   │ 反汇编 (可选)              │
 *   │ help            / h       │ 显示帮助                    │
 *   │ quit            / q       │ 退出                       │
 *   └──────────────────────────────────────────────────────────┘
 *
 * 3. 断点实现：
 *    两种实现方式 (任选其一)：
 *    a) 软件断点: 将断点地址的指令替换为 EBREAK (0x00100073)
 *       保存原始指令，命中后恢复。继续执行时：
 *       恢复原始指令 → 单步 → 重新插入 EBREAK → 继续
 *    b) 地址比较: 在 cpu_step() 循环中比较 PC 和断点列表
 *       (更简单但稍慢)
 *    推荐使用方案 a) 因为符合真实硬件行为
 *
 * 4. x (examine) 命令格式: x/[N][F][U] <addr>
 *    N = 显示数量 (默认 1)
 *    F = 格式: x(hex), d(dec), u(unsigned), o(oct), t(bin)
 *    U = 单位: b(byte), h(halfword), w(word)
 *    示例: x/8xw 0x80000000 → 从 0x80000000 开始显示 8 个字(hex)
 *          x/16xb $sp    → 从 sp 开始显示 16 个字节(hex)
 *
 * 5. 地址解析：
 *    - 十六进制: 0x80000000 / 0x8000_0000
 *    - 寄存器引用: $x10 / $a0 / $sp / $pc
 *    - 支持简单表达式: $x10 + 0x100
 *
 * 6. 命令解析器设计：
 *    使用简单的 strtok 分词，第一个 token 为命令名，
 *    后续 token 为参数。支持缩写匹配 (前缀唯一匹配)
 *
 * ─── 交互示例 ──────────────────────────────────────────────
 *   (riscv-dbg) b 0x80000100
 *   Breakpoint 1 set at 0x80000100
 *   (riscv-dbg) i r
 *   x0  (zero) = 0x00000000    x1  (ra)   = 0x80000080
 *   ...
 *   (riscv-dbg) c
 *   Continuing...
 *   Hit breakpoint 1 at 0x80000100
 *   (riscv-dbg) x/4xw 0x80000000
 *   0x80000000: 0x02a00513  0x00000593  0x00b50533  0x00100073
 *   (riscv-dbg) q
 * ============================================================
 */

#include "types.h"
#include "memory.h"
#include "mmu.h"
#include "debugger.h"

/* ── 外部声明 ────────────────────────────────────────────── */
extern bool cpu_step(CPUState *cpu, PhysicalMemory *pmem, MMUState *mmu);
extern bool g_trace_enabled;

#define MAX_CMD_LEN 256

/* ── 寄存器 ABI 名称 (调试器用) ──────────────────────────── */
static const char *reg_names[] = {
    "zero", "ra", "sp", "gp", "tp",
    "t0", "t1", "t2",
    "s0", "s1",
    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
    "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11",
    "t3", "t4", "t5", "t6"
};

/* ── 初始化 ──────────────────────────────────────────────── */
void debugger_init(DebuggerState *dbg, CPUState *cpu,
                   PhysicalMemory *pmem, MMUState *mmu)
{
    memset(dbg, 0, sizeof(*dbg));
    dbg->cpu     = cpu;
    dbg->pmem    = pmem;
    dbg->mmu     = mmu;
    dbg->running = true;
    dbg->next_bp_id = 1;
}

/* ── 断点管理 ────────────────────────────────────────────── */

int debugger_add_breakpoint(DebuggerState *dbg, uint32_t addr)
{
    if (dbg->bp_count >= MAX_BREAKPOINTS) {
        fprintf(stderr, "Error: Too many breakpoints\n");
        return -1;
    }
    Breakpoint *bp = &dbg->breakpoints[dbg->bp_count++];
    bp->id        = dbg->next_bp_id++;
    bp->addr      = addr;
    bp->enabled   = true;
    // TODO: 读取并保存原始指令 (用于软件断点恢复)
    // mmu_read_32(dbg->mmu, dbg->pmem, addr, &bp->original_insn, dbg->cpu->priv);
    // 将断点处指令替换为 EBREAK
    // mmu_write_32(dbg->mmu, dbg->pmem, addr, 0x00100073, dbg->cpu->priv);
    bp->original_insn = 0;
    return bp->id;
}

bool debugger_del_breakpoint(DebuggerState *dbg, int id)
{
    for (int i = 0; i < dbg->bp_count; i++) {
        if (dbg->breakpoints[i].id == id) {
            // TODO: 恢复原始指令
            // mmu_write_32(dbg->mmu, dbg->pmem, dbg->breakpoints[i].addr,
            //              dbg->breakpoints[i].original_insn, dbg->cpu->priv);
            // 移除：将末尾元素移到当前位置
            dbg->breakpoints[i] = dbg->breakpoints[--dbg->bp_count];
            return true;
        }
    }
    return false;
}

void debugger_list_breakpoints(const DebuggerState *dbg)
{
    printf("Num\tType\t\tEnb\tAddress\n");
    for (int i = 0; i < dbg->bp_count; i++) {
        printf("%d\tbreakpoint\ty\t0x%08x\n",
               dbg->breakpoints[i].id, dbg->breakpoints[i].addr);
    }
}

bool debugger_check_breakpoint(DebuggerState *dbg, uint32_t pc)
{
    // TODO: 简单比较实现
    for (int i = 0; i < dbg->bp_count; i++) {
        if (dbg->breakpoints[i].enabled && dbg->breakpoints[i].addr == pc) {
            return true;
        }
    }
    return false;
}

/* ── 单步执行 ────────────────────────────────────────────── */
void debugger_step(DebuggerState *dbg)
{
    cpu_step(dbg->cpu, dbg->pmem, dbg->mmu);
}

/* ── 继续执行 ────────────────────────────────────────────── */
void debugger_continue(DebuggerState *dbg)
{
    printf("Continuing...\n");
    while (dbg->cpu->running) {
        cpu_step(dbg->cpu, dbg->pmem, dbg->mmu);
        // 检查断点
        if (debugger_check_breakpoint(dbg, dbg->cpu->pc)) {
            printf("Hit breakpoint at 0x%08x\n", dbg->cpu->pc);
            break;
        }
    }
}

/* ── 寄存器打印 ──────────────────────────────────────────── */
void debugger_print_registers(const CPUState *cpu)
{
    cpu_dump_regs(cpu);
}

/* ── 内存查看 ────────────────────────────────────────────── */
void debugger_examine_memory(PhysicalMemory *pmem, MMUState *mmu,
                             uint32_t addr, int count,
                             char format, char unit)
{
    // TODO: 解析 format (x/d/u) 和 unit (b/h/w)
    // 默认: format='x', unit='w' (显示字)
    if (unit == 0) unit = 'w';
    if (format == 0) format = 'x';

    for (int i = 0; i < count; i++) {
        uint32_t vaddr = addr + i * 4;  // 默认字偏移
        uint32_t val = 0;
        if (mmu_read_32(mmu, pmem, vaddr, &val, PRIV_MACHINE)) {
            printf("0x%08x:\t0x%08x\n", vaddr, val);
        }
    }
}

/* ── 地址解析 (支持 $reg 语法) ──────────────────────────── */
static bool parse_addr(const char *str, CPUState *cpu, uint32_t *addr)
{
    if (str[0] == '$') {
        // 寄存器引用: $x10, $a0, $sp, $pc
        str++;
        if (strcmp(str, "pc") == 0) { *addr = cpu->pc; return true; }
        // 尝试 ABI 名称匹配
        for (int i = 0; i < REG_COUNT; i++) {
            if (strcmp(str, reg_names[i]) == 0) {
                *addr = cpu->regs[i];
                return true;
            }
        }
        // 尝试 xN 格式
        if (str[0] == 'x' || str[0] == 'X') {
            int n = atoi(str + 1);
            if (n >= 0 && n < REG_COUNT) {
                *addr = cpu->regs[n];
                return true;
            }
        }
        return false;
    }
    // 十六进制
    *addr = (uint32_t)strtoul(str, NULL, 0);
    return true;
}

/* ── 主 REPL ─────────────────────────────────────────────── */
void debugger_run(DebuggerState *dbg)
{
    char line[MAX_CMD_LEN];
    char *argv[8];

    printf("RISC-V Simulator Debugger\n");
    printf("Type 'help' for a list of commands.\n\n");

    /* 初始打印寄存器和下一条指令 */
    debugger_print_registers(dbg->cpu);

    while (dbg->running) {
        printf("(riscv-dbg) ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;

        // 分词 (空格分割，最多 7 个参数)
        int argc = 0;
        char *tok = strtok(line, " \t\r\n");
        while (tok && argc < 8) {
            argv[argc++] = tok;
            tok = strtok(NULL, " \t\r\n");
        }
        if (argc == 0) continue;

        // 命令分发 (支持缩写)
        const char *cmd = argv[0];

        if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
            printf("Exiting...\n");
            break;
        }
        else if (strcmp(cmd, "h") == 0 || strcmp(cmd, "help") == 0) {
            printf("Commands:\n");
            printf("  b/reak <addr>        Set breakpoint\n");
            printf("  d/elete <id>         Delete breakpoint\n");
            printf("  i/b nfo break        List breakpoints\n");
            printf("  s/tep                Single step\n");
            printf("  si/stepi [n]         Step n instructions\n");
            printf("  c/ontinue            Continue execution\n");
            printf("  i/r nfo registers    Show registers\n");
            printf("  x/<N><F><U> <addr>   Examine memory\n");
            printf("  q/uit                Exit\n");
        }
        else if ((strcmp(cmd, "b") == 0 || strcmp(cmd, "break") == 0)
                 && argc >= 2) {
            uint32_t addr;
            if (parse_addr(argv[1], dbg->cpu, &addr)) {
                int id = debugger_add_breakpoint(dbg, addr);
                if (id > 0)
                    printf("Breakpoint %d set at 0x%08x\n", id, addr);
            }
        }
        else if ((strcmp(cmd, "d") == 0 || strcmp(cmd, "delete") == 0)
                 && argc >= 2) {
            int id = atoi(argv[1]);
            if (debugger_del_breakpoint(dbg, id))
                printf("Breakpoint %d deleted\n", id);
            else
                printf("No breakpoint with id %d\n", id);
        }
        else if (strcmp(cmd, "i") == 0 || strcmp(cmd, "info") == 0) {
            if (argc >= 2) {
                if (strcmp(argv[1], "r") == 0 || strcmp(argv[1], "registers") == 0) {
                    debugger_print_registers(dbg->cpu);
                } else if (strcmp(argv[1], "b") == 0 || strcmp(argv[1], "break") == 0) {
                    debugger_list_breakpoints(dbg);
                }
            }
        }
        else if (strcmp(cmd, "s") == 0 || strcmp(cmd, "step") == 0) {
            debugger_step(dbg);
            debugger_print_registers(dbg->cpu);
        }
        else if (strcmp(cmd, "si") == 0 || strcmp(cmd, "stepi") == 0) {
            int n = (argc >= 2) ? atoi(argv[1]) : 1;
            for (int i = 0; i < n && dbg->cpu->running; i++) {
                debugger_step(dbg);
            }
            debugger_print_registers(dbg->cpu);
        }
        else if (strcmp(cmd, "c") == 0 || strcmp(cmd, "continue") == 0) {
            debugger_continue(dbg);
            debugger_print_registers(dbg->cpu);
        }
        else if (strcmp(cmd, "x") == 0 && argc >= 2) {
            // x/<N><F><U> addr
            // 简单版本: x addr
            uint32_t addr;
            if (parse_addr(argv[1], dbg->cpu, &addr)) {
                debugger_examine_memory(dbg->pmem, dbg->mmu, addr,
                                        16, 'x', 'w');
            }
        }
        else {
            printf("Unknown command: %s (type 'help')\n", cmd);
        }
    }
}
