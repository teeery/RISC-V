/* ============================================================================
 * elf_load.c — ELF 加载器主入口
 * ============================================================================
 *
 * 职责：编排整个 ELF 加载流程，作为 loader 模块对外的唯一入口。
 *
 * 加载流程：
 *   1. 打开 ELF 文件
 *   2. 读取 ELF Header
 *   3. 调用 elf_validate_header() 校验
 *   4. 遍历 Program Headers，对每个 PT_LOAD 段调用 elf_load_segment()
 *   5. 设置程序入口地址（*entry = e_entry）
 *   6. 调用 elf_setup_stack() 初始化用户栈
 *   7. 打印加载信息，返回成功
 *
 * 本函数不关心校验的细节（交给 elf_validate.c）、
 * 不关心段怎么搬运（交给 elf_segment.c）、
 * 不关心栈怎么布局（交给 elf_stack.c）。
 */

#include <stdio.h>
#include <stdlib.h>
#include "elf_loader.h"
#include "loader_internal.h"

bool elf_load(const char *filename, PhysicalMemory *pmem, MMUState *mmu,
              uint32_t *entry, uint32_t *stack_top)
{
    /* ---- 1. 打开文件 ---- */
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("Failed to open ELF file");
        return false;
    }

    /* ---- 2. 读取 ELF Header ---- */
    Elf32_Ehdr ehdr;
    if (fread(&ehdr, sizeof(ehdr), 1, fp) != 1) {
        fprintf(stderr, "Error: Failed to read ELF header\n");
        fclose(fp);
        return false;
    }

    /* ---- 3. 校验 ELF Header ---- */
    if (!elf_validate_header(&ehdr)) {
        fclose(fp);
        return false;
    }

    /* ---- 4. 遍历 Program Headers，加载 PT_LOAD 段 ---- */
    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf32_Phdr phdr;

        /* 计算 Program Header 在文件中的位置并读取 */
        long offset = ehdr.e_phoff + i * sizeof(Elf32_Phdr);
        fseek(fp, offset, SEEK_SET);
        if (fread(&phdr, sizeof(phdr), 1, fp) != 1) {
            fprintf(stderr, "Error: Failed to read program header %d\n", i);
            fclose(fp);
            return false;
        }

        /* 只处理 PT_LOAD 类型的段 */
        if (phdr.p_type == PT_LOAD) {
            if (!elf_load_segment(fp, &phdr, pmem, mmu)) {
                fclose(fp);
                return false;
            }
        }
    }

    /* ---- 5. 关闭文件 ---- */
    fclose(fp);

    /* ---- 6. 设置程序入口地址 ---- */
    *entry = ehdr.e_entry;

    /* ---- 7. 初始化用户栈 ---- */
    if (!elf_setup_stack(pmem, stack_top)) {
        return false;
    }

    /* ---- 8. 打印加载信息（调试用）---- */
    printf("ELF loaded: %s\n", filename);
    printf("  Entry:  0x%08x\n", *entry);
    printf("  Stack:  0x%08x (size: %u KB)\n",
           *stack_top, STACK_SIZE_DEFAULT / 1024);

    return true;
}
