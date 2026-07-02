#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "memory/memory.h"
#include "memory/mmu.h"

/* ============================================================
 * mmu.c — 虚拟内存管理单元实现（Sv32 页表）
 *
 * 实现 mmu.h 中声明的 13 个函数。
 *
 * 设计要点：
 *   - Bare 模式（satp.MODE = 0）：vaddr → paddr 恒等映射
 *   - Sv32 模式（satp.MODE = 1）：两级页表遍历 + PTE 权限校验
 *   - 返回值 bool + ExceptionType *exc 输出参数
 *   - 页表独立 malloc，不复用物理内存
 *   - 页表管理器：MMUState.pt_mgr 追踪 PPN→l2_pointer 映射
 * ============================================================
 */

/* ============================================================
 * 页表常量与辅助宏
 * ============================================================
 */

#define PT_ENTRIES      1024        // 每级页表 1024 项
#define PTE_PPN_MASK    0xFFFFF000  // PTE 高 22 位为 PPN
#define PTE_FLAGS_MASK  0x000003FF  // PTE 低 10 位为标志
#define PT_MGR_INIT_CAP 16          // 页表管理器初始容量

/* 从 PTE 提取物理页号 */
static inline uint32_t pte_to_ppn(uint32_t pte) {
    return (pte & PTE_PPN_MASK) >> PAGE_SHIFT;
}

/* 从 PPN + flags 构造 PTE */
static inline uint32_t make_pte(uint32_t ppn, uint32_t flags) {
    return (ppn << PAGE_SHIFT) | (flags & PTE_FLAGS_MASK);
}

/* ============================================================
 * 页表管理器 — PPN → 宿主机指针映射
 * ============================================================
 */

/* 向管理器中注册一个第二级页表 */
static void pt_mgr_add(MMUState *mmu, uint32_t ppn, uint32_t *l2_ptr)
{
    int n = mmu->pt_mgr.count;
    int cap = mmu->pt_mgr.capacity;

    /* 首次使用：分配管理器的内部数组 */
    if (!mmu->pt_mgr.ppns) {
        mmu->pt_mgr.capacity = PT_MGR_INIT_CAP;
        mmu->pt_mgr.ppns   = (uint32_t *)calloc((size_t)PT_MGR_INIT_CAP,
                                                  sizeof(uint32_t));
        mmu->pt_mgr.tables = (uint32_t **)calloc((size_t)PT_MGR_INIT_CAP,
                                                  sizeof(uint32_t *));
        mmu->pt_mgr.count = 0;
        cap = PT_MGR_INIT_CAP;
    }

    /* 动态扩容 */
    if (n >= cap) {
        int new_cap = cap * 2;
        mmu->pt_mgr.ppns   = (uint32_t *)realloc(mmu->pt_mgr.ppns,
                                                   (size_t)new_cap * sizeof(uint32_t));
        mmu->pt_mgr.tables = (uint32_t **)realloc(mmu->pt_mgr.tables,
                                                    (size_t)new_cap * sizeof(uint32_t *));
        mmu->pt_mgr.capacity = new_cap;
    }

    mmu->pt_mgr.ppns[n]   = ppn;
    mmu->pt_mgr.tables[n] = l2_ptr;
    mmu->pt_mgr.count++;
}

/* 根据物理页号查找第二级页表指针，找不到返回 NULL */
static uint32_t *pt_mgr_find(MMUState *mmu, uint32_t ppn)
{
    for (int i = 0; i < mmu->pt_mgr.count; i++) {
        if (mmu->pt_mgr.ppns[i] == ppn) {
            return mmu->pt_mgr.tables[i];
        }
    }
    return NULL;
}

/* 释放页表管理器及其追踪的所有第二级页表 */
static void pt_mgr_free(MMUState *mmu)
{
    for (int i = 0; i < mmu->pt_mgr.count; i++) {
        free(mmu->pt_mgr.tables[i]);
    }
    free(mmu->pt_mgr.ppns);
    free(mmu->pt_mgr.tables);
    mmu->pt_mgr.ppns    = NULL;
    mmu->pt_mgr.tables  = NULL;
    mmu->pt_mgr.count   = 0;
    mmu->pt_mgr.capacity = 0;
}

/* ============================================================
 * 初始化 / 销毁
 * ============================================================
 */

void mmu_init(MMUState *mmu)
{
    memset(mmu, 0, sizeof(MMUState));
    mmu->satp    = 0;
    mmu->enabled = false;
    mmu->root_page_table = (uint32_t *)calloc(PT_ENTRIES, sizeof(uint32_t));
}

void mmu_destroy(MMUState *mmu)
{
    pt_mgr_free(mmu);
    free(mmu->root_page_table);
    mmu->root_page_table = NULL;
    mmu->satp    = 0;
    mmu->enabled = false;
}

/* ============================================================
 * 地址翻译（Sv32 页表遍历 + 权限检查）
 * ============================================================
 */

bool mmu_translate(MMUState *mmu, uint32_t vaddr, uint32_t *paddr,
                   bool is_write, bool is_exec, PrivilegeLevel priv,
                   ExceptionType *exc)
{
    /* ── Bare 模式：vaddr = paddr ── */
    uint8_t mode = (mmu->satp >> 31) & 1;   // satp[31] = MODE
    if (!mmu->enabled || mode == SATP_MODE_OFF) {
        *paddr = vaddr;
        *exc   = EXC_NONE;
        return true;
    }

    /* ── Sv32 模式：两级页表遍历 ── */
    uint32_t vpn1   = (vaddr >> VPN1_SHIFT) & 0x3FF;
    uint32_t vpn0   = (vaddr >> VPN0_SHIFT) & 0x3FF;
    uint32_t offset = vaddr & (PAGE_SIZE - 1);

    /* 第一级：读 PDE */
    uint32_t *l1 = mmu->root_page_table;
    if (!l1) {
        *exc = EXC_LOAD_ACCESS_FAULT;
        return false;
    }
    uint32_t pde = l1[vpn1];

    if (!(pde & PTE_VALID)) {
        *exc = is_exec ? EXC_INST_ACCESS_FAULT :
               is_write ? EXC_STORE_ACCESS_FAULT :
                          EXC_LOAD_ACCESS_FAULT;
        return false;
    }

    /* 检查第一级是否为 4MB 大页（PDE 有 R/W/X 中任一位则视为叶子） */
    if (pde & (PTE_READ | PTE_WRITE | PTE_EXEC)) {
        /* 4MB 大页：PPN[1] 即物理页号，offset = vaddr[21:0] */
        uint32_t ppn1 = (pde >> PAGE_SHIFT) & 0x3FF;
        *paddr = (ppn1 << (PAGE_SHIFT + 10)) | (vaddr & 0x003FFFFF);
        *exc   = EXC_NONE;
        return true;
    }

    /* 非叶子：PDE.PPN 指向第二级页表 */
    uint32_t *l2 = pt_mgr_find(mmu, pte_to_ppn(pde));
    if (!l2) {
        /* 第二级页表不存在 → 页错误 */
        *exc = is_exec ? EXC_PAGE_FAULT_INST :
               is_write ? EXC_PAGE_FAULT_STORE :
                          EXC_PAGE_FAULT_LOAD;
        return false;
    }

    /* 第二级：读 PTE */
    uint32_t pte = l2[vpn0];

    if (!(pte & PTE_VALID)) {
        *exc = is_exec ? EXC_PAGE_FAULT_INST :
               is_write ? EXC_PAGE_FAULT_STORE :
                          EXC_PAGE_FAULT_LOAD;
        return false;
    }

    /* 权限检查 */
    if (is_write && !(pte & PTE_WRITE)) {
        *exc = EXC_STORE_ACCESS_FAULT;
        return false;
    }
    if (is_exec && !(pte & PTE_EXEC)) {
        *exc = EXC_INST_ACCESS_FAULT;
        return false;
    }
    if (priv == PRIV_USER && !(pte & PTE_USER)) {
        *exc = is_write ? EXC_STORE_ACCESS_FAULT : EXC_LOAD_ACCESS_FAULT;
        return false;
    }

    /* 翻译成功 */
    *paddr = pte_to_ppn(pte) * PAGE_SIZE + offset;
    *exc   = EXC_NONE;
    return true;
}

/* ============================================================
 * 虚拟地址单次读写
 * ============================================================
 */

bool mmu_read_8(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                uint8_t *val, PrivilegeLevel priv, ExceptionType *exc)
{
    uint32_t paddr;
    if (!mmu_translate(mmu, vaddr, &paddr, false, false, priv, exc))
        return false;
    if (!mem_read_8(pmem, paddr, val)) {
        *exc = EXC_LOAD_ACCESS_FAULT;
        return false;
    }
    *exc = EXC_NONE;
    return true;
}

bool mmu_read_16(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                 uint16_t *val, PrivilegeLevel priv, ExceptionType *exc)
{
    uint32_t paddr;
    if (!mmu_translate(mmu, vaddr, &paddr, false, false, priv, exc))
        return false;
    if (!mem_read_16(pmem, paddr, val)) {
        *exc = EXC_LOAD_ACCESS_FAULT;
        return false;
    }
    *exc = EXC_NONE;
    return true;
}

bool mmu_read_32(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                 uint32_t *val, PrivilegeLevel priv, ExceptionType *exc)
{
    uint32_t paddr;
    if (!mmu_translate(mmu, vaddr, &paddr, false, false, priv, exc))
        return false;
    if (!mem_read_32(pmem, paddr, val)) {
        *exc = EXC_LOAD_ACCESS_FAULT;
        return false;
    }
    *exc = EXC_NONE;
    return true;
}

bool mmu_write_8(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                 uint8_t val, PrivilegeLevel priv, ExceptionType *exc)
{
    uint32_t paddr;
    if (!mmu_translate(mmu, vaddr, &paddr, true, false, priv, exc))
        return false;
    if (!mem_write_8(pmem, paddr, val)) {
        *exc = EXC_STORE_ACCESS_FAULT;
        return false;
    }
    *exc = EXC_NONE;
    return true;
}

bool mmu_write_16(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                  uint16_t val, PrivilegeLevel priv, ExceptionType *exc)
{
    uint32_t paddr;
    if (!mmu_translate(mmu, vaddr, &paddr, true, false, priv, exc))
        return false;
    if (!mem_write_16(pmem, paddr, val)) {
        *exc = EXC_STORE_ACCESS_FAULT;
        return false;
    }
    *exc = EXC_NONE;
    return true;
}

bool mmu_write_32(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                  uint32_t val, PrivilegeLevel priv, ExceptionType *exc)
{
    uint32_t paddr;
    if (!mmu_translate(mmu, vaddr, &paddr, true, false, priv, exc))
        return false;
    if (!mem_write_32(pmem, paddr, val)) {
        *exc = EXC_STORE_ACCESS_FAULT;
        return false;
    }
    *exc = EXC_NONE;
    return true;
}

/* ============================================================
 * 虚拟地址批量读写（syscall 场景）
 * ============================================================
 */

bool mmu_read(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
              uint8_t *buf, uint32_t len, PrivilegeLevel priv,
              ExceptionType *exc)
{
    for (uint32_t i = 0; i < len; i++) {
        if (!mmu_read_8(mmu, pmem, vaddr + i, &buf[i], priv, exc))
            return false;
    }
    *exc = EXC_NONE;
    return true;
}

bool mmu_write(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
               const uint8_t *buf, uint32_t len, PrivilegeLevel priv,
               ExceptionType *exc)
{
    for (uint32_t i = 0; i < len; i++) {
        if (!mmu_write_8(mmu, pmem, vaddr + i, buf[i], priv, exc))
            return false;
    }
    *exc = EXC_NONE;
    return true;
}

/* ============================================================
 * 页表映射建立
 *
 * flags 为 MEM_* 标志位组合（来自 Loader）。
 * 内部左移 1 位转换为 PTE_*，并补上 PTE_VALID。
 * ============================================================
 */

bool mmu_map_page(MMUState *mmu, uint32_t vaddr, uint32_t paddr,
                  uint8_t flags)
{
    uint32_t vpn1 = (vaddr >> VPN1_SHIFT) & 0x3FF;
    uint32_t vpn0 = (vaddr >> VPN0_SHIFT) & 0x3FF;
    uint32_t ppn  = paddr >> PAGE_SHIFT;

    /* MEM_* → PTE_* 转换 */
    uint8_t pte_flags = mem_perm_to_pte_flags(flags);

    /* 确保根页表存在 */
    if (!mmu->root_page_table) {
        mmu->root_page_table = (uint32_t *)calloc(PT_ENTRIES, sizeof(uint32_t));
    }

    /* 第一级 PDE：检查是否需要分配第二级页表 */
    uint32_t *l1 = mmu->root_page_table;
    uint32_t *l2 = NULL;

    if (!(l1[vpn1] & PTE_VALID)) {
        /* 分配第二级页表 */
        l2 = (uint32_t *)calloc(PT_ENTRIES, sizeof(uint32_t));
        if (!l2) {
            return false;   // calloc 失败
        }

        /* 用 vpn1 的低位作为"虚拟 PPN"来追踪 l2 表。
         * 真实硬件用物理页号，模拟器用一个自定义索引即可，
         * 关键是 ppn 在 pt_mgr 中唯一即可。
         * 这里用 (vpn1 << 10) | 0xFF0 保证不冲突。 */
        uint32_t l2_ppn = (vpn1 << 10) | 0xFF0;

        /* 注册到页表管理器 */
        pt_mgr_add(mmu, l2_ppn, l2);

        /* 写第一级 PDE：指向第二级 + 仅设置 V 位（非叶子） */
        l1[vpn1] = make_pte(l2_ppn, PTE_VALID);
    }

    /* 第二级：写 PTE（重新查管理器获取 l2 指针，确保一致性） */
    uint32_t l2_ppn = pte_to_ppn(l1[vpn1]);
    l2 = pt_mgr_find(mmu, l2_ppn);
    if (!l2) {
        return false;   // 内部不一致
    }

    l2[vpn0] = make_pte(ppn, pte_flags);
    return true;
}

/* ============================================================
 * 堆管理包装 — CPU 只调 mmu_*，内部转发到 mem_brk
 * ============================================================
 */

uint32_t mmu_brk(MMUState *mmu, PhysicalMemory *pmem, uint32_t new_brk)
{
    (void)mmu;  // brk 不涉及地址翻译，mmu 未使用
    return mem_brk(pmem, new_brk);
}
