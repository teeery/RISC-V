/* ============================================================================
 * decode.c — 指令解码实现
 * ============================================================================
 *
 * 实现 cpu_decode(uint32_t insn)：
 *
 *   1. 提取 opcode = insn & 0x7F
 *   2. 根据 opcode 判断指令格式，调用对应的立即数拼接宏
 *   3. 对于每种 opcode，填充 DecodedInsn 结构体
 *
 * opcode 分类表：
 *   ┌──────────┬──────────┬──────────────────────┐
 *   │ opcode   │ 格式      │ 指令类型              │
 *   ├──────────┼──────────┼──────────────────────┤
 *   │ 0110011  │ R-type   │ OP (add, sub, ...)   │
 *   │ 0010011  │ I-type   │ OP-IMM (addi, ...)   │
 *   │ 0000011  │ I-type   │ LOAD (lw, lh, lb)    │
 *   │ 0100011  │ S-type   │ STORE (sw, sh, sb)   │
 *   │ 1100011  │ B-type   │ BRANCH (beq, bne..)  │
 *   │ 1101111  │ J-type   │ JAL                  │
 *   │ 1100111  │ I-type   │ JALR                 │
 *   │ 0110111  │ U-type   │ LUI                  │
 *   │ 0010111  │ U-type   │ AUIPC                │
 *   │ 0001111  │ I-type   │ FENCE / FENCE.I      │
 *   │ 1110011  │ I-type   │ SYSTEM (ecall/csr)   │
 *   │ 0111011  │ R-type   │ M扩展 (mul, div...)  │
 *   └──────────┴──────────┴──────────────────────┘
 *
 * 实现 cpu_disasm(uint32_t insn, uint32_t pc, char *buf, size_t bufsz)：
 *   根据解码结果，用 snprintf 生成汇编字符串。
 *   查阅 RISC-V 规范中每条指令对应的 ABI 寄存器名称。
 *   例如 rd=10 → 输出 "a0"，rs1=0 → 输出 "zero"，rs2=1 → 输出 "ra"
 *
 *   ABI 名称映射表（供反汇编使用）：
 *     x0→zero, x1→ra, x2→sp, x3→gp, x4→tp,
 *     x5→t0, x6→t1, x7→t2, x8→s0, x9→s1,
 *     x10→a0, x11→a1, x12→a2, x13→a3, x14→a4, x15→a5, x16→a6, x17→a7,
 *     x18→s2, x19→s3, x20→s4, x21→s5, x22→s6, x23→s7, x24→s8, x25→s9,
 *     x26→s10, x27→s11, x28→t3, x29→t4, x30→t5, x31→t6
 */

#include "decode.h"

/* ---- 在这里实现 cpu_decode 和 cpu_disasm ---- */
