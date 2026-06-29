/* ============================================================================
 * cpu.c — CPU 状态初始化
 * ============================================================================
 *
 * 实现 cpu_init() 和 cpu_reset()：
 *
 *   cpu_init(CPU *cpu):
 *       memset(cpu->regs, 0, sizeof(cpu->regs));
 *       cpu->pc = 0;
 *       cpu->running = false;
 *
 *   cpu_reset(CPU *cpu):
 *       和 init 一样，重置所有状态
 */

#include "cpu.h"

/* ---- 在这里实现 ---- */
