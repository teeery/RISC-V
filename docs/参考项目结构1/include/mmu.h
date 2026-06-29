#ifndef MMU_H
#define MMU_H

#include "types.h"
#include "memory.h"

/* ============================================================
 * mmu.h — 虚拟内存管理单元 (MMU)
 *
 * 需要编写的内容：
 * 1. mmu_translate()   — 虚拟地址 → 物理地址转换 (Sv32 页表遍历)
 * 2. mmu_read_8/16/32()— 虚拟地址读取 (先翻译再访问物理内存)
 * 3. mmu_write_8/16/32()— 虚拟地址写入
 * 4. mmu_map_page()    — 建立虚拟页到物理页的映射
 * 5. mmu_init()        — 初始化页表
 *
 * Sv32 虚拟内存方案 (RV32)：
 * - 32 位虚拟地址 = VPN[1] (10bits) | VPN[0] (10bits) | offset (12bits)
 * - 两级页表：第一级 1024 项 × 4B，第二级 1024 项 × 4B
 * - PTE 格式: PPN[1/0] (10+12bits) | flags (10bits: DAGUXWRV)
 *   V=Valid, R=Read, W=Write, X=Execute, U=User, G=Global, A=Accessed, D=Dirty
 * - 页大小 4KB
 * - satp CSR 存储第一级页表物理地址
 *
 * 设计要点：
 * - 无 MMU 模式：satp.MODE=0 时直接使用物理地址
 * - 页错误检测：无效映射、权限不足 → 抛出异常
 * - 按需分配第二级页表
 * ============================================================
 */

#define PAGE_SIZE        4096
#define PAGE_SHIFT       12
#define VPN0_SHIFT       12
#define VPN1_SHIFT       22
#define PTE_VALID        (1 << 0)
#define PTE_READ         (1 << 1)
#define PTE_WRITE        (1 << 2)
#define PTE_EXEC         (1 << 3)
#define PTE_USER         (1 << 4)
#define PTE_GLOBAL       (1 << 5)
#define PTE_ACCESSED     (1 << 6)
#define PTE_DIRTY        (1 << 7)

#define SATP_MODE_OFF    0   // 无地址转换
#define SATP_MODE_SV32   1   // Sv32 页表

/* MMU 状态 */
typedef struct {
    uint32_t satp;              // satp CSR 值 (MODE | ASID | PPN)
    uint32_t *root_page_table;  // 第一级页表 (物理地址)
    bool     enabled;           // 是否启用 MMU
} MMUState;

/* 初始化 MMU */
void mmu_init(MMUState *mmu);

/* 虚拟地址 → 物理地址 (返回 false 表示页错误) */
bool mmu_translate(MMUState *mmu, uint32_t vaddr, uint32_t *paddr,
                   bool is_write, bool is_exec, PrivilegeLevel priv,
                   ExceptionType *exc);

/* 虚拟地址读写 */
bool mmu_read_8 (MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                 uint8_t *val, PrivilegeLevel priv);
bool mmu_read_16(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                 uint16_t *val, PrivilegeLevel priv);
bool mmu_read_32(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                 uint32_t *val, PrivilegeLevel priv);
bool mmu_write_8 (MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                  uint8_t val, PrivilegeLevel priv);
bool mmu_write_16(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                  uint16_t val, PrivilegeLevel priv);
bool mmu_write_32(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                  uint32_t val, PrivilegeLevel priv);

/* 映射一页 (虚拟页 → 物理页) */
bool mmu_map_page(MMUState *mmu, uint32_t vaddr, uint32_t paddr,
                  uint8_t flags);

#endif // MMU_H
