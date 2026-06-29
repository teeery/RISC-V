/* ============================================================================
 * simulator.c — 模拟器顶层控制
 * ============================================================================
 *
 * 这个文件实现模拟器的"主循环"和顶层控制逻辑。
 * 它是整个模拟器的粘合剂，把所有子模块串起来。
 *
 * 需要实现的函数：
 *
 *   sim_init():
 *       1. 初始化 CPU 状态（所有寄存器 = 0，PC = 0）
 *       2. 初始化 MMU（region 数组为空）
 *       3. 初始化断点数组（空）
 *       4. 初始化统计计数器（cycle = 0, inst = 0）
 *       5. 初始化标志（running = false, single_step = false）
 *
 *   sim_load_elf(filename):
 *       1. 调用 elf_load() 解析 ELF 文件，填充 mmu 的 region 数组
 *       2. 设置 cpu.pc = elf 入口地址
 *       3. 设置 cpu.regs[2] (sp) = 栈顶地址（如 0xC0000000）
 *       4. 如果需要，初始化 argc/argv 到栈上
 *
 *   sim_step():
 *       1. 检查当前 PC 是否命中任何断点 → 如果是，暂停并进入调试器
 *       2. FETCH: 从 mmu 读取 PC 处的 4 字节指令（注意处理段错误）
 *       3. DECODE: 调用 cpu_decode() 解析 opcode/rd/rs1/rs2/imm/funct3/funct7
 *       4. EXECUTE: 调用 cpu_execute() 执行指令，可能会修改 next_pc
 *       5. 更新 PC = next_pc（默认 next_pc = pc + 4，跳转指令会修改）
 *       6. 更新统计：inst_count++, cycle_count++
 *       7. 回归特殊情况：regs[0] = 0（x0 硬连线）
 *       8. 如果是单步模式，设置 running = false
 *
 *   sim_run(filename):
 *       1. sim_init()
 *       2. sim_load_elf(filename)
 *       3. if (debug_mode) → 进入调试器 REPL
 *       4. else → while(running) { sim_step(); }
 *       5. 输出统计信息（指令数、周期数、CPI）
 *
 * 数据流：
 *   main.c → sim_run("hello") → sim_load_elf → elf_load
 *                            → while(running) sim_step
 *                                → fetch (读 mmu)
 *                                → decode (解析指令)
 *                                → execute (执行指令，可能触发 syscall)
 *                                → 检查断点/单步 → 进入 debugger
 */

#include "simulator.h"

/* ---- 在这里实现 sim_init, sim_load_elf, sim_step, sim_run ---- */
