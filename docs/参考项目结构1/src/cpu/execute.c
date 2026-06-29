#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ============================================================
 * execute.c — RISC-V 指令执行单元
 *
 * 这是模拟器最核心的执行文件。根据解码结果执行对应的指令操作。
 *
 * ─── 你需要实现的内容 ─────────────────────────────────────
 *
 * 1. execute(DecodedInstruction *dec, CPUState *cpu, void *mem, void *mmu)
 *    根据 opcode + funct3 + funct7 分发到具体的执行函数。
 *    返回 false 表示触发了异常 (如 ECALL/EBREAK/非法指令)。
 *
 * ─── 指令分组与实现 ───────────────────────────────────────
 *
 * 【LUI】 rd = imm
 *    将 20 位立即数左移 12 位后加载到 rd
 *
 * 【AUIPC】 rd = PC + imm
 *    将 PC 加上 20 位立即数左移 12 位后的值，存入 rd
 *
 * 【JAL】 rd = PC+4; PC += imm
 *    跳转并链接。返回地址 (PC+4) 保存到 rd (通常 ra=x1)
 *
 * 【JALR】 rd_val = PC+4; PC = (rs1 + imm) & ~1
 *    间接跳转。rd=0 时仅跳转不保存返回地址
 *
 * 【BRANCH】 6 种条件分支：
 *    funct3=000 BEQ  (相等分支):     if rs1 == rs2 → PC += imm
 *    funct3=001 BNE  (不等分支):     if rs1 != rs2 → PC += imm
 *    funct3=100 BLT  (小于)  :       if (int32_t)rs1 <  (int32_t)rs2 → PC += imm
 *    funct3=101 BGE  (大等于):        if (int32_t)rs1 >= (int32_t)rs2 → PC += imm
 *    funct3=110 BLTU (无符号小于):    if rs1 <  rs2 → PC += imm
 *    funct3=111 BGEU (无符号大等于):  if rs1 >= rs2 → PC += imm
 *
 * 【LOAD】 5 种内存加载：
 *    funct3=000 LB  (字节符号扩展):   rd = sext(mem[rs1+imm], 8)
 *    funct3=001 LH  (半字符号扩展):   rd = sext(mem[rs1+imm], 16)
 *    funct3=010 LW  (字):            rd = mem[rs1+imm]
 *    funct3=100 LBU (字节零扩展):     rd = zext(mem[rs1+imm], 8)
 *    funct3=101 LHU (半字零扩展):     rd = zext(mem[rs1+imm], 16)
 *    ★ 需要调用 mmu_read_8/16/32 进行虚拟地址访问
 *
 * 【STORE】 3 种内存存储：
 *    funct3=000 SB: mem[rs1+imm] = rs2[7:0]
 *    funct3=001 SH: mem[rs1+imm] = rs2[15:0]
 *    funct3=010 SW: mem[rs1+imm] = rs2[31:0]
 *    ★ 需要调用 mmu_write_8/16/32
 *
 * 【OP-IMM】 立即数算术/逻辑操作 (13 条)：
 *    ADDI  (000): rd = rs1 + imm
 *    SLTI  (010): rd = (int32_t)rs1 <  imm ? 1 : 0
 *    SLTIU (011): rd = rs1 < (uint32_t)imm ? 1 : 0
 *    XORI  (100): rd = rs1 ^ imm
 *    ORI   (110): rd = rs1 | imm
 *    ANDI  (111): rd = rs1 & imm
 *    SLLI  (001): rd = rs1 << shamt   (shamt = imm[4:0], funct7=0000000)
 *    SRLI  (101): rd = rs1 >> shamt   (逻辑右移, funct7=0000000)
 *    SRAI  (101): rd = (int32_t)rs1 >> shamt (算术右移, funct7=0100000)
 *
 * 【OP】 寄存器-寄存器算术/逻辑操作 (10 条 RV32I + 8 条 M 扩展)：
 *   funct7=0000000:
 *     ADD  (000): rd = rs1 + rs2
 *     SLL  (001): rd = rs1 << (rs2 & 0x1F)
 *     SLT  (010): rd = (int32_t)rs1 <  (int32_t)rs2 ? 1 : 0
 *     SLTU (011): rd = rs1 < rs2 ? 1 : 0
 *     XOR  (100): rd = rs1 ^ rs2
 *     SRL  (101): rd = rs1 >> (rs2 & 0x1F)
 *     OR   (110): rd = rs1 | rs2
 *     AND  (111): rd = rs1 & rs2
 *   funct7=0100000:
 *     SUB  (000): rd = rs1 - rs2
 *     SRA  (101): rd = (int32_t)rs1 >> (rs2 & 0x1F)
 *   funct7=0000001 (M 扩展)：
 *     MUL    (000): rd = (int32_t)rs1 * (int32_t)rs2  (低 32 位)
 *     MULH   (001): rd = ((int64_t)(int32_t)rs1 * (int64_t)(int32_t)rs2) >> 32
 *     MULHSU (010): rd = ((int64_t)(int32_t)rs1 * (uint64_t)rs2) >> 32
 *     MULHU  (011): rd = ((uint64_t)rs1 * (uint64_t)rs2) >> 32
 *     DIV    (100): rd = (int32_t)rs1 / (int32_t)rs2  (rs2=0 → rd=-1)
 *     DIVU   (101): rd = rs1 / rs2  (rs2=0 → rd=2^32-1)
 *     REM    (110): rd = (int32_t)rs1 % (int32_t)rs2  (rs2=0 → rd=rs1)
 *     REMU   (111): rd = rs1 % rs2  (rs2=0 → rd=rs1)
 *
 * 【SYSTEM】 系统指令：
 *   ECALL  (funct3=000, imm=0):   触发环境调用 → syscall_handler()
 *   EBREAK (funct3=000, imm=1):   触发断点异常 → 用于调试器
 *   CSRRW/CSRRS/CSRRC/CSRRWI/CSRRSI/CSRRCI: CSR 读写操作
 *     (具体实现在 csr.c，根据 funct3 区分)
 *
 * 【MISC-MEM】 FENCE / FENCE.I → 单核模拟器中可视为 NOP
 *
 * ─── 重要细节 ──────────────────────────────────────────────
 * ★ x0 寄存器写入保护：rd=0 时跳过写入 (x0 恒为 0)
 * ★ PC 更新：非跳转/分支指令默认 PC+4，跳转指令修改 next_pc
 * ★ 边界检查：LOAD/STORE 地址对齐检查
 * ★ 符号扩展宏：SIGN_EXT(x, n) 将 n 位值符号扩展到 32 位
 * ★ 注意算术右移 (SRA/SRAI) vs 逻辑右移 (SRL/SRLI) 的区别：
 *   逻辑右移高位补 0，算术右移高位补符号位
 * ============================================================
 */

#include "types.h"
#include "memory.h"
#include "mmu.h"
#include "syscall.h"

#define SIGN_EXT(x, n)  (((int32_t)((x) << (32 - (n)))) >> (32 - (n)))

/* 前向声明 */
static bool execute_load(DecodedInstruction *dec, CPUState *cpu,
                         PhysicalMemory *pmem, MMUState *mmu);
static bool execute_store(DecodedInstruction *dec, CPUState *cpu,
                          PhysicalMemory *pmem, MMUState *mmu);
static bool execute_op_imm(DecodedInstruction *dec, CPUState *cpu);
static bool execute_op(DecodedInstruction *dec, CPUState *cpu);
static bool execute_branch(DecodedInstruction *dec, CPUState *cpu);
static bool execute_system(DecodedInstruction *dec, CPUState *cpu,
                           PhysicalMemory *pmem, MMUState *mmu);

/**
 * execute() — 主分发函数
 * 返回 false 表示需要停止执行 (ecall exit / 异常)
 */
bool execute(DecodedInstruction *dec, CPUState *cpu,
             PhysicalMemory *pmem, MMUState *mmu)
{
    uint32_t rd_val = 0;
    bool     branch_taken = false;

    switch (dec->opcode) {

    case 0b0110111:   // LUI
        rd_val = dec->imm;
        break;

    case 0b0010111:   // AUIPC
        rd_val = cpu->pc + dec->imm;
        break;

    case 0b1101111:   // JAL
        rd_val = cpu->pc + 4;
        cpu->next_pc = cpu->pc + dec->imm;
        break;

    case 0b1100111:   // JALR
        rd_val = cpu->pc + 4;
        cpu->next_pc = (cpu->regs[dec->rs1] + dec->imm) & ~1u;
        // 注意：rs1 和 rd 相同是合法的，JALR 规范要求先读 rs1 再写 rd
        break;

    case 0b1100011:   // BRANCH
        return execute_branch(dec, cpu);

    case 0b0000011:   // LOAD
        return execute_load(dec, cpu, pmem, mmu);

    case 0b0100011:   // STORE
        return execute_store(dec, cpu, pmem, mmu);

    case 0b0010011:   // OP-IMM
        execute_op_imm(dec, cpu);
        return true;

    case 0b0110011:   // OP (含 M 扩展)
        execute_op(dec, cpu);
        return true;

    case 0b0001111:   // MISC-MEM (FENCE) → NOP
        return true;

    case 0b1110011:   // SYSTEM
        return execute_system(dec, cpu, pmem, mmu);

    default:
        return false;  // 非法指令
    }

    /* 写入目标寄存器 (x0 写保护) */
    if (dec->rd != 0) {
        cpu->regs[dec->rd] = rd_val;
    }

    return true;
}

/* ── LOAD 实现 ─────────────────────────────────────────────
 * 你需要在此实现 5 种加载指令
 */
static bool execute_load(DecodedInstruction *dec, CPUState *cpu,
                         PhysicalMemory *pmem, MMUState *mmu)
{
    uint32_t vaddr = cpu->regs[dec->rs1] + dec->imm;
    uint32_t val = 0;

    // TODO: 根据 dec->funct3 选择宽度和符号扩展
    // 000=LB(符号扩展8位), 001=LH(符号扩展16位)
    // 010=LW(直接32位), 100=LBU(零扩展8位), 101=LHU(零扩展16位)
    //
    // 使用 mmu_read_8/16/32(mmu, pmem, vaddr, &val, cpu->priv)
    // 边界/对齐检查 → 失败时返回 false

    switch (dec->funct3) {
    case 0: { // LB
        uint8_t b;
        if (!mmu_read_8(mmu, pmem, vaddr, &b, cpu->priv)) return false;
        val = SIGN_EXT(b, 8);
        break;
    }
    case 1: { // LH
        uint16_t h;
        if (!mmu_read_16(mmu, pmem, vaddr, &h, cpu->priv)) return false;
        val = SIGN_EXT(h, 16);
        break;
    }
    case 2: { // LW
        if (!mmu_read_32(mmu, pmem, vaddr, &val, cpu->priv)) return false;
        break;
    }
    case 4: { // LBU
        uint8_t b;
        if (!mmu_read_8(mmu, pmem, vaddr, &b, cpu->priv)) return false;
        val = b;  // 零扩展
        break;
    }
    case 5: { // LHU
        uint16_t h;
        if (!mmu_read_16(mmu, pmem, vaddr, &h, cpu->priv)) return false;
        val = h;  // 零扩展
        break;
    }
    default:
        return false;  // 非法 funct3
    }

    if (dec->rd != 0) cpu->regs[dec->rd] = val;
    return true;
}

/* ── STORE 实现 ──────────────────────────────────────────── */
static bool execute_store(DecodedInstruction *dec, CPUState *cpu,
                          PhysicalMemory *pmem, MMUState *mmu)
{
    uint32_t vaddr = cpu->regs[dec->rs1] + dec->imm;
    uint32_t val   = cpu->regs[dec->rs2];

    // TODO: 根据 dec->funct3 选择宽度 (000=SB, 001=SH, 010=SW)
    // 使用 mmu_write_8/16/32
    switch (dec->funct3) {
    case 0: return mmu_write_8 (mmu, pmem, vaddr, (uint8_t)val, cpu->priv);
    case 1: return mmu_write_16(mmu, pmem, vaddr, (uint16_t)val, cpu->priv);
    case 2: return mmu_write_32(mmu, pmem, vaddr, val, cpu->priv);
    default: return false;
    }
}

/* ── OP-IMM 实现 ───────────────────────────────────────────
 * 你需要实现所有 9 种立即数运算
 */
static bool execute_op_imm(DecodedInstruction *dec, CPUState *cpu)
{
    uint32_t rs1_val = cpu->regs[dec->rs1];
    int32_t  imm     = dec->imm;
    uint32_t shamt   = imm & 0x1F;  // 移位量 (低 5 位)
    uint32_t result  = 0;

    switch (dec->funct3) {
    case 0:  result = rs1_val + imm; break;                            // ADDI
    case 2:  result = ((int32_t)rs1_val < imm) ? 1 : 0; break;         // SLTI
    case 3:  result = (rs1_val < (uint32_t)imm) ? 1 : 0; break;        // SLTIU
    case 4:  result = rs1_val ^ imm; break;                            // XORI
    case 6:  result = rs1_val | imm; break;                            // ORI
    case 7:  result = rs1_val & imm; break;                            // ANDI
    case 1:  result = rs1_val << shamt; break;                         // SLLI
    case 5:  // SRLI / SRAI
        if (dec->funct7 == 0b0100000)
            result = (uint32_t)((int32_t)rs1_val >> shamt);            // SRAI
        else
            result = rs1_val >> shamt;                                  // SRLI
        break;
    default: return false;
    }

    if (dec->rd != 0) cpu->regs[dec->rd] = result;
    return true;
}

/* ── OP (R-type) 实现 ──────────────────────────────────────
 * 你需要实现 RV32I 10条 + M扩展 8条 → 共 18 条
 */
static bool execute_op(DecodedInstruction *dec, CPUState *cpu)
{
    uint32_t rs1_val = cpu->regs[dec->rs1];
    uint32_t rs2_val = cpu->regs[dec->rs2];
    uint32_t shamt   = rs2_val & 0x1F;
    uint32_t result  = 0;

    if (dec->funct7 == 0b0000001) {
        /* ── M 扩展：乘除法 ────────────────────── */
        switch (dec->funct3) {
        case 0:  result = (uint32_t)((int32_t)rs1_val * (int32_t)rs2_val); break;           // MUL
        case 1:  result = (uint32_t)(((int64_t)(int32_t)rs1_val * (int64_t)(int32_t)rs2_val) >> 32); break; // MULH
        case 2:  result = (uint32_t)(((int64_t)(int32_t)rs1_val * (uint64_t)rs2_val) >> 32); break;        // MULHSU
        case 3:  result = (uint32_t)(((uint64_t)rs1_val * (uint64_t)rs2_val) >> 32); break;               // MULHU
        case 4:  result = (rs2_val == 0) ? (uint32_t)-1 : (uint32_t)((int32_t)rs1_val / (int32_t)rs2_val); break; // DIV
        case 5:  result = (rs2_val == 0) ? (uint32_t)-1 : rs1_val / rs2_val; break;                       // DIVU
        case 6:  result = (rs2_val == 0) ? rs1_val : (uint32_t)((int32_t)rs1_val % (int32_t)rs2_val); break;     // REM
        case 7:  result = (rs2_val == 0) ? rs1_val : rs1_val % rs2_val; break;                            // REMU
        default: return false;
        }
    } else if (dec->funct7 == 0b0000000 || dec->funct7 == 0b0100000) {
        /* ── RV32I 基本整数操作 ───────────────── */
        switch (dec->funct3) {
        case 0:
            result = (dec->funct7 == 0b0100000) ? (rs1_val - rs2_val) : (rs1_val + rs2_val);  // SUB/ADD
            break;
        case 1:  result = rs1_val << shamt; break;                    // SLL
        case 2:  result = ((int32_t)rs1_val < (int32_t)rs2_val) ? 1 : 0; break;  // SLT
        case 3:  result = (rs1_val < rs2_val) ? 1 : 0; break;        // SLTU
        case 4:  result = rs1_val ^ rs2_val; break;                  // XOR
        case 5:
            result = (dec->funct7 == 0b0100000)
                ? (uint32_t)((int32_t)rs1_val >> shamt)              // SRA
                : (rs1_val >> shamt);                                  // SRL
            break;
        case 6:  result = rs1_val | rs2_val; break;                  // OR
        case 7:  result = rs1_val & rs2_val; break;                  // AND
        default: return false;
        }
    } else {
        return false;  // 未知 funct7
    }

    if (dec->rd != 0) cpu->regs[dec->rd] = result;
    return true;
}

/* ── BRANCH 实现 ──────────────────────────────────────────── */
static bool execute_branch(DecodedInstruction *dec, CPUState *cpu)
{
    uint32_t rs1_val = cpu->regs[dec->rs1];
    uint32_t rs2_val = cpu->regs[dec->rs2];
    bool taken = false;

    switch (dec->funct3) {
    case 0:  taken = (rs1_val == rs2_val); break;                             // BEQ
    case 1:  taken = (rs1_val != rs2_val); break;                             // BNE
    case 4:  taken = ((int32_t)rs1_val <  (int32_t)rs2_val); break;          // BLT
    case 5:  taken = ((int32_t)rs1_val >= (int32_t)rs2_val); break;          // BGE
    case 6:  taken = (rs1_val <  rs2_val); break;                            // BLTU
    case 7:  taken = (rs1_val >= rs2_val); break;                            // BGEU
    default: return false;
    }

    if (taken) {
        cpu->next_pc = cpu->pc + dec->imm;
    }
    return true;
}

/* ── SYSTEM 实现 ──────────────────────────────────────────── */
static bool execute_system(DecodedInstruction *dec, CPUState *cpu,
                           PhysicalMemory *pmem, MMUState *mmu)
{
    if (dec->funct3 == 0b000) {
        /* ECALL / EBREAK */
        if (dec->imm == 0) {
            /* ECALL: 系统调用 */
            return syscall_handler(cpu, pmem, mmu);
        } else if (dec->imm == 1) {
            /* EBREAK: 断点异常 → 调试器接管 */
            cpu->running = false;
            return false;
        }
    }
    /* CSR 指令 → 见 csr.c */
    // TODO: 实现 CSRRW/CSRRS/CSRRC/CSRRWI/CSRRSI/CSRRCI
    return false;
}
