#include "cpu/execute.h"
#include "cpu/decode.h"
#include "simulator.h"
#include "memory/mmu.h"
#include "types.h"
#include <stdio.h>

/* ============================================================================
 * cpu_trap — RISC-V 异常处理
 *
 * ── CSR 缩写速查（m- = Machine 模式寄存器）────────────────────────
 *
 *   mstatus  = Machine STATUS           — 全局状态寄存器（记录"CPU 当前什么状态"）
 *     ├─ MPP  = Machine Previous Privilege  — 异常前特权级（bit[12:11]）
 *     ├─ MPIE = Machine Previous IE         — 异常前中断使能（bit[7]）
 *     └─ MIE  = Machine Interrupt Enable    — 中断使能（bit[3]）
 *   mtvec    = Machine Trap VECtor      — 陷阱向量表入口地址
 *   mepc     = Machine Exception PC     — 异常指令地址（记录"哪条指令出的事"）
 *   mcause   = Machine CAUSE           — 异常原因（记录"出了什么事"）
 *   mtval    = Machine Trap VALue       — 异常附加信息（错误地址 / 非法指令码）
 *
 *   这些名字都是 RISC-V 规范定的，不用自己取——在 cpu.h 里查每个字段的详细注释。
 *
 * ── 调用时机 ──────────────────────────────────────────────────────
 *   - MMU 取指/读写失败（页错误、权限错误）
 *   - 非法指令（switch default）
 *   - ecall 系统调用
 *   - ebreak 断点
 *
 * ── 6 步处理流程（按 RISC-V 特权规范）─────────────────────────────
 *   ① mepc   = pc              — 保存"出事地址"
 *   ② mcause = exc              — 保存"出事原因"（ExceptionType 枚举值）
 *   ③ mtval  = tval             — 保存"出事的附加信息"（地址/指令码）
 *   ④ mstatus: MPP ← priv       — "出事前是什么特权级"
 *              MPIE ← MIE       — "出事前中断开着还是关着"
 *              MIE  ← 0         — 关掉中断（异常处理期间不能被打断）
 *   ⑤ priv = PRIV_MACHINE       — 切到机器态
 *   ⑥ pc   = mtvec & ~0x3u     — 跳到异常处理程序入口
 *      （若 mtvec == 0，无处理程序 → 打印错误 → 停机）
 * ============================================================================ */
void cpu_trap(Simulator *sim, uint32_t exc, uint32_t tval)
{
    CPU *cpu = &sim->cpu;

    /* ① mepc（Machine Exception PC）— 记录"出事的指令在哪"，方便异常处理完回来继续执行 */
    cpu->mepc = cpu->pc;

    /* ② mcause（Machine Cause）— 记录"出了什么事"：非法指令？页错误？ecall？ */
    cpu->mcause = exc;

    /* ③ mtval（Machine Trap Value）— 记录"附加信息"
     *    页错误时   = 访问的虚拟地址
     *    非法指令时 = 那条指令的 32 位编码 */
    cpu->mtval = tval;

    /* ④ mstatus（Machine Status）— 更新 CPU 状态寄存器的 3 个关键位
     *
     *   MPP  = Previous Privilege    = bit[12:11] ← 旧的 priv     "出事前是什么特权级"
     *   MPIE = Previous IE           = bit[7]     ← 旧的 MIE      "出事前中断开还是关"
     *   MIE  = Interrupt Enable      = bit[3]     ← 0             "关中断，处理异常时不能被打断" */
    uint32_t old_priv_bits = (cpu->priv & 0x3) << 11;    // 旧特权级 → MPP 位置
    uint32_t old_mie_bit   = (cpu->mstatus >> 3) & 1;    // 旧 MIE → 暂存

    cpu->mstatus &= ~((0x3u << 11) | (1u << 7) | (1u << 3));  // 先把这三位清零
    cpu->mstatus |= old_priv_bits | (old_mie_bit << 7);         // 再填入新值

    /* ⑤ 切到机器态 — 异常处理程序有最高权限 */
    cpu->priv = PRIV_MACHINE;

    /* ⑥ 跳转到 mtvec（Machine Trap Vector）— 异常处理程序的入口地址
     *    mtvec & ~0x3u 相当于取 bit[31:2]，因为入口必须 4 字节对齐 */
    if (cpu->mtvec != 0) {
        cpu->pc = cpu->mtvec & ~0x3u;
    } else {
        /* mtvec == 0 → 没有注册异常处理程序 → 打日志 + 停机 */
        printf("\n[Trap] exception=%u, tval=0x%08x at mepc=0x%08x\n",
               exc, tval, cpu->mepc);
        printf("[Trap] mtvec == 0 — no handler registered, halting CPU.\n");
        cpu->running = false;
    }
}

// ✅ cpu_trap
// ✅ LUI + AUIPC
// ✅ ADDI + ADD + SUB
// ✅ JAL + JALR
// ✅ B-type (6条分支)
// ✅ LW + SW + LH + LHU + LB + LBU + SB + SH  ← 刚完成
// ✅ 剩余 ALU 指令 (SLLI/SRLI/SRAI/SLTI/SLTIU/XORI/ORI/ANDI + SLL/SLT/SLTU/XOR/SRL/SRA/OR/AND)
// ✅ SYSTEM (ecall/ebreak)
// ✅ FENCE (NOP)

bool cpu_execute(Simulator *sim, DecodedInstr *d, uint32_t *next_pc)
{
    CPU *cpu = &sim->cpu;

    /* 默认顺序执行：下一条 = pc + 4 */
    *next_pc = cpu->pc + 4;

    switch (d->opcode) {

    /* ── LUI：Load Upper Immediate ───────────────────────────
     * 格式：lui rd, imm
     * 动作：rd = imm（立即数已在 bit[31:12]，低 12 位为 0）
     * 例：  lui x5, 0x12345 → x5 = 0x12345000 */
    case 0x37:
        cpu->regs[d->rd] = (uint32_t)d->imm;
        break;

    /* ── AUIPC：Add Upper Immediate to PC ────────────────────
     * 格式：auipc rd, imm
     * 动作：rd = pc + imm
     * 例：  auipc x5, 0x12345 → x5 = pc + 0x12345000 */
    case 0x17:
        cpu->regs[d->rd] = cpu->pc + (uint32_t)d->imm;
        break;

    /* ── OP-IMM：立即数算术（opcode = 0x13）──────────────────
     *
     *   9 条指令，靠 funct3 区分（其中 funct3=1/5 还需 funct7）：
     *   funct3=0: ADDI    funct3=1: SLLI    funct3=2: SLTI
     *   funct3=3: SLTIU   funct3=4: XORI    funct3=5: SRLI / SRAI
     *   funct3=6: ORI     funct3=7: ANDI
     *
     *   首批实现 ADDI，其余后续添加 */
    case 0x13:
        switch (d->funct3) {
        case 0:   // ADDI
            cpu->regs[d->rd] = cpu->regs[d->rs1] + d->imm;
            break;
        case 1:   // SLLI — 逻辑左移（funct7=0x00）
            cpu->regs[d->rd] = cpu->regs[d->rs1] << (d->imm & 0x1F);
            break;
        case 2:   // SLTI — 有符号比较
            cpu->regs[d->rd] = ((int32_t)cpu->regs[d->rs1] < d->imm) ? 1 : 0;
            break;
        case 3:   // SLTIU — 无符号比较
            cpu->regs[d->rd] = (cpu->regs[d->rs1] < (uint32_t)d->imm) ? 1 : 0;
            break;
        case 4:   // XORI
            cpu->regs[d->rd] = cpu->regs[d->rs1] ^ d->imm;
            break;
        case 5:
            if (d->funct7 == 0x00)         // SRLI — 逻辑右移
                cpu->regs[d->rd] = cpu->regs[d->rs1] >> (d->imm & 0x1F);
            else if (d->funct7 == 0x20)    // SRAI — 算术右移
                cpu->regs[d->rd] = (int32_t)cpu->regs[d->rs1] >> (d->imm & 0x1F);
            else
                cpu_trap(sim, EXC_ILLEGAL_INST, d->opcode);
            break;
        case 6:   // ORI
            cpu->regs[d->rd] = cpu->regs[d->rs1] | d->imm;
            break;
        case 7:   // ANDI
            cpu->regs[d->rd] = cpu->regs[d->rs1] & d->imm;
            break;
        default:
            cpu_trap(sim, EXC_ILLEGAL_INST, d->opcode);
            return true;
        }
        break;

    /* ── OP：寄存器算术（opcode = 0x33）─────────────────────── */
    case 0x33:
        switch (d->funct3) {
        case 0:
            if (d->funct7 == 0x00)         // ADD
                cpu->regs[d->rd] = cpu->regs[d->rs1] + cpu->regs[d->rs2];
            else if (d->funct7 == 0x20)     // SUB
                cpu->regs[d->rd] = cpu->regs[d->rs1] - cpu->regs[d->rs2];
            else
                cpu_trap(sim, EXC_ILLEGAL_INST, d->opcode);
            break;
        case 1:   // SLL
            cpu->regs[d->rd] = cpu->regs[d->rs1] << (cpu->regs[d->rs2] & 0x1F);
            break;
        case 2:   // SLT — 有符号比较
            cpu->regs[d->rd] = ((int32_t)cpu->regs[d->rs1] < (int32_t)cpu->regs[d->rs2]) ? 1 : 0;
            break;
        case 3:   // SLTU — 无符号比较
            cpu->regs[d->rd] = (cpu->regs[d->rs1] < cpu->regs[d->rs2]) ? 1 : 0;
            break;
        case 4:   // XOR
            cpu->regs[d->rd] = cpu->regs[d->rs1] ^ cpu->regs[d->rs2];
            break;
        case 5:
            if (d->funct7 == 0x00)         // SRL — 逻辑右移
                cpu->regs[d->rd] = cpu->regs[d->rs1] >> (cpu->regs[d->rs2] & 0x1F);
            else if (d->funct7 == 0x20)    // SRA — 算术右移
                cpu->regs[d->rd] = (int32_t)cpu->regs[d->rs1] >> (cpu->regs[d->rs2] & 0x1F);
            else
                cpu_trap(sim, EXC_ILLEGAL_INST, d->opcode);
            break;
        case 6:   // OR
            cpu->regs[d->rd] = cpu->regs[d->rs1] | cpu->regs[d->rs2];
            break;
        case 7:   // AND
            cpu->regs[d->rd] = cpu->regs[d->rs1] & cpu->regs[d->rs2];
            break;
        default:
            cpu_trap(sim, EXC_ILLEGAL_INST, d->opcode);
            return true;
        }
        break;

    /* ── JAL：Jump and Link ──────────────────────────────────
     * 格式：jal rd, offset
     * 动作：rd  = pc + 4           （保存返回地址）
     *      *next_pc = pc + imm     （跳转目标）
     * 例：  jal ra, 0x100 → ra = pc+4, 跳到 pc+0x100 */
    case 0x6F:
        cpu->regs[d->rd] = cpu->pc + 4;       // 返回地址
        *next_pc = cpu->pc + d->imm;           // 跳转目标
        break;

    /* ── JALR：Jump and Link Register ────────────────────────
     * 格式：jalr rd, rs1, offset
     * 动作：target = (rs1 + imm) & ~1u    （最低位清零对齐）
     *      rd     = pc + 4                （保存返回地址）
     *      *next_pc = target              （跳转目标）
     * 例：  jalr ra, x1, 0 → ra = pc+4, 跳到 x1 */
    case 0x67: {
        uint32_t target = (cpu->regs[d->rs1] + d->imm) & ~1u;
        cpu->regs[d->rd] = cpu->pc + 4;       // 返回地址
        *next_pc = target;                     // 跳转目标
        break;
    }

    /* ── B-type：条件分支（opcode = 0x63）────────────────────
     *
     *   6 条指令，靠 funct3 区分：
     *   funct3=0: BEQ      funct3=1: BNE
     *   funct3=4: BLT      funct3=5: BGE
     *   funct3=6: BLTU     funct3=7: BGEU
     *
     *   动作：比较 rs1 和 rs2，条件成立则 *next_pc = pc + imm
     *   注意：BLT/BGE 用有符号比较，BLTU/BGEU 用无符号比较 */
    case 0x63: {
        uint32_t a = cpu->regs[d->rs1];
        uint32_t b = cpu->regs[d->rs2];
        bool take = false;

        switch (d->funct3) {
        case 0: take = (a == b);                              break;  // BEQ
        case 1: take = (a != b);                              break;  // BNE
        case 4: take = ((int32_t)a <  (int32_t)b);           break;  // BLT
        case 5: take = ((int32_t)a >= (int32_t)b);           break;  // BGE
        case 6: take = (a <  b);                              break;  // BLTU
        case 7: take = (a >= b);                              break;  // BGEU
        default:
            cpu_trap(sim, EXC_ILLEGAL_INST, d->opcode);
            return true;
        }

        if (take)
            *next_pc = cpu->pc + d->imm;
        break;
    }

    /* ── Load：内存读取（opcode = 0x03）────────────────────────
     *
     *   5 条指令，靠 funct3 区分：
     *   funct3=0: LB    funct3=1: LH    funct3=2: LW
     *   funct3=4: LBU   funct3=5: LHU
     *
     *   动作：addr = rs1 + imm → mmu_read_* → 符号/零扩展 → rd
     *   失败则 cpu_trap */
    case 0x03: {
        uint32_t addr = cpu->regs[d->rs1] + d->imm;
        ExceptionType exc = EXC_NONE;

        switch (d->funct3) {
        case 0: {   // LB — 符号扩展
            uint8_t val;
            if (!mmu_read_8(&sim->mmu, &sim->pmem, addr, &val, cpu->priv, &exc))
                cpu_trap(sim, (uint32_t)exc, addr);
            else
                cpu->regs[d->rd] = (int32_t)(int8_t)val;
            break;
        }
        case 1: {   // LH — 符号扩展
            uint16_t val;
            if (!mmu_read_16(&sim->mmu, &sim->pmem, addr, &val, cpu->priv, &exc))
                cpu_trap(sim, (uint32_t)exc, addr);
            else
                cpu->regs[d->rd] = (int32_t)(int16_t)val;
            break;
        }
        case 2: {   // LW
            uint32_t val;
            if (!mmu_read_32(&sim->mmu, &sim->pmem, addr, &val, cpu->priv, &exc))
                cpu_trap(sim, (uint32_t)exc, addr);
            else
                cpu->regs[d->rd] = val;
            break;
        }
        case 4: {   // LBU — 零扩展
            uint8_t val;
            if (!mmu_read_8(&sim->mmu, &sim->pmem, addr, &val, cpu->priv, &exc))
                cpu_trap(sim, (uint32_t)exc, addr);
            else
                cpu->regs[d->rd] = val;    // uint8 → uint32 零扩展自动
            break;
        }
        case 5: {   // LHU — 零扩展
            uint16_t val;
            if (!mmu_read_16(&sim->mmu, &sim->pmem, addr, &val, cpu->priv, &exc))
                cpu_trap(sim, (uint32_t)exc, addr);
            else
                cpu->regs[d->rd] = val;    // uint16 → uint32 零扩展自动
            break;
        }
        default:
            cpu_trap(sim, EXC_ILLEGAL_INST, d->opcode);
            return true;
        }
        break;
    }

    /* ── Store：内存写入（opcode = 0x23）───────────────────────
     *
     *   3 条指令，靠 funct3 区分：
     *   funct3=0: SB    funct3=1: SH    funct3=2: SW
     *
     *   动作：addr = rs1 + imm → mmu_write_*(addr, rs2)
     *   失败则 cpu_trap */
    case 0x23: {
        uint32_t addr = cpu->regs[d->rs1] + d->imm;
        uint32_t val  = cpu->regs[d->rs2];
        ExceptionType exc = EXC_NONE;

        switch (d->funct3) {
        case 0:   // SB
            mmu_write_8(&sim->mmu, &sim->pmem, addr, (uint8_t)val, cpu->priv, &exc);
            break;
        case 1:   // SH
            mmu_write_16(&sim->mmu, &sim->pmem, addr, (uint16_t)val, cpu->priv, &exc);
            break;
        case 2:   // SW
            mmu_write_32(&sim->mmu, &sim->pmem, addr, val, cpu->priv, &exc);
            break;
        default:
            cpu_trap(sim, EXC_ILLEGAL_INST, d->opcode);
            return true;
        }

        if (exc != EXC_NONE)
            cpu_trap(sim, (uint32_t)exc, addr);
        break;
    }

    /* ── FENCE / FENCE.I（opcode = 0x0F）─────────────────────
     * 单核顺序执行，内存屏障无意义，直接当 NOP */
    case 0x0F:
        break;

    /* ── SYSTEM：特权指令（opcode = 0x73）────────────────────
     *   ecall  (funct3=0, imm=0)   → 系统调用异常
     *   ebreak (funct3=0, imm=1)   → 断点异常
     *   mret   (funct3=0, imm=0x302) → 异常返回（暂简化实现）
     *   CSR 指令暂不实现 → 非法指令 */
    case 0x73:
        switch (d->funct3) {
        case 0:
            if (d->imm == 0)                  // ECALL
                cpu_trap(sim, EXC_ECALL_M, 0);
            else if (d->imm == 1)             // EBREAK
                cpu_trap(sim, EXC_BREAKPOINT, cpu->pc);
            else if (d->imm == 0x302) {       // MRET — 简化：恢复 priv 和 pc
                cpu->priv = (cpu->mstatus >> 11) & 0x3;   // MPP → priv
                cpu->mstatus = (cpu->mstatus & ~(1u << 7)) | ((cpu->mstatus >> 3) & 1) << 3; // MPIE→MIE
                *next_pc = cpu->mepc;                      // 返回异常地址
            } else
                cpu_trap(sim, EXC_ILLEGAL_INST, d->opcode);
            break;
        default:
            cpu_trap(sim, EXC_ILLEGAL_INST, d->opcode);
            return true;
        }
        break;

    /* ── 未实现的 opcode → 非法指令异常 ────────────────────── */
    default:
        cpu_trap(sim, EXC_ILLEGAL_INST, d->opcode);
        return true;
    }

    return true;
}