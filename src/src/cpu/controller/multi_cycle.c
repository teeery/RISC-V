/* ============================================================
 * multi_cycle.c — 多周期 CPU 控制器
 *
 * 时序模型（Patterson & Hennessy §4.4–4.5）：
 *   每条指令拆分为最多 5 个状态，不同指令用不同周期数：
 *
 *     R-type (ALU):   IF → ID → EX → WB          (4 周期)
 *     I-type (ALU):   IF → ID → EX → WB          (4 周期)
 *     Load:           IF → ID → EX → MEM → WB    (5 周期)
 *     Store:          IF → ID → EX → MEM         (4 周期)
 *     Branch:         IF → ID → EX               (3 周期)
 *     Jump:           IF → ID → EX               (3 周期)
 *     SYSTEM/FENCE:   IF → ID → EX               (3 周期)
 *
 *   每个 sim_step_multi() 调用推进 1 个时钟周期（1 个 FSM 状态）。
 *
 * 中间结果（跨周期保存）：
 *   IR      — 指令寄存器（IF 锁存）
 *   A, B    — 寄存器堆输出（ID 锁存）
 *   ALUOut  — ALU 计算结果（EX 锁存）
 *   MDR     — 内存读取数据（MEM 锁存）
 *   temp_pc — ID 阶段预计算的跳转目标地址
 *
 * 数据通路（共享）：
 *   alu_compute()         — ALU 组合逻辑（EX/MEM/WB 直接调用）
 *   alu_branch_cond()     — 分支比较器（EX 阶段）
 *   cpu_decode()          — 指令译码（ID 阶段）
 *   mmu_read/write_*()    — 内存访问（IF/MEM 阶段）
 *   cpu_trap()            — 异常处理（保持与单周期一致）
 * ============================================================
 */

#include "cpu/controller/controller_internal.h"
#include "cpu/execute.h"
#include "cpu/decode.h"
#include "cpu/datapath/alu.h"
#include "cpu/exec_internal.h"
#include "memory/mmu.h"
#include "linux/syscall.h"
#include "types.h"
#include <stdio.h>
#include <string.h>

/* ── FSM 状态枚举 ────────────────────────────────────────────── */
enum { MC_IF = 0, MC_ID = 1, MC_EX = 2, MC_MEM = 3, MC_WB = 4 };

/* ── 辅助：判断 EX 阶段后是否需要进入 MEM ─────────────────────
 *
 * 只有 Load/Store 类指令需要访存。
 * 返回 true → 下一状态 = MC_MEM
 * 返回 false → 下一状态 = MC_WB（需要写回）或 MC_IF（指令完成）
 */
static inline bool mc_needs_mem(uint8_t opcode)
{
    return (opcode == 0x03) || (opcode == 0x23) ||   /* Load / Store */
           (opcode == 0x07) || (opcode == 0x27);      /* FLW  / FSW   */
}

/* ── 辅助：判断 EX 阶段后是否需要进入 WB ─────────────────────
 *
 * 需要写回寄存器的指令：R-type ALU、I-type ALU、Load、LUI、AUIPC、FP
 * 注意：Load 在 MEM 之后才进入 WB，此处判断的是 EX 之后直接进 WB 的类型
 */
static inline bool mc_needs_wb_after_ex(uint8_t opcode)
{
    return (opcode == 0x33) ||   /* OP (R-type)        */
           (opcode == 0x13) ||   /* OP-IMM (I-type)    */
           (opcode == 0x37) ||   /* LUI                */
           (opcode == 0x17) ||   /* AUIPC              */
           (opcode == 0x53) ||   /* OP-FP              */
           (opcode == 0x43) ||   /* FMA                */
           (opcode == 0x47) ||   /* FMA                */
           (opcode == 0x4B) ||   /* FMA                */
           (opcode == 0x4F);     /* FMA                */
}

/* ── 辅助：判断是否是浮点（写 fregs 而非 regs）──────────────── */
static inline bool mc_is_fp_op(uint8_t opcode)
{
    return (opcode == 0x07) || (opcode == 0x53) ||
           (opcode == 0x43) || (opcode == 0x47) ||
           (opcode == 0x4B) || (opcode == 0x4F);
}

/* ── ALU 操作名查找表 ──────────────────────────────────────────── */
static const char *mc_alu_op_name(AluOp op)
{
    switch (op) {
        case ALU_ADD:  return "add";
        case ALU_SUB:  return "sub";
        case ALU_SLL:  return "sll";
        case ALU_SLT:  return "slt";
        case ALU_SLTU: return "sltu";
        case ALU_XOR:  return "xor";
        case ALU_SRL:  return "srl";
        case ALU_SRA:  return "sra";
        case ALU_OR:   return "or";
        case ALU_AND:  return "and";
        default:       return "?";
    }
}

/* ── 填充 DatapathState（多周期指令完成时调用）──────────────────── */
static void mc_fill_dp(Simulator *sim, const DecodedInstr *d,
                       uint32_t pc, uint32_t next_pc,
                       uint32_t a, uint32_t b,
                       uint32_t alu_out, uint32_t mdr, bool load_done)
{
    DatapathState *dp = &sim->dp;
    memset(dp, 0, sizeof(*dp));
    dp->pc     = pc;
    dp->instr  = sim->mc.ir;
    dp->opcode = d->opcode;
    dp->rd     = d->rd;
    dp->rs1    = d->rs1;
    dp->rs2    = d->rs2;
    dp->funct3 = d->funct3;
    dp->funct7 = d->funct7;
    dp->imm    = d->imm;
    dp->rs1_val = a;
    dp->rs2_val = b;
    dp->next_pc = next_pc;
    cpu_disasm(sim->mc.ir, pc, dp->disasm, sizeof(dp->disasm));

    dp->reg_write = (d->rd != 0);
    if (dp->reg_write) {
        dp->rd_val = load_done ? mdr : alu_out;
    }

    switch (d->opcode) {
    case 0x13: /* OP-IMM */
        {
            AluOp op = alu_select_op(d->funct3, d->funct7, false);
            strncpy(dp->alu_op, mc_alu_op_name(op), sizeof(dp->alu_op) - 1);
            dp->alu_a = a;
            dp->alu_b = (uint32_t)d->imm;
            dp->alu_result = alu_compute(op, dp->alu_a, dp->alu_b);
        }
        break;
    case 0x33: /* OP */
        if (d->funct7 == 1) {
            strncpy(dp->alu_op, "mul", sizeof(dp->alu_op) - 1);
            dp->alu_a = a;
            dp->alu_b = b;
            dp->alu_result = sim->cpu.regs[d->rd];
        } else {
            AluOp op = alu_select_op(d->funct3, d->funct7, true);
            strncpy(dp->alu_op, mc_alu_op_name(op), sizeof(dp->alu_op) - 1);
            dp->alu_a = a;
            dp->alu_b = b;
            dp->alu_result = alu_compute(op, dp->alu_a, dp->alu_b);
        }
        break;
    case 0x03: /* LOAD */
        strncpy(dp->alu_op, "add", sizeof(dp->alu_op) - 1);
        dp->alu_a = a;
        dp->alu_b = (uint32_t)d->imm;
        dp->alu_result = alu_out;
        dp->mem_addr = alu_out;
        dp->mem_read = true;
        dp->mem_rdata = mdr;
        break;
    case 0x23: /* STORE */
        strncpy(dp->alu_op, "add", sizeof(dp->alu_op) - 1);
        dp->alu_a = a;
        dp->alu_b = (uint32_t)d->imm;
        dp->alu_result = alu_out;
        dp->mem_addr = alu_out;
        dp->mem_write = true;
        dp->mem_wdata = b;
        break;
    case 0x63: /* BRANCH */
        strncpy(dp->alu_op, "sub", sizeof(dp->alu_op) - 1);
        dp->alu_a = a;
        dp->alu_b = b;
        dp->branch_taken = (next_pc != pc + 4);
        break;
    case 0x6F: /* JAL */
        strncpy(dp->alu_op, "add", sizeof(dp->alu_op) - 1);
        dp->alu_a = pc;
        dp->alu_b = 4;
        dp->alu_result = pc + 4;
        dp->reg_write = true;
        dp->rd_val = pc + 4;
        dp->branch_taken = true;
        break;
    case 0x67: /* JALR */
        strncpy(dp->alu_op, "add", sizeof(dp->alu_op) - 1);
        dp->alu_a = a;
        dp->alu_b = (uint32_t)d->imm;
        dp->alu_result = (a + (uint32_t)d->imm) & ~1u;
        dp->reg_write = true;
        dp->rd_val = pc + 4;
        dp->branch_taken = true;
        break;
    case 0x17: /* AUIPC */
        strncpy(dp->alu_op, "add", sizeof(dp->alu_op) - 1);
        dp->alu_a = pc;
        dp->alu_b = (uint32_t)d->imm;
        dp->alu_result = pc + (uint32_t)d->imm;
        break;
    case 0x37: /* LUI */
        strncpy(dp->alu_op, "none", sizeof(dp->alu_op) - 1);
        dp->alu_result = (uint32_t)d->imm;
        break;
    default:
        strncpy(dp->alu_op, "none", sizeof(dp->alu_op) - 1);
        break;
    }
    dp->valid = true;

    /* 更新 FSM 状态名 */
    strncpy(dp->fsm_state, sim->mc.fsm_name, sizeof(dp->fsm_state) - 1);
}

/* ════════════════════════════════════════════════════════════
 * sim_step_multi — 多周期 CPU：推进 1 个 FSM 状态
 *
 * 每调用 1 次 = 1 个时钟周期。
 * 状态间通过 sim->mc 共享中间结果（IR / A / B / ALUOut / MDR）。
 *
 * ┌──────┬────────────────────────────────────────────┐
 * │ 状态  │  硬件动作                                   │
 * ├──────┼────────────────────────────────────────────┤
 * │  IF  │  IR ← Mem[PC]; PC ← PC + 4                │
 * │  ID  │  A ← Reg[rs1]; B ← Reg[rs2]; temp_pc ← PC+imm │
 * │  EX  │  ALUOut ← A op B; 分支/跳转在此完成         │
 * │ MEM  │  MDR ← Mem[ALUOut] (Load); Mem[ALUOut] ← B (Store) │
 * │  WB  │  Reg[rd] ← ALUOut 或 MDR                  │
 * └──────┴────────────────────────────────────────────┘
 * ════════════════════════════════════════════════════════
 */
void sim_step_multi(Simulator *sim)
{
    MultiCycleState *mc = &sim->mc;
    CPU             *cpu = &sim->cpu;
    ExceptionType    exc = EXC_NONE;

    sim->cycle_count++;   /* 每个周期递增一次 */

    /* ════════════════════════════════════════════════════════
     * State 0: IF — 取指
     * ════════════════════════════════════════════════════════
     */
    if (mc->state == MC_IF) {
        strncpy(mc->fsm_name, "IF", sizeof(mc->fsm_name) - 1);
        strncpy(sim->dp.fsm_state, "IF", sizeof(sim->dp.fsm_state) - 1);
        sim->dp.valid = false;
        sim->dp.pc = cpu->pc;

        mc->pc = cpu->pc;   /* 保存当前指令 PC */

        /* 取指：读 32-bit 指令 */
        if (!mmu_read_32(&sim->mmu, &sim->pmem, mc->pc, &mc->ir,
                         cpu->priv, &exc)) {
            cpu_trap(sim, (uint32_t)exc, mc->pc);
            sim->instr_count++;
            mc->state = MC_IF;   /* 下一条指令从头开始 */
            return;
        }

        /* PC ← PC + 4（顺序执行默认值） */
        mc->next_pc = mc->pc + 4;

        mc->state = MC_ID;
        return;
    }

    /* ════════════════════════════════════════════════════════
     * State 1: ID — 译码 + 读寄存器 + 预计算跳转目标
     * ════════════════════════════════════════════════════════
     */
    if (mc->state == MC_ID) {
        strncpy(mc->fsm_name, "ID", sizeof(mc->fsm_name) - 1);
        strncpy(sim->dp.fsm_state, "ID", sizeof(sim->dp.fsm_state) - 1);

        DecodedInstr d = cpu_decode(mc->ir);
        mc->ir_decoded = d;  /* 缓存解码结果，跨状态复用 */

        /* 读寄存器（所有指令都读，多路选择器在 EX 阶段选） */
        mc->a = cpu->regs[d.rs1];
        mc->b = cpu->regs[d.rs2];

        /* 预计算跳转目标地址
         *
         * B-type (0x63): 分支目标 = PC + imm
         * J-type (0x6F): 跳转目标  = PC + imm
         * I-type (0x67): JALR 目标 = rs1 + imm（在 EX 计算）
         * 其他指令:      不关心 temp_pc
         *
         * 在真实的五级流水线中，这个加法在 ID 阶段的专用加法器完成；
         * 在多周期中，我们利用 MC_ID→MC_EX 之间的空闲周期预计算。
         */
        switch (d.opcode) {
        case 0x63:   /* B-type: 分支 */
        case 0x6F:   /* J-type: JAL */
            mc->temp_pc = mc->pc + (uint32_t)d.imm;
            break;
        case 0x67:   /* I-type: JALR — 在 EX 阶段计算 */
        default:
            mc->temp_pc = 0;   /* 不适用 */
            break;
        }

        /* 保存解码字段到 DatapathState（复用 mc 存储）——
         * 因为需要跨状态保存 opcode/funct3/funct7/rd/imm */
        mc->state = MC_EX;
        return;
    }

    /* ── 解码当前指令（EX/MEM/WB 状态都需要）──────────────────── */
    /* 使用 ID 阶段缓存的解码结果 */
    DecodedInstr d = mc->ir_decoded;

    /* ════════════════════════════════════════════════════════
     * State 2: EX — 执行 / 地址计算
     *
     * 根据 opcode 分流：
     *   R-type: ALUOut = A op B           → WB
     *   I-type: ALUOut = A op imm         → WB
     *   Load:   ALUOut = A + imm           → MEM
     *   Store:  ALUOut = A + imm           → MEM
     *   Branch: if (cond) next_pc = temp_pc → IF (done)
     *   JAL:    next_pc = temp_pc; rd=PC+4  → IF (done)
     *   JALR:   next_pc = (A+imm)&~1; rd=PC+4 → IF (done)
     *   LUI:    ALUOut = imm               → WB
     *   AUIPC:  ALUOut = PC + imm          → WB
     *   SYSTEM: ecall/ebreak/mret          → IF (done)
     *   FENCE:  NOP                         → IF (done)
     * ════════════════════════════════════════════════════════
     */
    if (mc->state == MC_EX) {
        strncpy(mc->fsm_name, "EX", sizeof(mc->fsm_name) - 1);
        strncpy(sim->dp.fsm_state, "EX", sizeof(sim->dp.fsm_state) - 1);

        bool instr_done = false;   /* 本指令在此状态完成后结束 */
        bool needs_wb   = false;   /* 进入 WB 状态（而不是 MEM） */

        switch (d.opcode) {

        /* ── R-type: OP (RV32I + M) ─────────────────────── */
        case 0x33:
            if (d.funct7 == 1) {
                /* M 扩展 — 乘除法不经过 ALU，需单独处理。
                 * 多周期中复用 exec_m_muldiv 完成运算。
                 * 注意：exec_m_muldiv 直接写 regs[rd]，所以我们
                 * 把结果存入 mc->alu_out 供 WB 使用（或直接完成）。 */
                uint32_t saved_next_pc = mc->next_pc;
                exec_m_muldiv(sim, &d, &mc->next_pc);
                mc->next_pc = saved_next_pc;
                mc->alu_out = cpu->regs[d.rd];
            } else {
                AluOp op = alu_select_op(d.funct3, d.funct7, true);
                mc->alu_out = alu_compute(op, mc->a, mc->b);
            }
            needs_wb = true;
            break;

        /* ── I-type: OP-IMM ──────────────────────────────── */
        case 0x13: {
            AluOp op = alu_select_op(d.funct3, d.funct7, false);
            mc->alu_out = alu_compute(op, mc->a, (uint32_t)d.imm);
            needs_wb = true;
            break;
        }

        /* ── Load: LB/LH/LW/LBU/LHU ──────────────────────── */
        case 0x03:
            mc->alu_out = mc->a + (uint32_t)d.imm;   /* 地址计算 */
            /* → MEM */
            break;

        /* ── Store: SB/SH/SW ──────────────────────────────── */
        case 0x23:
            mc->alu_out = mc->a + (uint32_t)d.imm;   /* 地址计算 */
            /* → MEM */
            break;

        /* ── Branch: BEQ/BNE/BLT/BGE/BLTU/BGEU ───────────── */
        case 0x63:
            if (d.funct3 == 2 || d.funct3 == 3) {
                cpu_trap(sim, EXC_ILLEGAL_INST, d.opcode);
                instr_done = true;
                break;
            }
            if (alu_branch_cond(d.funct3, mc->a, mc->b)) {
                mc->next_pc = mc->temp_pc;   /* 分支跳转 */
            }
            instr_done = true;
            break;

        /* ── JAL: 跳转并链接 ──────────────────────────────── */
        case 0x6F:
            cpu->regs[d.rd] = mc->pc + 4;   /* 返回地址 */
            mc->next_pc = mc->temp_pc;       /* 跳转目标 */
            cpu->regs[0] = 0;                /* x0 硬连线 */
            instr_done = true;
            break;

        /* ── JALR: 寄存器间接跳转 ──────────────────────────── */
        case 0x67:
            cpu->regs[d.rd] = mc->pc + 4;   /* 返回地址 */
            mc->next_pc = (mc->a + (uint32_t)d.imm) & ~1u;  /* 目标 */
            cpu->regs[0] = 0;
            instr_done = true;
            break;

        /* ── LUI ──────────────────────────────────────────── */
        case 0x37:
            mc->alu_out = (uint32_t)d.imm;   /* 立即数直通 ALU */
            needs_wb = true;
            break;

        /* ── AUIPC ────────────────────────────────────────── */
        case 0x17:
            mc->alu_out = mc->pc + (uint32_t)d.imm;
            needs_wb = true;
            break;

        /* ── SYSTEM: ECALL/EBREAK/MRET ────────────────────── */
        case 0x73:
            {
                /* 在多周期模型中，系统指令在 EX 阶段完成。
                 * 复用 exec_system 的语义但手动分发。 */
                uint32_t saved_next_pc = mc->next_pc;
                if (d.funct3 == 0) {
                    if (d.imm == 0) {                    /* ECALL */
                        syscall_handler(sim);
                    } else if (d.imm == 1) {             /* EBREAK */
                        cpu_trap(sim, EXC_BREAKPOINT, mc->pc);
                    } else if (d.imm == 0x302) {         /* MRET */
                        cpu->priv = (cpu->mstatus >> 11) & 0x3;
                        /* MRET: MIE ← MPIE, MPIE ← 1 */
                        uint32_t mpie_mc = (cpu->mstatus >> 7) & 1;
                        cpu->mstatus = (cpu->mstatus & ~((1u << 7) | (1u << 3)))
                                     | (mpie_mc << 3) | (1u << 7);
                        mc->next_pc = cpu->mepc;
                    } else {
                        cpu_trap(sim, EXC_ILLEGAL_INST, d.opcode);
                    }
                } else {
                    /* CSR 指令暂不实现 → 非法指令 */
                    cpu_trap(sim, EXC_ILLEGAL_INST, d.opcode);
                }
                (void)saved_next_pc;   /* trap 可能修改 next_pc */
            }
            instr_done = true;
            break;

        /* ── FENCE ────────────────────────────────────────── */
        case 0x0F:
            /* NOP：单核中无意义 */
            instr_done = true;
            break;

        /* ── Load-FP (FLW) ────────────────────────────────── */
        case 0x07:
            mc->alu_out = mc->a + (uint32_t)d.imm;   /* 地址计算 */
            /* → MEM */
            break;

        /* ── Store-FP (FSW) ───────────────────────────────── */
        case 0x27:
            mc->alu_out = mc->a + (uint32_t)d.imm;   /* 地址计算 */
            /* → MEM */
            break;

        /* ── OP-FP / FMA ──────────────────────────────────── */
        case 0x53:
        case 0x43:
        case 0x47:
        case 0x4B:
        case 0x4F:
            /* 浮点运算 — 复用 exec_f 函数 */
            {
                uint32_t saved_next_pc = mc->next_pc;
                if (d.opcode == 0x53) {
                    exec_fp_op(sim, &d, &mc->next_pc);
                } else {
                    exec_fma(sim, &d, &mc->next_pc);
                }
                mc->next_pc = saved_next_pc;
                /* 浮点结果已写入 fregs[rd]，不需要额外 WB */
            }
            needs_wb = true;
            break;

        /* ── 非法 opcode ──────────────────────────────────── */
        default:
            cpu_trap(sim, EXC_ILLEGAL_INST, d.opcode);
            instr_done = true;
            break;
        }

        /* ── 确定下一状态 ────────────────────────────────── */
        if (instr_done) {
            mc->state = MC_IF;
            sim->instr_count++;
            sim->cpu.pc = mc->next_pc;
            /* 填充数据通路信号 */
            mc_fill_dp(sim, &d, mc->pc, mc->next_pc,
                       mc->a, mc->b, mc->alu_out, mc->mdr, false);
        } else if (mc_needs_mem(d.opcode)) {
            mc->state = MC_MEM;
        } else if (needs_wb || mc_needs_wb_after_ex(d.opcode)) {
            mc->state = MC_WB;
        } else {
            /* fallback：直接完成 */
            mc->state = MC_IF;
            sim->instr_count++;
            sim->cpu.pc = mc->next_pc;
        }
        return;
    }

    /* ════════════════════════════════════════════════════════
     * State 3: MEM — 内存访问
     *
     * Load:  MDR ← Mem[ALUOut]
     * Store: Mem[ALUOut] ← B
     * ════════════════════════════════════════════════════════
     */
    if (mc->state == MC_MEM) {
        strncpy(mc->fsm_name, "MEM", sizeof(mc->fsm_name) - 1);
        strncpy(sim->dp.fsm_state, "MEM", sizeof(sim->dp.fsm_state) - 1);

        switch (d.opcode) {
        case 0x03:   /* Load: LB/LH/LW/LBU/LHU */
            {
                /* 复用 mmu_read_* 按 funct3 选宽度 */
                switch (d.funct3) {
                case 0: {   /* LB */
                    uint8_t val8;
                    if (!mmu_read_8(&sim->mmu, &sim->pmem, mc->alu_out,
                                    &val8, cpu->priv, &exc)) {
                        cpu_trap(sim, (uint32_t)exc, mc->alu_out);
                        mc->state = MC_IF; sim->instr_count++;
                        sim->cpu.pc = mc->next_pc; return;
                    }
                    mc->mdr = (uint32_t)(int32_t)(int8_t)val8;
                    break;
                }
                case 1: {   /* LH */
                    uint16_t val16;
                    if (!mmu_read_16(&sim->mmu, &sim->pmem, mc->alu_out,
                                     &val16, cpu->priv, &exc)) {
                        cpu_trap(sim, (uint32_t)exc, mc->alu_out);
                        mc->state = MC_IF; sim->instr_count++;
                        sim->cpu.pc = mc->next_pc; return;
                    }
                    mc->mdr = (uint32_t)(int32_t)(int16_t)val16;
                    break;
                }
                case 2: {   /* LW */
                    if (!mmu_read_32(&sim->mmu, &sim->pmem, mc->alu_out,
                                     &mc->mdr, cpu->priv, &exc)) {
                        cpu_trap(sim, (uint32_t)exc, mc->alu_out);
                        mc->state = MC_IF; sim->instr_count++;
                        sim->cpu.pc = mc->next_pc; return;
                    }
                    break;
                }
                case 4: {   /* LBU */
                    uint8_t val8;
                    if (!mmu_read_8(&sim->mmu, &sim->pmem, mc->alu_out,
                                    &val8, cpu->priv, &exc)) {
                        cpu_trap(sim, (uint32_t)exc, mc->alu_out);
                        mc->state = MC_IF; sim->instr_count++;
                        sim->cpu.pc = mc->next_pc; return;
                    }
                    mc->mdr = (uint32_t)val8;
                    break;
                }
                case 5: {   /* LHU */
                    uint16_t val16;
                    if (!mmu_read_16(&sim->mmu, &sim->pmem, mc->alu_out,
                                     &val16, cpu->priv, &exc)) {
                        cpu_trap(sim, (uint32_t)exc, mc->alu_out);
                        mc->state = MC_IF; sim->instr_count++;
                        sim->cpu.pc = mc->next_pc; return;
                    }
                    mc->mdr = (uint32_t)val16;
                    break;
                }
                default:
                    cpu_trap(sim, EXC_ILLEGAL_INST, d.opcode);
                    mc->state = MC_IF; sim->instr_count++;
                    sim->cpu.pc = mc->next_pc; return;
                }
            }
            mc->state = MC_WB;   /* Load: MEM → WB */
            break;

        case 0x23:   /* Store: SB/SH/SW */
            {
                switch (d.funct3) {
                case 0:   /* SB */
                    mmu_write_8(&sim->mmu, &sim->pmem, mc->alu_out,
                                (uint8_t)(mc->b & 0xFF), cpu->priv, &exc);
                    break;
                case 1:   /* SH */
                    mmu_write_16(&sim->mmu, &sim->pmem, mc->alu_out,
                                 (uint16_t)(mc->b & 0xFFFF), cpu->priv, &exc);
                    break;
                case 2:   /* SW */
                    mmu_write_32(&sim->mmu, &sim->pmem, mc->alu_out,
                                 mc->b, cpu->priv, &exc);
                    break;
                default:
                    cpu_trap(sim, EXC_ILLEGAL_INST, d.opcode);
                    mc->state = MC_IF; sim->instr_count++;
                    sim->cpu.pc = mc->next_pc; return;
                }
                if (exc != EXC_NONE) {
                    cpu_trap(sim, (uint32_t)exc, mc->alu_out);
                }
            }
            mc->state = MC_IF;   /* Store: MEM → IF（无 WB） */
            sim->instr_count++;
            sim->cpu.pc = mc->next_pc;
            mc_fill_dp(sim, &d, mc->pc, mc->next_pc,
                       mc->a, mc->b, mc->alu_out, mc->mdr, false);
            break;

        case 0x07:   /* FLW — 浮点 Load */
            {
                if (!mmu_read_32(&sim->mmu, &sim->pmem, mc->alu_out,
                                 &mc->mdr, cpu->priv, &exc)) {
                    cpu_trap(sim, (uint32_t)exc, mc->alu_out);
                    mc->state = MC_IF; sim->instr_count++;
                    sim->cpu.pc = mc->next_pc; return;
                }
            }
            mc->state = MC_WB;   /* FLW: MEM → WB */
            break;

        case 0x27:   /* FSW — 浮点 Store（数据来自 fregs，非 regs） */
            {
                mmu_write_32(&sim->mmu, &sim->pmem, mc->alu_out,
                             cpu->fregs[d.rs2], cpu->priv, &exc);
                if (exc != EXC_NONE) {
                    cpu_trap(sim, (uint32_t)exc, mc->alu_out);
                }
            }
            mc->state = MC_IF;   /* FSW: MEM → IF（无 WB） */
            sim->instr_count++;
            sim->cpu.pc = mc->next_pc;
            mc_fill_dp(sim, &d, mc->pc, mc->next_pc,
                       mc->a, mc->b, mc->alu_out, mc->mdr, false);
            break;

        default:
            /* 不应到达此处 */
            mc->state = MC_IF;
            sim->instr_count++;
            sim->cpu.pc = mc->next_pc;
            break;
        }
        return;
    }

    /* ════════════════════════════════════════════════════════
     * State 4: WB — 写回
     *
     * R-type / I-type / LUI / AUIPC: Reg[rd] ← ALUOut
     * Load:                          Reg[rd] ← MDR
     * FLW / FP / FMA:                浮点结果已在 EX 写入 fregs
     * ════════════════════════════════════════════════════════
     */
    if (mc->state == MC_WB) {
        strncpy(mc->fsm_name, "WB", sizeof(mc->fsm_name) - 1);
        strncpy(sim->dp.fsm_state, "WB", sizeof(sim->dp.fsm_state) - 1);

        bool is_fp = mc_is_fp_op(d.opcode);
        bool load_done = false;

        if (!is_fp && d.rd != 0) {
            /* 整数写回：判断数据来源 */
            if (mc_needs_mem(d.opcode) && d.opcode != 0x23 && d.opcode != 0x27) {
                /* Load 类：数据来自 MDR（含 FLW 已在 MEM 存入 MDR） */
                if (d.opcode == 0x07) {
                    cpu->fregs[d.rd] = mc->mdr;   /* FLW → 浮点寄存器 */
                } else {
                    cpu->regs[d.rd] = mc->mdr;    /* LW/LH/LB → 整数寄存器 */
                }
                load_done = true;
            } else {
                /* ALU 类：数据来自 ALUOut */
                cpu->regs[d.rd] = mc->alu_out;
            }
        } else if (is_fp) {
            /* 浮点结果：0x53/0x43/0x47/0x4B/0x4F 已在 EX 阶段写入 fregs；
             * 0x07 (FLW) 数据来自 MEM 阶段的 MDR，需在此写入 fregs */
            if (d.opcode == 0x07 && d.rd != 0) {
                cpu->fregs[d.rd] = mc->mdr;
            }
        }

        /* x0 硬连线 */
        cpu->regs[0] = 0;

        /* 指令完成 */
        sim->instr_count++;
        sim->cpu.pc = mc->next_pc;
        mc->state = MC_IF;

        /* 填充数据通路信号 */
        mc_fill_dp(sim, &d, mc->pc, mc->next_pc,
                   mc->a, mc->b, mc->alu_out, mc->mdr, load_done);
        return;
    }
}
