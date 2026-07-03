/* ============================================================
 * exec_internal.h — execute/ 模块内部共享头文件
 *
 * 声明所有指令组的执行函数，供 execute.c（调度入口）调用。
 *
 * 每个函数：
 *   - 返回 true  = 执行成功，*next_pc 已设置
 *   - 返回 false = 调用方应触发非法指令异常
 *
 * 扩展开发规则：
 *   新增指令扩展 = 新增一个 exec_xxx.c + 在此声明新函数
 *   然后在 execute.c 的 cpu_execute() switch 中加对应的 case
 *
 * 约定（两人并行前必读）：
 *   1. 不要新增 opcode case — 所有新 opcode 在 execute.c 中统一分发
 *   2. exec_op()（0x33）内部按 funct7 分流：
 *      funct7=0x00 → RV32I ALU（ADD/SUB/SLL/...）
 *      funct7=0x01 → exec_m_muldiv()（M 扩展）
 *      funct7=0x20 → SUB（RV32I）
 *   3. 浮点需要 fregs[32]，在 cpu.h 中提前约定好
 * ============================================================
 */
#ifndef EXEC_INTERNAL_H
#define EXEC_INTERNAL_H

#include "types.h"
#include "cpu/decode.h"

/* 前置声明 */
struct Simulator;
typedef struct Simulator Simulator;

/* ================================================================
 * RV32I 基础指令（exec_rv32i.c）
 * ============================================================== */

bool exec_lui    (Simulator *sim, DecodedInstr *dec, uint32_t *next_pc);
bool exec_auipc  (Simulator *sim, DecodedInstr *dec, uint32_t *next_pc);
bool exec_op_imm (Simulator *sim, DecodedInstr *dec, uint32_t *next_pc);
bool exec_op     (Simulator *sim, DecodedInstr *dec, uint32_t *next_pc);
bool exec_load   (Simulator *sim, DecodedInstr *dec, uint32_t *next_pc);
bool exec_store  (Simulator *sim, DecodedInstr *dec, uint32_t *next_pc);
bool exec_branch (Simulator *sim, DecodedInstr *dec, uint32_t *next_pc);
bool exec_jal    (Simulator *sim, DecodedInstr *dec, uint32_t *next_pc);
bool exec_jalr   (Simulator *sim, DecodedInstr *dec, uint32_t *next_pc);
bool exec_system (Simulator *sim, DecodedInstr *dec, uint32_t *next_pc);
bool exec_fence  (Simulator *sim, DecodedInstr *dec, uint32_t *next_pc);

/* ================================================================
 * M 扩展 — 乘除法 8 条指令（exec_m.c）
 * ============================================================== */
bool exec_m_muldiv(Simulator *sim, DecodedInstr *dec, uint32_t *next_pc);

/* ================================================================
 * F 扩展 — 单精度浮点 26 条指令（exec_f.c）
 * ============================================================== */
bool exec_load_fp (Simulator *sim, DecodedInstr *dec, uint32_t *next_pc);
bool exec_store_fp(Simulator *sim, DecodedInstr *dec, uint32_t *next_pc);
bool exec_fp_op   (Simulator *sim, DecodedInstr *dec, uint32_t *next_pc);

#endif /* EXEC_INTERNAL_H */
