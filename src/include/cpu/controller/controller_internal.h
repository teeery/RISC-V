/* ============================================================
 * controller_internal.h — 三种 CPU 控制器的内部接口
 *
 * 这些函数由 simulator.c 的 sim_step() 按 cpu_model 分发调用。
 * 每个控制器实现一种 CPU 时序模型：
 *
 *   sim_step_single()   — 单周期：IF→ID→EX→MEM→WB（1 周期完成）
 *   sim_step_multi()    — 多周期：5 状态 FSM，每周期推进一个状态
 *   sim_step_pipeline() — 流水线：5 级重叠 + forwarding + stall + flush
 *
 * 三种控制器共享同一份数据通路（alu.c + decode.c + execute/）。
 * ============================================================
 */
#ifndef CONTROLLER_INTERNAL_H
#define CONTROLLER_INTERNAL_H

#include "simulator.h"

/* ── 单周期控制器 ────────────────────────────────────────────
 * 1 次调用 = 1 条完整指令（IF → ID → EX → MEM → WB）
 * 内部直接调用 cpu_decode() + cpu_execute()，和当前 sim_step 行为完全一致。
 */
void sim_step_single(Simulator *sim);

/* ── 多周期控制器（待实现）──────────────────────────────────
 * 每周期调用 1 次，内部维护一个 FSM（5 状态：IF/ID/EX/MEM/WB）。
 * Stub 实现直接委托给 sim_step_single。
 */
void sim_step_multi(Simulator *sim);

/* ── 流水线控制器（待实现）──────────────────────────────────
 * 每周期推进一级流水线，5 个阶段重叠执行。
 * Stub 实现直接委托给 sim_step_single。
 */
void sim_step_pipeline(Simulator *sim);

#endif /* CONTROLLER_INTERNAL_H */
