/* ============================================================================
 * execute.c — 指令执行实现
 * ============================================================================
 *
 * 这是整个模拟器最大的 switch-case 语句。
 *
 * 实现结构：
 *
 *   bool cpu_execute(Simulator *sim, DecodedInsn *d, uint32_t *next_pc) {
 *       uint32_t *r = sim->cpu.regs;  // 快捷访问
 *
 *       switch (d->opcode) {
 *
 *       case 0x33:  // OP (R-type)
 *           switch (d->funct3) {
 *           case 0x0:
 *               if      (d->funct7 == 0x00) r[d->rd] = r[d->rs1] + r[d->rs2];  // ADD
 *               else if (d->funct7 == 0x20) r[d->rd] = r[d->rs1] - r[d->rs2];  // SUB
 *               else if (d->funct7 == 0x01) r[d->rd] = r[d->rs1] * r[d->rs2];  // MUL (M扩展)
 *               break;
 *           case 0x1: ...  // SLL / MULH
 *           case 0x2: ...  // SLT
 *           case 0x3: ...  // SLTU
 *           case 0x4: ...  // XOR / DIV
 *           case 0x5: ...  // SRL / SRA / DIVU
 *           case 0x6: ...  // OR / REM
 *           case 0x7: ...  // AND / REMU
 *           }
 *           break;
 *
 *       case 0x13:  // OP-IMM (I-type)
 *           switch (d->funct3) {
 *           case 0x0: r[d->rd] = r[d->rs1] + d->imm; break;  // ADDI
 *           case 0x1: r[d->rd] = r[d->rs1] << (d->imm & 0x1F); break;  // SLLI
 *           case 0x2: r[d->rd] = ((int32_t)r[d->rs1] < d->imm) ? 1 : 0; break;  // SLTI
 *           case 0x3: ...  // SLTIU
 *           case 0x4: ...  // XORI
 *           case 0x5: ...  // SRLI / SRAI（通过 funct7 区分）
 *           case 0x6: ...  // ORI
 *           case 0x7: ...  // ANDI
 *           }
 *           break;
 *
 *       case 0x03:  // LOAD
 *           // 计算地址: uint32_t addr = r[d->rs1] + d->imm;
 *           // 根据 funct3 调用 mmu_read8/mmu_read16/mmu_read32
 *           // LB/LH 需要符号扩展
 *           break;
 *
 *       case 0x23:  // STORE
 *           // 计算地址: uint32_t addr = r[d->rs1] + d->imm;  (imm 用 IMM_S 格式)
 *           // 根据 funct3 调用 mmu_write8/mmu_write16/mmu_write32
 *           break;
 *
 *       case 0x63:  // BRANCH
 *           // 根据 funct3 判断条件（BEQ/BNE/BLT/BGE/BLTU/BGEU）
 *           // 如果条件满足: *next_pc = sim->cpu.pc + d->imm;  (imm 用 IMM_B 格式)
 *           break;
 *
 *       case 0x6F:  // JAL
 *           // r[d->rd] = sim->cpu.pc + 4;  (返回地址)
 *           // *next_pc = sim->cpu.pc + d->imm;  (imm 用 IMM_J 格式)
 *           break;
 *
 *       case 0x67:  // JALR
 *           // uint32_t target = (r[d->rs1] + d->imm) & ~1;  (最低位清零)
 *           // r[d->rd] = sim->cpu.pc + 4;
 *           // *next_pc = target;
 *           break;
 *
 *       case 0x37:  // LUI
 *           r[d->rd] = d->imm & 0xFFFFF000;
 *           break;
 *
 *       case 0x17:  // AUIPC
 *           r[d->rd] = sim->cpu.pc + (d->imm & 0xFFFFF000);
 *           break;
 *
 *       case 0x73:  // SYSTEM
 *           // ECALL: 调用 syscall_handler(sim)
 *           // EBREAK: 检查是否命中软件断点
 *           // CSR 指令: 根据 funct3 区分 CSRRW/CSRRS/CSRRC/CSRRWI/CSRRSI/CSRRCI
 *           break;
 *
 *       case 0x0F:  // FENCE
 *           // 模拟器不需要内存屏障，直接 nop
 *           break;
 *
 *       default:
 *           fprintf(stderr, "Unknown opcode 0x%02x at PC=0x%08x\n",
 *                   d->opcode, sim->cpu.pc);
 *           return false;
 *       }
 *
 *       // 关键！x0 永远是 0
 *       r[0] = 0;
 *       return true;
 *   }
 *
 * RV32I 所有指令的 funct3/funct7 映射表（实现时对照）：
 *
 *   OP (0x33):
 *     0x00: ADD(f7=00) SUB(f7=20) MUL(f7=01)  ← MUL 是 M 扩展
 *     0x01: SLL(f7=00) MULH(f7=01)
 *     0x02: SLT(f7=00)
 *     0x03: SLTU(f7=00)
 *     0x04: XOR(f7=00) DIV(f7=01)
 *     0x05: SRL(f7=00) SRA(f7=20) DIVU(f7=01)
 *     0x06: OR(f7=00)  REM(f7=01)
 *     0x07: AND(f7=00) REMU(f7=01)
 *
 *   OP-IMM (0x13):
 *     0x00: ADDI    0x01: SLLI
 *     0x02: SLTI    0x03: SLTIU
 *     0x04: XORI    0x05: SRLI(f7=00) SRAI(f7=20)
 *     0x06: ORI     0x07: ANDI
 *
 *   LOAD (0x03):
 *     0x00: LB     0x01: LH     0x02: LW
 *     0x04: LBU    0x05: LHU
 *
 *   STORE (0x23):
 *     0x00: SB     0x01: SH     0x02: SW
 *
 *   BRANCH (0x63):
 *     0x00: BEQ    0x01: BNE
 *     0x04: BLT    0x05: BGE
 *     0x06: BLTU   0x07: BGEU
 *
 *   SYSTEM (0x73):
 *     0x00: ECALL(f7=00) EBREAK(f7=01) / CSR(f7≠00,01)
 *     0x01: CSRRW   0x02: CSRRS   0x03: CSRRC
 *     0x05: CSRRWI  0x06: CSRRSI  0x07: CSRRCI
 */

#include "execute.h"
// #include "../simulator.h"
// #include "../memory/memory.h"
// #include "../syscall/syscall.h"
// #include "../debugger/breakpoint.h"

/* ---- 在这里实现 cpu_execute ---- */
