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
 *   STACK_TOP_DEFAULT  = 0xC0000000  ← 结论 7（Loader → CPU 参数）
 *   STACK_SIZE_DEFAULT = 256KB       ← 结论 7
 *
 *   栈范围：0xBFFC0000 ~ 0xC0000000（0xC0000000 - 256KB ~ 0xC0000000）
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
 * 栈顶地址（团队结论 7）
 *
 * 0xC0000000 接近 32 位地址空间的 3GB 位置（真实 Linux 用户空间顶部）。
 * 从 0x00010000（代码段起点）到 0xBFFBFFFF（栈底）有约 3GB 空间，
 * 足够代码、数据、堆从低地址向高地址增长。
 */
#define STACK_TOP_DEFAULT   0xC0000000

/*
 * 栈大小（团队结论 7）
 *
 * 256KB（0x40000 字节），是大多数简单程序的合理默认值。
 * 如需更大栈，可在完善版中通过命令行参数或 ELF 元数据指定。
 * 栈范围：0xC0000000 - 256KB = 0xBFFC0000 ~ 0xC0000000
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

#endif /* LOADER_INTERNAL_H */
