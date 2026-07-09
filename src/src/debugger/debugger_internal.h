#ifndef DEBUGGER_INTERNAL_H
#define DEBUGGER_INTERNAL_H

/* ================================================================
 * debugger_internal.h — debugger 模块内部函数声明
 *
 * 仅在编译测试时（-DDEBUGGER_TEST）暴露 static 函数。
 * 正常编译时这些函数保持 static，不影响模块封装。
 *
 * 模式参考：src/src/loader/loader_internal.h
 * ================================================================
 */

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
