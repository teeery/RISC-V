/* ============================================================================
 * elf_segment.c — ELF 段加载
 * ============================================================================
 *
 * 职责：遍历 Program Header Table，对每个 PT_LOAD 类型的段：
 *   1. 转换 ELF 权限标志（PF_*）→ 内存权限标志（MEM_*）
 *   2. 在物理内存中分配空间（当前简化版使用恒等映射 paddr = p_vaddr）
 *   3. 建立物理内存区域映射（mem_map）
 *   4. 从 ELF 文件读取段数据到物理内存
 *   5. 对 p_memsz > p_filesz 的部分（.bss 段）清零
 *
 * TODO（完善版）:
 *   - 使用 mem_find_free() 动态分配物理地址，替代恒等映射
 *   - 调用 mmu_map_page() 为每个虚拟页建立页表映射
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elf_loader.h"
#include "loader_internal.h"

bool elf_load_segment(FILE *fp, Elf32_Phdr *phdr,
                      PhysicalMemory *pmem, MMUState *mmu)
{
    /*
     * ============================================================
     * 当前简化实现：虚拟地址 → 物理地址 恒等映射
     *
     * 完善版 TODO:
     *   uint32_t paddr = mem_find_free(pmem, phdr->p_memsz, phdr->p_align);
     *   if (paddr == (uint32_t)-1) { ... 分配失败 ... }
     *   然后用 mmu_map_page() 建立 vaddr → paddr 的页表映射。
     * ============================================================
     */
    uint32_t paddr = phdr->p_vaddr;  // 恒等映射

    /* ---- 边界检查 ---- */
    if (paddr + phdr->p_memsz > pmem->size) {
        fprintf(stderr, "Error: Segment exceeds physical memory "
                "(need 0x%08x + 0x%08x > 0x%08x)\n",
                paddr, phdr->p_memsz, pmem->size);
        return false;
    }

    /* ---- 权限转换：PF_* (ELF) → MEM_* (模拟器) ---- */
    uint8_t flags = 0;
    if (phdr->p_flags & PF_R) flags |= MEM_READ;
    if (phdr->p_flags & PF_W) flags |= MEM_WRITE;
    if (phdr->p_flags & PF_X) flags |= MEM_EXEC;

    /* ---- 建立物理内存映射 ---- */
    mem_map(pmem, paddr, phdr->p_memsz, flags, "segment");

    /* ---- 从 ELF 文件拷贝段数据 ---- */
    if (phdr->p_filesz > 0) {
        fseek(fp, phdr->p_offset, SEEK_SET);
        size_t n = fread(pmem->data + paddr, 1, phdr->p_filesz, fp);
        if (n != phdr->p_filesz) {
            fprintf(stderr, "Error: Failed to read segment data "
                    "(expected %u bytes, got %zu)\n",
                    phdr->p_filesz, n);
            return false;
        }
    }

    /* ---- .bss 段清零 ---- */
    if (phdr->p_memsz > phdr->p_filesz) {
        uint32_t bss_offset = paddr + phdr->p_filesz;
        uint32_t bss_size   = phdr->p_memsz - phdr->p_filesz;
        memset(pmem->data + bss_offset, 0, bss_size);
    }

    return true;
}
