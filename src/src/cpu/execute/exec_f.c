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

/* ── FLW: 从内存加载 32 位浮点数 ────────────────────────────── */
bool exec_load_fp(Simulator *sim, DecodedInstr *d, uint32_t *next_pc)
{
    (void)sim;
    (void)d;
    (void)next_pc;

    /* TODO: 实现 FLW 指令
     *
     * uint32_t addr = sim->cpu.regs[d->rs1] + (uint32_t)d->imm;
     * uint32_t val;
     * ExceptionType exc = EXC_NONE;
     * if (mmu_read_32(&sim->mmu, &sim->pmem, addr, &val, sim->cpu.priv, &exc)) {
     *     sim->cpu.fregs[d->rd] = val;   // 直接存 IEEE 754 位模式
     * } else {
     *     cpu_trap(sim, (uint32_t)exc, addr);
     *     return false;
     * }
     */

    cpu_trap(sim, EXC_ILLEGAL_INST, d->opcode);
    return false;
}

/* ── FSW: 将 32 位浮点数写入内存 ────────────────────────────── */
bool exec_store_fp(Simulator *sim, DecodedInstr *d, uint32_t *next_pc)
{
    (void)sim;
    (void)d;
    (void)next_pc;

    /* TODO: 实现 FSW 指令
     *
     * uint32_t addr = sim->cpu.regs[d->rs1] + (uint32_t)d->imm;
     * uint32_t data = sim->cpu.fregs[d->rs2];  // 从浮点寄存器读位模式
     * ExceptionType exc = EXC_NONE;
     * mmu_write_32(&sim->mmu, &sim->pmem, addr, data, sim->cpu.priv, &exc);
     * if (exc != EXC_NONE) {
     *     cpu_trap(sim, (uint32_t)exc, addr);
     *     return false;
     * }
     */

    cpu_trap(sim, EXC_ILLEGAL_INST, d->opcode);
    return false;
}

/* ── FP 运算：FADD/FSUB/FMUL/FDIV/FSQRT/FMIN/FMAX/FCVT/FMV/... ─ */
bool exec_fp_op(Simulator *sim, DecodedInstr *d, uint32_t *next_pc)
{
    (void)sim;
    (void)d;
    (void)next_pc;

    /* TODO: 实现 24 条浮点运算指令
     *
     * 关键技巧 — uint32_t 和 float 互转（避免 strict-aliasing 警告）：
     *
     *   union { uint32_t u; float f; } a, b, result;
     *   a.u = sim->cpu.fregs[d->rs1];
     *   b.u = sim->cpu.fregs[d->rs2];
     *
     *   switch (d->funct7) {
     *   case 0x00: result.f = a.f + b.f; break;  // FADD.S
     *   case 0x04: result.f = a.f - b.f; break;  // FSUB.S
     *   ...
     *   }
     *   sim->cpu.fregs[d->rd] = result.u;
     *
     * 注意：FCVT.W.S / FCVT.S.W 等转换指令的 rs1/rs2 语义不同，
     * 需仔细对照 RISC-V 规范中每条指令的编码。
     */

    cpu_trap(sim, EXC_ILLEGAL_INST, d->opcode);
    return false;
}
