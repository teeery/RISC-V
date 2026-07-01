#ifndef MMU_H
#define MMU_H

#include "memory.h"
#include "types.h"

/* ============================================================
 * mmu.h — 虚拟内存管理单元接口（MMU 层）
 *
 * 职责：
 *   - Sv32 虚拟地址 → 物理地址翻译（两级页表遍历）
 *   - 虚拟地址读写（先 translate 再调 PhysicalMemory 层）
 *   - 页表映射建立（mmu_map_page）
 *   - PTE 权限校验（V / R / W / X / U）
 *
 * Sv32 虚拟地址结构（RV32）：
 *   31        22 21        12 11         0
 *  ┌────────────┬────────────┬────────────┐
 *  │  VPN[1]    │  VPN[0]    │  offset    │
 *  │  10 bits   │  10 bits   │  12 bits   │
 *  └────────────┴────────────┴────────────┘
 *
 * PTE 格式（32 位）：
 *   31        20 19        10 9 8 7 6 5 4 3 2 1 0
 *  ┌────────────┬────────────┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┐
 *  │ PPN[1]     │ PPN[0]     │-│D│A│G│U│X│W│R│V│
 *  └────────────┴────────────┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┘
 *
 * 设计要点：
 *   - Bare 模式（satp.MODE = 0）：vaddr = paddr，恒等映射
 *   - Sv32 模式（satp.MODE = 1）：两级页表翻译 + 权限检查
 *   - 返回值统一用 bool（true 成功 / false 失败）
 *   - 错误详情通过 ExceptionType *exc 输出参数传递
 *   - 成功时 *exc = EXC_NONE
 *   - CPU 只调 MMU 层，不直接调 mem_* 函数
 *   - 页表独立 malloc，不复用物理内存
 *
 * 调用方：
 *   - CPU：mmu_read_* / mmu_write_*（取指、load/store）
 *   - Loader：mmu_map_page（建立虚拟→物理映射）
 *   - syscall：mmu_read / mmu_write（批量读写）、mmu_brk（堆管理）
 *   - Debugger：mmu_read_32 / mmu_write_32（断点操作）、mem_dump
 * ============================================================
 */

#define PAGE_SIZE        4096
#define PAGE_SHIFT       12
#define VPN0_SHIFT       12
#define VPN1_SHIFT       22

/* PTE 标志位（MMU 层使用，与 RISC-V 规范一致） */
#define PTE_VALID        (1 << 0)
#define PTE_READ         (1 << 1)
#define PTE_WRITE        (1 << 2)
#define PTE_EXEC         (1 << 3)
#define PTE_USER         (1 << 4)
#define PTE_GLOBAL       (1 << 5)
#define PTE_ACCESSED     (1 << 6)
#define PTE_DIRTY        (1 << 7)

#define SATP_MODE_OFF    0   // Bare：无地址翻译
#define SATP_MODE_SV32   1   // Sv32：两级页表

/* MMU 状态 */
typedef struct {
    uint32_t satp;              // satp CSR 值（MODE | ASID | PPN）
    uint32_t *root_page_table;  // 第一级页表（1024 项 × 4B）
    bool     enabled;           // 是否启用地址翻译
} MMUState;

/* ============================================================
 * 接口（12 个）
 * ============================================================
 */

/* 初始化 MMU（satp = 0, 分配根页表） */
void mmu_init(MMUState *mmu);

/* 虚拟地址 → 物理地址（Sv32 页表遍历 + 权限检查）
 *
 * 参数：
 *   vaddr    — 输入：虚拟地址
 *   paddr    — 输出：翻译后的物理地址
 *   is_write — 本次访问是否为写操作（检查 PTE.W）
 *   is_exec  — 本次访问是否为执行（检查 PTE.X）
 *   priv     — 当前特权级（检查 PTE.U）
 *   exc      — 输出：异常类型（成功时为 EXC_NONE）
 *
 * 返回 false 表示页错误，*exc 携带具体异常类型
 */
bool mmu_translate(MMUState *mmu, uint32_t vaddr, uint32_t *paddr,
                   bool is_write, bool is_exec, PrivilegeLevel priv,
                   ExceptionType *exc);

/* 虚拟地址单次读取 — 先 translate 再调 mem_read_*
 * 成功时 *exc = EXC_NONE，失败时 *exc = 对应异常类型
 */
bool mmu_read_8 (MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                 uint8_t *val, PrivilegeLevel priv, ExceptionType *exc);
bool mmu_read_16(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                 uint16_t *val, PrivilegeLevel priv, ExceptionType *exc);
bool mmu_read_32(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                 uint32_t *val, PrivilegeLevel priv, ExceptionType *exc);

/* 虚拟地址单次写入 — 先 translate 再调 mem_write_*
 * 成功时 *exc = EXC_NONE，失败时 *exc = 对应异常类型
 */
bool mmu_write_8 (MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                  uint8_t val, PrivilegeLevel priv, ExceptionType *exc);
bool mmu_write_16(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                  uint16_t val, PrivilegeLevel priv, ExceptionType *exc);
bool mmu_write_32(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                  uint32_t val, PrivilegeLevel priv, ExceptionType *exc);

/* 虚拟地址批量读写（syscall 场景：sys_write / sys_read）
 * 逐字节访问，遇到第一个失败字节即停止，*exc 携带异常类型
 */
bool mmu_read (MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
               uint8_t *buf, uint32_t len, PrivilegeLevel priv,
               ExceptionType *exc);
bool mmu_write(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
               const uint8_t *buf, uint32_t len, PrivilegeLevel priv,
               ExceptionType *exc);

/* 建立虚拟页 → 物理页的映射（按需分配二级页表）
 * flags 为 PTE_* 标志位组合（PTE_READ | PTE_WRITE | PTE_EXEC | PTE_USER | PTE_VALID）
 */
bool mmu_map_page(MMUState *mmu, uint32_t vaddr, uint32_t paddr,
                  uint8_t flags);

/* 堆管理包装 — CPU 只调 mmu_*，brk 通过 MMU 层间接调用 mem_brk */
uint32_t mmu_brk(MMUState *mmu, PhysicalMemory *pmem, uint32_t new_brk);

#endif // MMU_H
