#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ============================================================
 * mmu.c — 虚拟内存管理单元 (MMU) : Sv32 页表实现
 *
 * ─── 你需要实现的内容 ─────────────────────────────────────
 *
 * 1. mmu_translate(mmu, vaddr, *paddr, is_write, is_exec, priv, *exc)
 *    Sv32 虚拟地址翻译流程：
 *
 *    a) 从 satp CSR 获取第一级页表基址 (PPN × 4096)
 *       - 如果 satp.MODE == 0 → 直接使用物理地址 (paddr = vaddr)
 *
 *    b) 提取 VPN[1] (vaddr[31:22]) 作为第一级页表索引
 *       - 读取 PDE = root_page_table[VPN[1]]
 *
 *    c) 检查 PDE 有效性:
 *       - V=0 → 页错误 (Page Fault)
 *       - R=W=0 且非叶子 → 这是指向第二级的指针
 *
 *    d) 提取 VPN[0] (vaddr[21:12]) 作为第二级页表索引
 *       - 计算第二级页表地址 = PDE.PPN × 4096
 *       - 读取 PTE = leaf_table[VPN[0]]
 *
 *    e) 检查 PTE 权限:
 *       - V=0 → 页错误
 *       - 如果是写操作但 W=0 → 页错误
 *       - 如果是执行但 X=0 → 页错误
 *       - 如果是 U 模式访问但 U=0 → 页错误
 *
 *    f) 计算物理地址:
 *       paddr = PTE.PPN × 4096 + (vaddr & 0xFFF)
 *
 *    g) 更新 A/D 位:
 *       - 访问时置 A=1
 *       - 写操作时置 D=1
 *
 * 2. mmu_read_8/16/32(mmu, pmem, vaddr, *val, priv)
 *    - 调用 mmu_translate 获取物理地址
 *    - 调用 mem_read_8/16/32 读写物理内存
 *    - 跨页边界处理 (对于 16/32 位访问)
 *
 * 3. mmu_write_8/16/32(mmu, pmem, vaddr, val, priv)
 *    - 同上
 *
 * 4. mmu_map_page(mmu, vaddr, paddr, flags)
 *    - 建立虚拟页到物理页的映射
 *    - 按需分配第二级页表
 *    - 设置 PTE 标志位
 *
 * ─── Sv32 PTE 格式 ────────────────────────────────────────
 *  31         20 19        10 9 8 7 6 5 4 3 2 1 0
 * ┌─────────────┬────────────┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┐
 * │ PPN[1]      │ PPN[0]     │ - │ D A G U X W R V
 * └─────────────┴────────────┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┘
 *   PPN = {PPN[1], PPN[0]} → 物理页号 = PPN << 12
 *
 * ─── 页表管理 ──────────────────────────────────────────────
 * - 第一级页表: 1024 项 × 4 字节 = 4096 字节 (正好一页)
 * - 第二级页表: 同上，按需分配
 * - 使用 malloc 分配页表，存储在单独的页表管理结构中
 * - 页表本身也需要映射到物理地址空间
 *
 * ─── 大页支持 (可选) ──────────────────────────────────────
 * 可以在第一级 PDE 中设置 R/W/X 标志实现 4MB 大页，
 * 但简化实现可忽略，仅使用两级映射。
 * ============================================================
 */

#include "types.h"
#include "memory.h"
#include "mmu.h"

/* ── PTE 操作辅助 ────────────────────────────────────────── */

#define PTE_PPN_MASK  0xFFFFF000   // PPN 在 PTE 的高 22 位
#define PTE_FLAGS_MASK 0x00000FFF

/* 从 PTE 提取物理页号 */
static inline uint32_t pte_to_ppn(uint32_t pte) {
    return (pte & PTE_PPN_MASK) >> PAGE_SHIFT;
}

/* 从 PPN 构造 PTE */
static inline uint32_t ppn_to_pte(uint32_t ppn, uint32_t flags) {
    return (ppn << PAGE_SHIFT) | (flags & PTE_FLAGS_MASK);
}

/* ── 页表管理 ────────────────────────────────────────────── */

#define PT_ENTRIES 1024

/* 页表管理器 (跟踪所有分配的页表) */
typedef struct {
    uint32_t *tables;       // 页表存储池
    int       table_count;
    int       table_capacity;
} PageTableManager;

/* ── 初始化 ──────────────────────────────────────────────── */

void mmu_init(MMUState *mmu)
{
    mmu->satp   = 0;
    mmu->enabled = false;
    // 分配根页表
    mmu->root_page_table = (uint32_t *)calloc(PT_ENTRIES, sizeof(uint32_t));
}

/* ── 地址翻译 ────────────────────────────────────────────── */

bool mmu_translate(MMUState *mmu, uint32_t vaddr, uint32_t *paddr,
                   bool is_write, bool is_exec, PrivilegeLevel priv,
                   ExceptionType *exc)
{
    // TODO: MODE == 0 (Bare 模式) → 直接使用物理地址
    if (!mmu->enabled || ((mmu->satp >> 31) & 1) == 0) {
        *paddr = vaddr;
        *exc   = EXC_NONE;
        return true;
    }

    // Sv32 两级页遍历
    uint32_t vpn1 = (vaddr >> VPN1_SHIFT) & 0x3FF;   // VPN[1]
    uint32_t vpn0 = (vaddr >> VPN0_SHIFT) & 0x3FF;   // VPN[0]
    uint32_t offset = vaddr & (PAGE_SIZE - 1);

    // 第一级: 读取 PDE
    uint32_t *l1_table = mmu->root_page_table;
    uint32_t pde = l1_table[vpn1];

    if (!(pde & PTE_VALID)) {
        *exc = is_exec ? EXC_INSTR_ACCESS_FAULT :
               is_write ? EXC_STORE_ACCESS_FAULT :
                          EXC_LOAD_ACCESS_FAULT;
        return false;
    }

    // 检查第一级是否是叶子 (大页，可选) 或指针
    // 简单实现: 总是视为指针 (R=W=0 时为非叶子)
    if ((pde & (PTE_READ | PTE_WRITE | PTE_EXEC)) == 0) {
        // 非叶子: 指向第二级页表
        // TODO: 需要维护第二级页表的映射，此处需要额外的页表管理器
        // 简化实现：将 PPN 作为物理地址偏移使用
        uint32_t l2_paddr = pte_to_ppn(pde) << PAGE_SHIFT;
        // 假设第二级页表位于物理内存的该地址处
        // 实际实现需要维护页表物理地址到指针的映射
        *exc = EXC_NONE;
        // 暂不实现完整的页表遍历，留待扩展
        *paddr = vaddr;  // identity mapping fallback
        return true;
    }

    // 第一级是叶子或需要第二级
    // (完整实现应在此处继续遍历第二级)

    // TODO: 完整实现两级页表遍历
    // 此处为简化版本 - 恒等映射
    *paddr = vaddr;
    *exc = EXC_NONE;
    return true;
}

/* ── 虚拟地址读写 ────────────────────────────────────────── */

bool mmu_read_8(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                uint8_t *val, PrivilegeLevel priv)
{
    uint32_t paddr;
    ExceptionType exc;
    if (!mmu_translate(mmu, vaddr, &paddr, false, false, priv, &exc))
        return false;
    return mem_read_8(pmem, paddr, val);
}

bool mmu_read_16(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                 uint16_t *val, PrivilegeLevel priv)
{
    uint32_t paddr;
    ExceptionType exc;
    if (!mmu_translate(mmu, vaddr, &paddr, false, false, priv, &exc))
        return false;
    // 跨页检查 (可选: 对于未对齐的半字)
    return mem_read_16(pmem, paddr, val);
}

bool mmu_read_32(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                 uint32_t *val, PrivilegeLevel priv)
{
    uint32_t paddr;
    ExceptionType exc;
    if (!mmu_translate(mmu, vaddr, &paddr, false, false, priv, &exc))
        return false;
    return mem_read_32(pmem, paddr, val);
}

bool mmu_write_8(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                 uint8_t val, PrivilegeLevel priv)
{
    uint32_t paddr;
    ExceptionType exc;
    if (!mmu_translate(mmu, vaddr, &paddr, true, false, priv, &exc))
        return false;
    return mem_write_8(pmem, paddr, val);
}

bool mmu_write_16(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                  uint16_t val, PrivilegeLevel priv)
{
    uint32_t paddr;
    ExceptionType exc;
    if (!mmu_translate(mmu, vaddr, &paddr, true, false, priv, &exc))
        return false;
    return mem_write_16(pmem, paddr, val);
}

bool mmu_write_32(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                  uint32_t val, PrivilegeLevel priv)
{
    uint32_t paddr;
    ExceptionType exc;
    if (!mmu_translate(mmu, vaddr, &paddr, true, false, priv, &exc))
        return false;
    return mem_write_32(pmem, paddr, val);
}

/* ── 页表映射 ────────────────────────────────────────────── */
bool mmu_map_page(MMUState *mmu, uint32_t vaddr, uint32_t paddr,
                  uint8_t flags)
{
    // TODO: 完整实现页表映射
    // 1. 提取 vpn1, vpn0
    // 2. 检查第一级 PDE 是否存在，需要时分配第二级
    // 3. 在第二级写入 PTE = PPN | flags
    // 4. 设置 A=0, D=0 (初始)
    return true;
}
