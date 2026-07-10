/* ============================================================================
 * m_test.c — M 扩展（乘除法 8 条指令）单元测试
 *
 * 用法：
 *   gcc -std=c11 -I src/include -o build/m_test \
 *       src/src/cpu/cpu.c \
 *       src/src/cpu/decode.c \
 *       src/src/cpu/execute/execute.c \
 *       src/src/cpu/execute/exec_rv32i.c \
 *       src/src/cpu/execute/exec_m.c \
 *       src/src/simulator.c \
 *       src/src/memory/memory.c \
 *       src/src/memory/mmu.c \
 *       src/test/cpu/m_test.c
 *   ./build/m_test
 *
 * 测试策略：直接构造 DecodedInstr → cpu_execute() → 检查 regs[rd]
 * ============================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "simulator.h"
#include "cpu/decode.h"
#include "cpu/execute.h"
#include "memory/mmu.h"
#include "types.h"

/* ── 辅助函数 ────────────────────────────────────────────────────── */

static Simulator *new_sim(void)
{
    Simulator *sim = calloc(1, sizeof(Simulator));
    if (!sim) { fprintf(stderr, "Fatal: calloc\n"); exit(1); }
    sim_init(sim);
    mem_map(&sim->pmem, 0, 64 * 1024, MEM_READ | MEM_WRITE, "test");
    return sim;
}

static bool exec(Simulator *sim, uint32_t instr, uint32_t *next_pc)
{
    DecodedInstr d = cpu_decode(instr);
    return cpu_execute(sim, &d, next_pc);
}

/* ── R-type 指令编码（opcode=0x33, funct7=0x01） ─────────────────── */
#define M_INSTR(funct3, rd, rs1, rs2) \
    ((0x01u << 25) | ((rs2) << 20) | ((rs1) << 15) | ((funct3) << 12) | ((rd) << 7) | 0x33u)

/* ── 测试宏 ──────────────────────────────────────────────────────── */
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

/* ================================================================
 * MUL — 乘法低 32 位
 * ================================================================ */
void test_mul(void)
{
    printf("\n─── MUL ───\n");
    Simulator *sim = new_sim();
    uint32_t np;

    /* 3 × 4 = 12 */
    sim->cpu.regs[1] = 3;
    sim->cpu.regs[2] = 4;
    exec(sim, M_INSTR(0, 3, 1, 2), &np);   // MUL x3, x1, x2
    CHECK(sim->cpu.regs[3] == 12, "3 × 4 = %u (expected 12)", sim->cpu.regs[3]);

    /* 0 × anything = 0 */
    sim->cpu.regs[1] = 0;
    sim->cpu.regs[2] = 999;
    exec(sim, M_INSTR(0, 3, 1, 2), &np);
    CHECK(sim->cpu.regs[3] == 0, "0 × 999 = %u (expected 0)", sim->cpu.regs[3]);

    /* negative: -1 × 2 = -2, 低 32 位 = 0xFFFFFFFE */
    sim->cpu.regs[1] = 0xFFFFFFFFu;  // -1
    sim->cpu.regs[2] = 2;
    exec(sim, M_INSTR(0, 3, 1, 2), &np);
    CHECK(sim->cpu.regs[3] == 0xFFFFFFFEu, "-1 × 2 低 32 = 0x%08X (expected 0xFFFFFFFE)", sim->cpu.regs[3]);

    /* large: 0x80000000 × 2 = 0x100000000, 低 32 位 = 0 */
    sim->cpu.regs[1] = 0x80000000u;
    sim->cpu.regs[2] = 2;
    exec(sim, M_INSTR(0, 3, 1, 2), &np);
    CHECK(sim->cpu.regs[3] == 0, "0x80000000 × 2 低 32 = 0x%08X (expected 0)", sim->cpu.regs[3]);

    free(sim);
}

/* ================================================================
 * MULH — 有符号 × 有符号 → 高 32 位
 * ================================================================ */
void test_mulh(void)
{
    printf("\n─── MULH ───\n");
    Simulator *sim = new_sim();
    uint32_t np;

    /* 3 × 4 = 12, 高 32 位 = 0 */
    sim->cpu.regs[1] = 3;
    sim->cpu.regs[2] = 4;
    exec(sim, M_INSTR(1, 3, 1, 2), &np);   // MULH x3, x1, x2
    CHECK(sim->cpu.regs[3] == 0, "MULH 3×4 = 0x%08X (expected 0)", sim->cpu.regs[3]);

    /* -1 × 2 = -2, 高 32 位 = 0xFFFFFFFF（全是符号位） */
    sim->cpu.regs[1] = 0xFFFFFFFFu;  // -1
    sim->cpu.regs[2] = 2;
    exec(sim, M_INSTR(1, 3, 1, 2), &np);
    CHECK(sim->cpu.regs[3] == 0xFFFFFFFFu, "MULH -1×2 高 32 = 0x%08X (expected 0xFFFFFFFF)", sim->cpu.regs[3]);

    /* INT32_MIN × -1: -2^31 × -1 = +2^31, 高 32 位 = 0 */
    sim->cpu.regs[1] = 0x80000000u;  // INT32_MIN
    sim->cpu.regs[2] = 0xFFFFFFFFu;  // -1
    exec(sim, M_INSTR(1, 3, 1, 2), &np);
    CHECK(sim->cpu.regs[3] == 0, "MULH INT32_MIN×-1 高 32 = 0x%08X (expected 0)", sim->cpu.regs[3]);

    free(sim);
}

/* ================================================================
 * MULHSU — 有符号 × 无符号 → 高 32 位
 * ================================================================ */
void test_mulhsu(void)
{
    printf("\n─── MULHSU ───\n");
    Simulator *sim = new_sim();
    uint32_t np;

    /* -1 (有符号) × 0xFFFFFFFF (无符号 = 4294967295) = -4294967295
     * = 0xFFFFFFFF_00000001, 高 32 位 = 0xFFFFFFFF */
    sim->cpu.regs[1] = 0xFFFFFFFFu;  // -1 有符号
    sim->cpu.regs[2] = 0xFFFFFFFFu;  // 4294967295 无符号
    exec(sim, M_INSTR(2, 3, 1, 2), &np);   // MULHSU x3, x1, x2
    CHECK(sim->cpu.regs[3] == 0xFFFFFFFFu, "MULHSU -1×Umax 高 32 = 0x%08X (expected 0xFFFFFFFF)", sim->cpu.regs[3]);

    /* 3 × 4 = 12, 高 32 = 0 */
    sim->cpu.regs[1] = 3;
    sim->cpu.regs[2] = 4;
    exec(sim, M_INSTR(2, 3, 1, 2), &np);
    CHECK(sim->cpu.regs[3] == 0, "MULHSU 3×4 = 0x%08X (expected 0)", sim->cpu.regs[3]);

    free(sim);
}

/* ================================================================
 * MULHU — 无符号 × 无符号 → 高 32 位
 * ================================================================ */
void test_mulhu(void)
{
    printf("\n─── MULHU ───\n");
    Simulator *sim = new_sim();
    uint32_t np;

    /* 0xFFFFFFFF × 2 = 0x1_FFFFFFFE, 高 32 = 1 */
    sim->cpu.regs[1] = 0xFFFFFFFFu;  // 4294967295
    sim->cpu.regs[2] = 2;
    exec(sim, M_INSTR(3, 3, 1, 2), &np);   // MULHU x3, x1, x2
    CHECK(sim->cpu.regs[3] == 1, "MULHU Umax×2 高 32 = %u (expected 1)", sim->cpu.regs[3]);

    /* 3 × 4 = 12, 高 32 = 0 */
    sim->cpu.regs[1] = 3;
    sim->cpu.regs[2] = 4;
    exec(sim, M_INSTR(3, 3, 1, 2), &np);
    CHECK(sim->cpu.regs[3] == 0, "MULHU 3×4 = 0x%08X (expected 0)", sim->cpu.regs[3]);

    free(sim);
}

/* ================================================================
 * DIV — 有符号除法
 * ================================================================ */
void test_div(void)
{
    printf("\n─── DIV ───\n");
    Simulator *sim = new_sim();
    uint32_t np;

    /* 10 ÷ 3 = 3（向零舍入） */
    sim->cpu.regs[1] = 10;
    sim->cpu.regs[2] = 3;
    exec(sim, M_INSTR(4, 3, 1, 2), &np);   // DIV x3, x1, x2
    CHECK(sim->cpu.regs[3] == 3, "10 ÷ 3 = %u (expected 3)", sim->cpu.regs[3]);

    /* -10 ÷ 3 = -3（向零舍入） */
    sim->cpu.regs[1] = (uint32_t)-10;   // 0xFFFFFFF6
    sim->cpu.regs[2] = 3;
    exec(sim, M_INSTR(4, 3, 1, 2), &np);
    CHECK((int32_t)sim->cpu.regs[3] == -3, "-10 ÷ 3 = %d (expected -3)", (int32_t)sim->cpu.regs[3]);

    /* 除零 → -1 */
    sim->cpu.regs[1] = 10;
    sim->cpu.regs[2] = 0;
    exec(sim, M_INSTR(4, 3, 1, 2), &np);
    CHECK(sim->cpu.regs[3] == 0xFFFFFFFFu, "10 ÷ 0 = 0x%08X (expected 0xFFFFFFFF)", sim->cpu.regs[3]);

    /* INT32_MIN ÷ -1 → INT32_MIN（溢出） */
    sim->cpu.regs[1] = 0x80000000u;   // INT32_MIN
    sim->cpu.regs[2] = 0xFFFFFFFFu;   // -1
    exec(sim, M_INSTR(4, 3, 1, 2), &np);
    CHECK(sim->cpu.regs[3] == 0x80000000u, "INT32_MIN ÷ -1 = 0x%08X (expected 0x80000000 overflow)", sim->cpu.regs[3]);

    free(sim);
}

/* ================================================================
 * DIVU — 无符号除法
 * ================================================================ */
void test_divu(void)
{
    printf("\n─── DIVU ───\n");
    Simulator *sim = new_sim();
    uint32_t np;

    /* 10 ÷ 3 = 3 */
    sim->cpu.regs[1] = 10;
    sim->cpu.regs[2] = 3;
    exec(sim, M_INSTR(5, 3, 1, 2), &np);   // DIVU x3, x1, x2
    CHECK(sim->cpu.regs[3] == 3, "10 ÷ 3 = %u (expected 3)", sim->cpu.regs[3]);

    /* 除零 → 全1 */
    sim->cpu.regs[1] = 100;
    sim->cpu.regs[2] = 0;
    exec(sim, M_INSTR(5, 3, 1, 2), &np);
    CHECK(sim->cpu.regs[3] == 0xFFFFFFFFu, "100 ÷ 0 = 0x%08X (expected 0xFFFFFFFF)", sim->cpu.regs[3]);

    /* 大数: 0xFFFFFFFF ÷ 2 = 0x7FFFFFFF */
    sim->cpu.regs[1] = 0xFFFFFFFFu;
    sim->cpu.regs[2] = 2;
    exec(sim, M_INSTR(5, 3, 1, 2), &np);
    CHECK(sim->cpu.regs[3] == 0x7FFFFFFFu, "Umax ÷ 2 = 0x%08X (expected 0x7FFFFFFF)", sim->cpu.regs[3]);

    free(sim);
}

/* ================================================================
 * REM — 有符号取余
 * ================================================================ */
void test_rem(void)
{
    printf("\n─── REM ───\n");
    Simulator *sim = new_sim();
    uint32_t np;

    /* 10 % 3 = 1 */
    sim->cpu.regs[1] = 10;
    sim->cpu.regs[2] = 3;
    exec(sim, M_INSTR(6, 3, 1, 2), &np);   // REM x3, x1, x2
    CHECK(sim->cpu.regs[3] == 1, "10 %% 3 = %u (expected 1)", sim->cpu.regs[3]);

    /* -10 % 3 = -1 */
    sim->cpu.regs[1] = (uint32_t)-10;
    sim->cpu.regs[2] = 3;
    exec(sim, M_INSTR(6, 3, 1, 2), &np);
    CHECK((int32_t)sim->cpu.regs[3] == -1, "-10 %% 3 = %d (expected -1)", (int32_t)sim->cpu.regs[3]);

    /* 除零 → 被除数 */
    sim->cpu.regs[1] = 10;
    sim->cpu.regs[2] = 0;
    exec(sim, M_INSTR(6, 3, 1, 2), &np);
    CHECK(sim->cpu.regs[3] == 10, "10 %% 0 = %u (expected 10 = 被除数)", sim->cpu.regs[3]);

    /* INT32_MIN % -1 → 0（溢出） */
    sim->cpu.regs[1] = 0x80000000u;
    sim->cpu.regs[2] = 0xFFFFFFFFu;
    exec(sim, M_INSTR(6, 3, 1, 2), &np);
    CHECK(sim->cpu.regs[3] == 0, "INT32_MIN %% -1 = %u (expected 0 overflow)", sim->cpu.regs[3]);

    free(sim);
}

/* ================================================================
 * REMU — 无符号取余
 * ================================================================ */
void test_remu(void)
{
    printf("\n─── REMU ───\n");
    Simulator *sim = new_sim();
    uint32_t np;

    /* 10 % 3 = 1 */
    sim->cpu.regs[1] = 10;
    sim->cpu.regs[2] = 3;
    exec(sim, M_INSTR(7, 3, 1, 2), &np);   // REMU x3, x1, x2
    CHECK(sim->cpu.regs[3] == 1, "10 %% 3 = %u (expected 1)", sim->cpu.regs[3]);

    /* 除零 → 被除数 */
    sim->cpu.regs[1] = 99;
    sim->cpu.regs[2] = 0;
    exec(sim, M_INSTR(7, 3, 1, 2), &np);
    CHECK(sim->cpu.regs[3] == 99, "99 %% 0 = %u (expected 99 = 被除数)", sim->cpu.regs[3]);

    free(sim);
}

int main(void)
{
    printf("═══════════════════════════════════════\n");
    printf("  M 扩展测试 — 乘除法 8 条指令\n");
    printf("═══════════════════════════════════════\n");

    test_mul();
    test_mulh();
    test_mulhsu();
    test_mulhu();
    test_div();
    test_divu();
    test_rem();
    test_remu();

    printf("\n═══════════════════════════════════════\n");
    printf("  结果: %d 通过, %d 失败, 共 %d 项\n", passed, failed, passed + failed);
    printf("═══════════════════════════════════════\n");

    return failed ? 1 : 0;
}
