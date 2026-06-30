/* ============================================================================
 * loader_internal.h — Loader 模块内部头文件
 * ============================================================================
 *
 * 这个头文件只在 src/loader/ 内部使用，不对外暴露。
 * 外部模块只需要 include "elf_loader.h" 即可调用 elf_load()。
 */

#ifndef LOADER_INTERNAL_H
#define LOADER_INTERNAL_H

#include "elf_loader.h"
#include "memory.h"
#include "mmu.h"

/* ---- 常量 ---- */
#define STACK_TOP_DEFAULT   0x7FFFF000
#define STACK_SIZE_DEFAULT  (256 * 1024)  // 256 KB

/* ---- 内部函数声明 ---- */

/* elf_validate.c — 校验 ELF Header 合法性 */
bool elf_validate_header(Elf32_Ehdr *ehdr);

/* elf_segment.c — 加载单个 PT_LOAD 段到物理内存 */
bool elf_load_segment(FILE *fp, Elf32_Phdr *phdr,
                      PhysicalMemory *pmem, MMUState *mmu);

/* elf_stack.c — 初始化用户栈 */
bool elf_setup_stack(PhysicalMemory *pmem, uint32_t *stack_top);

#endif /* LOADER_INTERNAL_H */
