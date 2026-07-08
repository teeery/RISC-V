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
 *   - 除零：DIV/DIVU  → 商 = -1 (全1)；REM/REMU → 余 = 被除数
 *   - 溢出：DIV(-2^31, -1) → 商 = -2^31 (0x80000000)；REM → 余 = 0
 *   - 乘法：MULH/MULHSU/MULHU 用 64 位中间结果 ((u)int64_t)，再取高/低 32 位
 * ============================================================
 */

#include "cpu/execute.h"
#include "cpu/decode.h"
#include "simulator.h"
#include "types.h"
#include "cpu/exec_internal.h"

bool exec_m_muldiv(Simulator *sim, DecodedInstr *dec, uint32_t *next_pc)
{
    (void)next_pc;  // 当前所有 M 指令均不修改 next_pc

    /* dec = 解码后的指令, op_a/op_b = 从寄存器文件读出的操作数 */
    CPU *cpu = &sim->cpu;
    uint32_t op_a = cpu->regs[dec->rs1];  // 操作数 A（rs1 的值）
    uint32_t op_b = cpu->regs[dec->rs2];  // 操作数 B（rs2 的值）

    switch (dec->funct3){
        case 0: { /* MUL — 乘法低 32 位（有符号/无符号结果相同） */
            uint64_t product = (uint64_t)op_a * (uint64_t)op_b;
            cpu->regs[dec->rd] = (uint32_t)(product & 0xFFFFFFFFu);
            return true;
        }
        case 1: { /* MULH — 有符号 × 有符号 → 高 32 位 */
            int64_t product = (int64_t)(int32_t)op_a * (int64_t)(int32_t)op_b;
            cpu->regs[dec->rd] = (uint32_t)((uint64_t)product >> 32);
            return true;
        }
        case 2: { /* MULHSU — 有符号 × 无符号 → 高 32 位 */
            int64_t product = (int64_t)(int32_t)op_a * (int64_t)(uint64_t)op_b;
            cpu->regs[dec->rd] = (uint32_t)((uint64_t)product >> 32);
            return true;
        }
        case 3: { /* MULHU — 无符号 × 无符号 → 高 32 位 */
            uint64_t product = (uint64_t)op_a * (uint64_t)op_b;
            cpu->regs[dec->rd] = (uint32_t)(product >> 32);
            return true;
        }
        case 4: { /* DIV — 有符号除法（除零=-1，溢出=被除数） */
            int32_t dividend = (int32_t)op_a;
            int32_t divisor  = (int32_t)op_b;
            if (divisor == 0) {
                cpu->regs[dec->rd] = (uint32_t)-1;          // 除零 → 全 1
            } else if (dividend == INT32_MIN && divisor == -1) {
                cpu->regs[dec->rd] = (uint32_t)INT32_MIN;   // 溢出 → 被除数
            } else {
                cpu->regs[dec->rd] = (uint32_t)(dividend / divisor);
            }
            return true;
        }
        case 5: { /* DIVU — 无符号除法（除零=全1） */
            if (op_b == 0) {
                cpu->regs[dec->rd] = (uint32_t)-1;          // 除零 → 全 1
            } else {
                cpu->regs[dec->rd] = op_a / op_b;
            }
            return true;
        }
        case 6: { /* REM — 有符号取余（除零=被除数，溢出=0） */
            int32_t dividend = (int32_t)op_a;
            int32_t divisor  = (int32_t)op_b;
            if (divisor == 0) {
                cpu->regs[dec->rd] = op_a;                  // 除零 → 被除数
            } else if (dividend == INT32_MIN && divisor == -1) {
                cpu->regs[dec->rd] = 0;                     // 溢出 → 0
            } else {
                cpu->regs[dec->rd] = (uint32_t)(dividend % divisor);
            }
            return true;
        }
        case 7: { /* REMU — 无符号取余（除零=被除数） */
            if (op_b == 0) {
                cpu->regs[dec->rd] = op_a;                  // 除零 → 被除数
            } else {
                cpu->regs[dec->rd] = op_a % op_b;
            }
            return true;
        }
        default:
            cpu_trap(sim, EXC_ILLEGAL_INST, dec->opcode);
            return false;
    }
}
