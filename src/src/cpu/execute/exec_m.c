/* ============================================================
 * exec_m.c — M 扩展（乘除法），8 条指令
 *
 * 指令列表（均为 R-type，opcode=0x33, funct7=0x01）：
 *   funct3=0: MUL     — 乘法低 32 位
 *   funct3=1: MULH    — 乘法高 32 位（有符号×有符号）
 *   funct3=2: MULHSU  — 乘法高 32 位（有符号×无符号）
 *   funct3=3: MULHU   — 乘法高 32 位（无符号×无符号）
 *   funct3=4: DIV     — 有符号除法
 *   funct3=5: DIVU    — 无符号除法
 *   funct3=6: REM     — 有符号取余
 *   funct3=7: REMU    — 无符号取余
 *
 * 调用路径：cpu_execute() → exec_op() → exec_m_muldiv()
 * （由 exec_rv32i.c 中的 exec_op() 在 funct7==1 时分流）
 *
 * 边界情况：
 *   - 除零：DIV/DIVU → 商 = -1 (全1)，余 = 被除数
 *   - 溢出：DIV(-2^31, -1) → 商 = -2^31 (即 0x80000000)，余 = 0
 *   - MULH/MULHSU/MULHU：需要 64 位中间结果（(int64_t)a * (int64_t)b）
 * ============================================================
 *
 * 开发说明：
 *   - 所有操作数来自 cpu->regs[d->rs1] 和 cpu->regs[d->rs2]
 *   - 结果写入 cpu->regs[d->rd]
 *   - 使用 (int64_t) 强转做 64 位乘法，再取高/低 32 位
 *
 * 待实现：全部 8 条指令。
 */

#include "cpu/execute.h"
#include "cpu/decode.h"
#include "simulator.h"
#include "types.h"
#include "cpu/exec_internal.h"

bool exec_m_muldiv(Simulator *sim, DecodedInstr *d, uint32_t *next_pc)
{
    (void)sim;
    (void)d;
    (void)next_pc;

    /* TODO: 实现 8 条乘除法指令
     *
     * CPU *cpu = &sim->cpu;
     * uint32_t a = cpu->regs[d->rs1];
     * uint32_t b = cpu->regs[d->rs2];
     *
     * switch (d->funct3) {
     * case 0: // MUL
     *     cpu->regs[d->rd] = a * b;
     *     break;
     * case 4: // DIV
     *     if (b == 0) {
     *         cpu->regs[d->rd] = (uint32_t)(-1);
     *     } else if ((int32_t)a == INT32_MIN && (int32_t)b == -1) {
     *         cpu->regs[d->rd] = (uint32_t)INT32_MIN;
     *     } else {
     *         cpu->regs[d->rd] = (uint32_t)((int32_t)a / (int32_t)b);
     *     }
     *     break;
     * ...
     * }
     */

    /* 暂未实现 → 触发非法指令异常 */
    cpu_trap(sim, EXC_ILLEGAL_INST, d->opcode);
    return false;
}
