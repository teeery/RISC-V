/* ============================================================================
 * gen_minimal_elf.c — 最小 RISC-V ELF32 文件生成器
 * ============================================================================
 *
 * 用法（在 x86 宿主机上运行）：
 *   gcc gen_minimal_elf.c -o gen_minimal_elf
 *   ./gen_minimal_elf
 *   → 产出 minimal.elf
 *
 * 生成的 ELF 内容（addi a7,zero,93; addi a0,zero,42; ecall）：
 *   运行后 a7=93(SYS_exit), a0=42，触发 ecall → syscall handler 正常退出。
 *   这个程序的目的是给 Loader 测试提供一个合法的最小 ELF32 文件，
 *   同时也供 E2E 测试完整流水线使用。
 *
 * 文件布局（总 96 字节）：
 *   ┌─────────────────┐ 偏移 0x00
 *   │ ELF Header (52B) │
 *   ├─────────────────┤ 偏移 0x34 (52)
 *   │ Program Hdr (32B)│
 *   ├─────────────────┤ 偏移 0x54 (84)
 *   │ addi a7,zero,93 │  0x05D00893（4 字节，小端序）
 *   │ addi a0,zero,42 │  0x02A00513（4 字节，小端序）
 *   │ ecall           │  0x00000073（4 字节，小端序）
 *   └─────────────────┘ 偏移 0x60 (96)
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* ── ELF 常量（和 elf_loader.h 中的值一致）── */
#define EI_NIDENT  16
#define ELFCLASS32 1
#define ELFDATA2LSB 1
#define ET_EXEC    2
#define EM_RISCV   243
#define PT_LOAD    1
#define PF_R       4
#define PF_X       1

/* ── 写入小端序 16 位值 ── */
static void write16(FILE *f, uint16_t val) {
    fputc(val & 0xFF, f);
    fputc((val >> 8) & 0xFF, f);
}

/* ── 写入小端序 32 位值 ── */
static void write32(FILE *f, uint32_t val) {
    fputc(val & 0xFF, f);
    fputc((val >> 8) & 0xFF, f);
    fputc((val >> 16) & 0xFF, f);
    fputc((val >> 24) & 0xFF, f);
}

int main(void) {
    FILE *f = fopen("minimal.elf", "wb");
    if (!f) { perror("fopen"); return 1; }

    /*
     * ═══════════════════════════════════════════════
     * 第 1 部分：ELF Header（52 字节）
     * ═══════════════════════════════════════════════
     */

    /* e_ident[0..15] */
    fputc(0x7F, f);  // EI_MAG0
    fputc('E',  f);  // EI_MAG1
    fputc('L',  f);  // EI_MAG2
    fputc('F',  f);  // EI_MAG3
    fputc(ELFCLASS32,  f);  // EI_CLASS  = 1 (32-bit)
    fputc(ELFDATA2LSB, f);  // EI_DATA   = 1 (小端序)
    fputc(1,  f);           // EI_VERSION = 1
    for (int i = 7; i < EI_NIDENT; i++) fputc(0, f);  // 填充 0

    write16(f, ET_EXEC);     // e_type     = 2 (可执行)
    write16(f, EM_RISCV);    // e_machine  = 243 (RISC-V)
    write32(f, 1);           // e_version  = 1
    write32(f, 0x00010000);  // e_entry    = 入口虚拟地址
    write32(f, 52);          // e_phoff    = Program Header 偏移 (紧接 ELF Header)
    write32(f, 0);           // e_shoff    = 0 (无 Section Header)
    write32(f, 0);           // e_flags    = 0
    write16(f, 52);          // e_ehsize   = 52
    write16(f, 32);          // e_phentsize = 32
    write16(f, 1);           // e_phnum    = 1
    write16(f, 0);           // e_shentsize = 0
    write16(f, 0);           // e_shnum    = 0
    write16(f, 0);           // e_shstrndx = 0

    /*
     * ═══════════════════════════════════════════════
     * 第 2 部分：Program Header（32 字节）
     * ═══════════════════════════════════════════════
     */
    write32(f, PT_LOAD);       // p_type   = 1 (PT_LOAD)
    write32(f, 84);            // p_offset = 数据在文件中的偏移
    write32(f, 0x00010000);    // p_vaddr  = 虚拟地址
    write32(f, 0x00010000);    // p_paddr  = 物理地址（恒等映射）
    write32(f, 12);            // p_filesz = 文件中的数据大小（3 条指令 = 12 字节）
    write32(f, 12);            // p_memsz  = 内存中大小（无 .bss，= filesz）
    write32(f, PF_R | PF_X);   // p_flags  = 5 (R+X)
    write32(f, 0x1000);        // p_align  = 4KB 页对齐

    /*
     * ═══════════════════════════════════════════════
     * 第 3 部分：代码段数据（12 字节）
     * ═══════════════════════════════════════════════
     *
     * addi a7, zero, 93 → a7 = 93（SYS_exit）
     * addi a0, zero, 42 → a0 = 42（返回值寄存器）
     * ecall              → 触发系统调用
     *
     * RISC-V 指令编码（小端序）：
     *   addi x17, x0, 93  → 0x05D00893
     *   addi x10, x0, 42  → 0x02A00513
     *   ecall             → 0x00000073
     */
    write32(f, 0x05D00893);  // addi a7, zero, 93  (SYS_exit)
    write32(f, 0x02A00513);  // addi a0, zero, 42
    write32(f, 0x00000073);  // ecall

    fclose(f);
    printf("Generated minimal.elf (96 bytes)\n");
    printf("  Entry:  0x00010000\n");
    printf("  Segment: 0x00010000 (8 bytes, R+X)\n");
    return 0;
}
