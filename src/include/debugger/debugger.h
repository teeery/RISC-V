#ifndef DEBUGGER_H
#define DEBUGGER_H

#include "types.h"        // PrivilegeLevel, ExceptionType

struct Simulator;          // 前置声明（避免循环依赖）

/* ========== REPL 入口 ========== */

/* 启动调试 REPL 主循环（阻塞，直到用户 quit 或程序正常退出） */
void debugger_run(struct Simulator *sim);

/* ========== 断点管理（操作 sim->breakpoints[]）========== */

/* 在 addr 处设置软件断点，返回下标（也是编号），失败返回 -1 */
int  debugger_add_breakpoint(struct Simulator *sim, uint32_t addr);

/* 删除指定下标的断点，返回 true 成功 */
bool debugger_del_breakpoint(struct Simulator *sim, int index);

/* 列出所有断点（info breakpoints） */
void debugger_list_breakpoints(const struct Simulator *sim);

/* 检查 pc 是否命中某个启用的断点（CPU 在 ebreak 时调用） */
bool debugger_check_breakpoint(struct Simulator *sim, uint32_t pc);

/* ========== 执行控制 ========== */

/* 单步执行一条指令，自动处理断点恢复/重新插入 */
void debugger_step(struct Simulator *sim);

/* 继续执行，直到命中断点或程序退出（内部循环调 sim_step） */
void debugger_continue(struct Simulator *sim);

/* ========== 状态查看 ========== */

/* 打印全部 32 个通用寄存器 + PC + 关键 CSR */
void debugger_print_registers(const struct Simulator *sim);

/* 查看虚拟地址内存内容（hexdump 风格，通过 mmu_read_*） */
void debugger_examine_memory(struct Simulator *sim, uint32_t vaddr,
                             int count, char format, char unit);

/* 打印调用栈（帧指针链回溯，通过 mmu_read_32 读栈帧） */
void debugger_print_backtrace(struct Simulator *sim);

#endif /* DEBUGGER_H */
