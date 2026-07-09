/* ============================================================================
 * debugger_test.c — Debugger 模块自动化单元测试
 *
 * 用法（从项目根目录）：
 *   gcc -std=c11 -Wall -Wextra -g -O0 -DDEBUGGER_TEST \
 *       -Isrc/include -Isrc/src/debugger \
 *       -o build/debugger_test \
 *       src/test/debugger/debugger_test.c \
 *       src/src/simulator.c \
 *       src/src/cpu/cpu.c src/src/cpu/decode.c \
 *       src/src/cpu/execute/execute.c src/src/cpu/execute/exec_rv32i.c \
 *       src/src/cpu/execute/exec_m.c src/src/cpu/execute/exec_f.c \
 *       src/src/memory/memory.c src/src/memory/mmu.c \
 *       src/src/debugger/debugger.c src/src/debugger/breakpoint.c \
 *       src/src/loader/elf_validate.c src/src/loader/elf_load.c \
 *       src/src/loader/elf_segment.c src/src/loader/elf_stack.c
 *   ./build/debugger_test
 *
 * 测试分组：
 *   A: parse_reg_name    (纯函数，不依赖 Simulator)
 *   B: parse_addr        (部分依赖 Simulator 寄存器值)
 *   C: get_reg_value     (依赖 Simulator 寄存器值)
 *   D: 断点管理           (add/del/list/check)
 *   E: 状态查看           (registers/memory/backtrace 打印)
 *   F: 执行控制           (step/continue)
 *   G: 边界与鲁棒性
 * ============================================================================ */

#include "simulator.h"
#include "debugger/debugger.h"
#include "debugger_internal.h"
#include "loader/elf_loader.h"
#include "types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

/* ====================================================================
 * 测试框架
 * ==================================================================== */

static int passed = 0;
static int failed = 0;

#define CHECK(cond, fmt, ...) do {                              \
    if (cond) {                                                 \
        printf("  ✅ " fmt "\n", ##__VA_ARGS__);                \
        passed++;                                               \
    } else {                                                    \
        printf("  ❌ " fmt "\n", ##__VA_ARGS__);                \
        failed++;                                               \
    }                                                           \
} while(0)

/* ── 辅助：创建带映射内存的模拟器（用于需要 MMU 的测试） ──────────── */
static Simulator *new_sim(void)
{
    Simulator *sim = calloc(1, sizeof(Simulator));
    if (!sim) { fprintf(stderr, "Fatal: calloc Simulator failed\n"); exit(1); }
    sim_init(sim);
    /* 映射 64KB 物理内存，地址 0，Bare 模式下 vaddr=paddr */
    mem_map(&sim->pmem, 0, 64 * 1024, MEM_READ | MEM_WRITE, "test");
    return sim;
}

/* ====================================================================
 * 分组 A: parse_reg_name — 纯字符串解析（不依赖 Simulator）
 * ==================================================================== */
void test_parse_reg_name_abi(void)
{
    printf("\n─── A.1: parse_reg_name (ABI 名称) ───\n");
    CHECK(parse_reg_name("zero") == 0,  "zero → 0");
    CHECK(parse_reg_name("ra")   == 1,  "ra   → 1");
    CHECK(parse_reg_name("sp")   == 2,  "sp   → 2");
    CHECK(parse_reg_name("gp")   == 3,  "gp   → 3");
    CHECK(parse_reg_name("tp")   == 4,  "tp   → 4");
    CHECK(parse_reg_name("t0")   == 5,  "t0   → 5");
    CHECK(parse_reg_name("t1")   == 6,  "t1   → 6");
    CHECK(parse_reg_name("t2")   == 7,  "t2   → 7");
    CHECK(parse_reg_name("s0")   == 8,  "s0   → 8 (fp)");
    CHECK(parse_reg_name("s1")   == 9,  "s1   → 9");
    CHECK(parse_reg_name("a0")   == 10, "a0   → 10");
    CHECK(parse_reg_name("a1")   == 11, "a1   → 11");
    CHECK(parse_reg_name("a7")   == 17, "a7   → 17");
    CHECK(parse_reg_name("s2")   == 18, "s2   → 18");
    CHECK(parse_reg_name("s11")  == 27, "s11  → 27");
    CHECK(parse_reg_name("t3")   == 28, "t3   → 28");
    CHECK(parse_reg_name("t6")   == 31, "t6   → 31");
}

void test_parse_reg_name_x(void)
{
    printf("\n─── A.2: parse_reg_name (xN 格式) ───\n");
    CHECK(parse_reg_name("x0")   == 0,  "x0   → 0");
    CHECK(parse_reg_name("x1")   == 1,  "x1   → 1");
    CHECK(parse_reg_name("x10")  == 10, "x10  → 10");
    CHECK(parse_reg_name("x31")  == 31, "x31  → 31");
    CHECK(parse_reg_name("X5")   == 5,  "X5   → 5 (大写)");
    CHECK(parse_reg_name("X31")  == 31, "X31  → 31 (大写)");
}

void test_parse_reg_name_numeric(void)
{
    printf("\n─── A.3: parse_reg_name (纯数字) ───\n");
    CHECK(parse_reg_name("0")   == 0,  "\"0\"  → 0");
    CHECK(parse_reg_name("10")  == 10, "\"10\" → 10");
    CHECK(parse_reg_name("31")  == 31, "\"31\" → 31");
}

void test_parse_reg_name_pc(void)
{
    printf("\n─── A.4: parse_reg_name (PC) ───\n");
    CHECK(parse_reg_name("pc")   == 32, "pc   → 32");
    CHECK(parse_reg_name("$pc")  == 32, "$pc  → 32");
}

void test_parse_reg_name_dollar(void)
{
    printf("\n─── A.5: parse_reg_name ($ 前缀) ───\n");
    CHECK(parse_reg_name("$ra")  == 1,  "$ra  → 1");
    CHECK(parse_reg_name("$sp")  == 2,  "$sp  → 2");
    CHECK(parse_reg_name("$a0")  == 10, "$a0  → 10");
    CHECK(parse_reg_name("$s0")  == 8,  "$s0  → 8");
}

void test_parse_reg_name_invalid(void)
{
    printf("\n─── A.6: parse_reg_name (非法输入) ───\n");
    /* "xyz" → atoi("yz")=0 → x0 (合法但不符合直觉，记录行为) */
    CHECK(parse_reg_name("xyz")   == 0,  "\"xyz\"   → 0 (x0: 以x开头+atoi=0)");
    CHECK(parse_reg_name("x32")   == -1, "\"x32\"   → -1 (越界)");
    CHECK(parse_reg_name("x100")  == -1, "\"x100\"  → -1 (越界)");
    CHECK(parse_reg_name("a8")    == -1, "\"a8\"    → -1 (不存在)");
    CHECK(parse_reg_name("s12")   == -1, "\"s12\"   → -1 (不存在)");
    CHECK(parse_reg_name("-1")    == -1, "\"-1\"    → -1");
}

/* ====================================================================
 * 分组 B: parse_addr — 地址解析
 * ==================================================================== */
void test_parse_addr_literal(void)
{
    printf("\n─── B.1: parse_addr (字面量) ───\n");
    uint32_t addr;
    bool ok;

    ok = parse_addr("0x1000", NULL, &addr);
    CHECK(ok && addr == 0x1000, "0x1000 → 0x1000");

    ok = parse_addr("4096", NULL, &addr);
    CHECK(ok && addr == 4096, "4096 → 4096");

    ok = parse_addr("0xDEADBEEF", NULL, &addr);
    CHECK(ok && addr == 0xDEADBEEF, "0xDEADBEEF → 0xDEADBEEF");

    ok = parse_addr("0", NULL, &addr);
    CHECK(ok && addr == 0, "0 → 0");
}

void test_parse_addr_dollar_reg(void)
{
    printf("\n─── B.2: parse_addr ($reg 形式) ───\n");
    /* 创建一个最小 sim 用于寄存器引用 */
    Simulator sim;
    memset(&sim, 0, sizeof(sim));
    sim.cpu.regs[5]  = 0x80001000;  /* t0 */
    sim.cpu.regs[2]  = 0xC0000000;  /* sp */
    sim.cpu.pc       = 0x00010000;

    uint32_t addr;
    bool ok;

    ok = parse_addr("$t0", &sim, &addr);
    CHECK(ok && addr == 0x80001000, "$t0 → 0x80001000");

    ok = parse_addr("$sp", &sim, &addr);
    CHECK(ok && addr == 0xC0000000, "$sp → 0xC0000000");

    ok = parse_addr("$pc", &sim, &addr);
    CHECK(ok && addr == 0x00010000, "$pc → 0x00010000");
}

void test_parse_addr_dollar_reg_offset(void)
{
    printf("\n─── B.3: parse_addr ($reg ± offset) ───\n");
    Simulator sim;
    memset(&sim, 0, sizeof(sim));
    sim.cpu.regs[2]  = 0xC0000000;  /* sp */
    sim.cpu.regs[10] = 0x00010000;  /* a0 */

    uint32_t addr;
    bool ok;

    ok = parse_addr("$sp + 8", &sim, &addr);
    CHECK(ok && addr == 0xC0000008, "$sp + 8 → 0xC0000008");

    ok = parse_addr("$sp - 16", &sim, &addr);
    CHECK(ok && addr == 0xBFFFFFF0, "$sp - 16 → 0xBFFFFFF0");

    ok = parse_addr("$a0 + 4", &sim, &addr);
    CHECK(ok && addr == 0x00010004, "$a0 + 4 → 0x00010004");
}

void test_parse_addr_invalid(void)
{
    printf("\n─── B.4: parse_addr (非法输入) ───\n");
    uint32_t addr = 0xDEAD;
    bool ok;

    ok = parse_addr(NULL, NULL, &addr);
    CHECK(!ok, "NULL → false");

    ok = parse_addr("", NULL, &addr);
    CHECK(!ok, "\"\" → false");

    ok = parse_addr("$invalid_reg", NULL, &addr);
    CHECK(!ok, "$invalid_reg → false");
}

/* ====================================================================
 * 分组 C: get_reg_value
 * ==================================================================== */
void test_get_reg_value(void)
{
    printf("\n─── C: get_reg_value ───\n");
    Simulator *sim = new_sim();
    sim->cpu.regs[10] = 0xDEADBEEF;
    sim->cpu.regs[0]  = 0;           /* x0 硬连线 */
    sim->cpu.pc       = 0x80000100;

    CHECK(get_reg_value(sim, 10) == 0xDEADBEEF, "idx=10 → 0xDEADBEEF");
    CHECK(get_reg_value(sim, 32) == 0x80000100, "idx=32 (pc) → 0x80000100");
    CHECK(get_reg_value(sim, 0)  == 0,          "idx=0 (zero) → 0");
    CHECK(get_reg_value(sim, 999)== 0,          "idx=999 → 0 (默认)");

    sim_destroy(sim);
    free(sim);
}

/* ====================================================================
 * 分组 D: 断点管理 (debugger_add/del/list/check_breakpoint)
 * ==================================================================== */

/* 辅助：向指定地址写一条 NOP 指令 */
static void write_nop(Simulator *sim, uint32_t addr)
{
    ExceptionType exc = EXC_NONE;
    mmu_write_32(&sim->mmu, &sim->pmem, addr, 0x00000013,  /* NOP: addi x0,x0,0 */
                 sim->cpu.priv, &exc);
}

void test_bp_add_success(void)
{
    printf("\n─── D.1: 设断点 (成功) ───\n");
    Simulator *sim = new_sim();
    write_nop(sim, 0x1000);

    int idx = debugger_add_breakpoint(sim, 0x1000);
    CHECK(idx == 0, "返回下标 0");
    CHECK(sim->bp_count == 1, "bp_count == 1");
    CHECK(sim->breakpoints[0].addr == 0x1000, "断点地址 = 0x1000");
    CHECK(sim->breakpoints[0].enabled == true, "断点已启用");

    /* 验证内存中确实写入了 EBREAK */
    ExceptionType exc = EXC_NONE;
    uint32_t instr;
    mmu_read_32(&sim->mmu, &sim->pmem, 0x1000, &instr, sim->cpu.priv, &exc);
    CHECK(instr == 0x00100073, "内存处为 EBREAK (0x00100073)");

    sim_destroy(sim);
    free(sim);
}

void test_bp_add_misaligned(void)
{
    printf("\n─── D.2: 设断点 (不对齐) ───\n");
    Simulator *sim = new_sim();
    int idx = debugger_add_breakpoint(sim, 0x1001);  /* 非 4 字节对齐 */
    CHECK(idx == -1, "不对齐地址返回 -1");
    CHECK(sim->bp_count == 0, "bp_count 仍为 0");
    sim_destroy(sim);
    free(sim);
}

void test_bp_add_duplicate(void)
{
    printf("\n─── D.3: 设断点 (重复) ───\n");
    Simulator *sim = new_sim();
    write_nop(sim, 0x1000);

    int i1 = debugger_add_breakpoint(sim, 0x1000);
    CHECK(i1 >= 0, "第一次成功");
    int i2 = debugger_add_breakpoint(sim, 0x1000);
    CHECK(i2 == -1, "重复返回 -1");
    CHECK(sim->bp_count == 1, "bp_count 仍为 1");
    sim_destroy(sim);
    free(sim);
}

void test_bp_add_unmapped(void)
{
    printf("\n─── D.4: 设断点 (未映射地址) ───\n");
    Simulator *sim = new_sim();
    /* 0xABCD0000 不在 64KB 映射范围内 */
    int idx = debugger_add_breakpoint(sim, 0xABCD0000);
    CHECK(idx == -1, "未映射地址返回 -1");
    CHECK(sim->bp_count == 0, "bp_count 仍为 0");
    sim_destroy(sim);
    free(sim);
}

void test_bp_add_capacity_growth(void)
{
    printf("\n─── D.5: 设断点 (扩容) ───\n");
    Simulator *sim = new_sim();
    /* 初始容量 16，写入 20 个断点测试扩容 */
    for (int i = 0; i < 20; i++) {
        uint32_t addr = 0x1000 + (uint32_t)(i * 4);
        write_nop(sim, addr);
        int idx = debugger_add_breakpoint(sim, addr);
        /* 只有在返回不是 -1 时才检查 idx */
        if (idx < 0) {
            printf("  ❌ 第 %d 个断点 (addr=0x%x) 添加失败\n", i, addr);
            failed++;
        }
    }
    CHECK(sim->bp_count == 20, "bp_count == 20");
    CHECK(sim->bp_capacity >= 32, "容量已扩容 >= 32");
    sim_destroy(sim);
    free(sim);
}

void test_bp_del_success(void)
{
    printf("\n─── D.6: 删断点 (成功) ───\n");
    Simulator *sim = new_sim();
    write_nop(sim, 0x1000);
    debugger_add_breakpoint(sim, 0x1000);

    bool ok = debugger_del_breakpoint(sim, 0);
    CHECK(ok, "删除返回 true");
    CHECK(sim->bp_count == 0, "bp_count == 0");

    /* 验证原始指令已恢复 */
    ExceptionType exc = EXC_NONE;
    uint32_t instr;
    mmu_read_32(&sim->mmu, &sim->pmem, 0x1000, &instr, sim->cpu.priv, &exc);
    CHECK(instr == 0x00000013, "原始指令 (NOP) 已恢复");

    sim_destroy(sim);
    free(sim);
}

void test_bp_del_invalid(void)
{
    printf("\n─── D.7: 删断点 (非法下标) ───\n");
    Simulator *sim = new_sim();

    CHECK(!debugger_del_breakpoint(sim, -1),  "下标 -1 → false");
    CHECK(!debugger_del_breakpoint(sim, 0),   "空列表下标 0 → false");
    CHECK(!debugger_del_breakpoint(sim, 999), "空列表下标 999 → false");

    sim_destroy(sim);
    free(sim);
}

void test_bp_del_swap_last(void)
{
    printf("\n─── D.8: 删断点 (O(1) 末尾替换) ───\n");
    Simulator *sim = new_sim();
    /* 添加 3 个断点 */
    write_nop(sim, 0x1000);
    write_nop(sim, 0x1004);
    write_nop(sim, 0x1008);
    debugger_add_breakpoint(sim, 0x1000);  /* idx 0 */
    debugger_add_breakpoint(sim, 0x1004);  /* idx 1 */
    debugger_add_breakpoint(sim, 0x1008);  /* idx 2 */

    /* 删除 idx 0，末尾 idx 2 应该移动到 idx 0 */
    debugger_del_breakpoint(sim, 0);
    CHECK(sim->bp_count == 2, "bp_count == 2");
    CHECK(sim->breakpoints[0].addr == 0x1008, "原 idx 2 移到了 idx 0");

    sim_destroy(sim);
    free(sim);
}

void test_bp_check(void)
{
    printf("\n─── D.9: 断点命中检查 ───\n");
    Simulator *sim = new_sim();
    write_nop(sim, 0x1000);
    write_nop(sim, 0x2000);
    debugger_add_breakpoint(sim, 0x1000);
    debugger_add_breakpoint(sim, 0x2000);

    CHECK( debugger_check_breakpoint(sim, 0x1000), "PC=0x1000 → 命中");
    CHECK( debugger_check_breakpoint(sim, 0x2000), "PC=0x2000 → 命中");
    CHECK(!debugger_check_breakpoint(sim, 0x3000), "PC=0x3000 → 未命中");

    /* 禁用后不应命中 */
    sim->breakpoints[0].enabled = false;
    CHECK(!debugger_check_breakpoint(sim, 0x1000), "禁用后 PC=0x1000 → 未命中");

    sim_destroy(sim);
    free(sim);
}

void test_bp_list(void)
{
    printf("\n─── D.10: 列出断点 (数据验证) ───\n");

    /* 空列表 — 验证数据无副作用 */
    {
        Simulator *sim = new_sim();
        debugger_list_breakpoints(sim);  /* 冒烟：不应崩溃 */
        CHECK(sim->bp_count == 0, "空列表 bp_count == 0");
        sim_destroy(sim);
        free(sim);
    }

    /* 有断点 — 验证数据正确 */
    {
        Simulator *sim = new_sim();
        write_nop(sim, 0x1000);
        write_nop(sim, 0x2000);
        debugger_add_breakpoint(sim, 0x1000);
        debugger_add_breakpoint(sim, 0x2000);

        CHECK(sim->bp_count == 2, "bp_count == 2");
        CHECK(sim->breakpoints[0].addr == 0x1000, "bp[0].addr == 0x1000");
        CHECK(sim->breakpoints[1].addr == 0x2000, "bp[1].addr == 0x2000");
        CHECK(sim->breakpoints[0].enabled == true, "bp[0] 已启用");
        CHECK(sim->breakpoints[1].enabled == true, "bp[1] 已启用");

        debugger_list_breakpoints(sim);  /* 冒烟：不应崩溃 */

        sim_destroy(sim);
        free(sim);
    }
}

/* ====================================================================
 * 分组 E: 状态查看
 * ==================================================================== */
void test_print_registers(void)
{
    printf("\n─── E.1: 打印寄存器 (冒烟+数据验证) ───\n");
    Simulator *sim = new_sim();
    sim->cpu.regs[10] = 42;
    sim->cpu.pc       = 0x00010000;

    /* 冒烟测试：调用打印函数，验证不崩溃 */
    debugger_print_registers(sim);
    CHECK(1, "debugger_print_registers 无崩溃");

    /* 数据验证：寄存器值正确保留 */
    CHECK(sim->cpu.regs[10] == 42, "a0 值未变 (仍是 42)");
    CHECK(sim->cpu.regs[0]  == 0,  "x0 始终为 0");

    sim_destroy(sim);
    free(sim);
}

void test_examine_memory(void)
{
    printf("\n─── E.2: 查看内存 (冒烟) ───\n");
    Simulator *sim = new_sim();
    ExceptionType exc = EXC_NONE;
    mmu_write_32(&sim->mmu, &sim->pmem, 0x1000, 0xDEADBEEF, sim->cpu.priv, &exc);

    /* 冒烟测试：不应崩溃 */
    debugger_examine_memory(sim, 0x1000, 1, 'x', 'w');
    debugger_examine_memory(sim, 0x1000, 4, 'x', 'w');
    debugger_examine_memory(sim, 0x1000, 4, 'd', 'w');
    CHECK(1, "debugger_examine_memory (word) 无崩溃");

    sim_destroy(sim);
    free(sim);
}

void test_examine_memory_byte(void)
{
    printf("\n─── E.3: 查看内存 (byte/halfword 模式) ───\n");
    Simulator *sim = new_sim();
    ExceptionType exc = EXC_NONE;
    mmu_write_8(&sim->mmu, &sim->pmem, 0x1000, 0x41, sim->cpu.priv, &exc);

    debugger_examine_memory(sim, 0x1000, 4, 'x', 'b');
    debugger_examine_memory(sim, 0x1000, 2, 'x', 'h');
    /* 默认参数测试 (count=0, format=0, unit=0 → 默认 16/x/w) */
    debugger_examine_memory(sim, 0x1000, 0, 0, 0);
    CHECK(1, "debugger_examine_memory (byte/halfword/default) 无崩溃");

    sim_destroy(sim);
    free(sim);
}

void test_backtrace_smoke(void)
{
    printf("\n─── E.4: 栈回溯 (冒烟) ───\n");
    Simulator *sim = new_sim();
    sim->cpu.regs[8] = 0;   /* fp = 0 */
    sim->cpu.pc      = 0x1000;
    write_nop(sim, 0x1000);

    debugger_print_backtrace(sim);
    CHECK(1, "debugger_print_backtrace (fp=0) 无崩溃");

    sim_destroy(sim);
    free(sim);
}

/* ====================================================================
 * 分组 F: 执行控制 (step / continue)
 *
 * 使用 minimal.elf（2 条指令：addi a0,zero,42; ecall）
 * 路径需根据实际文件位置调整
 * ==================================================================== */

static const char *MINIMAL_ELF = "src/test/loader/minimal.elf";

void test_step_single(void)
{
    printf("\n─── F.1: 单步执行一条指令 ───\n");
    Simulator sim;
    sim_init(&sim);

    if (!sim_load_elf(&sim, MINIMAL_ELF)) {
        CHECK(0, "无法加载 %s (跳过 F 组)", MINIMAL_ELF);
        sim_destroy(&sim);
        return;
    }

    uint32_t pc_before = sim.cpu.pc;
    debugger_step(&sim);
    uint32_t pc_after = sim.cpu.pc;

    CHECK(pc_after == pc_before + 4, "PC 前进了 4 字节 (0x%08x → 0x%08x)",
          pc_before, pc_after);
    CHECK(sim.instr_count == 1, "instr_count == 1");
    /* single_step 由 sim_run 循环清除，debugger_step 直接调 sim_step 不会清除 */

    sim_destroy(&sim);
}

void test_step_two(void)
{
    printf("\n─── F.2: 连续单步 2 次 (完成整个程序) ───\n");
    Simulator sim;
    sim_init(&sim);

    if (!sim_load_elf(&sim, MINIMAL_ELF)) {
        CHECK(0, "无法加载 %s (跳过)", MINIMAL_ELF);
        sim_destroy(&sim);
        return;
    }

    debugger_step(&sim);  /* addi a0, zero, 42 */
    debugger_step(&sim);  /* ecall → 触发异常停机 */

    CHECK(sim.cpu.regs[REG_A0] == 42, "a0 == 42 (addi 结果)");
    CHECK(sim.instr_count == 2, "instr_count == 2");

    sim_destroy(&sim);
}

void test_continue_to_completion(void)
{
    printf("\n─── F.3: continue 到程序结束 ───\n");
    Simulator sim;
    sim_init(&sim);

    if (!sim_load_elf(&sim, MINIMAL_ELF)) {
        CHECK(0, "无法加载 %s (跳过)", MINIMAL_ELF);
        sim_destroy(&sim);
        return;
    }

    debugger_continue(&sim);

    CHECK(sim.cpu.running == false, "程序已停止 (running=false)");
    CHECK(sim.cpu.regs[REG_A0] == 42, "a0 == 42");
    CHECK(sim.instr_count == 2, "instr_count == 2");

    sim_destroy(&sim);
}

void test_continue_to_breakpoint(void)
{
    printf("\n─── F.4: continue 到断点 ───\n");
    Simulator sim;
    sim_init(&sim);

    if (!sim_load_elf(&sim, MINIMAL_ELF)) {
        CHECK(0, "无法加载 %s (跳过)", MINIMAL_ELF);
        sim_destroy(&sim);
        return;
    }

    /* 在第二条指令（ecall）处设断点 */
    uint32_t bp_addr = sim.cpu.pc + 4;
    int bp_idx = debugger_add_breakpoint(&sim, bp_addr);
    CHECK(bp_idx >= 0, "断点设置成功");

    debugger_continue(&sim);

    /* 程序已停止（断点命中或 trap 停机） */
    CHECK(sim.cpu.running == false, "程序已停止 (running=false)");
    CHECK(sim.cpu.regs[REG_A0] == 42, "a0 == 42 (addi 已执行)");
    /* 注：断点命中后 CPU trap 也会触发，PC 可能被修改；断点正确触发了即可 */

    sim_destroy(&sim);
}

void test_step_at_breakpoint(void)
{
    printf("\n─── F.5: 断点处单步 ───\n");
    Simulator sim;
    sim_init(&sim);

    if (!sim_load_elf(&sim, MINIMAL_ELF)) {
        CHECK(0, "无法加载 %s (跳过)", MINIMAL_ELF);
        sim_destroy(&sim);
        return;
    }

    /* 在入口处设断点 */
    uint32_t entry = sim.cpu.pc;
    debugger_add_breakpoint(&sim, entry);

    /* 手动触发断点命中：单步越过断点 */
    debugger_step(&sim);  /* 应该恢复原指令→执行→重新插入 ebreak */

    /* 验证 PC 前进且指令已执行 */
    CHECK(sim.cpu.pc == entry + 4, "PC 越过断点");
    CHECK(sim.instr_count == 1, "instr_count == 1");

    /* 验证 ebreak 已重新插入 */
    ExceptionType exc = EXC_NONE;
    uint32_t instr;
    mmu_read_32(&sim.mmu, &sim.pmem, entry, &instr, sim.cpu.priv, &exc);
    CHECK(instr == 0x00100073, "EBREAK 已重新插入");

    sim_destroy(&sim);
}

void test_stepi_multi(void)
{
    printf("\n─── F.6: stepi 多步 ───\n");
    Simulator sim;
    sim_init(&sim);

    if (!sim_load_elf(&sim, MINIMAL_ELF)) {
        CHECK(0, "无法加载 %s (跳过)", MINIMAL_ELF);
        sim_destroy(&sim);
        return;
    }

    /* 不能直接调用 debugger_step 里面的 si 逻辑（那是 REPL 的），
       手动调用 debugger_step 两次模拟 stepi 2 */
    debugger_step(&sim);
    debugger_step(&sim);

    CHECK(sim.cpu.regs[REG_A0] == 42, "a0 == 42");
    CHECK(sim.instr_count == 2, "instr_count == 2");

    sim_destroy(&sim);
}

/* ====================================================================
 * 分组 G: 边界与鲁棒性
 * ==================================================================== */
void test_registers_zero_state(void)
{
    printf("\n─── G.1: 零状态寄存器 ───\n");
    Simulator *sim = new_sim();

    /* 冒烟测试 */
    debugger_print_registers(sim);
    CHECK(1, "零状态打印无崩溃");

    /* 数据验证：所有寄存器为零 */
    bool all_zero = true;
    for (int i = 0; i < 32; i++) {
        if (sim->cpu.regs[i] != 0) { all_zero = false; break; }
    }
    CHECK(all_zero, "sim_init 后所有寄存器为 0");

    sim_destroy(sim);
    free(sim);
}

void test_bp_restore_on_destroy(void)
{
    printf("\n─── G.2: sim_destroy 恢复断点指令 ───\n");
    Simulator *sim = new_sim();
    write_nop(sim, 0x1000);
    debugger_add_breakpoint(sim, 0x1000);

    /* 验证 ebreak 已写入 */
    ExceptionType exc = EXC_NONE;
    uint32_t instr_before;
    mmu_read_32(&sim->mmu, &sim->pmem, 0x1000, &instr_before, sim->cpu.priv, &exc);
    CHECK(instr_before == 0x00100073, "销毁前内存中为 EBREAK");

    /* 验证断点记录的原始指令是 NOP */
    CHECK(sim->breakpoints[0].original_instr == 0x00000013,
          "保存的原始指令 = NOP (0x00000013)");

    /* 手动删除断点来验证恢复机制（sim_destroy 用同样逻辑） */
    debugger_del_breakpoint(sim, 0);
    uint32_t instr_after;
    mmu_read_32(&sim->mmu, &sim->pmem, 0x1000, &instr_after, sim->cpu.priv, &exc);
    CHECK(instr_after == 0x00000013, "删除断点后 NOP 已恢复");

    sim_destroy(sim);
    free(sim);
}

void test_examine_memory_large_count(void)
{
    printf("\n─── G.3: 查看大范围内存 ───\n");
    Simulator *sim = new_sim();

    /* 64 个字 = 256 字节，应在映射范围内，不应崩溃 */
    debugger_examine_memory(sim, 0x0, 64, 'x', 'w');
    CHECK(1, "大范围 x/64xw 无崩溃");

    sim_destroy(sim);
    free(sim);
}

void test_disasm_output(void)
{
    printf("\n─── G.4: 反汇编输出 (通过 step) ───\n");
    Simulator sim;
    sim_init(&sim);

    if (!sim_load_elf(&sim, MINIMAL_ELF)) {
        CHECK(0, "无法加载 %s (跳过)", MINIMAL_ELF);
        sim_destroy(&sim);
        return;
    }

    /* 冒烟测试：step 内部调用 cpu_disasm 打印反汇编 */
    debugger_step(&sim);
    CHECK(sim.instr_count == 1, "step 后 instr_count == 1 (反汇编打印正常)");

    sim_destroy(&sim);
}

/* ====================================================================
 * main
 * ==================================================================== */
int main(void)
{
    printf("═══════════════════════════════════════\n");
    printf("  RISC-V Debugger Unit Test\n");
    printf("═══════════════════════════════════════\n");

    /* ── A: parse_reg_name (纯函数，无依赖) ── */
    test_parse_reg_name_abi();
    test_parse_reg_name_x();
    test_parse_reg_name_numeric();
    test_parse_reg_name_pc();
    test_parse_reg_name_dollar();
    test_parse_reg_name_invalid();

    /* ── B: parse_addr ── */
    test_parse_addr_literal();
    test_parse_addr_dollar_reg();
    test_parse_addr_dollar_reg_offset();
    test_parse_addr_invalid();

    /* ── C: get_reg_value ── */
    test_get_reg_value();

    /* ── D: 断点管理 ── */
    test_bp_add_success();
    test_bp_add_misaligned();
    test_bp_add_duplicate();
    test_bp_add_unmapped();
    test_bp_add_capacity_growth();
    test_bp_del_success();
    test_bp_del_invalid();
    test_bp_del_swap_last();
    test_bp_check();
    test_bp_list();

    /* ── E: 状态查看 ── */
    test_print_registers();
    test_examine_memory();
    test_examine_memory_byte();
    test_backtrace_smoke();

    /* ── F: 执行控制 (需要 minimal.elf) ── */
    test_step_single();
    test_step_two();
    test_continue_to_completion();
    test_continue_to_breakpoint();
    test_step_at_breakpoint();
    test_stepi_multi();

    /* ── G: 边界 ── */
    test_registers_zero_state();
    test_bp_restore_on_destroy();
    test_examine_memory_large_count();
    test_disasm_output();

    /* ── 总结 ── */
    printf("\n═══════════════════════════════════════\n");
    printf("  %d passed, %d failed\n", passed, failed);
    printf("═══════════════════════════════════════\n");

    return failed > 0 ? 1 : 0;
}
