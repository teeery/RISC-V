
/* ============================================================================
 * elf_segment.c — ELF 段加载
 * ============================================================================
 *
 * 职责：将 ELF 文件中 PT_LOAD 类型的段数据加载到模拟器的物理内存中。
 *
 * 每个 PT_LOAD 段代表一块连续内存区域（如 .text 代码段、.data 数据段），
 * 本函数负责：
 *   1. 转换权限标志 — PF_* (ELF 规范) → MEM_* (模拟器 PhysicalMemory 层)
 *   2. 分配物理地址 — 当前简化版使用恒等映射（paddr = p_vaddr）
 *   3. 注册内存区域 — mem_map() 在 PhysicalMemory 中登记
 *   4. 拷贝段数据   — malloc 临时缓冲 → fread 读文件 → mem_load 写内存
 *   5. 清零 .bss    — calloc 零缓冲 → mem_load 写零（p_memsz > p_filesz 部分）
 *
 * ── 当前简化版 vs 完善版 ───────────────────────────────────
 *
 *   当前（简化版）：paddr = p_vaddr（恒等映射）
 *     - 优点：简单，不依赖 mem_find_free
 *     - 缺点：多段可能物理地址冲突；不模拟真实页分配
 *
 *   完善版 TODO：
 *     a) paddr = mem_find_free(pmem, p_memsz, p_align)
 *        在物理内存中动态找到空闲区域
 *     b) for each page in [p_vaddr, p_vaddr + p_memsz):
 *          mmu_map_page(mmu, vaddr_page, paddr_page, pte_flags)
 *        建立 Sv32 页表，CPU 之后通过虚拟地址取指/访存
 *     c) pte_flags 通过 mem_perm_to_pte_flags(mem_flags) 转换
 *
 * ── 权限转换说明 ──────────────────────────────────────────
 *
 *   ELF 用 PF_*（和模拟器不同！）：PF_R=4, PF_W=2, PF_X=1
 *   模拟器用 MEM_*：              MEM_READ=1, MEM_WRITE=2, MEM_EXEC=4
 *   完善版还要转 PTE_*：          PTE_VALID=1, PTE_READ=2, PTE_WRITE=4, PTE_EXEC=8
 *
 *   例如 .text 段（代码，p_flags = 5 = PF_R|PF_X）：
 *     → mem_flags = MEM_READ|MEM_EXEC = 1|4 = 5
 *     → pte_flags = PTE_VALID|PTE_READ|PTE_EXEC = 1|2|8 = 11
 * ============================================================================
 */

#include <stdlib.h>            // malloc, calloc, free — loader_internal.h 不间接包含
#include "loader_internal.h"   // 已间接包含 elf_loader.h + stdio.h + 依赖模块

bool elf_load_segment(FILE *fp, Elf32_Phdr *phdr,
                      PhysicalMemory *pmem, MMUState *mmu)
{
    (void)mmu;  // 简化版（恒等映射）不用，完善版传入 mmu_map_page

    /*
     * ── 步骤 1：确定物理地址（当前简化版：恒等映射）──
     *
     * 恒等映射意味着虚拟地址直接作为物理地址使用。
     * 这要求 ELF 的段地址不重叠且都在物理内存范围内。
     * 对于简单的单段/双段 ELF 完全够用。
     */
    uint32_t paddr = phdr->p_vaddr;  // 恒等映射 vaddr → paddr

    /* ── 步骤 2：边界检查 ──
     *
     * 确保段的物理地址范围不超出物理内存总大小。
     * 如果超了，说明这个 ELF 太大了（或地址设置不合理），无法加载。
     */
    if (paddr + phdr->p_memsz > pmem->size) {
        fprintf(stderr, "Error: Segment exceeds physical memory "
                "(need 0x%08x + 0x%08x > 0x%08x)\n",
                paddr, phdr->p_memsz, pmem->size);
        return false;
    }

    /* ── 步骤 3：权限转换 ──
     *
     * 逐个检查 p_flags 的每个位，转换为模拟器的 MEM_* 标志。
     * 注意 PF_* 和 MEM_* 的 bit 位置正好是置换关系：
     *   PF:  X=bit0(1), W=bit1(2), R=bit2(4)
     *   MEM: R=bit0(1), W=bit1(2), X=bit2(4)
     * 所以不能直接 memcpy 或用相同的值！
     */
    uint8_t flags = 0;
    if (phdr->p_flags & PF_R) flags |= MEM_READ;   // PF_R(4) → MEM_READ(1)
    if (phdr->p_flags & PF_W) flags |= MEM_WRITE;  // PF_W(2) → MEM_WRITE(2)
    if (phdr->p_flags & PF_X) flags |= MEM_EXEC;   // PF_X(1) → MEM_EXEC(4)

    /* ── 步骤 4：注册物理内存区域 ──
     *
     * mem_map 在 PhysicalMemory 的 regions 列表中记录这个区域。
     * 之后可以通过 mem_dump 查看到它。
     * name = "segment" 标识这是一个 ELF 加载的段。
     */
    if (!mem_map(pmem, paddr, phdr->p_memsz, flags, "segment")) {
        fprintf(stderr, "Error: Failed to map segment at 0x%08x\n", paddr);
        return false;
    }

    /* ── 步骤 5：从 ELF 文件拷贝段数据 ──
     *
     * 流程：malloc 临时缓冲 → fseek 定位 → fread 读文件 → mem_load 写内存 → free
     *
     * 为什么不直接 fread(pmem->data + paddr, ...)？
     *   对齐 memory 模块接口约定（并行方案）：
     *   Loader 通过 mem_load() 写入物理内存，不绕过 memory 层直接操作 data[]。
     *   这保持了模块边界清晰——即使 PhysicalMemory 内部实现改了，
     *   Loader 的代码不需要变。
     *
     * p_filesz 可能为 0（纯 .bss 段，文件中不占空间），此时跳过读取。
     */
    if (phdr->p_filesz > 0) {
        /* 分配恰好容纳段数据的临时缓冲区 */
        uint8_t *buf = (uint8_t *)malloc(phdr->p_filesz);
        if (!buf) {
            fprintf(stderr, "Error: Failed to allocate segment buffer "
                    "(%u bytes)\n", phdr->p_filesz);
            return false;
        }

        /* 定位到段数据在 ELF 文件中的位置 */
        fseek(fp, phdr->p_offset, SEEK_SET);

        /* 读取完整的段数据 */
        size_t n = fread(buf, 1, phdr->p_filesz, fp);
        if (n != phdr->p_filesz) {
            fprintf(stderr, "Error: Failed to read segment data "
                    "(expected %u bytes, got %zu)\n",
                    phdr->p_filesz, n);
            free(buf);
            return false;
        }

        /* 通过 memory 模块接口写入物理内存 */
        if (!mem_load(pmem, paddr, buf, phdr->p_filesz)) {
            fprintf(stderr, "Error: mem_load failed for segment "
                    "at 0x%08x\n", paddr);
            free(buf);
            return false;
        }

        free(buf);
    }

    /* ── 步骤 6：清零 .bss 段 ──
     *
     * 当 p_memsz > p_filesz 时，多出的部分是 .bss 段——
     * 未初始化的全局变量和静态变量。C 标准要求它们初始值为 0。
     *
     * 例：p_filesz=0x200, p_memsz=0x300 → bss_size=0x100 字节需要填零
     *
     * 使用 calloc 分配零缓冲区，然后通过 mem_load 写入。
     * calloc 返回的内存已经被初始化为全零，不需要再 memset。
     */
    if (phdr->p_memsz > phdr->p_filesz) {
        uint32_t bss_addr = paddr + phdr->p_filesz;    // .bss 起始物理地址
        uint32_t bss_size = phdr->p_memsz - phdr->p_filesz; // .bss 字节数

        uint8_t *zero_buf = (uint8_t *)calloc(1, bss_size);
        if (!zero_buf) {
            fprintf(stderr, "Error: Failed to allocate bss buffer "
                    "(%u bytes)\n", bss_size);
            return false;
        }

        if (!mem_load(pmem, bss_addr, zero_buf, bss_size)) {
            fprintf(stderr, "Error: mem_load bss failed at 0x%08x\n",
                    bss_addr);
            free(zero_buf);
            return false;
        }

        free(zero_buf);
    }

    /*
     * TODO（完善版）：建立 MMU 页表映射
     *
     * for (uint32_t off = 0; off < phdr->p_memsz; off += PAGE_SIZE) {
     *     uint8_t pte_flags = mem_perm_to_pte_flags(flags);
     *     mmu_map_page(mmu, phdr->p_vaddr + off, paddr + off, pte_flags);
     * }
     *
     * 为什么要逐页映射？因为 RISC-V Sv32 页表的最小管理单位是 4KB 页。
     * 每个虚拟页独立映射到一个物理页，权限也逐页设置。
     */

    return true;
}
