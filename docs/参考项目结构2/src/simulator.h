/* ============================================================================
 * simulator.h — 模拟器顶层结构体定义
 * ============================================================================
 *
 * 这个文件定义 Simulator 结构体，它是整个模拟器的"大总管"。
 * 所有子模块（CPU、内存、调试器）的数据都挂在这个结构体下面。
 *
 * 结构体字段说明：
 *
 *   struct Simulator {
 *       CPU         cpu;          // CPU 状态（寄存器、PC、运行标志）
 *       MMU         mmu;          // 虚拟内存管理（MemRegion 数组）
 *       Breakpoint  *breakpoints; // 断点数组（动态扩容）
 *       int         bp_count;     // 当前断点数量
 *       int         bp_capacity;  // 断点数组容量
 *       bool        single_step;  // true = 执行一条指令后停下
 *       bool        debug_mode;   // true = 启动调试器
 *       uint64_t    cycle_count;  // 总时钟周期数（用于 CPI 统计）
 *       uint64_t    inst_count;   // 已完成的指令数（用于 CPI 统计）
 *   };
 *
 *   // 初始化模拟器
 *   void sim_init(Simulator *sim);
 *
 *   // 运行（主循环）
 *   int sim_run(Simulator *sim, const char *elf_path);
 *
 *   // 加载 ELF 文件到虚拟内存，设置入口地址
 *   int sim_load_elf(Simulator *sim, const char *elf_path);
 *
 *   // 执行一条指令，返回 true 表示正常
 *   bool sim_step(Simulator *sim);
 */

#ifndef SIMULATOR_H
#define SIMULATOR_H

#include "common.h"
// #include "cpu/cpu.h"      // CPU 结构体定义
// #include "memory/memory.h" // MMU 结构体定义

/* ---- 在这里定义 Simulator 结构体和 API ---- */

#endif /* SIMULATOR_H */
