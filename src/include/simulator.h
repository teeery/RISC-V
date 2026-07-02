#ifndef SIMULATOR_H
#define SIMULATOR_H

#include "types.h"           // PrivilegeLevel, ExceptionType, RegisterID
#include "cpu/cpu.h"         // CPU (regs[32], pc, running, priv, CSR)
#include "memory/memory.h"   // PhysicalMemory, MemoryRegion, mem_*
#include "memory/mmu.h"      // MMUState, mmu_*
#include "cpu/decode.h"      // DecodedInstr, cpu_decode, cpu_disasm

/* ================================================================
 * simulator.h — 模拟器顶层结构体
 *
 * 聚合所有模块状态（CPU / MMU / PhysicalMemory / Breakpoint），
 * 供 main.c、debugger、cpu_execute 等统一使用。
 *
 * 断点数组：放在 Simulator 下（讨论结论 3）
 *   设断点：Debugger 操作 sim->breakpoints[]
 *   命中断点：CPU 执行 ebreak → 查 sim->breakpoints[] → 通知 Debugger
 *
 * 栈地址（讨论结论 7）：
 *   栈顶 0xC0000000，栈大小 256KB（0x40000）
 *   范围 0xBFFC0000 ~ 0xC0000000
 * ================================================================
 */

/* ── 栈地址常量 ──────────────────────────────────────────────────
 *
 * 这些地址是 RISC-V 规范中用户态程序的典型栈布局：
 *   STACK_TOP  = 0xC0000000（3GB 位置，接近真实 Linux 用户空间顶部）
 *   STACK_SIZE = 0x40000   （256KB，足够运行标准测试程序）
 *   STACK_BASE = 0xBFFC0000（STACK_TOP - STACK_SIZE）
 *
 * 注意：这些宏由 Loader 定义并使用（elf_stack.c），Debugger 也引用它们
 */
#define STACK_TOP_DEFAULT   0xC0000000
#define STACK_SIZE_DEFAULT  (256 * 1024)   // 256KB
#define STACK_TOP           STACK_TOP_DEFAULT
#define STACK_BASE          (STACK_TOP_DEFAULT - STACK_SIZE_DEFAULT)

/* ── 断点结构体 ────────────────────────────────────────────────── */
typedef struct {
    uint32_t addr;              // 断点地址
    uint32_t original_instr;     // 被替换前的原始指令（用于恢复）
    bool     enabled;           // 是否启用
} Breakpoint;

/* ── 模拟器顶层结构体 ──────────────────────────────────────────── */
typedef struct Simulator {
    /* 核心模块 */
    CPU             cpu;            // CPU 状态（寄存器、PC、CSR）        → 李特
    MMUState        mmu;            // MMU 页表 / satp                    → 焕聪
    PhysicalMemory  pmem;           // 物理内存                           → 焕聪

    /* 调试相关 */
    Breakpoint     *breakpoints;    // 断点动态数组（Debugger 管理）
    int             bp_count;       // 当前断点数量
    int             bp_capacity;    // 断点数组容量（初始 16，按需 ×2）

    bool            single_step;    // 单步执行标志
    bool            debug_mode;     // 调试模式（Debugger 接管时设为 true）

    /* 统计 */
    uint64_t        inst_count;     // 已执行指令数
    uint64_t        cycle_count;    // 周期计数（基础版 ≈ inst_count）
} Simulator;

/* ================================================================
 * Simulator 级别接口
 * ================================================================
 */

/* 初始化模拟器（分配 pmem、mmu、breakpoints，清零 CPU） */
void sim_init(Simulator *sim);

/* 销毁模拟器（释放 pmem、breakpoints 等） */
void sim_destroy(Simulator *sim);

/* 执行单条指令（取指 → 译码 → 执行 → 写回）
 *
 * 主执行循环体，内部调用：
 *   mmu_read_32(pc) → cpu_decode → cpu_execute →
 *   （如果需要）cpu_trap
 *
 * 每执行一条指令前检查 single_step 和 breakpoints 状态。
 */
void sim_step(Simulator *sim);

/* 加载 ELF 文件并设置 CPU 初始状态
 *
 * 内部调用 elf_load()，然后设置：
 *   sim->cpu.pc          = entry（ELF 入口地址）
 *   sim->cpu.regs[REG_SP] = STACK_TOP（栈顶 0xC0000000）
 *
 * 返回 true 加载成功，false 失败
 */
bool sim_load_elf(Simulator *sim, const char *path);

/* 主执行循环：while (cpu.running) sim_step() */
void sim_run(Simulator *sim);

#endif /* SIMULATOR_H */
