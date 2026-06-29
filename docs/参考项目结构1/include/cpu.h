#ifndef CPU_H
#define CPU_H

#include "types.h"

/* ============================================================
 * cpu.h — CPU 模拟器核心接口
 *
 * 需要编写的内容：
 * 1. cpu_init()      — 初始化寄存器 (x0=0, PC=入口地址, 特权级=M)
 * 2. cpu_step()      — 单步执行一条指令 (取值→译码→执行→写回)
 * 3. cpu_run()       — 循环执行直到遇到断点/ecall/异常
 * 4. cpu_reset()     — 重置 CPU 状态
 * 5. cpu_dump_regs() — 打印寄存器状态 (调试用)
 *
 * 设计要点：
 * - 每个 cpu_step() 调用完成一次完整的 取值→译码→执行→写回 循环
 * - PC 更新逻辑：默认 PC+4，跳转指令修改 next_pc
 * - 异常处理：设置 mcause/mepc/mtval，跳转到 mtvec
 * - x0 寄存器写保护：任何对 x0 的写入必须被忽略
 * - M 扩展乘除法使用同一 OP 操作码，funct7=0x01 区分
 * ============================================================
 */

/* 初始化 CPU，设置入口地址和栈指针 */
void cpu_init(CPUState *cpu, uint32_t entry_addr, uint32_t stack_addr);

/* 单步执行一条指令，返回 true 表示正常，false 表示触发异常/ecall */
bool cpu_step(CPUState *cpu, void *mem, void *mmu);

/* 循环运行直到停止 (用于 continue 命令) */
void cpu_run(CPUState *cpu, void *mem, void *mmu);

/* 重置 CPU */
void cpu_reset(CPUState *cpu);

/* 打印所有寄存器值 */
void cpu_dump_regs(const CPUState *cpu);

#endif // CPU_H
