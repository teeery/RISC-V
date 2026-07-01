#ifndef DECODE_H
#define DECODE_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================
 * decode.h — RISC-V 指令解码接口
 *
 * 一条 32 位指令的内存布局：
 *   31        25 24    20 19    15 14    12 11      7 6        0
 *  ┌───────────┬────────┬────────┬────────┬─────────┬──────────┐
 *  │  funct7   │  rs2   │  rs1   │ funct3 │   rd    │  opcode  │
 *  │   7 位    │  5 位  │  5 位  │  3 位   │  5 位   │   7 位   │
 *  └───────────┴────────┴────────┴────────┴─────────┴──────────┘
 *
 * 零依赖（纯宏定义 + 结构体），可独立编译测试
 * ============================================================ */

/* ── 位域提取宏 ── */
#define OPCODE(instr)   ((instr) & 0x7F)            // [6:0]
#define RD(instr)       (((instr) >> 7) & 0x1F)     // [11:7]
#define FUNCT3(instr)   (((instr) >> 12) & 0x7)     // [14:12]
#define RS1(instr)      (((instr) >> 15) & 0x1F)    // [19:15]
#define RS2(instr)      (((instr) >> 20) & 0x1F)    // [24:20]
#define FUNCT7(instr)   (((instr) >> 25) & 0x7F)    // [31:25]

/* ── 5 种立即数拼接宏（RISC-V 共 6 种指令格式，其中 R-type 无立即数，其余 5 种各有不同立即数布局）── */

/* I-type: 12 位立即数，位于 [31:20]，符号扩展 */
#define IMM_I(instr)  ((int32_t)((instr) >> 20))

/* S-type: 12 位立即数，拆成两段：高位 [31:25] + 低位 [11:7] */
#define IMM_S(instr)  ((int32_t)((((instr) >> 25) << 5) | \
                                  (((instr) >> 7) & 0x1F)))

/* B-type: 13 位立即数，bit[0] 恒为 0，分散在 4 段 */
#define IMM_B(instr)  ((int32_t)((((instr) >> 31) << 12) | \
                                  ((((instr) >> 7) & 1) << 11) | \
                                  ((((instr) >> 25) & 0x3F) << 5) | \
                                  ((((instr) >> 8) & 0xF) << 1)))

/* U-type: 高 20 位立即数，低 12 位为 0 */
#define IMM_U(instr)  ((int32_t)((instr) & 0xFFFFF000))

/* J-type: 21 位立即数，bit[0] 恒为 0，分散在 4 段 */
#define IMM_J(instr)  ((int32_t)((((instr) >> 31) << 20) | \
                                  ((((instr) >> 12) & 0xFF) << 12) | \
                                  ((((instr) >> 20) & 1) << 11) | \
                                  ((((instr) >> 21) & 0x3FF) << 1)))

/* ── 解码结果结构体 ── */
typedef struct {
    uint8_t  opcode;   // 指令类型大类 [6:0]
    uint8_t  rd;       // 目标寄存器号 (0-31)
    uint8_t  rs1;      // 源寄存器1号  (0-31)
    uint8_t  rs2;      // 源寄存器2号  (0-31)
    uint8_t  funct3;   // 3 位功能码
    uint8_t  funct7;   // 7 位功能码（R-type 用）
    int32_t  imm;      // 立即数（已符号扩展到 32 位）
} DecodedInstr;

/* 解码一条 32 位指令 → DecodedInstr */
DecodedInstr cpu_decode(uint32_t instr);

/* 反汇编：instr → 人类可读的汇编字符串（供 Debugger 使用）*/
void cpu_disasm(uint32_t instr, uint32_t pc, char *buf, size_t bufsz);

#endif // DECODE_H
