/* ============================================================================
 * breakpoint.h / breakpoint.c — 软件断点管理
 * ============================================================================
 *
 * 软件断点原理（复习）：
 *   把目标地址处的原始指令替换为 ebreak (0x00100073)。
 *   CPU 执行到该地址时触发异常，模拟器暂停，进入调试器。
 *
 * ---- 数据结构 ----
 *
 *   typedef struct {
 *       uint32_t addr;         // 断点地址
 *       uint32_t orig_Instr;    // 被替换前的原始指令
 *       bool     active;       // 是否活跃
 *   } Breakpoint;
 *
 * ---- 需要实现的函数 ----
 *
 *   // 在指定地址设置断点，返回断点编号（-1 表示失败）
 *   int bp_set(Simulator *sim, uint32_t addr);
 *
 *   // 删除指定编号的断点
 *   void bp_remove(Simulator *sim, int id);
 *
 *   // 删除指定地址的断点
 *   void bp_remove_at(Simulator *sim, uint32_t addr);
 *
 *   // 检查当前 PC 是否命中某个断点
 *   Breakpoint *bp_find(Simulator *sim, uint32_t addr);
 *
 *   // 列出所有断点
 *   void bp_list(Simulator *sim);
 *
 * ---- 断点设置流程 ----
 *
 *   int bp_set(Simulator *sim, uint32_t addr) {
 *       // 1. 检查地址是否在已分配的内存区域内
 *       // 2. 检查是否已有断点在该地址（去重）
 *       // 3. 扩容断点数组（如果需要）
 *       // 4. 保存原始指令：
 *       //    mmu_read32(&sim->mmu, addr, &bp.orig_Instr);
 *       // 5. 写入 ebreak：
 *       //    mmu_write32(&sim->mmu, addr, EBREAK_Instr);
 *       //    注意：需要检查写入权限，代码段通常是 R+X，可能需要临时改权限
 *       // 6. 设置 bp.addr = addr, bp.active = true
 *       // 7. 返回断点数组索引
 *   }
 *
 * ---- 断点命中处理（在 sim_step 中）----
 *
 *   if (Instr == EBREAK_Instr) {
 *       Breakpoint *bp = bp_find(sim, sim->cpu.pc);
 *       if (bp) {
 *           // 这是一个软件断点！
 *           // 1. 恢复原始指令
 *           mmu_write32(&sim->mmu, bp->addr, bp->orig_Instr);
 *           // 2. PC 退回到断点地址（因为 ebreak 指令已经消耗了 PC+4）
 *           //    但实际上 PC 还没更新，所以不需要动
 *           // 3. 暂停执行
 *           sim->cpu.running = false;
 *           printf("Hit breakpoint at 0x%08x\n", bp->addr);
 *           // 4. 进入调试器
 *           debugger_repl(sim);
 *       } else {
 *           // 程序自身有意使用了 ebreak（罕见但可能）
 *           // 当作正常异常处理
 *       }
 *   }
 *
 * ---- 继续执行的流程（continue 命令）----
 *
 *   1. 执行断点处的原始指令（使用单步）
 *   2. 重新把 ebreak 写回去（以便下次再次命中）
 *   3. 恢复正常执行
 */

#ifndef BREAKPOINT_H
#define BREAKPOINT_H

#include "common.h"

/* ---- 在这里定义 Breakpoint 结构体和断点管理函数 ---- */

#endif /* BREAKPOINT_H */
