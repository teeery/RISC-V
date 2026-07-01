/* ============================================================================
 * types.h — 公共类型定义（所有模块共享，零依赖）
 * ============================================================================
 *
 * 对齐：并行开发方案 §0.1 + 公共类型定义文档
 */

#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ── 特权级 ── */
typedef enum {
    PRIV_USER       = 0,
    PRIV_SUPERVISOR = 1,
    PRIV_MACHINE    = 3,
} PrivilegeLevel;

/* ── 异常类型 ── */
typedef enum {
    EXC_NONE                   = 0,
    EXC_INST_ADDR_MISALIGNED   = 1,
    EXC_INST_ACCESS_FAULT      = 2,
    EXC_ILLEGAL_INST           = 3,
    EXC_BREAKPOINT             = 4,
    EXC_LOAD_ADDR_MISALIGNED   = 5,
    EXC_LOAD_ACCESS_FAULT      = 6,
    EXC_STORE_ADDR_MISALIGNED  = 7,
    EXC_STORE_ACCESS_FAULT     = 8,
    EXC_ECALL_U                = 9,
    EXC_ECALL_S                = 10,
    EXC_ECALL_M                = 11,
    EXC_PAGE_FAULT_INST        = 12,
    EXC_PAGE_FAULT_LOAD        = 13,
    EXC_PAGE_FAULT_STORE       = 15,
} ExceptionType;

/* ── 内存权限标志（PhysicalMemory 层用）── */
#define MEM_READ   (1 << 0)
#define MEM_WRITE  (1 << 1)
#define MEM_EXEC   (1 << 2)

/* ── PTE 权限标志（MMU 页表层用）── */
#define PTE_VALID    (1 << 0)
#define PTE_READ     (1 << 1)
#define PTE_WRITE    (1 << 2)
#define PTE_EXEC     (1 << 3)
#define PTE_USER     (1 << 4)
#define PTE_GLOBAL   (1 << 5)
#define PTE_ACCESSED (1 << 6)
#define PTE_DIRTY    (1 << 7)

/* ── Sv32 常量 ── */
#define PAGE_SIZE    4096
#define PAGE_SHIFT   12
#define SATP_MODE_OFF  0
#define SATP_MODE_SV32 1

/* ── MEM → PTE 权限转换（Loader / MMU 使用）── */
static inline uint8_t mem_perm_to_pte_flags(uint8_t mem_flags) {
    uint8_t pte = PTE_VALID;
    if (mem_flags & MEM_READ)  pte |= PTE_READ;
    if (mem_flags & MEM_WRITE) pte |= PTE_WRITE;
    if (mem_flags & MEM_EXEC)  pte |= PTE_EXEC;
    return pte;
}

#endif /* TYPES_H */
