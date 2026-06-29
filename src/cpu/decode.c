#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* ============================================================
 * decode.c — RISC-V RV32I + M 扩展指令译码器
 *
 * 这是整个模拟器最核心的文件之一。
 * 输入：32 位原始指令
 * 输出：DecodedInstruction 结构体 (opcode, rd, rs1, rs2, funct3, funct7, imm, fmt)
 *
 * ─── 你需要实现的内容 ─────────────────────────────────────
 *
 * 1. decode(uint32_t raw, DecodedInstruction *dec)
 *    根据指令的低 7 位 opcode 判断指令格式，提取各字段
 *
 * 2. RV32I 指令格式 (6 种) 的位域分布：
 *
 *   R-type:  funct7[31:25] | rs2[24:20] | rs1[19:15] | funct3[14:12] | rd[11:7] | opcode[6:0]
 *   I-type:  imm[31:20]    | rs1[19:15] | funct3[14:12] | rd[11:7] | opcode[6:0]
 *   S-type:  imm[11:5][31:25] | rs2[24:20] | rs1[19:15] | funct3[14:12] | imm[4:0][11:7] | opcode[6:0]
 *   B-type:  imm[12|10:5][31:25] | rs2[24:20] | rs1[19:15] | funct3[14:12] | imm[4:1|11][11:7] | opcode[6:0]
 *   U-type:  imm[31:12][31:12] | rd[11:7] | opcode[6:0]
 *   J-type:  imm[20|10:1|11|19:12][31:12] | rd[11:7] | opcode[6:0]
 *
 *   你需要实现 6 个辅助函数来提取每种格式：
 *     decode_r_type(), decode_i_type(), decode_s_type(),
 *     decode_b_type(), decode_u_type(), decode_j_type()
 *
 * 3. RV32I 指令分类 (按 opcode)：
 *   - 0110111 LUI      (U-type)   → rd = imm (高20位)
 *   - 0010111 AUIPC    (U-type)   → rd = PC + imm
 *   - 1101111 JAL      (J-type)   → rd = PC+4, PC += imm
 *   - 1100111 JALR     (I-type)   → rd = PC+4, PC = (rs1+imm)&~1
 *   - 1100011 BRANCH   (B-type)   → BEQ/BNE/BLT/BGE/BLTU/BGEU
 *   - 0000011 LOAD     (I-type)   → LB/LH/LW/LBU/LHU
 *   - 0100011 STORE    (S-type)   → SB/SH/SW
 *   - 0010011 OP-IMM   (I-type)   → ADDI/SLTI/SLTIU/XORI/ORI/ANDI/SLLI/SRLI/SRAI
 *   - 0110011 OP       (R-type)   → ADD/SUB/SLL/SLT/SLTU/XOR/SRL/SRA/OR/AND
 *   - 0001111 MISC-MEM (I-type)   → FENCE/FENCE.I (可做 NOP)
 *   - 1110011 SYSTEM   (I-type)   → ECALL/EBREAK/CSRRW/CSRRS/CSRRC/CSRRWI/CSRRSI/CSRRCI
 *
 * 4. M 扩展指令 (OP 操作码, funct7=0000001)：
 *   - MUL/MULH/MULHSU/MULHU (funct3=000-011)
 *   - DIV/DIVU/REM/REMU     (funct3=100-111)
 *   共 8 条，使用 R-type 格式
 *
 * 5. 立即数符号扩展：
 *   I-type imm: 12 位 → 符号扩展到 32 位 (将 12 位视为有符号数)
 *   S-type imm: 12 位 → 符号扩展
 *   B-type imm: 13 位 (bit0 恒为 0) → 符号扩展
 *   U-type imm: 20 位 → 左移 12 位后符号扩展 (= imm << 12)
 *   J-type imm: 21 位 (bit0 恒为 0) → 符号扩展
 *
 * 6. 未知指令处理：如果 opcode 不匹配任何已知模式，
 *    返回 false，上层代码会抛出 IllegalInstruction 异常
 *
 * ─── 测试建议 ─────────────────────────────────────────────
 * 使用在线 RISC-V 汇编器生成指令机器码，验证解码结果
 * 例如: addi x5, x0, 42 → 0x02A00293
 *       (rd=5=x5, rs1=0=x0, funct3=000, imm=42=0x02A, opcode=0010011)
 * ============================================================
 */

#include "types.h"

/* ── 位域提取宏 ─────────────────────────────────────────── */
#define MASK(n)  ((1u << (n)) - 1)
#define BITS(x, hi, lo)  (((x) >> (lo)) & MASK((hi)-(lo)+1))
#define SIGN_EXT(x, n)   (((int32_t)((x) << (32 - (n)))) >> (32 - (n)))

/* ── 指令格式解码 ────────────────────────────────────────── */

/* R-type: ADD rd, rs1, rs2 等 */
static void decode_r_type(uint32_t raw, DecodedInstruction *dec)
{
    dec->opcode = BITS(raw, 6, 0);     // 7 bits
    dec->rd     = BITS(raw, 11, 7);    // 5 bits
    dec->funct3 = BITS(raw, 14, 12);   // 3 bits
    dec->rs1    = BITS(raw, 19, 15);   // 5 bits
    dec->rs2    = BITS(raw, 24, 20);   // 5 bits
    dec->funct7 = BITS(raw, 31, 25);   // 7 bits
    dec->imm    = 0;
    dec->fmt    = FMT_R;
}

/* I-type: ADDI, LW, JALR 等 (12 位立即数, 符号扩展) */
static void decode_i_type(uint32_t raw, DecodedInstruction *dec)
{
    dec->opcode = BITS(raw, 6, 0);
    dec->rd     = BITS(raw, 11, 7);
    dec->funct3 = BITS(raw, 14, 12);
    dec->rs1    = BITS(raw, 19, 15);
    dec->rs2    = 0;
    dec->funct7 = 0;
    dec->imm    = SIGN_EXT(BITS(raw, 31, 20), 12);   // 12-bit imm
    dec->fmt    = FMT_I;
}

/* S-type: SW rs2, offset(rs1) (12 位立即数，分两段) */
static void decode_s_type(uint32_t raw, DecodedInstruction *dec)
{
    dec->opcode = BITS(raw, 6, 0);
    int32_t imm_4_0  = BITS(raw, 11, 7);     // imm[4:0]
    dec->funct3 = BITS(raw, 14, 12);
    dec->rs1    = BITS(raw, 19, 15);
    dec->rs2    = BITS(raw, 24, 20);
    int32_t imm_11_5 = BITS(raw, 31, 25);    // imm[11:5]
    dec->imm    = SIGN_EXT((imm_11_5 << 5) | imm_4_0, 12);
    dec->rd     = 0;
    dec->funct7 = 0;
    dec->fmt    = FMT_S;
}

/* B-type: BEQ rs1, rs2, offset (13 位立即数，bit0 恒为 0) */
static void decode_b_type(uint32_t raw, DecodedInstruction *dec)
{
    dec->opcode = BITS(raw, 6, 0);
    int32_t imm_11    = BITS(raw, 7, 7);      // imm[11]
    int32_t imm_4_1   = BITS(raw, 11, 8);     // imm[4:1]
    dec->funct3 = BITS(raw, 14, 12);
    dec->rs1    = BITS(raw, 19, 15);
    dec->rs2    = BITS(raw, 24, 20);
    int32_t imm_10_5  = BITS(raw, 30, 25);    // imm[10:5]
    int32_t imm_12    = BITS(raw, 31, 31);     // imm[12]
    dec->imm    = SIGN_EXT(
        (imm_12 << 12) | (imm_11 << 11) | (imm_10_5 << 5) | (imm_4_1 << 1),
        13);
    dec->rd     = 0;
    dec->funct7 = 0;
    dec->fmt    = FMT_B;
}

/* U-type: LUI rd, imm20 / AUIPC rd, imm20 (20 位立即数，高 20 位) */
static void decode_u_type(uint32_t raw, DecodedInstruction *dec)
{
    dec->opcode = BITS(raw, 6, 0);
    dec->rd     = BITS(raw, 11, 7);
    dec->imm    = (int32_t)(BITS(raw, 31, 12) << 12);  // imm[31:12]
    dec->rs1    = 0;
    dec->rs2    = 0;
    dec->funct3 = 0;
    dec->funct7 = 0;
    dec->fmt    = FMT_U;
}

/* J-type: JAL rd, offset (21 位立即数，bit0 恒为 0) */
static void decode_j_type(uint32_t raw, DecodedInstruction *dec)
{
    dec->opcode = BITS(raw, 6, 0);
    dec->rd     = BITS(raw, 11, 7);
    int32_t imm_20    = BITS(raw, 31, 31);     // imm[20]
    int32_t imm_10_1  = BITS(raw, 30, 21);     // imm[10:1]
    int32_t imm_11    = BITS(raw, 20, 20);     // imm[11]
    int32_t imm_19_12 = BITS(raw, 19, 12);     // imm[19:12]
    dec->imm    = SIGN_EXT(
        (imm_20 << 20) | (imm_19_12 << 12) | (imm_11 << 11) | (imm_10_1 << 1),
        21);
    dec->rs1    = 0;
    dec->rs2    = 0;
    dec->funct3 = 0;
    dec->funct7 = 0;
    dec->fmt    = FMT_J;
}

/* ── 主译码函数 ──────────────────────────────────────────── */

/**
 * decode() — 将 32 位原始指令译码为结构化形式
 * 返回 false 表示无法识别的指令 (触发 IllegalInstruction)
 */
bool decode(uint32_t raw, DecodedInstruction *dec)
{
    memset(dec, 0, sizeof(*dec));
    dec->raw = raw;

    uint8_t opcode = BITS(raw, 6, 0);

    switch (opcode) {
    /* ── U-type ──────────────────────────────── */
    case 0b0110111:   // LUI
    case 0b0010111:   // AUIPC
        decode_u_type(raw, dec);
        return true;

    /* ── J-type ──────────────────────────────── */
    case 0b1101111:   // JAL
        decode_j_type(raw, dec);
        return true;

    /* ── I-type ──────────────────────────────── */
    case 0b1100111:   // JALR
    case 0b0000011:   // LOAD (LB/LH/LW/LBU/LHU)
    case 0b0010011:   // OP-IMM (ADDI/SLTI/.../SRAI)
    case 0b0001111:   // MISC-MEM (FENCE/FENCE.I)
        decode_i_type(raw, dec);
        return true;

    /* ── SYSTEM (I-type variant) ─────────────── */
    case 0b1110011:   // ECALL/EBREAK/CSR*
        decode_i_type(raw, dec);
        return true;

    /* ── S-type ──────────────────────────────── */
    case 0b0100011:   // STORE (SB/SH/SW)
        decode_s_type(raw, dec);
        return true;

    /* ── B-type ──────────────────────────────── */
    case 0b1100011:   // BRANCH (BEQ/BNE/BLT/...)
        decode_b_type(raw, dec);
        return true;

    /* ── R-type ──────────────────────────────── */
    case 0b0110011:   // OP (ADD/SUB/.../MUL/DIV/...)
        decode_r_type(raw, dec);
        return true;

    default:
        return false;  // 未知指令
    }
}
