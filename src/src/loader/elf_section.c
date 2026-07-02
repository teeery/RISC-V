/* ============================================================================
 * elf_section.c — ELF Section Header 解析
 * ============================================================================
 *
 * 职责：解析 Section Header Table，按名称查找节。
 *
 * 调用方：
 *   - Debugger（嘉俊）：反汇编时显示函数名（查 .symtab）
 *
 * 不影响现有 Program Header 加载流程——这是额外的元数据解析，
 * 在 elf_load() 之外独立调用。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "loader_internal.h"

/* ===================================================================
 * elf_parse_sections — 解析 Section Header Table
 *
 * 打开 ELF 文件，读取所有 Section Header 和节名称字符串表。
 * 调用者提供足够大的数组来存放解析结果。
 *
 * 参数:
 *   filename — ELF 文件路径
 *   shdrs    — [输出] Section Header 数组（调用者分配）
 *   max      — 最大解析数量（通常是 shdrs 数组大小）
 *   shstrtab — [输出] 节名称字符串表（调用者分配，或传入 NULL）
 *   strsize  — shstrtab 缓冲区大小
 *
 * 返回: 解析到的 Section 数量（0 表示失败或无 Section）
 * =================================================================== */
int elf_parse_sections(const char *filename,
                       Elf32_Shdr *shdrs, int max,
                       char *shstrtab, int strsize)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp) return 0;

    /* 读取 ELF Header */
    Elf32_Ehdr ehdr;
    if (fread(&ehdr, sizeof(ehdr), 1, fp) != 1) {
        fclose(fp);
        return 0;
    }

    /* 没有 Section Header Table？ */
    if (ehdr.e_shoff == 0 || ehdr.e_shnum == 0) {
        fclose(fp);
        return 0;
    }

    int count = (ehdr.e_shnum < max) ? ehdr.e_shnum : max;

    /* 读取所有 Section Header */
    fseek(fp, ehdr.e_shoff, SEEK_SET);
    for (int i = 0; i < count; i++) {
        if (fread(&shdrs[i], sizeof(Elf32_Shdr), 1, fp) != 1) {
            fclose(fp);
            return i;  // 返回已读取的数量
        }
    }

    /* 读取节名称字符串表（.shstrtab）
     *
     * ehdr.e_shstrndx 是 .shstrtab 在 Section Header 表中的索引。
     * .shstrtab 的 sh_offset 指向文件中字符串表的位置。
     * 每个 Section 的 sh_name 是字符串表中的偏移。
     */
    if (shstrtab && strsize > 0 &&
        ehdr.e_shstrndx > 0 && ehdr.e_shstrndx < count) {
        Elf32_Shdr *shstrtab_hdr = &shdrs[ehdr.e_shstrndx];
        int copy_size = (shstrtab_hdr->sh_size < (Elf32_Word)(strsize - 1))
                        ? (int)shstrtab_hdr->sh_size : strsize - 1;
        fseek(fp, shstrtab_hdr->sh_offset, SEEK_SET);
        fread(shstrtab, 1, copy_size, fp);
        shstrtab[copy_size] = '\0';  // 安全截断
    }

    fclose(fp);
    return count;
}

/* ===================================================================
 * elf_find_section — 按名称查找 Section
 *
 * 参数:
 *   shdrs    — elf_parse_sections() 输出的数组
 *   count    — 数组中的 Section 数量
 *   shstrtab — 节名称字符串表
 *   name     — 要查找的节名称（如 ".symtab", ".strtab"）
 *
 * 返回: 指向 Elf32_Shdr 的指针，找不到返回 NULL
 * =================================================================== */
const Elf32_Shdr *elf_find_section(const Elf32_Shdr *shdrs, int count,
                                   const char *shstrtab, const char *name)
{
    for (int i = 0; i < count; i++) {
        if (shdrs[i].sh_name == 0) continue;  // 无名节（索引 0）
        if (strcmp(&shstrtab[shdrs[i].sh_name], name) == 0) {
            return &shdrs[i];
        }
    }
    return NULL;
}

/* ===================================================================
 * elf_get_section_name — 获取 Section 的名称字符串
 *
 * 参数:
 *   shdr     — Section Header
 *   shstrtab — 节名称字符串表
 *
 * 返回: 指向名称字符串的指针（不要 free）
 * =================================================================== */
const char *elf_get_section_name(const Elf32_Shdr *shdr,
                                 const char *shstrtab)
{
    if (!shstrtab || shdr->sh_name == 0) return "(unnamed)";
    return &shstrtab[shdr->sh_name];
}
