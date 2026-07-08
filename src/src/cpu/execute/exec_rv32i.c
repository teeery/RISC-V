/* ============================================================
 * exec_rv32i.c — RV32I 基础整数指令集（37 条）实现
 *
 * 所有函数由 cpu_execute() 按 opcode 分发调用。
 * 函数命名：exec_<opcode-name>，如 exec_lui、exec_op_imm。
 *
 * 设计约定：
 *   - 每个函数返回 true（成功），异常情况内部调 cpu_trap() 后返回 false
 *   - *next_pc 默认不用改（调用方已设为 pc+4），分支/跳转覆盖即可
 *   - 所有 Load/Store 函数通过 mmu_read/write_* 访问内存
 * ============================================================
 */

#include "cpu/execute.h"
#include "cpu/decode.h"
#include "simulator.h"
#include "memory/mmu.h"
#include "types.h"
#include "cpu/exec_internal.h"
#include "linux/syscall.h"
#include <stdio.h>

/* ════════════════════════════════════════════════════════════
 * LUI — 加载立即数到高位
 *   rd = imm[31:12] << 12（低 12 位自动填 0）
 * ════════════════════════════════════════════════════════════
 */
bool exec_lui(Simulator *sim, DecodedInstr *dec, uint32_t *next_pc)
{
    (void)next_pc;
    sim->cpu.regs[dec->rd] = (uint32_t)dec->imm;
    return true;
}

/* ════════════════════════════════════════════════════════════
 * AUIPC — PC 相对地址高位
 *   rd = pc + (imm[31:12] << 12)
 * ════════════════════════════════════════════════════════════
 */
bool exec_auipc(Simulator *sim, DecodedInstr *dec, uint32_t *next_pc)
{
    (void)next_pc;
    sim->cpu.regs[dec->rd] = sim->cpu.pc + (uint32_t)dec->imm;
    return true;
}

/* ════════════════════════════════════════════════════════════
 * OP-IMM — 立即数算术/逻辑指令（9 条）
 *   opcode = 0x13
 *   funct3 区分：ADDI / SLTI / SLTIU / XORI / ORI / ANDI
 *              / SLLI / SRLI / SRAI
 * ════════════════════════════════════════════════════════════
 */
bool exec_op_imm(Simulator *sim, DecodedInstr *dec, uint32_t *next_pc)
{
    (void)next_pc;
    CPU *cpu = &sim->cpu;
    int32_t  src  = (int32_t)cpu->regs[dec->rs1];
    int32_t  imm  = dec->imm;
    uint32_t shamt = imm & 0x1F;  /* SLLI/SRLI/SRAI 只用低 5 位 */

    switch (dec->funct3) {
    case 0: /* ADDI */
        cpu->regs[dec->rd] = (uint32_t)(src + imm);
        break;
    case 2: /* SLTI — 有符号比较 */
        cpu->regs[dec->rd] = (src < imm) ? 1 : 0;
        break;
    case 3: /* SLTIU — 无符号比较 */
        cpu->regs[dec->rd] = ((uint32_t)src < (uint32_t)imm) ? 1 : 0;
        break;
    case 4: /* XORI */
        cpu->regs[dec->rd] = (uint32_t)src ^ (uint32_t)imm;
        break;
    case 5: /* SRLI / SRAI — funct7[5] 区分 */
        if (dec->funct7 == 0x20) {
            /* SRAI：算术右移（符号扩展） */
            cpu->regs[dec->rd] = (uint32_t)(src >> (int32_t)shamt);
        } else {
            /* SRLI：逻辑右移（零扩展） */
            cpu->regs[dec->rd] = (uint32_t)src >> shamt;
        }
        break;
    case 6: /* ORI */
        cpu->regs[dec->rd] = (uint32_t)src | (uint32_t)imm;
        break;
    case 7: /* ANDI */
        cpu->regs[dec->rd] = (uint32_t)src & (uint32_t)imm;
        break;
    case 1: /* SLLI */
        cpu->regs[dec->rd] = (uint32_t)src << shamt;
        break;
    default:
        cpu_trap(sim, EXC_ILLEGAL_INST, dec->opcode);
        return false;
    }
    return true;
}

/* ════════════════════════════════════════════════════════════
 * OP — 寄存器间算术/逻辑指令（10 条 RV32I）
 *   opcode = 0x33
 *   funct3 + funct7 共同区分操作
 *
 *   分流规则：
 *     funct7 == 0x01  → exec_m_muldiv()（M 扩展）
 *     funct7 == 0x00  → ADD/SLL/SLT/SLTU/XOR/SRL/SRA/OR/AND
 *     funct7 == 0x20  → SUB/SRA（与 0x00 共用 funct3）
 * ════════════════════════════════════════════════════════════
 */
bool exec_op(Simulator *sim, DecodedInstr *dec, uint32_t *next_pc)
{
    (void)next_pc;
    CPU *cpu = &sim->cpu;

    /* ── M 扩展分流 ── */
    if (dec->funct7 == 1) {
        return exec_m_muldiv(sim, dec, next_pc);
    }

    uint32_t rs1_val = cpu->regs[dec->rs1];
    uint32_t rs2_val = cpu->regs[dec->rs2];
    int32_t  s1 = (int32_t)rs1_val;
    int32_t  s2 = (int32_t)rs2_val;

    switch (dec->funct3) {
    case 0: /* ADD / SUB */
        if (dec->funct7 == 0x20) {
            cpu->regs[dec->rd] = (uint32_t)(s1 - s2);       /* SUB */
        } else {
            cpu->regs[dec->rd] = rs1_val + rs2_val;         /* ADD */
        }
        break;
    case 1: /* SLL — 逻辑左移 */
        cpu->regs[dec->rd] = rs1_val << (rs2_val & 0x1F);
        break;
    case 2: /* SLT — 有符号比较 */
        cpu->regs[dec->rd] = (s1 < s2) ? 1 : 0;
        break;
    case 3: /* SLTU — 无符号比较 */
        cpu->regs[dec->rd] = (rs1_val < rs2_val) ? 1 : 0;
        break;
    case 4: /* XOR */
        cpu->regs[dec->rd] = rs1_val ^ rs2_val;
        break;
    case 5: /* SRL / SRA */
        if (dec->funct7 == 0x20) {
            cpu->regs[dec->rd] = (uint32_t)(s1 >> (int32_t)(rs2_val & 0x1F)); /* SRA */
        } else {
            cpu->regs[dec->rd] = rs1_val >> (rs2_val & 0x1F);                /* SRL */
        }
        break;
    case 6: /* OR */
        cpu->regs[dec->rd] = rs1_val | rs2_val;
        break;
    case 7: /* AND */
        cpu->regs[dec->rd] = rs1_val & rs2_val;
        break;
    default:
        cpu_trap(sim, EXC_ILLEGAL_INST, dec->opcode);
        return false;
    }
    return true;
}

/* ════════════════════════════════════════════════════════════
 * LOAD — 内存加载指令（5 条）
 *   opcode = 0x03
 *   funct3 区分：LB / LH / LW / LBU / LHU
 *   地址 = rs1 + sign-extend(imm)
 * ════════════════════════════════════════════════════════════
 */
bool exec_load(Simulator *sim, DecodedInstr *dec, uint32_t *next_pc)
{
    (void)next_pc;
    CPU *cpu = &sim->cpu;

    uint32_t addr = cpu->regs[dec->rs1] + (uint32_t)dec->imm;
    ExceptionType exc = EXC_NONE;

    switch (dec->funct3) {
    case 0: { /* LB — 有符号字节加载 */
        uint8_t val8;
        if (!mmu_read_8(&sim->mmu, &sim->pmem, addr, &val8, cpu->priv, &exc)) {
            cpu_trap(sim, (uint32_t)exc, addr);
            return false;
        }
        cpu->regs[dec->rd] = (uint32_t)(int32_t)(int8_t)val8;  /* 符号扩展 */
        break;
    }
    case 1: { /* LH — 有符号半字加载 */
        uint16_t val16;
        if (!mmu_read_16(&sim->mmu, &sim->pmem, addr, &val16, cpu->priv, &exc)) {
            cpu_trap(sim, (uint32_t)exc, addr);
            return false;
        }
        cpu->regs[dec->rd] = (uint32_t)(int32_t)(int16_t)val16;  /* 符号扩展 */
        break;
    }
    case 2: { /* LW — 字加载 */
        uint32_t val32;
        if (!mmu_read_32(&sim->mmu, &sim->pmem, addr, &val32, cpu->priv, &exc)) {
            cpu_trap(sim, (uint32_t)exc, addr);
            return false;
        }
        cpu->regs[dec->rd] = val32;
        break;
    }
    case 4: { /* LBU — 无符号字节加载 */
        uint8_t val8;
        if (!mmu_read_8(&sim->mmu, &sim->pmem, addr, &val8, cpu->priv, &exc)) {
            cpu_trap(sim, (uint32_t)exc, addr);
            return false;
        }
        cpu->regs[dec->rd] = (uint32_t)val8;  /* 零扩展 */
        break;
    }
    case 5: { /* LHU — 无符号半字加载 */
        uint16_t val16;
        if (!mmu_read_16(&sim->mmu, &sim->pmem, addr, &val16, cpu->priv, &exc)) {
            cpu_trap(sim, (uint32_t)exc, addr);
            return false;
        }
        cpu->regs[dec->rd] = (uint32_t)val16;  /* 零扩展 */
        break;
    }
    default:
        cpu_trap(sim, EXC_ILLEGAL_INST, dec->opcode);
        return false;
    }
    return true;
}

/* ════════════════════════════════════════════════════════════
 * STORE — 内存存储指令（3 条）
 *   opcode = 0x23
 *   funct3 区分：SB / SH / SW
 *   地址 = rs1 + sign-extend(imm)，数据 = rs2 的低位
 * ════════════════════════════════════════════════════════════
 */
bool exec_store(Simulator *sim, DecodedInstr *dec, uint32_t *next_pc)
{
    (void)next_pc;
    CPU *cpu = &sim->cpu;

    uint32_t addr = cpu->regs[dec->rs1] + (uint32_t)dec->imm;
    uint32_t data = cpu->regs[dec->rs2];
    ExceptionType exc = EXC_NONE;

    switch (dec->funct3) {
    case 0: /* SB */
        mmu_write_8(&sim->mmu, &sim->pmem, addr, (uint8_t)(data & 0xFF),
                    cpu->priv, &exc);
        break;
    case 1: /* SH */
        mmu_write_16(&sim->mmu, &sim->pmem, addr, (uint16_t)(data & 0xFFFF),
                     cpu->priv, &exc);
        break;
    case 2: /* SW */
        mmu_write_32(&sim->mmu, &sim->pmem, addr, data, cpu->priv, &exc);
        break;
    default:
        cpu_trap(sim, EXC_ILLEGAL_INST, dec->opcode);
        return false;
    }

    if (exc != EXC_NONE) {
        cpu_trap(sim, (uint32_t)exc, addr);
        return false;
    }
    return true;
}

/* ════════════════════════════════════════════════════════════
 * BRANCH — 条件分支指令（6 条）
 *   opcode = 0x63
 *   funct3 区分：BEQ / BNE / BLT / BGE / BLTU / BGEU
 *   条件满足时 *next_pc = pc + imm，否则保持 pc+4
 * ════════════════════════════════════════════════════════════
 */
bool exec_branch(Simulator *sim, DecodedInstr *dec, uint32_t *next_pc)
{
    CPU *cpu = &sim->cpu;

    uint32_t rs1_val = cpu->regs[dec->rs1];
    uint32_t rs2_val = cpu->regs[dec->rs2];
    int32_t  s1 = (int32_t)rs1_val;
    int32_t  s2 = (int32_t)rs2_val;
    bool     taken = false;

    switch (dec->funct3) {
    case 0: /* BEQ  — 相等 */
        taken = (rs1_val == rs2_val);
        break;
    case 1: /* BNE  — 不等 */
        taken = (rs1_val != rs2_val);
        break;
    case 4: /* BLT  — 有符号小于 */
        taken = (s1 < s2);
        break;
    case 5: /* BGE  — 有符号大于等于 */
        taken = (s1 >= s2);
        break;
    case 6: /* BLTU — 无符号小于 */
        taken = (rs1_val < rs2_val);
        break;
    case 7: /* BGEU — 无符号大于等于 */
        taken = (rs1_val >= rs2_val);
        break;
    default:
        cpu_trap(sim, EXC_ILLEGAL_INST, dec->opcode);
        return false;
    }

    if (taken) {
        *next_pc = cpu->pc + (uint32_t)dec->imm;
    }
    return true;
}

/* ════════════════════════════════════════════════════════════
 * JAL — 跳转并链接
 *   rd = pc + 4（返回地址）
 *   next_pc = pc + imm（跳转目标）
 * ════════════════════════════════════════════════════════════
 */
bool exec_jal(Simulator *sim, DecodedInstr *dec, uint32_t *next_pc)
{
    CPU *cpu = &sim->cpu;

    cpu->regs[dec->rd] = cpu->pc + 4;
    *next_pc = cpu->pc + (uint32_t)dec->imm;
    return true;
}

/* ════════════════════════════════════════════════════════════
 * JALR — 跳转并链接（寄存器间接）
 *   rd = pc + 4（返回地址）
 *   next_pc = (rs1 + imm) & ~1（最低位清零对齐）
 * ════════════════════════════════════════════════════════════
 */
bool exec_jalr(Simulator *sim, DecodedInstr *dec, uint32_t *next_pc)
{
    CPU *cpu = &sim->cpu;

    cpu->regs[dec->rd] = cpu->pc + 4;
    *next_pc = (cpu->regs[dec->rs1] + (uint32_t)dec->imm) & ~1u;
    return true;
}

/* ════════════════════════════════════════════════════════════
 * SYSTEM — 特权指令
 *   opcode = 0x73
 *   funct3=0: ecall(imm=0) / ebreak(imm=1) / mret(imm=0x302)
 *   CSR 指令暂不实现 → 非法指令
 * ════════════════════════════════════════════════════════════
 */
bool exec_system(Simulator *sim, DecodedInstr *dec, uint32_t *next_pc)
{
    CPU *cpu = &sim->cpu;

    switch (dec->funct3) {
    case 0:
        if (dec->imm == 0) {                    /* ECALL */
            cpu_trap(sim, EXC_ECALL_M, 0);
        } else if (dec->imm == 1) {             /* EBREAK */
            cpu_trap(sim, EXC_BREAKPOINT, cpu->pc);
        } else if (dec->imm == 0x302) {         /* MRET */
            cpu->priv = (cpu->mstatus >> 11) & 0x3;   /* MPP → priv */
            cpu->mstatus = (cpu->mstatus & ~(1u << 7))
                         | ((cpu->mstatus >> 3) & 1) << 3;  /* MPIE→MIE */
            *next_pc = cpu->mepc;              /* 返回异常地址 */
        } else {
            cpu_trap(sim, EXC_ILLEGAL_INST, dec->opcode);
            return false;
        }
        break;
    default:
        cpu_trap(sim, EXC_ILLEGAL_INST, dec->opcode);
        return false;
    }
    return true;
}

/* ════════════════════════════════════════════════════════════
 * FENCE — 内存屏障（NOP）
 *   单核模拟器中无需实现真实屏障，只做 PC+4
 * ════════════════════════════════════════════════════════════
 */
bool exec_fence(Simulator *sim, DecodedInstr *dec, uint32_t *next_pc)
{
    (void)sim;
    (void)dec;
    (void)next_pc;
    /* NOP: *next_pc 已由调用方设为 pc+4 */
    return true;
}
