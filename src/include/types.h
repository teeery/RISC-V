#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * types.h — 公共基础类型、枚举、常量
 *
 * 这是全项目最底层的公共基础，零依赖（只依赖 C 标准库）。
 * 所有模块的头文件都 include 它。
 *
 * 放什么：
 *   - ≥3 个模块都需要的简单类型 / 枚举 / 常量
 *   - 跨模块的桥接函数（如 mem_perm_to_pte_flags）
 *
 * 不放什么：
 *   - 复杂结构体（即使被多人用）——结构体跟它的函数 API 是一体的，
 *     放在模块自己的头文件里（如 CPU 在 cpu.h、PhysicalMemory 在 memory.h）
 * ============================================================
 */

/* ============================================================
 * 1. 寄存器 ABI 名称
 *
 * 使用者：CPU（regs 数组下标）、Debugger（寄存器显示）、Loader（设 sp）
 * ============================================================
 */
typedef enum {
    REG_ZERO = 0,   // x0  - 恒为 0
    REG_RA   = 1,   // x1  - 返回地址
    REG_SP   = 2,   // x2  - 栈指针
    REG_GP   = 3,   // x3  - 全局指针
    REG_TP   = 4,   // x4  - 线程指针
    REG_T0   = 5,   // x5  - 临时寄存器
    REG_T1   = 6,   // x6
    REG_T2   = 7,   // x7
    REG_S0   = 8,   // x8  - 帧指针 (fp)
    REG_S1   = 9,   // x9
    REG_A0   = 10,  // x10 - 函数参数 / 返回值
    REG_A1   = 11,  // x11
    REG_A2   = 12,  // x12
    REG_A3   = 13,  // x13
    REG_A4   = 14,  // x14
    REG_A5   = 15,  // x15
    REG_A6   = 16,  // x16
    REG_A7   = 17,  // x17 - syscall 编号
    REG_S2   = 18,  // x18
    REG_S3   = 19,  // x19
    REG_S4   = 20,  // x20
    REG_S5   = 21,  // x21
    REG_S6   = 22,  // x22
    REG_S7   = 23,  // x23
    REG_S8   = 24,  // x24
    REG_S9   = 25,  // x25
    REG_S10  = 26,  // x26
    REG_S11  = 27,  // x27
    REG_T3   = 28,  // x28
    REG_T4   = 29,  // x29
    REG_T5   = 30,  // x30
    REG_T6   = 31,  // x31
    REG_COUNT = 32,
} RegisterID;

/* ============================================================
 * 2. 特权级
 *
 * 使用者：CPU（cpu.priv）、MMU（地址翻译时检查 U 位）、
 *         Debugger（显示当前特权级）、Loader（不直接使用，但通过接口传递）
 * ============================================================
 */
typedef enum {
    PRIV_USER       = 0,
    PRIV_SUPERVISOR = 1,
    PRIV_MACHINE    = 3,
} PrivilegeLevel;

/* ============================================================
 * 3. 异常类型（RISC-V 特权规范 §3.1.15 mcause）
 *
 * 使用者：MMU（翻译失败时填充 *exc）、CPU（cpu_trap 填写 mcause）、
 *         Debugger（显示异常信息）
 *
 * 值对齐 RISC-V 规范：异常码从 0 开始。
 * EXC_NONE = 0 表示无异常（mcause 寄存器的复位值）。
 * ============================================================
 */
typedef enum {
    EXC_NONE                    = 0,
    EXC_INST_ADDR_MISALIGNED    = 1,   // 指令地址未对齐
    EXC_INST_ACCESS_FAULT       = 2,   // 取指访问错误（页错误 / 越权）
    EXC_ILLEGAL_INST            = 3,   // 非法指令
    EXC_BREAKPOINT              = 4,   // 断点 (ebreak)
    EXC_LOAD_ADDR_MISALIGNED    = 5,   // Load 地址未对齐
    EXC_LOAD_ACCESS_FAULT       = 6,   // Load 访问错误
    EXC_STORE_ADDR_MISALIGNED   = 7,   // Store 地址未对齐
    EXC_STORE_ACCESS_FAULT      = 8,   // Store 访问错误
    EXC_ECALL_U                 = 9,   // ECALL from U-mode
    EXC_ECALL_S                 = 10,  // ECALL from S-mode（保留）
    EXC_ECALL_M                 = 11,  // ECALL from M-mode
    EXC_PAGE_FAULT_INST         = 12,  // 取指页错误（Sv32 模式）
    EXC_PAGE_FAULT_LOAD         = 13,  // Load 页错误（Sv32 模式）
    EXC_PAGE_FAULT_STORE        = 15,  // Store 页错误（Sv32 模式）
} ExceptionType;

/* ============================================================
 * 4. 内存权限标志（PhysicalMemory 层使用）
 *
 * 使用者：Memory（mem_map flags）、Loader（段加载时设权限）、
 *         MMU（mmu_map_page 权限转换的输入）
 *
 * 注意：这些值和 ELF 的 PF_*（PF_R=4, PF_W=2, PF_X=1）不同！
 *       Loader 在 elf_load_segment 中做 PF_* → MEM_* 转换。
 * ============================================================
 */
#define MEM_READ   (1 << 0)
#define MEM_WRITE  (1 << 1)
#define MEM_EXEC   (1 << 2)

/* ============================================================
 * 5. PTE 权限标志（MMU 页表层使用，与 RISC-V 规范一致）
 *
 * 使用者：MMU（页表遍历 + 权限校验）、Loader（mmu_map_page 传 flags）
 *
 * PTE 格式（32 位）：
 *   31        20 19        10 9 8 7 6 5 4 3 2 1 0
 *  ┌────────────┬────────────┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┐
 *  │ PPN[1]     │ PPN[0]     │-│D│A│G│U│X│W│R│V│
 *  └────────────┴────────────┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┘
 *
 * 注意：这些值和 MEM_* 的 bit 位置不同！使用 mem_perm_to_pte_flags() 做转换。
 * ============================================================
 */
#define PTE_VALID    (1 << 0)
#define PTE_READ     (1 << 1)
#define PTE_WRITE    (1 << 2)
#define PTE_EXEC     (1 << 3)
#define PTE_USER     (1 << 4)
#define PTE_GLOBAL   (1 << 5)
#define PTE_ACCESSED (1 << 6)
#define PTE_DIRTY    (1 << 7)

/* ============================================================
 * 6. Sv32 页表常量
 *
 * 使用者：MMU（页表遍历）、Loader（按页映射段数据）
 * ============================================================
 */
#define PAGE_SIZE       4096
#define PAGE_SHIFT      12
#define SATP_MODE_OFF   0    // Bare 模式：vaddr = paddr，不做翻译
#define SATP_MODE_SV32  1    // Sv32 模式：两级页表翻译

/* ============================================================
 * 7. MEM_* → PTE_* 权限转换
 *
 * 使用者：Loader（elf_load_segment: PF_* → MEM_* → PTE_*）、
 *         MMU（mmu_map_page 内部也可用）
 *
 * 为什么放在 types.h：需要同时看到 MEM_* 和 PTE_* 的值，
 * types.h 是唯一被两者都 include 的位置。
 * ============================================================
 */
static inline uint8_t mem_perm_to_pte_flags(uint8_t mem_flags) {
    uint8_t pte = PTE_VALID;  // 所有映射默认有效
    if (mem_flags & MEM_READ)  pte |= PTE_READ;
    if (mem_flags & MEM_WRITE) pte |= PTE_WRITE;
    if (mem_flags & MEM_EXEC)  pte |= PTE_EXEC;
    return pte;
}

#endif // TYPES_H
