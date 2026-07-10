/* ============================================================================
 * loader_internal.h — Loader 模块内部头文件
 * ============================================================================
 *
 * ── 作用 ───────────────────────────────────────────────────
 *
 *   这个头文件只在 src/src/loader/ 内部的 .c 文件之间共享。
 *   外部模块（Simulator / main.c / 其他队友）不应该也不需 include 它。
 *
 *   外部只需要 #include "loader/elf_loader.h" 即可调用 elf_load()。
 *
 * ── 设计原则 ───────────────────────────────────────────────
 *
 *   1. 对内可见：.c 文件之间的共享常量、函数声明放在这里
 *   2. 对外隐藏：外部模块通过 elf_loader.h 的 elf_load() 调用，
 *      看不到 validate、segment、stack 内部函数
 *   3. 不重复 include：elf_loader.h 已包含 memory/memory.h 和
 *      memory/mmu.h，本文件不再重复
 *
 * ── 函数可见性 ────────────────────────────────────────────
 *
 *   elf_load()                ← 对外（elf_loader.h 声明，elf_load.c 实现）
 *   elf_validate_header()     ← 对内（本文件声明，elf_validate.c 实现）
 *   elf_load_segment()        ← 对内（本文件声明，elf_segment.c 实现）
 *   elf_setup_stack()         ← 对内（本文件声明，elf_stack.c 实现）
 *
 * ── 常量 → 团队结论映射 ──────────────────────────────────
 *
 *   STACK_TOP_DEFAULT  = 0x07F00000  ← 恒等映射可用；完善版改为 0xC0000000
 *   STACK_SIZE_DEFAULT = 256KB
 *
 *   当前范围：0x07EC0000 ~ 0x07F00000（128MB 物理内存内）
 *   完善版范围：0xBFFC0000 ~ 0xC0000000（团队结论 7）
 * ============================================================================
 */

#ifndef LOADER_INTERNAL_H
#define LOADER_INTERNAL_H

/*
 * elf_loader.h 已经包含了 Loader 需要的所有外部依赖：
 *   - types.h           (PrivilegeLevel, ExceptionType, MEM_*, PTE_*,
 *                         mem_perm_to_pte_flags)
 *   - memory/memory.h   (PhysicalMemory, MemoryRegion, mem_*)
 *   - memory/mmu.h      (MMUState, mmu_*)
 *   - stdint.h, stdbool.h, stdio.h
 *   - ELF 类型 / 常量 / 结构体
 *
 * stdio.h 也在这里引入——FILE 类型（声明 elf_load_segment 需要）、
 * fprintf/stderr（各 .c 文件打印错误用）都依赖它。
 */
#include <stdio.h>
#include "loader/elf_loader.h"

/* ═══════════════════════════════════════════════════════════════
 * 内部常量
 * ═══════════════════════════════════════════════════════════════ */

/*
 * 栈顶地址
 *
 * 当前简化版（恒等映射）：0x07F00000，在 128MB 物理内存高位。
 * 完善版（MMU Sv32 后）：0xC0000000（团队结论 7，3GB 位置）。
 */
#define STACK_TOP_DEFAULT   0x07F00000  // 在 128MB 物理内存高位，Bare 模式可用

/*
 * 栈大小
 *
 * 256KB（0x40000），不随版本变化。
 * 当前范围：0x07EC0000 ~ 0x07F00000
 * 完善版范围：0xBFFC0000 ~ 0xC0000000
 */
#define STACK_SIZE_DEFAULT  (256 * 1024)

/* ═══════════════════════════════════════════════════════════════
 * 内部函数声明
 * ═══════════════════════════════════════════════════════════════
 *
 * 以下函数只在 src/src/loader/ 内部的 .c 文件之间调用，
 * 外部模块不应直接调用它们（没有声明在 elf_loader.h 中）。
 */

/*
 * elf_validate_header — 校验 ELF Header 的五重合法性
 *
 * 参数：ehdr — 已读入的 ELF Header 指针
 * 返回：true = 合法，false = 不合法（已打印错误原因）
 *
 * 实现：src/src/loader/elf_validate.c
 * 调用者：elf_load.c 的 elf_load() 步骤 3
 */
bool elf_validate_header(Elf32_Ehdr *ehdr);

/*
 * elf_load_segment — 加载单个 PT_LOAD 段到物理内存
 *
 * 参数：
 *   fp   — 已打开的 ELF 文件指针（定位到段数据用）
 *   phdr — 当前 Program Header（描述段的地址/大小/权限）
 *   pmem — 物理内存（段数据写入目标）
 *   mmu  — MMU 状态（当前简化版未使用，完善版用于 mmu_map_page）
 *
 * 返回：true = 加载成功，false = 加载失败（已打印错误原因）
 *
 * 实现：src/src/loader/elf_segment.c
 * 调用者：elf_load.c 的 elf_load() 步骤 4（循环内）
 */
bool elf_load_segment(FILE *fp, Elf32_Phdr *phdr,
                      PhysicalMemory *pmem, MMUState *mmu);

/*
 * elf_setup_stack — 在物理内存中分配并初始化用户栈
 *
 * 参数：
 *   pmem      — 物理内存（栈区域分配目标）
 *   stack_top — [输出] 栈顶地址（固定为 STACK_TOP_DEFAULT）
 *
 * 返回：true = 分配成功，false = 分配失败（mem_map 失败）
 *
 * 实现：src/src/loader/elf_stack.c
 * 调用者：elf_load.c 的 elf_load() 步骤 7
 */
bool elf_setup_stack(PhysicalMemory *pmem, uint32_t *stack_top);

/* ═══════════════════════════════════════════════════════════════
 * Section 解析（elf_section.c — 调试/符号表用）═══════════════
 * ═══════════════════════════════════════════════════════════════ */

/* 解析 ELF 文件的 Section Header Table */
int elf_parse_sections(const char *filename,
                       Elf32_Shdr *shdrs, int max,
                       char *shstrtab, int strsize);

/* 按名称查找 Section（如 ".symtab", ".strtab"）*/
const Elf32_Shdr *elf_find_section(const Elf32_Shdr *shdrs, int count,
                                   const char *shstrtab, const char *name);

/* 获取 Section 的名称字符串 */
const char *elf_get_section_name(const Elf32_Shdr *shdr,
                                 const char *shstrtab);

#endif /* LOADER_INTERNAL_H */
