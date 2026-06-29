#ifndef DEBUGGER_H
#define DEBUGGER_H

#include "types.h"
#include "memory.h"
#include "mmu.h"

/* ============================================================
 * debugger.h — 交互式调试器
 *
 * 需要编写的内容：
 * 1. debugger_run()       — 启动调试 REPL 主循环
 * 2. cmd_break()          — break <addr>：设置断点
 * 3. cmd_delete()         — delete <num>：删除断点
 * 4. cmd_step()           — step / si：单步执行一条指令
 * 5. cmd_continue()       — continue / c：继续运行直到断点或退出
 * 6. cmd_info_registers() — info registers / info r：打印所有寄存器
 * 7. cmd_examine()        — x /<fmt> <addr>：查看内存 (x/8xw addr)
 * 8. cmd_backtrace()      — bt：打印调用栈 (需要嵌入帧指针链解析)
 * 9. cmd_quit()           — quit / q：退出模拟器
 * 10. 命令解析器           — 解析用户输入，分发到对应函数
 *
 * 断点机制：
 * - 维护 breakpoint_list (addr → 原始指令)，地址-指令映射
 * - 断点命中时暂停，打印 PC 和附近反汇编
 * - 继续执行时恢复原始指令，单步越过断点后重新插入
 * - 实现方式：断点处临时替换为 EBREAK 指令 (0x00100073)
 *
 * 设计要点：
 * - 使用 readline 风格的行编辑 (或简单 fgets)
 * - 命令支持缩写 (c=continue, s=step, r=registers, q=quit)
 * - 地址支持十进制、十六进制 (0x 前缀)
 * - x 命令支持格式: x/NFU addr
 *   N=数量, F=格式(x/d/u/o/t), U=大小(b/h/w)
 * ============================================================
 */

#define MAX_BREAKPOINTS 256

/* 断点信息 */
typedef struct {
    int      id;              // 断点编号
    uint32_t addr;            // 地址
    uint32_t original_insn;   // 原始指令 (用于恢复)
    bool     enabled;
} Breakpoint;

/* 调试器状态 */
typedef struct {
    Breakpoint breakpoints[MAX_BREAKPOINTS];
    int        bp_count;
    int        next_bp_id;
    bool       step_mode;        // 单步模式
    bool       running;          // 调试器是否在运行
    CPUState  *cpu;
    PhysicalMemory *pmem;
    MMUState  *mmu;
} DebuggerState;

/* 初始化调试器 */
void debugger_init(DebuggerState *dbg, CPUState *cpu,
                   PhysicalMemory *pmem, MMUState *mmu);

/* 启动调试 REPL */
void debugger_run(DebuggerState *dbg);

/* 断点管理 */
int  debugger_add_breakpoint(DebuggerState *dbg, uint32_t addr);
bool debugger_del_breakpoint(DebuggerState *dbg, int id);
void debugger_list_breakpoints(const DebuggerState *dbg);

/* 检查当前 PC 是否命中断点 */
bool debugger_check_breakpoint(DebuggerState *dbg, uint32_t pc);

/* 单步执行 (汇编级) */
void debugger_step(DebuggerState *dbg);

/* 继续执行直到断点 */
void debugger_continue(DebuggerState *dbg);

/* 打印寄存器 */
void debugger_print_registers(const CPUState *cpu);

/* 查看内存 */
void debugger_examine_memory(PhysicalMemory *pmem, MMUState *mmu,
                             uint32_t addr, int count,
                             char format, char unit);

#endif // DEBUGGER_H
