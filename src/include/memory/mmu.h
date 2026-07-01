/* ============================================================================
 * mmu.h — MMU 接口（合约头文件）
 * ============================================================================
 *
 * 对齐：并行开发方案 §0.3
 * 实现者：焕聪
 */

#ifndef MMU_H
#define MMU_H

#include "types.h"
#include "memory/memory.h"

/* MMU 状态 */
typedef struct {
    uint32_t  satp;
    uint32_t *root_page_table;
    bool      enabled;
} MMUState;

/* 初始化 */
void mmu_init(MMUState *mmu);

/* 虚拟地址 → 物理地址 */
bool mmu_translate(MMUState *mmu, uint32_t vaddr, uint32_t *paddr,
                   bool is_write, bool is_exec, PrivilegeLevel priv,
                   ExceptionType *exc);

/* 虚拟地址读写（CPU 取指 / Load / Store 用）*/
bool mmu_read_8 (MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                 uint8_t  *val, PrivilegeLevel priv, ExceptionType *exc);
bool mmu_read_16(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                 uint16_t *val, PrivilegeLevel priv, ExceptionType *exc);
bool mmu_read_32(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                 uint32_t *val, PrivilegeLevel priv, ExceptionType *exc);

bool mmu_write_8 (MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                  uint8_t  val, PrivilegeLevel priv, ExceptionType *exc);
bool mmu_write_16(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                  uint16_t val, PrivilegeLevel priv, ExceptionType *exc);
bool mmu_write_32(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                  uint32_t val, PrivilegeLevel priv, ExceptionType *exc);

/* 批量读写（syscall 用）*/
bool mmu_read (MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
               uint8_t *buf, uint32_t len, PrivilegeLevel priv,
               ExceptionType *exc);
bool mmu_write(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
               const uint8_t *buf, uint32_t len, PrivilegeLevel priv,
               ExceptionType *exc);

/* 页映射（Loader 用）*/
bool     mmu_map_page(MMUState *mmu, uint32_t vaddr, uint32_t paddr,
                      uint8_t flags);

/* 堆管理（syscall brk 用）*/
uint32_t mmu_brk(MMUState *mmu, PhysicalMemory *pmem, uint32_t new_brk);

#endif /* MMU_H */
