#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include "types.h"
#include "memory.h"
#include "mmu.h"

/* ============================================================
 * elf_loader.h — ELF32/ELF64 文件解析与加载器
 *
 * 需要编写的内容：
 * 1. elf_load()           — 加载 ELF 文件，返回入口地址
 * 2. elf_parse_header()   — 解析 ELF Header (e_ident, e_type, e_machine, e_entry)
 * 3. elf_load_segments()  — 遍历 Program Header Table，加载 LOAD 段到内存
 * 4. elf_setup_stack()    — 设置用户栈 (argc, argv, envp, auxv)
 * 5. elf_validate()       — 校验 ELF 魔数 (0x7f 'E' 'L' 'F') 和架构 (EM_RISCV=243)
 *
 * ELF 结构概述：
 * ┌─────────────────────┐
 * │   ELF Header        │  ← e_ident[16], e_entry, e_phoff, e_shoff, ...
 * ├─────────────────────┤
 * │   Program Headers   │  ← p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags
 * ├─────────────────────┤
 * │   Section Headers   │  ← 调试信息，运行时不需要
 * ├─────────────────────┤
 * │   Segment Data      │  ← .text (RX), .rodata (R), .data/.bss (RW)
 * └─────────────────────┘
 *
 * 设计要点：
 * - 优先支持 ELF32 (RV32)，兼顾 ELF64 识别
 * - 只加载 PT_LOAD 类型的段，忽略其他 (NOTE, TLS, GNU_STACK 等)
 * - p_memsz > p_filesz 时，多余部分清零 (.bss 段)
 * - 使用 mmu_map_page() 为每个段建立虚拟地址映射
 * - 栈默认放在高地址 (如 0x7FFFF000)，向下增长
 * - 辅助向量 (auxv) 至少包含 AT_PHDR, AT_PHENT, AT_PHNUM, AT_PAGESZ
 * ============================================================
 */

#include <stdint.h>

/* ----- ELF32 类型定义 ----- */
typedef uint32_t Elf32_Addr;
typedef uint32_t Elf32_Off;
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Word;
typedef int32_t  Elf32_Sword;

#define EI_NIDENT    16
#define EM_RISCV     243       // RISC-V 机器码
#define ET_EXEC      2         // 可执行文件
#define PT_LOAD      1         // 可加载段
#define PT_GNU_STACK 0x6474e551
#define PF_X         1
#define PF_W         2
#define PF_R         4

/* ELF32 Header */
typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    Elf32_Half e_type;
    Elf32_Half e_machine;
    Elf32_Word e_version;
    Elf32_Addr e_entry;
    Elf32_Off  e_phoff;       // Program Header 偏移
    Elf32_Off  e_shoff;       // Section Header 偏移
    Elf32_Word e_flags;
    Elf32_Half e_ehsize;
    Elf32_Half e_phentsize;   // Program Header 条目大小
    Elf32_Half e_phnum;       // Program Header 条目数
    Elf32_Half e_shentsize;
    Elf32_Half e_shnum;
    Elf32_Half e_shstrndx;
} Elf32_Ehdr;

/* ELF32 Program Header */
typedef struct {
    Elf32_Word p_type;       // 段类型 (1=PT_LOAD)
    Elf32_Off  p_offset;     // 在文件中的偏移
    Elf32_Addr p_vaddr;      // 虚拟地址
    Elf32_Addr p_paddr;      // 物理地址
    Elf32_Word p_filesz;     // 文件中的大小
    Elf32_Word p_memsz;      // 内存中的大小
    Elf32_Word p_flags;      // 权限 (R=4, W=2, X=1)
    Elf32_Word p_align;      // 对齐要求
} Elf32_Phdr;

/* ----- 加载器接口 ----- */

/*
 * 加载 ELF 文件到内存
 * 参数:
 *   filename  - ELF 文件路径
 *   pmem      - 物理内存
 *   mmu       - MMU 状态 (用于建立虚拟映射)
 *   entry     - [输出] 程序入口地址
 *   stack_top - [输出] 栈顶地址
 * 返回: 成功返回 true
 */
bool elf_load(const char *filename, PhysicalMemory *pmem, MMUState *mmu,
              uint32_t *entry, uint32_t *stack_top);

#endif // ELF_LOADER_H
