/* ============================================================
 * pipeline.c — 五级流水线 CPU 控制器
 *
 * 时序模型（Patterson & Hennessy §4.7-4.8）：
 *   每个时钟周期 5 个阶段并行工作：
 *     IF  — 取指（Instruction Fetch）
 *     ID  — 译码 + 读寄存器（Instruction Decode）
 *     EX  — 执行 / 地址计算（Execute）
 *     MEM — 内存访问（Memory Access）
 *     WB  — 写回（Write Back）
 *
 *   最多 5 条指令同时在 fly。理想 CPI = 1.0。
 *
 * 数据冒险处理：
 *   + Forwarding（前递）: EX/MEM → EX, MEM/WB → EX
 *     - EX/MEM 优先级高于 MEM/WB（最新结果优先）
 *     - MEM/WB 转发源：Load 选 mem_data，ALU 选 alu_result
 *     - 只转发整数寄存器（FP 寄存器文件独立，暂不转发）
 *   + Stall（停顿）:     Load-use 冒险 → 插入 1 个气泡
 *     - 检测条件：ID/EX 是 Load 且 rd == IF/ID 的 rs1 或 rs2
 *
 * 控制冒险处理：
 *   + 分支预测：总是预测不跳转（predict not-taken）
 *   + 误预测惩罚：2 周期（冲刷 IF/ID + ID/EX）
 *   + 分支/jump 在 EX 阶段决议
 *
 * 异常处理：
 *   + 陷阱（ecall/ebreak）在 EX 阶段触发
 *   + 触发后冲刷 IF/ID + ID/EX，MEM/WB 自然排空
 *   + 访存异常（Load/Store fault）在 MEM 阶段触发
 *
 * 数据通路（共享）：
 *   alu_compute()         — ALU 组合逻辑
 *   alu_branch_cond()     — 分支比较器
 *   alu_select_op()       — funct3/funct7 → AluOp 映射
 *   cpu_decode()          — 指令译码
 *   mmu_read/write_*()    — 内存访问
 *   cpu_trap()            — 异常处理
 *   exec_m_muldiv()       — M 扩展（乘除法）
 *   exec_fp_op/exec_fma() — F 扩展（浮点运算）
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

/* ════════════════════════════════════════════════════════════
 * sim_step_pipeline — 五级流水线：推进 1 个时钟周期
 *
 * 处理顺序：WB → MEM → EX → ID → IF（逆序）
 *
 * 逆序处理的原因：
 *   WB 先写寄存器 → ID 后读寄存器，同周期内 WB→ID 无需转发
 *   EX 需要从 EX/MEM 和 MEM/WB 转发（这些是上一周期的值）
 *
 * 每周期步骤：
 *   ① 判断流水线是否已排空（CPU 停止 + 全部寄存器无效）
 *   ② 冒险检测（Load-use hazard → stall）
 *   ③ WB 阶段：写回结果到寄存器文件
 *   ④ MEM 阶段：访存（Load/Store）
 *   ⑤ EX 阶段：ALU 运算 + 转发 + 分支决议
 *   ⑥ ID 阶段：译码 + 读寄存器
 *   ⑦ IF 阶段：取指
 *   ⑧ 应用 stall/flush 覆盖
 *   ⑨ 推进流水线寄存器
 * ════════════════════════════════════════════════════════════
 */
void sim_step_pipeline(Simulator *sim)
{
    PipelineState *p = &sim->pipe;
    CPU           *cpu = &sim->cpu;

    /* ── ① 流水线排空检查 ──────────────────────────────────
     *
     * CPU 已停止（trap/ecall）且所有流水线寄存器无效 → 无事可做。
     * 这样设计保证了 trap 后 MEM 和 WB 阶段的指令能自然完成。
     */
    bool pipeline_empty = !p->if_id.valid && !p->id_ex.valid &&
                          !p->ex_mem.valid && !p->mem_wb.valid;
    if (!cpu->running && pipeline_empty) {
        return;
    }

    /* ── 下周期流水线寄存器（临时变量，最后统一赋值）────── */
    PipeIFID   next_if_id  = {0};
    PipeIDEX   next_id_ex  = {0};
    PipeEXMEM  next_ex_mem = {0};
    PipeMEMWB  next_mem_wb = {0};

    bool stall = false;   /* Load-use 停顿 */
    bool flush = false;   /* 分支/跳转/陷阱冲刷  */
    bool fwd_a = false;   /* rs1 转发激活（供 DatapathState） */
    bool fwd_b = false;   /* rs2 转发激活（供 DatapathState） */

    /* ════════════════════════════════════════════════════════
     * ② 冒险检测（ID 阶段 — 检测 Load-use hazard）
     *
     * 条件：
     *   - ID/EX 阶段是一条 Load（mem_read=true）
     *   - Load 的目标寄存器 ≠ x0
     *   - IF/ID 阶段的任一源寄存器 == Load 的目标寄存器
     *
     * 处理：停顿 1 周期（保持 IF/ID，ID/EX 插入气泡）
     * 停顿后 Load 进入 MEM→WB，通过转发将数据传给下一条指令的 EX
     * ════════════════════════════════════════════════════════
     */
    /* 检查 ID/EX 是否为 Load（opcode=0x03 或 FLW=0x07） */
    bool id_ex_is_load = (p->id_ex.d.opcode == 0x03 || p->id_ex.d.opcode == 0x07);

    if (p->id_ex.valid && id_ex_is_load && p->id_ex.d.rd != 0) {
        if (p->if_id.valid) {
            uint8_t rs1 = RS1(p->if_id.instr);
            uint8_t rs2 = RS2(p->if_id.instr);
            if (rs1 == p->id_ex.d.rd || rs2 == p->id_ex.d.rd) {
                stall = true;
            }
        }
    }

    /* ════════════════════════════════════════════════════════
     * ③ WB 阶段 — 写回
     *
     *   数据来源：
     *     Load 指令 → mem_wb.mem_data
     *     ALU 指令 → mem_wb.alu_result
     *   浮点 Load → mem_wb.mem_data → fregs[rd]
     * ════════════════════════════════════════════════════════
     */
    if (p->mem_wb.valid) {
        uint32_t wb_data = 0;
        if (p->mem_wb.reg_write && p->mem_wb.rd != 0) {
            /* 整数写回：Load 选 mem_data，ALU 选 alu_result */
            wb_data = p->mem_wb.is_load
                    ? p->mem_wb.mem_data
                    : p->mem_wb.alu_result;
            cpu->regs[p->mem_wb.rd] = wb_data;
        } else if (p->mem_wb.is_fp_load && p->mem_wb.rd != 0) {
            /* 浮点 Load (FLW) */
            wb_data = p->mem_wb.mem_data;
            cpu->fregs[p->mem_wb.rd] = wb_data;
        }
        cpu->regs[0] = 0;           /* x0 硬连线 */
        sim->instr_count++;         /* 指令在 WB 阶段完成 */

        /* ── 填充 WB 阶段数据通路快照 ──────────────────── */
        DatapathState *wbdp = &p->last_wb_dp;
        memset(wbdp, 0, sizeof(*wbdp));
        wbdp->pc     = p->mem_wb.pc;
        wbdp->instr  = p->mem_wb.instr;
        wbdp->opcode = p->mem_wb.opcode;
        wbdp->rd     = p->mem_wb.rd;
        wbdp->funct3 = p->mem_wb.funct3;
        wbdp->alu_result = p->mem_wb.alu_result;
        wbdp->rd_val = wb_data;
        wbdp->reg_write = p->mem_wb.reg_write;
        wbdp->valid = true;

        /* 反汇编 */
        cpu_disasm(p->mem_wb.instr, p->mem_wb.pc,
                   wbdp->disasm, sizeof(wbdp->disasm));

        /* 根据 opcode + funct3 + funct7 推导 ALU 操作名 */
        {
            uint8_t op = p->mem_wb.opcode;
            uint8_t f3 = p->mem_wb.funct3;
            uint8_t f7 = p->mem_wb.funct7;

            switch (op) {
            case 0x33: /* R-type */
                {
                    AluOp alu = alu_select_op(f3, f7, true);
                    strncpy(wbdp->alu_op,
                            alu == ALU_ADD ? "add" : alu == ALU_SUB ? "sub" :
                            alu == ALU_SLL ? "sll" : alu == ALU_SLT ? "slt" :
                            alu == ALU_SLTU ? "sltu" : alu == ALU_XOR ? "xor" :
                            alu == ALU_SRL ? "srl" : alu == ALU_SRA ? "sra" :
                            alu == ALU_OR ? "or" : alu == ALU_AND ? "and" : "?",
                            sizeof(wbdp->alu_op)-1);
                }
                break;
            case 0x13: /* I-type */
                {
                    AluOp alu = alu_select_op(f3, f7, false);
                    strncpy(wbdp->alu_op,
                            alu == ALU_ADD ? "addi" : alu == ALU_SLL ? "slli" :
                            alu == ALU_SLT ? "slti" : alu == ALU_SLTU ? "sltiu" :
                            alu == ALU_XOR ? "xori" : alu == ALU_OR ? "ori" :
                            alu == ALU_AND ? "andi" : alu == ALU_SRL ? "srli" :
                            alu == ALU_SRA ? "srai" : "?",
                            sizeof(wbdp->alu_op)-1);
                }
                break;
            case 0x03: /* Load */
            case 0x07: /* FLW — 浮点 Load */
                strncpy(wbdp->alu_op, "add", sizeof(wbdp->alu_op)-1);
                wbdp->mem_read = true;
                wbdp->mem_addr = p->mem_wb.alu_result;
                wbdp->mem_rdata = wb_data;
                break;
            case 0x23: /* Store */
            case 0x27: /* FSW — 浮点 Store */
                strncpy(wbdp->alu_op, "add", sizeof(wbdp->alu_op)-1);
                wbdp->mem_write = true;
                wbdp->mem_addr = p->mem_wb.alu_result;
                break;
            case 0x63: /* Branch */
                strncpy(wbdp->alu_op, "sub", sizeof(wbdp->alu_op)-1);
                break;
            case 0x6F: /* JAL */
                strncpy(wbdp->alu_op, "add", sizeof(wbdp->alu_op)-1);
                wbdp->reg_write = true;
                wbdp->branch_taken = true;
                break;
            case 0x67: /* JALR */
                strncpy(wbdp->alu_op, "add", sizeof(wbdp->alu_op)-1);
                wbdp->reg_write = true;
                wbdp->branch_taken = true;
                break;
            case 0x37: /* LUI */
                strncpy(wbdp->alu_op, "lui", sizeof(wbdp->alu_op)-1);
                break;
            case 0x17: /* AUIPC */
                strncpy(wbdp->alu_op, "auipc", sizeof(wbdp->alu_op)-1);
                break;
            default:
                strncpy(wbdp->alu_op, "none", sizeof(wbdp->alu_op)-1);
                break;
            }
        }

        /* 如果 WB 完成的是 Load，设置 next_pc = pc + 4（已推进） */
        wbdp->next_pc = p->mem_wb.pc + 4;
    }

    /* ════════════════════════════════════════════════════════
     * ④ MEM 阶段 — 内存访问
     *
     *   Load:  读内存 → next_mem_wb.mem_data
     *   Store: 写内存（使用 EX/MEM 中的 rs2_val）
     *   非访存指令: 直通 alu_result 到 MEM/WB
     * ════════════════════════════════════════════════════════
     */
    if (p->ex_mem.valid) {
        ExceptionType exc = EXC_NONE;

        if (p->ex_mem.mem_read) {
            /* ── Load 类指令 ──────────────────────────── */
            next_mem_wb.is_load = true;

            switch (p->ex_mem.funct3) {
            case 0: {   /* LB — 有符号字节 */
                uint8_t val8;
                if (!mmu_read_8(&sim->mmu, &sim->pmem, p->ex_mem.alu_result,
                                &val8, cpu->priv, &exc)) goto mem_fault;
                next_mem_wb.mem_data = (uint32_t)(int32_t)(int8_t)val8;
                break;
            }
            case 1: {   /* LH — 有符号半字 */
                uint16_t val16;
                if (!mmu_read_16(&sim->mmu, &sim->pmem, p->ex_mem.alu_result,
                                 &val16, cpu->priv, &exc)) goto mem_fault;
                next_mem_wb.mem_data = (uint32_t)(int32_t)(int16_t)val16;
                break;
            }
            case 2: {   /* LW / FLW — 字 */
                uint32_t val32;
                if (!mmu_read_32(&sim->mmu, &sim->pmem, p->ex_mem.alu_result,
                                 &val32, cpu->priv, &exc)) goto mem_fault;
                next_mem_wb.mem_data = val32;
                break;
            }
            case 4: {   /* LBU — 无符号字节 */
                uint8_t val8;
                if (!mmu_read_8(&sim->mmu, &sim->pmem, p->ex_mem.alu_result,
                                &val8, cpu->priv, &exc)) goto mem_fault;
                next_mem_wb.mem_data = (uint32_t)val8;
                break;
            }
            case 5: {   /* LHU — 无符号半字 */
                uint16_t val16;
                if (!mmu_read_16(&sim->mmu, &sim->pmem, p->ex_mem.alu_result,
                                 &val16, cpu->priv, &exc)) goto mem_fault;
                next_mem_wb.mem_data = (uint32_t)val16;
                break;
            }
            default:
                cpu_trap(sim, EXC_ILLEGAL_INST, p->ex_mem.opcode);
                flush = true;
                goto mem_done;  /* 不产生有效 MEM/WB */
            }

            /* 浮点 Load (FLW, opcode=0x07) */
            if (p->ex_mem.is_fp) {
                next_mem_wb.is_fp_load = true;
                next_mem_wb.reg_write  = false;   /* 写 fregs 而非 regs */
            } else {
                next_mem_wb.reg_write  = p->ex_mem.reg_write;
            }

mem_fault:
            if (exc != EXC_NONE) {
                cpu_trap(sim, (uint32_t)exc, p->ex_mem.alu_result);
                flush = true;
                goto mem_done;
            }

        } else if (p->ex_mem.mem_write) {
            /* ── Store 类指令 ──────────────────────────── */
            switch (p->ex_mem.funct3) {
            case 0:   /* SB */
                mmu_write_8(&sim->mmu, &sim->pmem, p->ex_mem.alu_result,
                            (uint8_t)(p->ex_mem.rs2_val & 0xFF),
                            cpu->priv, &exc);
                break;
            case 1:   /* SH */
                mmu_write_16(&sim->mmu, &sim->pmem, p->ex_mem.alu_result,
                             (uint16_t)(p->ex_mem.rs2_val & 0xFFFF),
                             cpu->priv, &exc);
                break;
            case 2:   /* SW / FSW */
                mmu_write_32(&sim->mmu, &sim->pmem, p->ex_mem.alu_result,
                             p->ex_mem.rs2_val, cpu->priv, &exc);
                break;
            default:
                cpu_trap(sim, EXC_ILLEGAL_INST, p->ex_mem.opcode);
                flush = true;
                goto mem_done;
            }

            if (exc != EXC_NONE) {
                cpu_trap(sim, (uint32_t)exc, p->ex_mem.alu_result);
                flush = true;
                goto mem_done;
            }

            /* Store 不写回 → MEM/WB 无效 */
            goto mem_done;

        } else {
            /* ── 非访存指令：直通 ALU 结果 ─────────────── */
            next_mem_wb.alu_result = p->ex_mem.alu_result;
            next_mem_wb.reg_write  = p->ex_mem.reg_write;
            next_mem_wb.is_load    = false;
        }

        /* 公共字段 */
        next_mem_wb.pc        = p->ex_mem.pc;
        next_mem_wb.instr     = p->ex_mem.instr;
        next_mem_wb.rd        = p->ex_mem.rd;
        next_mem_wb.opcode    = p->ex_mem.opcode;
        next_mem_wb.funct3    = p->ex_mem.funct3;
        next_mem_wb.funct7    = p->ex_mem.d.funct7; /* 透传 funct7 */
        next_mem_wb.valid     = true;

mem_done:;
    }

    /* ════════════════════════════════════════════════════════
     * ⑤ EX 阶段 — 执行 / 地址计算
     *
     *   ① 转发多路选择器：EX/MEM → EX, MEM/WB → EX
     *   ② ALU 计算 / 分支条件 / 跳转目标
     *   ③ 控制冒险决议（分支/跳转 → flush）
     *   ④ 陷阱触发（ecall/ebreak → flush）
     * ════════════════════════════════════════════════════════
     */
    /* 注意：Load-use stall 时 Load 必须继续推进 EX → MEM → WB。
     * stall 只阻止后续指令（IF/ID）进入 ID/EX，Load 本身不受影响。 */
    if (p->id_ex.valid) {

        DecodedInstr *d  = &p->id_ex.d;
        uint32_t     pc  = p->id_ex.pc;

        /* ── ① 转发多路选择器 ───────────────────────────
         *
         * 优先级：EX/MEM > MEM/WB（最新结果优先）
         * 条件：reg_write=true, rd!=0, rd 匹配源寄存器号
         *
         * MEM/WB 数据源：
         *   is_load  → mem_data（Load 的结果）
         *   !is_load → alu_result（ALU 的结果）
         *
         * 注意：只转发整数寄存器。FP 指令设置 reg_write=false，
         * 不会参与整数转发（FP 寄存器有独立的转发逻辑，暂未实现）。
         */

        /* 计算 MEM/WB 的转发数据（如果需要） */
        uint32_t wb_forward_data = p->mem_wb.is_load
                                   ? p->mem_wb.mem_data
                                   : p->mem_wb.alu_result;

        uint32_t forward_a = p->id_ex.rs1_val;
        uint32_t forward_b = p->id_ex.rs2_val;

        /* EX/MEM 转发（优先级 1） */
        if (p->ex_mem.valid && p->ex_mem.reg_write && p->ex_mem.rd != 0) {
            if (p->ex_mem.rd == d->rs1) {
                forward_a = p->ex_mem.alu_result;
                /* forwarded from EX/MEM */
            }
            if (p->ex_mem.rd == d->rs2) {
                forward_b = p->ex_mem.alu_result;
                /* forwarded from EX/MEM */
            }
        }

        /* MEM/WB 转发（优先级 2，且 EX/MEM 未转发该寄存器） */
        if (p->mem_wb.valid && p->mem_wb.reg_write && p->mem_wb.rd != 0) {
            bool ex_fwd_rs1 = p->ex_mem.valid && p->ex_mem.reg_write
                              && p->ex_mem.rd == d->rs1;
            bool ex_fwd_rs2 = p->ex_mem.valid && p->ex_mem.reg_write
                              && p->ex_mem.rd == d->rs2;

            if (!ex_fwd_rs1 && p->mem_wb.rd == d->rs1) {
                forward_a = wb_forward_data;
                /* forwarded from MEM/WB */
            }
            if (!ex_fwd_rs2 && p->mem_wb.rd == d->rs2) {
                forward_b = wb_forward_data;
                /* forwarded from MEM/WB */
            }
        }

        /* 记录转发激活（供 DatapathState） */
        fwd_a = (forward_a != p->id_ex.rs1_val);
        fwd_b = (forward_b != p->id_ex.rs2_val);

        /* ── ② 按 opcode 分发执行 ─────────────────────── */
        switch (d->opcode) {

        /* ── R-type: OP (0x33) — RV32I + M 扩展 ──────── */
        case 0x33:
            if (d->funct7 == 1) {
                /* M 扩展 — 乘除法不经过 ALU。
                 * exec_m_muldiv 直接读写 cpu->regs，所以需要
                 * 临时把转发值写入寄存器，调用后再恢复。 */
                uint32_t save_rs1 = cpu->regs[d->rs1];
                uint32_t save_rs2 = cpu->regs[d->rs2];
                uint32_t save_rd  = cpu->regs[d->rd];
                cpu->regs[d->rs1] = forward_a;
                cpu->regs[d->rs2] = forward_b;
                uint32_t dummy_next_pc = pc + 4;
                exec_m_muldiv(sim, d, &dummy_next_pc);
                next_ex_mem.alu_result = cpu->regs[d->rd];
                cpu->regs[d->rs1] = save_rs1;
                cpu->regs[d->rs2] = save_rs2;
                cpu->regs[d->rd]  = save_rd;
            } else {
                AluOp op = alu_select_op(d->funct3, d->funct7, true);
                next_ex_mem.alu_result = alu_compute(op, forward_a, forward_b);
            }
            next_ex_mem.reg_write = true;
            break;

        /* ── I-type: OP-IMM (0x13) ────────────────────── */
        case 0x13: {
            AluOp op = alu_select_op(d->funct3, d->funct7, false);
            next_ex_mem.alu_result = alu_compute(op, forward_a,
                                                 (uint32_t)d->imm);
            next_ex_mem.reg_write = true;
            break;
        }

        /* ── Load (0x03) — LB/LH/LW/LBU/LHU ──────────── */
        case 0x03:
            next_ex_mem.alu_result = forward_a + (uint32_t)d->imm;
            next_ex_mem.mem_read   = true;
            next_ex_mem.reg_write  = true;
            break;

        /* ── Store (0x23) — SB/SH/SW ──────────────────── */
        case 0x23:
            next_ex_mem.alu_result = forward_a + (uint32_t)d->imm;
            next_ex_mem.rs2_val    = forward_b;
            next_ex_mem.mem_write  = true;
            break;

        /* ── Branch (0x63) ──────────────────────────────
         *
         * 预测策略：总是预测不跳转（predict not-taken）。
         * 若实际跳转 → flush IF/ID + ID/EX（2 周期惩罚）。
         */
        case 0x63:
            if (d->funct3 == 2 || d->funct3 == 3) {
                cpu_trap(sim, EXC_ILLEGAL_INST, d->opcode);
                flush = true;
                break;
            }
            if (alu_branch_cond(d->funct3, forward_a, forward_b)) {
                cpu->pc = pc + (uint32_t)d->imm;   /* 更新取指地址 */
                flush   = true;                     /* 冲刷错误指令 */
            }
            /* 分支不写寄存器 */
            break;

        /* ── JAL (0x6F) — 跳转并链接 ──────────────────── */
        case 0x6F:
            next_ex_mem.alu_result = pc + 4;         /* 返回地址 */
            next_ex_mem.reg_write  = true;
            cpu->pc = pc + (uint32_t)d->imm;         /* 跳转目标 */
            flush   = true;
            break;

        /* ── JALR (0x67) — 寄存器间接跳转 ────────────── */
        case 0x67:
            next_ex_mem.alu_result = pc + 4;         /* 返回地址 */
            next_ex_mem.reg_write  = true;
            cpu->pc = (forward_a + (uint32_t)d->imm) & ~1u;
            flush   = true;
            break;

        /* ── LUI (0x37) ───────────────────────────────── */
        case 0x37:
            next_ex_mem.alu_result = (uint32_t)d->imm;
            next_ex_mem.reg_write  = true;
            break;

        /* ── AUIPC (0x17) ─────────────────────────────── */
        case 0x17:
            next_ex_mem.alu_result = pc + (uint32_t)d->imm;
            next_ex_mem.reg_write  = true;
            break;

        /* ── SYSTEM (0x73) — ECALL/EBREAK/MRET ─────────
         *
         * trap 设置 cpu->running=false 后，流水线会在排空后
         * 自然停止。IF/ID 和 ID/EX 立即冲刷。
         */
        case 0x73:
            /* 设置故障指令 PC（cpu_trap 使用 cpu->pc 填充 mepc） */
            sim->cpu.pc = pc;

            if (d->funct3 == 0) {
                if (d->imm == 0) {                    /* ECALL */
                    syscall_handler(sim);
                } else if (d->imm == 1) {             /* EBREAK */
                    cpu_trap(sim, EXC_BREAKPOINT, pc);
                } else if (d->imm == 0x302) {         /* MRET */
                    cpu->priv   = (cpu->mstatus >> 11) & 0x3;
                    /* MRET: MIE ← MPIE, MPIE ← 1 */
                    uint32_t mpie_pipe = (cpu->mstatus >> 7) & 1;
                    cpu->mstatus = (cpu->mstatus & ~((1u << 7) | (1u << 3)))
                                 | (mpie_pipe << 3) | (1u << 7);
                    cpu->pc     = cpu->mepc;
                } else {
                    cpu_trap(sim, EXC_ILLEGAL_INST, d->opcode);
                }
            } else {
                /* CSR 指令暂不实现 */
                cpu_trap(sim, EXC_ILLEGAL_INST, d->opcode);
            }
            flush = true;
            break;

        /* ── FENCE (0x0F) — NOP ────────────────────────── */
        case 0x0F:
            /* 单核系统中无需实现真实内存屏障 */
            break;

        /* ── FLW (0x07) — 浮点 Load ──────────────────── */
        case 0x07:
            next_ex_mem.alu_result = forward_a + (uint32_t)d->imm;
            next_ex_mem.mem_read   = true;
            next_ex_mem.is_fp      = true;
            break;

        /* ── FSW (0x27) — 浮点 Store ────────────────────
         *
         * Store 数据来自 fregs[rs2]（不是 regs[rs2]）。
         * 注意：FP 寄存器目前无转发，直接读 fregs。
         */
        case 0x27:
            next_ex_mem.alu_result = forward_a + (uint32_t)d->imm;
            next_ex_mem.rs2_val    = cpu->fregs[d->rs2];
            next_ex_mem.mem_write  = true;
            next_ex_mem.is_fp      = true;
            break;

        /* ── OP-FP (0x53) / FMA (0x43,0x47,0x4B,0x4F) ─
         *
         * 浮点运算在 EX 阶段完成（调用 exec_fp_op / exec_fma）。
         * 结果直接写入 fregs[rd]，无需 WB 写回。
         * 注意：FP 转发暂未实现，存在 FP-FP RAW 冒险时结果不正确。
         */
        case 0x53:
        case 0x43:
        case 0x47:
        case 0x4B:
        case 0x4F:
            {
                uint32_t dummy_next_pc = pc + 4;
                if (d->opcode == 0x53) {
                    exec_fp_op(sim, d, &dummy_next_pc);
                } else {
                    exec_fma(sim, d, &dummy_next_pc);
                }
                /* FP 结果已写 fregs，不经过 MEM/WB 写回 */
                next_ex_mem.is_fp = true;
            }
            break;

        /* ── 非法 opcode ──────────────────────────────── */
        default:
            sim->cpu.pc = pc;
            cpu_trap(sim, EXC_ILLEGAL_INST, d->opcode);
            flush = true;
            break;
        }

        /* ── 公共字段 ────────────────────────────────── */
        next_ex_mem.pc      = pc;
        next_ex_mem.instr   = p->id_ex.instr; /* 原始指令字（从 ID/EX 透传） */
        next_ex_mem.d       = *d;             /* 解码结果透传 */
        next_ex_mem.rs1_val = p->id_ex.rs1_val; /* 转发前 rs1 原始值 */
        next_ex_mem.rd      = d->rd;
        next_ex_mem.opcode  = d->opcode;
        next_ex_mem.funct3  = d->funct3;
        next_ex_mem.valid   = true;
    }

    /* ════════════════════════════════════════════════════════
     * ⑥ ID 阶段 — 译码 + 读寄存器
     *
     * 注意：此阶段在 WB 之后执行，所以同周期 WB 写入的值
     * 能被 ID 读到（无需转发）。
     * ════════════════════════════════════════════════════════
     */
    if (p->if_id.valid && !stall && !flush) {
        DecodedInstr d = cpu_decode(p->if_id.instr);

        next_id_ex.pc      = p->if_id.pc;
        next_id_ex.instr   = p->if_id.instr; /* 保存原始指令字供后续阶段使用 */
        next_id_ex.d       = d;
        next_id_ex.rs1_val = cpu->regs[d.rs1];
        next_id_ex.rs2_val = cpu->regs[d.rs2];
        next_id_ex.valid   = true;
    }

    /* ════════════════════════════════════════════════════════
     * ⑦ IF 阶段 — 取指
     *
     * 停顿或 CPU 停止时跳过取指。
     * 取指成功 → pc += 4（顺序执行默认）。
     * flush 时不递增 pc（pc 已在 EX 阶段被设为跳转目标）。
     * ════════════════════════════════════════════════════════
     */
    if (cpu->running && !stall) {
        uint32_t     fetch_pc = cpu->pc;
        ExceptionType exc = EXC_NONE;
        uint32_t     instr;

        if (mmu_read_32(&sim->mmu, &sim->pmem, fetch_pc, &instr,
                         cpu->priv, &exc)) {
            next_if_id.pc    = fetch_pc;
            next_if_id.instr = instr;
            next_if_id.valid = true;

            /* flush 时不递增 PC（PC 已由 EX 阶段设为跳转目标） */
            if (!flush) {
                cpu->pc = fetch_pc + 4;
            }
        } else {
            /* 取指失败 → 触发异常 */
            sim->cpu.pc = fetch_pc;
            cpu_trap(sim, (uint32_t)exc, fetch_pc);
            /* 不设置 next_if_id.valid（保持 invalid） */
        }
    }

    /* ════════════════════════════════════════════════════════
     * ⑧ 应用 stall / flush 覆盖
     *
     *   Stall:  IF/ID 保持不变，ID/EX 插入气泡
     *   Flush:  IF/ID 和 ID/EX 无效化（EX/MEM 保留，当前指令继续）
     * ════════════════════════════════════════════════════════
     */
    if (stall) {
        next_if_id     = p->if_id;    /* 保持 IF/ID 不变 */
        next_id_ex.valid = false;     /* ID/EX → 气泡（NOP） */
        p->stall_cycles++;
    }

    if (flush) {
        /* 冲刷 IF/ID 和 ID/EX 中错误取入的指令 */
        next_if_id.valid = false;
        next_id_ex.valid = false;
        p->flush_cycles++;
    }

    /* ════════════════════════════════════════════════════════
     * ⑨ 推进流水线寄存器
     *
     *   每个阶段输出 → 下一阶段的输入寄存器
     *   IF/ID ← IF 输出, ID/EX ← ID 输出, 依此类推
     * ════════════════════════════════════════════════════════
     */
    p->if_id  = next_if_id;
    p->id_ex  = next_id_ex;
    p->ex_mem = next_ex_mem;
    p->mem_wb = next_mem_wb;

    sim->cycle_count++;

    /* ════════════════════════════════════════════════════════
     * ⑩ 更新 DatapathState（Web 调试器可视化）
     *
     * 流水线模式每周期都更新 dp 的基本字段（PC / 指令 / 反汇编），
     * 让 Web 界面在流水线填满前就能看到当前取指进度。
     *
     * 当 WB 完成指令时 → 整份数据通路快照覆盖 dp（含寄存器写回等）。
     * 同时更新流水线特有的 stall/flush/转发/阶段 valid 标志。
     * ════════════════════════════════════════════════════════
     */
    DatapathState *dp = &sim->dp;

    /* ── 每周期基础信息（IF 阶段快照） ────────────────── */
    if (p->if_id.valid) {
        dp->pc     = p->if_id.pc;
        dp->instr  = p->if_id.instr;
        cpu_disasm(p->if_id.instr, p->if_id.pc, dp->disasm, sizeof(dp->disasm));
        snprintf(dp->fsm_state, sizeof(dp->fsm_state), "IF");
    } else if (p->id_ex.valid) {
        dp->pc     = p->id_ex.pc;
        dp->instr  = p->id_ex.instr;
        cpu_disasm(p->id_ex.instr, p->id_ex.pc, dp->disasm, sizeof(dp->disasm));
        snprintf(dp->fsm_state, sizeof(dp->fsm_state), "ID");
    } else {
        /* 流水线全空：显示当前取指 PC（即使还没 fetch） */
        dp->pc     = cpu->pc;
        dp->instr  = 0;
        dp->disasm[0] = '\0';
        snprintf(dp->fsm_state, sizeof(dp->fsm_state), "IF");
    }

    /* ── WB 完成 → 全量覆盖 ──────────────────────────── */
    if (p->last_wb_dp.valid) {
        memcpy(dp, &p->last_wb_dp, sizeof(DatapathState));
        /* 清除一次性快照 */
        p->last_wb_dp.valid = false;
    }

    /* ── 每周期流水线标志 ────────────────────────────── */
    dp->stall     = stall;
    dp->flush     = flush;
    dp->fwd_a     = fwd_a;
    dp->fwd_b     = fwd_b;
    dp->pipe_valid_mask = (p->if_id.valid  ? 1 : 0)
                        | (p->id_ex.valid  ? 2 : 0)
                        | (p->ex_mem.valid ? 4 : 0)
                        | (p->mem_wb.valid ? 8 : 0);
}
