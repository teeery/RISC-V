/* ============================================================================
 * elf_stack.c — 用户栈初始化
 * ============================================================================
 *
 * 职责：为加载的用户程序设置栈空间。
 *
 * 栈布局（高地址 → 低地址）：
 *   ┌──────────────────────┐  stack_top = 0x7FFFF000
 *   │  environment strings │  ← TODO: 完善版添加
 *   │  argument strings    │  ← TODO: 完善版添加
 *   │  auxv array          │  ← TODO: AT_PHDR, AT_PHENT, AT_PHNUM, AT_PAGESZ
 *   │  argv array          │  ← TODO: 完善版添加
 *   │  argc                │  ← TODO: 完善版添加
 *   ├──────────────────────┤
 *   │  stack data ...      │
 *   └──────────────────────┘  stack_base = 0x7FFFF000 - 256KB
 *
 * 当前简化实现：仅分配栈空间，不放置 argc/argv/auxv。
 * 这对简单的 hello 程序已经足够。
 */

#include <stdio.h>
#include "memory.h"
#include "loader_internal.h"

bool elf_setup_stack(PhysicalMemory *pmem, uint32_t *stack_top)
{
    uint32_t stack_base = STACK_TOP_DEFAULT - STACK_SIZE_DEFAULT;

    /* 分配栈区域（可读可写，不可执行） */
    if (!mem_map(pmem, stack_base, STACK_SIZE_DEFAULT,
                 MEM_READ | MEM_WRITE, "stack")) {
        fprintf(stderr, "Error: Failed to allocate stack region\n");
        return false;
    }

    *stack_top = STACK_TOP_DEFAULT;

    /*
     * TODO（完善版）：在栈上放置辅助信息
     *
     * 标准 RISC-V 用户态程序启动时，栈顶应包含：
     *   [envp strings] [argv strings] [auxv] [argv] [argc]
     * 其中 auxv 至少需要：
     *   AT_PAGESZ  = 4096
     *   AT_PHDR    = ELF program headers 的虚拟地址
     *   AT_PHENT   = sizeof(Elf32_Phdr) = 32
     *   AT_PHNUM   = program header 的数量
     *   AT_NULL    = 0（结束标记）
     */

    return true;
}
