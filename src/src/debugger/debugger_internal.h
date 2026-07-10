#ifndef DEBUGGER_INTERNAL_H
#define DEBUGGER_INTERNAL_H

/* ================================================================
 * debugger_internal.h — debugger 模块内部共享定义
 *
 * 仅在编译测试时（-DDEBUGGER_TEST）暴露 static 函数。
 * EBREAK_INSTR 在此统一定义，避免 breakpoint.c 和 debugger.c 重复。
 * ================================================================
 */

/* RISC-V ebreak 指令编码 */
#define EBREAK_INSTR  0x00100073

#ifdef DEBUGGER_TEST

struct Simulator;

/* 寄存器 ABI 名称 -> 索引 (0-31, 32=pc)，失败返回 -1 */
int parse_reg_name(const char *name);

/* 地址字符串 -> uint32_t：支持 hex(0x...)、dec、$reg、$reg+offset */
bool parse_addr(const char *str, struct Simulator *sim, uint32_t *addr);

/* 读寄存器值：idx 0-31 = regs[idx]，idx 32 = pc，其他返回 0 */
uint32_t get_reg_value(const struct Simulator *sim, int idx);

#endif /* DEBUGGER_TEST */

#endif /* DEBUGGER_INTERNAL_H */
