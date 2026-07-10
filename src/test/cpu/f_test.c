/* ============================================================================
 * f_test.c — F 扩展（单精度浮点 26 条指令）单元测试
 *
 * 用法：
 *   gcc -std=c11 -I src/include -o build/f_test \
 *       src/src/cpu/cpu.c \
 *       src/src/cpu/decode.c \
 *       src/src/cpu/execute/execute.c \
 *       src/src/cpu/execute/exec_rv32i.c \
 *       src/src/cpu/execute/exec_f.c \
 *       src/src/simulator.c \
 *       src/src/memory/memory.c \
 *       src/src/memory/mmu.c \
 *       src/test/cpu/f_test.c \
 *       -lm
 *   ./build/f_test
 *
 * 测试策略：构造 DecodedInstr → cpu_execute() → 检查 fregs[rd] 位模式
 * ============================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "simulator.h"
#include "cpu/decode.h"
#include "cpu/execute.h"
#include "memory/mmu.h"
#include "types.h"

/* ── uint32_t ↔ float 互转 ──────────────────────────────────────── */
typedef union { uint32_t u; float f; } fp32_t;

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

/* ── 指令编码宏 ──────────────────────────────────────────────────── */
#define R_TYPE(opcode, rd, funct3, rs1, rs2, funct7) \
    (((funct7) << 25) | ((rs2) << 20) | ((rs1) << 15) | ((funct3) << 12) | ((rd) << 7) | (opcode))

/* I-type: opcode=0x07 (FLW) */
#define FLW_INSTR(rd, rs1, imm) \
    ((((imm) & 0xFFF) << 20) | ((rs1) << 15) | (0x2 << 12) | ((rd) << 7) | 0x07)

/* S-type: opcode=0x27 (FSW) */
#define FSW_INSTR(rs2, rs1, imm) \
    (((((imm) >> 5) & 0x7F) << 25) | ((rs2) << 20) | ((rs1) << 15) | (0x2 << 12) | (((imm) & 0x1F) << 7) | 0x27)

/* OP-FP: opcode=0x43 */
#define FP_OP(rd, funct3, rs1, rs2, funct7) \
    R_TYPE(0x43, rd, funct3, rs1, rs2, funct7)

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

#define FMT_F(v)  ({ fp32_t _x; _x.u = (v); _x.f; })

/* ================================================================
 * FLW / FSW — 浮点 Load / Store
 * ================================================================ */
void test_flw_fsw(void)
{
    printf("\n─── FLW / FSW ───\n");
    Simulator *sim = new_sim();
    uint32_t np;
    fp32_t val;

    /* 在内存 0x100 处写入 3.14f 的位模式 */
    val.f = 3.14f;
    mem_write_32(&sim->pmem, 0x100, val.u);

    /* FLW f5, 0x100(x0): rs1=0, rd=5, imm=0x100 */
    sim->cpu.regs[0] = 0;
    exec(sim, FLW_INSTR(5, 0, 0x100), &np);
    CHECK(sim->cpu.fregs[5] == val.u, "FLW: loaded 0x%08X (expected 0x%08X = 3.14f)", sim->cpu.fregs[5], val.u);

    /* FSW f5, 0x200(x0): 把 f5 存到 0x200 */
    sim->cpu.regs[0] = 0;
    exec(sim, FSW_INSTR(5, 0, 0x200), &np);
    uint32_t stored;
    mem_read_32(&sim->pmem, 0x200, &stored);
    CHECK(stored == val.u, "FSW: stored 0x%08X (expected 0x%08X)", stored, val.u);

    free(sim);
}

/* ================================================================
 * FADD.S / FSUB.S / FMUL.S / FDIV.S — 四则运算
 * ================================================================ */
void test_fp_arith(void)
{
    printf("\n─── FADD / FSUB / FMUL / FDIV ───\n");
    Simulator *sim = new_sim();
    uint32_t np;
    fp32_t va, vb;

    /* FADD: 1.5 + 2.5 = 4.0 */
    va.f = 1.5f;  vb.f = 2.5f;
    sim->cpu.fregs[1] = va.u;
    sim->cpu.fregs[2] = vb.u;
    exec(sim, FP_OP(3, 0, 1, 2, 0x00), &np);   // FADD.S f3, f1, f2
    CHECK(FMT_F(sim->cpu.fregs[3]) == 4.0f,
          "1.5 + 2.5 = %f (expected 4.0)", FMT_F(sim->cpu.fregs[3]));

    /* FSUB: 5.0 - 2.0 = 3.0 */
    va.f = 5.0f;  vb.f = 2.0f;
    sim->cpu.fregs[1] = va.u;
    sim->cpu.fregs[2] = vb.u;
    exec(sim, FP_OP(3, 0, 1, 2, 0x04), &np);   // FSUB.S f3, f1, f2
    CHECK(FMT_F(sim->cpu.fregs[3]) == 3.0f,
          "5.0 - 2.0 = %f (expected 3.0)", FMT_F(sim->cpu.fregs[3]));

    /* FMUL: 2.0 × 3.0 = 6.0 */
    va.f = 2.0f;  vb.f = 3.0f;
    sim->cpu.fregs[1] = va.u;
    sim->cpu.fregs[2] = vb.u;
    exec(sim, FP_OP(3, 0, 1, 2, 0x08), &np);   // FMUL.S f3, f1, f2
    CHECK(FMT_F(sim->cpu.fregs[3]) == 6.0f,
          "2.0 × 3.0 = %f (expected 6.0)", FMT_F(sim->cpu.fregs[3]));

    /* FDIV: 6.0 ÷ 2.0 = 3.0 */
    va.f = 6.0f;  vb.f = 2.0f;
    sim->cpu.fregs[1] = va.u;
    sim->cpu.fregs[2] = vb.u;
    exec(sim, FP_OP(3, 0, 1, 2, 0x0C), &np);   // FDIV.S f3, f1, f2
    CHECK(FMT_F(sim->cpu.fregs[3]) == 3.0f,
          "6.0 ÷ 2.0 = %f (expected 3.0)", FMT_F(sim->cpu.fregs[3]));

    free(sim);
}

/* ================================================================
 * FSQRT.S — 平方根
 * ================================================================ */
void test_fsqrt(void)
{
    printf("\n─── FSQRT ───\n");
    Simulator *sim = new_sim();
    uint32_t np;
    fp32_t va;

    /* sqrt(4.0) = 2.0 */
    va.f = 4.0f;
    sim->cpu.fregs[1] = va.u;
    exec(sim, FP_OP(3, 0, 1, 0, 0x2C), &np);   // FSQRT.S f3, f1
    CHECK(FMT_F(sim->cpu.fregs[3]) == 2.0f,
          "sqrt(4.0) = %f (expected 2.0)", FMT_F(sim->cpu.fregs[3]));

    /* sqrt(2.0) ≈ 1.414 */
    va.f = 2.0f;
    sim->cpu.fregs[1] = va.u;
    exec(sim, FP_OP(3, 0, 1, 0, 0x2C), &np);
    float diff = FMT_F(sim->cpu.fregs[3]) - 1.41421356f;
    if (diff < 0) diff = -diff;
    CHECK(diff < 0.0001f, "sqrt(2.0) = %f (expected ≈ 1.414214)", FMT_F(sim->cpu.fregs[3]));

    free(sim);
}

/* ================================================================
 * FSGNJ.S / FSGNJN.S / FSGNJX.S — 符号注入
 * ================================================================ */
void test_fsgnj(void)
{
    printf("\n─── FSGNJ / FSGNJN / FSGNJX ───\n");
    Simulator *sim = new_sim();
    uint32_t np;
    fp32_t va, vb;

    va.f = -3.0f;  /* 负号 */
    vb.f =  1.0f;  /* 正号 */

    /* FSGNJ.S: 用 rs2 的符号 */
    sim->cpu.fregs[1] = va.u;       // -3.0
    sim->cpu.fregs[2] = vb.u;       // +1.0
    exec(sim, FP_OP(3, 0, 1, 2, 0x10), &np);   // FSGNJ.S f3, f1, f2
    CHECK(FMT_F(sim->cpu.fregs[3]) > 0.0f,
          "FSGNJ: -3.0 signed by +1.0 = %f (expected positive)", FMT_F(sim->cpu.fregs[3]));

    /* FSGNJN.S: 用 rs2 的反符号 */
    exec(sim, FP_OP(3, 1, 1, 2, 0x10), &np);   // FSGNJN.S f3, f1, f2
    CHECK(FMT_F(sim->cpu.fregs[3]) < 0.0f,
          "FSGNJN: -3.0 signed by NOT(+1.0) = %f (expected negative)", FMT_F(sim->cpu.fregs[3]));

    /* FSGNJX.S: XOR 符号 */
    va.f = -3.0f;
    vb.f = -1.0f;
    sim->cpu.fregs[1] = va.u;       // -3.0
    sim->cpu.fregs[2] = vb.u;       // -1.0
    exec(sim, FP_OP(3, 2, 1, 2, 0x10), &np);   // FSGNJX.S f3, f1, f2
    CHECK(FMT_F(sim->cpu.fregs[3]) > 0.0f,
          "FSGNJX: -3.0 XOR -1.0 = %f (expected positive)", FMT_F(sim->cpu.fregs[3]));

    free(sim);
}

/* ================================================================
 * FMIN.S / FMAX.S — 最值
 * ================================================================ */
void test_fminmax(void)
{
    printf("\n─── FMIN / FMAX ───\n");
    Simulator *sim = new_sim();
    uint32_t np;
    fp32_t va, vb;

    /* FMIN: min(3.0, 5.0) = 3.0 */
    va.f = 3.0f;  vb.f = 5.0f;
    sim->cpu.fregs[1] = va.u;
    sim->cpu.fregs[2] = vb.u;
    exec(sim, FP_OP(3, 0, 1, 2, 0x14), &np);   // FMIN.S f3, f1, f2
    CHECK(FMT_F(sim->cpu.fregs[3]) == 3.0f,
          "min(3.0, 5.0) = %f (expected 3.0)", FMT_F(sim->cpu.fregs[3]));

    /* FMAX: max(3.0, 5.0) = 5.0 */
    exec(sim, FP_OP(3, 1, 1, 2, 0x14), &np);   // FMAX.S f3, f1, f2
    CHECK(FMT_F(sim->cpu.fregs[3]) == 5.0f,
          "max(3.0, 5.0) = %f (expected 5.0)", FMT_F(sim->cpu.fregs[3]));

    free(sim);
}

/* ================================================================
 * FEQ.S / FLT.S / FLE.S — 比较（结果写整数寄存器）
 * ================================================================ */
void test_fcmp(void)
{
    printf("\n─── FEQ / FLT / FLE ───\n");
    Simulator *sim = new_sim();
    uint32_t np;
    fp32_t va, vb;

    /* FLE: 3.0 <= 5.0 → 1 */
    va.f = 3.0f;  vb.f = 5.0f;
    sim->cpu.fregs[1] = va.u;
    sim->cpu.fregs[2] = vb.u;
    exec(sim, FP_OP(3, 0, 1, 2, 0x50), &np);   // FLE.S x3, f1, f2
    CHECK(sim->cpu.regs[3] == 1, "3.0 <= 5.0 → %u (expected 1)", sim->cpu.regs[3]);

    /* FLT: 5.0 < 3.0 → 0 */
    va.f = 5.0f;  vb.f = 3.0f;
    sim->cpu.fregs[1] = va.u;
    sim->cpu.fregs[2] = vb.u;
    exec(sim, FP_OP(3, 1, 1, 2, 0x50), &np);   // FLT.S x3, f1, f2
    CHECK(sim->cpu.regs[3] == 0, "5.0 < 3.0 → %u (expected 0)", sim->cpu.regs[3]);

    /* FEQ: 2.5 == 2.5 → 1 */
    va.f = 2.5f;  vb.f = 2.5f;
    sim->cpu.fregs[1] = va.u;
    sim->cpu.fregs[2] = vb.u;
    exec(sim, FP_OP(3, 2, 1, 2, 0x50), &np);   // FEQ.S x3, f1, f2
    CHECK(sim->cpu.regs[3] == 1, "2.5 == 2.5 → %u (expected 1)", sim->cpu.regs[3]);

    free(sim);
}

/* ================================================================
 * FCVT.W.S / FCVT.WU.S — float → 整数
 * ================================================================ */
void test_fcvt_w(void)
{
    printf("\n─── FCVT.W.S / FCVT.WU.S ───\n");
    Simulator *sim = new_sim();
    uint32_t np;
    fp32_t va;

    /* FCVT.W.S: 3.7f → 3 (截断向零) */
    va.f = 3.7f;
    sim->cpu.fregs[1] = va.u;
    exec(sim, FP_OP(3, 0, 1, 0, 0x60), &np);   // FCVT.W.S x3, f1 (rs2=0)
    CHECK((int32_t)sim->cpu.regs[3] == 3, "FCVT.W.S 3.7 → %d (expected 3)", (int32_t)sim->cpu.regs[3]);

    /* FCVT.W.S: -3.7f → -3 */
    va.f = -3.7f;
    sim->cpu.fregs[1] = va.u;
    exec(sim, FP_OP(3, 0, 1, 0, 0x60), &np);
    CHECK((int32_t)sim->cpu.regs[3] == -3, "FCVT.W.S -3.7 → %d (expected -3)", (int32_t)sim->cpu.regs[3]);

    /* FCVT.WU.S: 3.7f → 3 (rs2=1) */
    va.f = 3.7f;
    sim->cpu.fregs[1] = va.u;
    exec(sim, FP_OP(3, 0, 1, 1, 0x60), &np);   // rs2=1 → unsigned
    CHECK(sim->cpu.regs[3] == 3, "FCVT.WU.S 3.7 → %u (expected 3)", sim->cpu.regs[3]);

    free(sim);
}

/* ================================================================
 * FCVT.S.W / FCVT.S.WU — 整数 → float
 * ================================================================ */
void test_fcvt_s(void)
{
    printf("\n─── FCVT.S.W / FCVT.S.WU ───\n");
    Simulator *sim = new_sim();
    uint32_t np;

    /* FCVT.S.W: 42 → 42.0f */
    sim->cpu.regs[1] = 42;
    exec(sim, FP_OP(3, 0, 1, 0, 0x68), &np);   // FCVT.S.W f3, x1 (rs2=0)
    CHECK(FMT_F(sim->cpu.fregs[3]) == 42.0f,
          "FCVT.S.W 42 → %f (expected 42.0)", FMT_F(sim->cpu.fregs[3]));

    /* FCVT.S.W: -5 → -5.0f */
    sim->cpu.regs[1] = (uint32_t)-5;
    exec(sim, FP_OP(3, 0, 1, 0, 0x68), &np);
    CHECK(FMT_F(sim->cpu.fregs[3]) == -5.0f,
          "FCVT.S.W -5 → %f (expected -5.0)", FMT_F(sim->cpu.fregs[3]));

    /* FCVT.S.WU: 3000000000u → 3e9f */
    sim->cpu.regs[1] = 3000000000u;
    exec(sim, FP_OP(3, 0, 1, 1, 0x68), &np);   // rs2=1 → unsigned
    CHECK(FMT_F(sim->cpu.fregs[3]) == 3.0e9f,
          "FCVT.S.WU 3e9 → %f (expected 3000000000.0)", FMT_F(sim->cpu.fregs[3]));

    free(sim);
}

/* ================================================================
 * FMV.X.W / FMV.W.X — freg ↔ ireg 搬移
 * ================================================================ */
void test_fmv(void)
{
    printf("\n─── FMV.X.W / FMV.W.X ───\n");
    Simulator *sim = new_sim();
    uint32_t np;
    fp32_t va;

    /* FMV.X.W: freg → ireg */
    va.f = -1.0f;   /* 0xBF800000 */
    sim->cpu.fregs[1] = va.u;
    exec(sim, FP_OP(3, 0, 1, 0, 0x70), &np);   // FMV.X.W x3, f1
    CHECK(sim->cpu.regs[3] == va.u,
          "FMV.X.W: 0x%08X (expected 0x%08X = -1.0f)", sim->cpu.regs[3], va.u);

    /* FMV.W.X: ireg → freg */
    sim->cpu.regs[1] = 0x40490FDBu;   /* 3.14159... = π */
    exec(sim, FP_OP(3, 0, 1, 0, 0x78), &np);   // FMV.W.X f3, x1
    va.u = sim->cpu.fregs[3];
    CHECK(va.f > 3.14f && va.f < 3.142f,
          "FMV.W.X: %f (expected ≈ 3.14159 = π)", va.f);

    free(sim);
}

int main(void)
{
    printf("═══════════════════════════════════════\n");
    printf("  F 扩展测试 — 单精度浮点指令\n");
    printf("═══════════════════════════════════════\n");

    test_flw_fsw();
    test_fp_arith();
    test_fsqrt();
    test_fsgnj();
    test_fminmax();
    test_fcmp();
    test_fcvt_w();
    test_fcvt_s();
    test_fmv();

    printf("\n═══════════════════════════════════════\n");
    printf("  结果: %d 通过, %d 失败, 共 %d 项\n", passed, failed, passed + failed);
    printf("═══════════════════════════════════════\n");

    return failed ? 1 : 0;
}
