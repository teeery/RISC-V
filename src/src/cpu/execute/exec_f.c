/* ============================================================
 * exec_f.c — F 扩展（单精度浮点），26 条指令
 *
 * 指令分布：
 *   opcode=0x07 LOAD-FP:  FLW         — 从内存加载 32 位浮点数到 fregs
 *   opcode=0x27 STORE-FP: FSW         — 将 fregs 中的 32 位浮点数写入内存
 *   opcode=0x43 OP-FP:    浮点运算指令（funct7 区分具体操作）
 *
 * OP-FP 内部按 funct7 分为：
 *   funct7=0x00: FADD.S   — 浮点加法
 *   funct7=0x04: FSUB.S   — 浮点减法
 *   funct7=0x08: FMUL.S   — 浮点乘法
 *   funct7=0x0C: FDIV.S   — 浮点除法
 *   funct7=0x10: FSGNJ.S  — 符号注入（funct3=0/1/2 区分 FSGNJ/FSGNJN/FSGNJX）
 *   funct7=0x14: FMIN.S / FMAX.S
 *   funct7=0x2C: FSQRT.S  — 平方根（funct3=0，rs2 未使用）
 *   funct7=0x50: FEQ.S / FLT.S / FLE.S — 浮点比较
 *   funct7=0x60: FCVT.W.S / FCVT.WU.S  — 浮点→整数
 *   funct7=0x68: FCVT.S.W / FCVT.S.WU  — 整数→浮点
 *   funct7=0x70: FMV.X.W / FCLASS.S    — freg→整数寄存器搬移
 *   funct7=0x78: FMV.W.X               — 整数寄存器→freg搬移
 *
 * 实现要点：
 *   - 需要 cpu.h 中新增 uint32_t fregs[32] 浮点寄存器组
 *   - 浮点运算使用 C 标准 float 类型（IEEE 754 单精度）：
 *     uint32_t → float:  memcpy 或 union { uint32_t u; float f; }
 *     float → uint32_t:  同理
 *   - FLW/FSW 通过 mmu_read_32 / mmu_write_32 访存（和 LW/SW 一样）
 *   - 浮点异常（除零、上溢、NaN）第一阶段暂不处理，允许产生 inf/NaN
 *
 * 调用路径：
 *   0x07 → exec_load_fp()   |  cpu_execute() 直接分发
 *   0x27 → exec_store_fp()  |
 *   0x43 → exec_fp_op()     |
 * ============================================================
 *
 * 开发说明：
 *   - 两人并行时，此文件由负责 F 拓展的人独占开发
 *   - 写 FLW/FSW 时直接调用 mmu_read_32 / mmu_write_32
 *     （浮点 load/store 和整数 load/store 是同一个物理内存操作）
 *   - 和 M 扩展（exec_m.c）的并行建议：
 *     两人在各自的 exec_*.c 中独立测试，最终在 execute.c 中只需
 *     确认三个 opcode case (0x07/0x27/0x43) 已分发到对应函数
 */

#include "cpu/execute.h"
#include "cpu/decode.h"
#include "simulator.h"
#include "memory/mmu.h"
#include "types.h"
#include "cpu/exec_internal.h"
#include <math.h>

/* ── FLW: 从内存加载 32 位浮点数 ────────────────────────────── */
bool exec_load_fp(Simulator *sim, DecodedInstr *dec, uint32_t *next_pc)
{
    (void)next_pc;
    uint32_t addr = sim->cpu.regs[dec->rs1] + (uint32_t)dec->imm;
    uint32_t val;
    ExceptionType exc = EXC_NONE;
    if(mmu_read_32(&sim->mmu, &sim->pmem, addr, &val, sim->cpu.priv, &exc)) {
        sim->cpu.fregs[dec->rd] = val;   // 直接存 IEEE 754 位模式
        return true;
    } else {
        cpu_trap(sim, (uint32_t)exc, addr);
        return false;
    }


}

/* ── FSW: 将 32 位浮点数写入内存 ────────────────────────────── */
bool exec_store_fp(Simulator *sim, DecodedInstr *dec, uint32_t *next_pc)
{
    (void)next_pc;
    uint32_t addr = sim->cpu.regs[dec->rs1] + (uint32_t)dec->imm;
    uint32_t data = sim->cpu.fregs[dec->rs2];  // 从浮点寄存器读位模式
    ExceptionType exc = EXC_NONE;
    if (mmu_write_32(&sim->mmu, &sim->pmem, addr, data, sim->cpu.priv, &exc)) {
        return true;
    } else {
        cpu_trap(sim, (uint32_t)exc, addr);
        return false;
    }

}

/* ── uint32_t ↔ float 互转（避免 strict-aliasing 警告）── */
typedef union { uint32_t u; float f; } fp32_t;

/* ── FP 运算：FADD/FSUB/FMUL/FDIV/FSQRT/FMIN/FMAX/FCVT/FMV/... ─ */
bool exec_fp_op(Simulator *sim, DecodedInstr *dec, uint32_t *next_pc)
{
    (void)next_pc;   // FP 运算不改变 PC

    /* op_a/op_b = 浮点操作数, res = 浮点结果 (IEEE 754 位模式) */
    fp32_t op_a, op_b, res;
    uint32_t bits;
    int32_t  s32;
    uint32_t u32;

    switch (dec->funct7) {

    /* ── 四则运算：FADD.S / FSUB.S / FMUL.S / FDIV.S ──────────── */
    case 0x00: /* FADD.S */
        op_a.u = sim->cpu.fregs[dec->rs1];
        op_b.u = sim->cpu.fregs[dec->rs2];
        res.f = op_a.f + op_b.f;
        sim->cpu.fregs[dec->rd] = res.u;
        return true;

    case 0x04: /* FSUB.S */
        op_a.u = sim->cpu.fregs[dec->rs1];
        op_b.u = sim->cpu.fregs[dec->rs2];
        res.f = op_a.f - op_b.f;
        sim->cpu.fregs[dec->rd] = res.u;
        return true;

    case 0x08: /* FMUL.S */
        op_a.u = sim->cpu.fregs[dec->rs1];
        op_b.u = sim->cpu.fregs[dec->rs2];
        res.f = op_a.f * op_b.f;
        sim->cpu.fregs[dec->rd] = res.u;
        return true;

    case 0x0C: /* FDIV.S */
        op_a.u = sim->cpu.fregs[dec->rs1];
        op_b.u = sim->cpu.fregs[dec->rs2];
        res.f = op_a.f / op_b.f;
        sim->cpu.fregs[dec->rd] = res.u;
        return true;

    /* ── 平方根：FSQRT.S ───────────────────────────────────────── */
    case 0x2C: /* FSQRT.S */
        op_a.u = sim->cpu.fregs[dec->rs1];
        res.f = sqrtf(op_a.f);
        sim->cpu.fregs[dec->rd] = res.u;
        return true;

    /* ── 符号注入：FSGNJ.S / FSGNJN.S / FSGNJX.S (funct3=0/1/2) ─ */
    case 0x10:
        bits = sim->cpu.fregs[dec->rs1] & 0x7FFFFFFF;   // rs1 数值部分（去掉符号）
        switch (dec->funct3) {
        case 0: /* FSGNJ.S — 用 rs2 的符号 */
            sim->cpu.fregs[dec->rd] = bits | (sim->cpu.fregs[dec->rs2] & 0x80000000);
            return true;
        case 1: /* FSGNJN.S — 用 rs2 的反符号 */
            sim->cpu.fregs[dec->rd] = bits | (~sim->cpu.fregs[dec->rs2] & 0x80000000);
            return true;
        case 2: /* FSGNJX.S — XOR 符号 */
            sim->cpu.fregs[dec->rd] = (sim->cpu.fregs[dec->rs1] & 0x7FFFFFFF)
                                  | ((sim->cpu.fregs[dec->rs1]
                                     ^ sim->cpu.fregs[dec->rs2]) & 0x80000000);
            return true;
        }
        break;

    /* ── 最值：FMIN.S / FMAX.S (funct3=0/1) ───────────────────── */
    case 0x14:
        op_a.u = sim->cpu.fregs[dec->rs1];
        op_b.u = sim->cpu.fregs[dec->rs2];
        switch (dec->funct3) {
        case 0: /* FMIN.S */
            res.f = fminf(op_a.f, op_b.f);
            sim->cpu.fregs[dec->rd] = res.u;
            return true;
        case 1: /* FMAX.S */
            res.f = fmaxf(op_a.f, op_b.f);
            sim->cpu.fregs[dec->rd] = res.u;
            return true;
        }
        break;

    /* ── 比较：FLE.S / FLT.S / FEQ.S (funct3=0/1/2) ──────────── */
    /* 结果写入整数寄存器 */
    case 0x50:
        op_a.u = sim->cpu.fregs[dec->rs1];
        op_b.u = sim->cpu.fregs[dec->rs2];
        switch (dec->funct3) {
        case 0: /* FLE.S — rs1 <= rs2 */
            sim->cpu.regs[dec->rd] = (op_a.f <= op_b.f) ? 1 : 0;
            return true;
        case 1: /* FLT.S — rs1 < rs2 */
            sim->cpu.regs[dec->rd] = (op_a.f < op_b.f) ? 1 : 0;
            return true;
        case 2: /* FEQ.S — rs1 == rs2 */
            sim->cpu.regs[dec->rd] = (op_a.f == op_b.f) ? 1 : 0;
            return true;
        }
        break;

    /* ── float → 整数：FCVT.W.S / FCVT.WU.S (rs2=0/1) ────────── */
    case 0x60:
        op_a.u = sim->cpu.fregs[dec->rs1];
        if (dec->rs2 == 0) {
            /* FCVT.W.S — 有符号 */
            s32 = (int32_t)op_a.f;      // C 截断向零，符合 RISC-V RTZ
            sim->cpu.regs[dec->rd] = (uint32_t)s32;
        } else {
            /* FCVT.WU.S — 无符号 */
            u32 = (uint32_t)op_a.f;     // C 截断向零
            sim->cpu.regs[dec->rd] = u32;
        }
        return true;

    /* ── 整数 → float：FCVT.S.W / FCVT.S.WU (rs2=0/1) ────────── */
    case 0x68:
        if (dec->rs2 == 0) {
            /* FCVT.S.W */
            s32 = (int32_t)sim->cpu.regs[dec->rs1];
            res.f = (float)s32;
        } else {
            /* FCVT.S.WU */
            u32 = sim->cpu.regs[dec->rs1];
            res.f = (float)u32;
        }
        sim->cpu.fregs[dec->rd] = res.u;
        return true;

    /* ── freg → 整数：FMV.X.W / FCLASS.S (funct3=0/1) ────────── */
    case 0x70:
        switch (dec->funct3) {
        case 0: /* FMV.X.W — 浮点寄存器位拷贝到整数寄存器 */
            sim->cpu.regs[dec->rd] = sim->cpu.fregs[dec->rs1];
            return true;
        case 1: { /* FCLASS.S — 返回 rs1 的浮点类型码 */
            op_a.u = sim->cpu.fregs[dec->rs1];
            int sign  = (op_a.u >> 31) & 1;
            int exp   = (op_a.u >> 23) & 0xFF;
            int mant  = op_a.u & 0x7FFFFF;

            if (exp == 0 && mant == 0)
                sim->cpu.regs[dec->rd] = sign ? (1 << 3) : (1 << 4);   // ±0
            else if (exp == 0)
                sim->cpu.regs[dec->rd] = sign ? (1 << 2) : (1 << 5);   // ±subnormal
            else if (exp == 0xFF && mant == 0)
                sim->cpu.regs[dec->rd] = sign ? (1 << 0) : (1 << 7);   // ±∞
            else if (exp == 0xFF && mant != 0)
                sim->cpu.regs[dec->rd] = sign ? (1 << 8) : (1 << 9);   // ±NaN(quiet/signal)
            else
                sim->cpu.regs[dec->rd] = sign ? (1 << 1) : (1 << 6);   // ±normal
            return true;
        }
        }
        break;

    /* ── 整数 → freg：FMV.W.X (funct3=0) ───────────────────────── */
    case 0x78: /* FMV.W.X */
        sim->cpu.fregs[dec->rd] = sim->cpu.regs[dec->rs1];
        return true;

    } /* switch(dec->funct7) */

    /* 未识别的 funct7 */
    cpu_trap(sim, EXC_ILLEGAL_INST, dec->opcode);
    return false;
}

/* ── FMA 融合乘加 4 条：FMADD/FMSUB/FNMSUB/FNMADD.S ─────────────
 *
 * R4 格式（4 个寄存器）：rd = op(rs1, rs2, rs3)
 *   31    27 26   25 24   20 19   15 14   12 11   7 6      0
 *  ┌───────┬──────┬───────┬───────┬───────┬───────┬─────────┐
 *  │  rs3  │ fmt  │  rs2  │  rs1  │ funct3│  rd   │ opcode  │
 *  └───────┴──────┴───────┴───────┴───────┴───────┴─────────┘
 *
 * opcode: 0x43=FMADD, 0x47=FMSUB, 0x4B=FNMSUB, 0x4F=FNMADD
 * fmt (bit[26:25]) = 00 表示单精度 .S
 *
 * TODO: 用 fmaf() 替代 op_a*op_b+op_c 以获得单次舍入的精确结果
 * ================================================================== */
bool exec_fma(Simulator *sim, DecodedInstr *dec, uint32_t *next_pc)
{
    (void)next_pc;

    /* op_a/op_b/op_c = 操作数, res = 结果 */
    fp32_t op_a, op_b, op_c, res;
    op_a.u = sim->cpu.fregs[dec->rs1];
    op_b.u = sim->cpu.fregs[dec->rs2];
    op_c.u = sim->cpu.fregs[dec->rs3];       // R4 格式：第三源寄存器

    switch (dec->opcode) {
    case 0x43: res.f = op_a.f * op_b.f + op_c.f; break;      // FMADD.S
    case 0x47: res.f = op_a.f * op_b.f - op_c.f; break;      // FMSUB.S
    case 0x4B: res.f = -(op_a.f * op_b.f) + op_c.f; break;   // FNMSUB.S
    case 0x4F: res.f = -(op_a.f * op_b.f) - op_c.f; break;   // FNMADD.S
    default:
        cpu_trap(sim, EXC_ILLEGAL_INST, dec->opcode);
        return false;
    }

    sim->cpu.fregs[dec->rd] = res.u;
    return true;
}
