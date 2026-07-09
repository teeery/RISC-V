/* ============================================================
 * single_cycle.c — 单周期 CPU 控制器
 *
 * 时序模型：
 *   1 次 sim_step_single() 调用 = 1 条完整指令
 *   取指 → 译码 → 执行 → 写回，全部在同一个周期内完成
 *
 * 这是三种控制器中最简单的一种，CPI = 1.0。
 * 内部直接复用 cpu_decode() + cpu_execute()，与原始 sim_step
 * 行为完全一致。
 *
 * 数据通路依赖（共享）：
 *   - alu.c          — ALU 组合逻辑（通过 execute/ 间接使用）
 *   - decode.c       — 指令译码
 *   - execute/       — 指令执行（内部调用 alu_compute）
 *   - memory/mmu.c   — 内存访问
 * ============================================================
 */

#include "cpu/controller/controller_internal.h"
#include "cpu/execute.h"
#include "cpu/decode.h"
#include "cpu/datapath/alu.h"
#include "memory/mmu.h"
#include "types.h"
#include <stdio.h>
#include <string.h>

/* ── ALU 操作名查找表 ──────────────────────────────────────────── */
static const char *alu_op_name(AluOp op)
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

/* ════════════════════════════════════════════════════════════
 * sim_step_single — 单周期执行 1 条指令 + 数据通路信号捕获
 *
 * 5 级流程：
 *   ① IF:   mmu_read_32(pc) → 32-bit 指令字
 *   ② ID:   cpu_decode(instr) → DecodedInstr，捕获 rs1_val/rs2_val
 *   ③ EX:   cpu_execute(sim, &d, &next_pc)
 *   ④ WB:   sim->cpu.pc = next_pc
 *   ⑤ 后处理: x0 硬连线、统计计数、填充 DatapathState
 * ════════════════════════════════════════════════════════════
 */
void sim_step_single(Simulator *sim)
{
    uint32_t pc = sim->cpu.pc;

    /* ① 取指 */
    ExceptionType exc = EXC_NONE;
    uint32_t instr;
    if (!mmu_read_32(&sim->mmu, &sim->pmem, pc, &instr,
                     sim->cpu.priv, &exc)) {
        cpu_trap(sim, (uint32_t)exc, pc);
        sim->instr_count++;
        sim->cycle_count++;
        sim->dp.valid = false;
        return;
    }

    /* ② 译码 + 捕获源操作数（执行前快照） */
    DecodedInstr d = cpu_decode(instr);
    uint32_t rs1_val = sim->cpu.regs[d.rs1];
    uint32_t rs2_val = sim->cpu.regs[d.rs2];

    /* ③ 执行 */
    uint32_t next_pc = pc + 4;
    bool ok = cpu_execute(sim, &d, &next_pc);

    /* ④ 写回 */
    sim->cpu.pc = next_pc;

    /* ⑤ x0 硬连线 */
    sim->cpu.regs[0] = 0;

    /* ⑥ 统计 */
    sim->instr_count++;
    sim->cycle_count++;
    (void)ok;

    /* ════════════════════════════════════════════════════════
     * ⑦ 填充数据通路信号（Web 调试器 SVG 可视化）
     * ════════════════════════════════════════════════════════
     */
    DatapathState *dp = &sim->dp;
    memset(dp, 0, sizeof(*dp));
    dp->pc     = pc;
    dp->instr  = instr;
    dp->opcode = d.opcode;
    dp->rd     = d.rd;
    dp->rs1    = d.rs1;
    dp->rs2    = d.rs2;
    dp->funct3 = d.funct3;
    dp->funct7 = d.funct7;
    dp->imm    = d.imm;
    dp->rs1_val = rs1_val;
    dp->rs2_val = rs2_val;
    dp->next_pc = next_pc;
    cpu_disasm(instr, pc, dp->disasm, sizeof(dp->disasm));

    /* 写寄存器？ */
    dp->reg_write = (d.rd != 0);
    if (dp->reg_write) {
        dp->rd_val = sim->cpu.regs[d.rd];  /* 执行后的新值 */
    }

    /* 推导 ALU 输入 / 操作 / 输出 */
    switch (d.opcode) {
    case 0x13: /* OP-IMM */
        {
            AluOp op = alu_select_op(d.funct3, d.funct7, false);
            strncpy(dp->alu_op, alu_op_name(op), sizeof(dp->alu_op) - 1);
            dp->alu_a = rs1_val;
            dp->alu_b = (uint32_t)d.imm;
            dp->alu_result = alu_compute(op, dp->alu_a, dp->alu_b);
        }
        break;
    case 0x33: /* OP */
        if (d.funct7 == 1) {
            /* M 扩展 — 不经过 ALU */
            strncpy(dp->alu_op, "mul", sizeof(dp->alu_op) - 1);
            dp->alu_a = rs1_val;
            dp->alu_b = rs2_val;
            dp->alu_result = sim->cpu.regs[d.rd];
        } else {
            AluOp op = alu_select_op(d.funct3, d.funct7, true);
            strncpy(dp->alu_op, alu_op_name(op), sizeof(dp->alu_op) - 1);
            dp->alu_a = rs1_val;
            dp->alu_b = rs2_val;
            dp->alu_result = alu_compute(op, dp->alu_a, dp->alu_b);
        }
        break;
    case 0x03: /* LOAD */
        strncpy(dp->alu_op, "add", sizeof(dp->alu_op) - 1);
        dp->alu_a = rs1_val;
        dp->alu_b = (uint32_t)d.imm;
        dp->alu_result = rs1_val + (uint32_t)d.imm;
        dp->mem_addr = dp->alu_result;
        dp->mem_read = true;
        dp->mem_rdata = sim->cpu.regs[d.rd];
        break;
    case 0x23: /* STORE */
        strncpy(dp->alu_op, "add", sizeof(dp->alu_op) - 1);
        dp->alu_a = rs1_val;
        dp->alu_b = (uint32_t)d.imm;
        dp->alu_result = rs1_val + (uint32_t)d.imm;
        dp->mem_addr = dp->alu_result;
        dp->mem_write = true;
        dp->mem_wdata = rs2_val;
        break;
    case 0x63: /* BRANCH */
        strncpy(dp->alu_op, "sub", sizeof(dp->alu_op) - 1);
        dp->alu_a = rs1_val;
        dp->alu_b = rs2_val;
        dp->branch_taken = (next_pc != pc + 4);
        break;
    case 0x6F: /* JAL */
        strncpy(dp->alu_op, "add", sizeof(dp->alu_op) - 1);
        dp->alu_a = pc;
        dp->alu_b = 4;
        dp->alu_result = pc + 4;  /* return address */
        dp->reg_write = true;  /* JAL always writes rd */
        dp->rd_val = pc + 4;
        dp->branch_taken = true;
        break;
    case 0x67: /* JALR */
        strncpy(dp->alu_op, "add", sizeof(dp->alu_op) - 1);
        dp->alu_a = rs1_val;
        dp->alu_b = (uint32_t)d.imm;
        dp->alu_result = (rs1_val + (uint32_t)d.imm) & ~1u;
        dp->reg_write = true;
        dp->rd_val = pc + 4;
        dp->branch_taken = true;
        break;
    case 0x17: /* AUIPC */
        strncpy(dp->alu_op, "add", sizeof(dp->alu_op) - 1);
        dp->alu_a = pc;
        dp->alu_b = (uint32_t)d.imm;
        dp->alu_result = pc + (uint32_t)d.imm;
        break;
    case 0x37: /* LUI */
        strncpy(dp->alu_op, "none", sizeof(dp->alu_op) - 1);
        dp->alu_result = (uint32_t)d.imm;
        break;
    default:
        strncpy(dp->alu_op, "none", sizeof(dp->alu_op) - 1);
        break;
    }

    dp->valid = true;
}
