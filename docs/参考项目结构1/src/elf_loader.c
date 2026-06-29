#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ============================================================
 * elf_loader.c — ELF32 可执行文件加载器
 *
 * ─── 你需要实现的内容 ─────────────────────────────────────
 *
 * 1. elf_load(filename, pmem, mmu, entry, stack_top)
 *    完整的 ELF 加载流程：
 *
 *    a) 打开文件 (fopen)
 *    b) 读取并验证 ELF Header
 *       - 魔数检查: e_ident[0..3] == {0x7f, 'E', 'L', 'F'}
 *       - 类别检查: e_ident[4] == 1 (ELF32) 或 2 (ELF64)
 *       - 架构检查: e_machine == EM_RISCV (243)
 *       - 类型检查: e_type == ET_EXEC (2)
 *    c) 遍历 Program Header Table
 *       - 计算每个 LOAD 段的分配需求
 *       - 在物理内存中分配空间 (mem_find_free)
 *       - 建立虚拟地址→物理地址映射 (mmu_map_page)
 *       - 从文件读取段数据写入物理内存 (mem_load)
 *       - 对于 p_memsz > p_filesz 的部分填零 (.bss)
 *    d) 设置入口地址 (*entry = ehdr->e_entry)
 *    e) 设置用户栈 (可选: 栈顶默认 0x7FFFF000)
 *       - 在栈上放置 argc, argv, envp, auxv
 *       - 设置 sp 寄存器
 *
 * 2. elf_parse_program_headers()
 *    解析 Program Header 条目，提取所需信息：
 *    - p_type=PT_LOAD: 需要加载的段
 *    - p_vaddr: 虚拟地址 (用于建立 MMU 映射)
 *    - p_filesz: 文件中实际数据大小
 *    - p_memsz: 内存中占用大小 (bss 段会大于文件大小)
 *    - p_flags: PF_R(4)|PF_W(2)|PF_X(1) → 映射到 MMU 的 R/W/X
 *
 * 3. 辅助功能:
 *    - 从 Section Header 中读取符号名称 (用于反汇编)
 *    - 可选: 解析 .symtab 和 .strtab 获取函数名→地址映射
 *
 * ─── ELF32 Header 详细字段 ─────────────────────────────────
 *   偏移  大小  字段
 *   0x00   4    e_ident[0..3]: 魔数 {0x7f, 'E', 'L', 'F'}
 *   0x04   1    e_ident[4]: 类别 (1=32bit, 2=64bit)
 *   0x05   1    e_ident[5]: 数据编码 (1=小端, 2=大端)
 *   0x10   2    e_type: 文件类型 (1=reloc, 2=exec, 3=shared)
 *   0x12   2    e_machine: 目标架构 (EM_RISCV=243)
 *   0x18   4    e_entry: 程序入口虚拟地址
 *   0x1C   4    e_phoff: Program Header 表偏移
 *   0x2A   2    e_phnum: Program Header 条目数
 *
 * ─── Program Header 详细字段 (ELF32) ────────────────────────
 *   偏移  大小  字段
 *   0x00   4    p_type: 段类型
 *   0x04   4    p_offset: 段在文件中的偏移
 *   0x08   4    p_vaddr: 虚拟地址
 *   0x0C   4    p_paddr: 物理地址
 *   0x10   4    p_filesz: 段在文件中的大小
 *   0x14   4    p_memsz: 段在内存中的大小
 *   0x18   4    p_flags: 段标志 (读=4, 写=2, 执行=1)
 *   0x1C   4    p_align: 对齐
 *
 * ─── 栈布局 (可选，启动用户态程序时需要) ────────────────────
 *   高位地址:
 *     [ environment strings ]
 *     [ argument strings     ]
 *     [ auxv array          ]  ← AT_NULL 结束
 *     [ argv array          ]  ← NULL 结束
 *     [ argc                ]  ← sp 指向
 *   低位地址 (栈底):
 *   标准 auxv 条目: AT_PAGESZ=4096, AT_PHDR, AT_PHENT, AT_PHNUM
 * ============================================================
 */

#include "types.h"
#include "memory.h"
#include "mmu.h"
#include "elf_loader.h"

#define STACK_TOP_DEFAULT   0x7FFFF000
#define STACK_SIZE_DEFAULT  (256 * 1024)  // 256 KB

/* ── ELF Header 解析 ─────────────────────────────────────── */

static bool elf_validate_header(Elf32_Ehdr *ehdr)
{
    // 魔数检查
    if (ehdr->e_ident[0] != 0x7f ||
        ehdr->e_ident[1] != 'E'  ||
        ehdr->e_ident[2] != 'L'  ||
        ehdr->e_ident[3] != 'F') {
        fprintf(stderr, "Error: Not a valid ELF file\n");
        return false;
    }
    // 32 位检查
    if (ehdr->e_ident[4] != 1) {  // ELFCLASS32
        fprintf(stderr, "Error: Only ELF32 is supported (got class %d)\n",
                ehdr->e_ident[4]);
        return false;
    }
    // 小端检查
    if (ehdr->e_ident[5] != 1) {  // ELFDATA2LSB
        fprintf(stderr, "Error: Only little-endian is supported\n");
        return false;
    }
    // 架构检查
    if (ehdr->e_machine != EM_RISCV) {
        fprintf(stderr, "Error: Not a RISC-V binary (machine=%d)\n",
                ehdr->e_machine);
        return false;
    }
    // 可执行文件检查
    if (ehdr->e_type != ET_EXEC) {
        fprintf(stderr, "Error: Not an executable file (type=%d)\n",
                ehdr->e_type);
        return false;
    }
    return true;
}

/* ── Program Header 加载 ─────────────────────────────────── */

static bool elf_load_segment(FILE *fp, Elf32_Phdr *phdr,
                             PhysicalMemory *pmem, MMUState *mmu)
{
    // TODO: 为这个段分配物理内存
    // 1. 在物理内存中找空闲区域
    //    uint32_t paddr = mem_find_free(pmem, phdr->p_memsz, phdr->p_align);
    // 2. 建立 MMU 虚拟地址映射
    //    mmu_map_page(mmu, phdr->p_vaddr, paddr, flags)
    // 3. 从文件读取数据到 pmem->data[paddr..paddr+p_filesz]
    // 4. 对 p_memsz > p_filesz 部分填零

    // 简化实现：使用虚拟地址作为物理地址 (恒等映射)
    uint32_t paddr = phdr->p_vaddr;

    // 分配空间需要确保物理内存足够大
    if (paddr + phdr->p_memsz > pmem->size) {
        fprintf(stderr, "Error: Segment exceeds physical memory\n");
        return false;
    }

    // 映射区域
    uint8_t flags = 0;
    if (phdr->p_flags & PF_R) flags |= MEM_READ;
    if (phdr->p_flags & PF_W) flags |= MEM_WRITE;
    if (phdr->p_flags & PF_X) flags |= MEM_EXEC;
    mem_map(pmem, paddr, phdr->p_memsz, flags, "segment");

    // 从文件读取数据
    if (phdr->p_filesz > 0) {
        fseek(fp, phdr->p_offset, SEEK_SET);
        size_t n = fread(pmem->data + paddr, 1, phdr->p_filesz, fp);
        if (n != phdr->p_filesz) {
            fprintf(stderr, "Error: Failed to read segment data\n");
            return false;
        }
    }

    // .bss 清零
    if (phdr->p_memsz > phdr->p_filesz) {
        memset(pmem->data + paddr + phdr->p_filesz, 0,
               phdr->p_memsz - phdr->p_filesz);
    }

    return true;
}

/* ── 主加载函数 ──────────────────────────────────────────── */

bool elf_load(const char *filename, PhysicalMemory *pmem, MMUState *mmu,
              uint32_t *entry, uint32_t *stack_top)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("Failed to open ELF file");
        return false;
    }

    // 读取 ELF Header
    Elf32_Ehdr ehdr;
    if (fread(&ehdr, sizeof(ehdr), 1, fp) != 1) {
        fprintf(stderr, "Error: Failed to read ELF header\n");
        fclose(fp);
        return false;
    }

    // 验证
    if (!elf_validate_header(&ehdr)) {
        fclose(fp);
        return false;
    }

    // 遍历 Program Headers
    // TODO: 读取所有 Program Header，调用 elf_load_segment
    // 需要 seek 到 ehdr.e_phoff，然后逐个读取 ehdr.e_phnum 个 Phdr

    // 暂时简化：遍历
    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf32_Phdr phdr;
        long offset = ehdr.e_phoff + i * sizeof(Elf32_Phdr);
        fseek(fp, offset, SEEK_SET);
        if (fread(&phdr, sizeof(phdr), 1, fp) != 1) {
            fprintf(stderr, "Error: Failed to read program header %d\n", i);
            fclose(fp);
            return false;
        }

        if (phdr.p_type == PT_LOAD) {
            if (!elf_load_segment(fp, &phdr, pmem, mmu)) {
                fclose(fp);
                return false;
            }
        }
    }

    fclose(fp);

    // 设置入口
    *entry = ehdr.e_entry;

    // 设置栈
    *stack_top = STACK_TOP_DEFAULT;
    mem_map(pmem, STACK_TOP_DEFAULT - STACK_SIZE_DEFAULT, STACK_SIZE_DEFAULT,
            MEM_READ | MEM_WRITE, "stack");

    printf("ELF loaded: entry=0x%08x, stack=0x%08x\n", *entry, *stack_top);
    return true;
}
