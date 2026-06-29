/* ============================================================================
 * decode.h — 指令解码
 * ============================================================================
 *
 * RISC-V 指令只有 6 种格式，解码非常规整。
 *
 * ---- 位域提取宏 ----
 *
 *   #define OPCODE(insn)   ((insn) & 0x7F)
 *   #define RD(insn)       (((insn) >> 7) & 0x1F)
 *   #define FUNCT3(insn)   (((insn) >> 12) & 0x7)
 *   #define RS1(insn)      (((insn) >> 15) & 0x1F)
 *   #define RS2(insn)      (((insn) >> 20) & 0x1F)
 *   #define FUNCT7(insn)   (((insn) >> 25) & 0x7F)
 *
 * ---- 立即数拼接宏（最容易写错的地方！）----
 *
 * I-type (12位，用于 addi/lw/jalr 等):
 *   #define IMM_I(insn)  ((int32_t)((insn) >> 20))
 *   解读：取指令 [31:20] 共 12 位，强制转为有符号数实现符号扩展
 *
 * S-type (12位，用于 sw/sh/sb):
 *   #define IMM_S(insn)  ...  // 高位在 [31:25]，低位在 [11:7]
 *   解读：立即数被拆成两段，需要拼接
 *
 * B-type (13位，但 bit[0] 恒为 0，用于 beq/bne/blt 等):
 *   #define IMM_B(insn)  ...  // 分散在 4 个区域
 *   解读：bit[0] 永远是 0（因为地址必须是 2 字节对齐）
 *
 * U-type (32位，高 20 位来自指令，低 12 位为 0，用于 lui/auipc):
 *   #define IMM_U(insn)  ((int32_t)((insn) & 0xFFFFF000))
 *
 * J-type (21位，但 bit[0] 恒为 0，用于 jal):
 *   #define IMM_J(insn)  ...  // 类似 B-type，分散在 4 个区域
 *
 * ---- 解码结果结构体 ----
 *
 *   typedef struct {
 *       uint8_t  opcode;   // 指令类型大类
 *       uint8_t  rd;       // 目标寄存器号
 *       uint8_t  rs1;      // 源寄存器1号
 *       uint8_t  rs2;      // 源寄存器2号
 *       uint8_t  funct3;   // 功能码（进一步细分）
 *       uint8_t  funct7;   // 功能码（R-type 细分）
 *       int32_t  imm;      // 立即数（已符号扩展）
 *   } DecodedInsn;
 *
 *   // 解码一条 32 位指令
 *   DecodedInsn cpu_decode(uint32_t insn);
 *
 * ---- 反汇编器 ----
 *   // 把一条指令转成人类可读的字符串（调试器用它来显示 x addr 的结果）
 *   void cpu_disasm(uint32_t insn, uint32_t pc, char *buf, size_t bufsz);
 *   // 例如输入 0x00300513, pc=0x10074 → buf = "addi a0, zero, 3"
 */

#ifndef DECODE_H
#define DECODE_H

#include "common.h"

/* ---- 在这里定义位域提取宏和解码结构体 ---- */

#endif /* DECODE_H */
