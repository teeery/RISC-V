/* ============================================================================
 * gen_hello_elf.c — 生成 Hello World RISC-V ELF32
 *
 * 在 x86 宿主机上编译运行，产出 hello.elf。
 * 不依赖 RISC-V 交叉编译器。
 *
 * 生成的程序逻辑：
 *   write(1, msg, 14)   → a7=64, a0=1, a1=msg, a2=14, ecall
 *   exit(0)             → a7=93, a0=0, ecall
 *
 * RISC-V Linux syscall 约定：
 *   a7 = syscall 编号 (64=write, 93=exit)
 *   a0 = fd / exit code
 *   a1 = buf
 *   a2 = len
 * ============================================================================ */

#include <stdio.h>
#include <stdint.h>

/* ── ELF 常量 ── */
#define EI_NIDENT  16
#define ELFCLASS32 1
#define ELFDATA2LSB 1
#define ET_EXEC    2
#define EM_RISCV   243
#define PT_LOAD    1
#define PF_R       4
#define PF_X       1

/* ── 小端序写入 ── */
static void w16(FILE *f, uint16_t v) { fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f); }
static void w32(FILE *f, uint32_t v) { fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f);
                                        fputc((v >> 16) & 0xFF, f); fputc((v >> 24) & 0xFF, f); }

int main(void)
{
    FILE *f = fopen("src/test/e2e/hello.elf", "wb");
    if (!f) { perror("fopen"); return 1; }

    /*
     * Hello World RISC-V 指令编码（共 9 条，36 字节）：
     *
     * 0x00010000:  addi x17, x0, 64    → 0x04000893   li a7, 64
     * 0x00010004:  addi x10, x0, 1     → 0x00100513   li a0, 1
     * 0x00010008:  lui  x11, 0x00010   → 0x000105B7   lui a1, 0x00010
     * 0x0001000C:  addi x11, x11, 0x24 → 0x02458593   addi a1, a1, 0x24
     * 0x00010010:  addi x12, x0, 14    → 0x00E00613   li a2, 14
     * 0x00010014:  ecall                → 0x00000073
     * 0x00010018:  addi x17, x0, 93    → 0x05D00893   li a7, 93
     * 0x0001001C:  addi x10, x0, 0     → 0x00000513   li a0, 0
     * 0x00010020:  ecall                → 0x00000073
     *
     * 0x00010024:  "Hello, World!\n"   (14 bytes)
     */

    uint32_t instrs[] = {
        0x04000893,  // addi a7, zero, 64
        0x00100513,  // addi a0, zero, 1
        0x000105B7,  // lui a1, 0x00010
        0x02458593,  // addi a1, a1, 0x24
        0x00E00613,  // addi a2, zero, 14
        0x00000073,  // ecall
        0x05D00893,  // addi a7, zero, 93
        0x00000513,  // addi a0, zero, 0
        0x00000073,  // ecall
    };

    const char *msg = "Hello, World!\n";
    uint32_t code_size = sizeof(instrs);      // 36 bytes
    uint32_t msg_size  = 14;                  // strlen("Hello, World!\n")
    uint32_t total_size = code_size + msg_size; // 50 bytes
    /* 4 字节对齐 */
    uint32_t filesz = (total_size + 3) & ~3u;  // 52 bytes
    uint32_t entry   = 0x00010000;

    /* ═══ ELF Header (52 bytes) ═══ */
    fputc(0x7F, f); fputc('E', f); fputc('L', f); fputc('F', f);
    fputc(ELFCLASS32, f); fputc(ELFDATA2LSB, f); fputc(1, f);
    for (int i = 7; i < EI_NIDENT; i++) fputc(0, f);
    w16(f, ET_EXEC);
    w16(f, EM_RISCV);
    w32(f, 1);           // e_version
    w32(f, entry);       // e_entry
    w32(f, 52);          // e_phoff
    w32(f, 0);           // e_shoff
    w32(f, 0);           // e_flags
    w16(f, 52);          // e_ehsize
    w16(f, 32);          // e_phentsize
    w16(f, 1);           // e_phnum
    w16(f, 0); w16(f, 0); w16(f, 0);

    /* ═══ Program Header (32 bytes) ═══ */
    w32(f, PT_LOAD);
    w32(f, 84);          // p_offset (ELF header 52 + program header 32)
    w32(f, entry);       // p_vaddr
    w32(f, entry);       // p_paddr
    w32(f, filesz);      // p_filesz
    w32(f, filesz);      // p_memsz
    w32(f, PF_R | PF_X); // p_flags
    w32(f, 0x1000);      // p_align

    /* ═══ 代码段：指令 + 消息 ═══ */
    for (int i = 0; i < 9; i++) w32(f, instrs[i]);  // 36 bytes 指令
    fwrite(msg, 1, msg_size, f);                     // 14 bytes 消息
    /* 对齐填充 */
    for (uint32_t i = total_size; i < filesz; i++) fputc(0, f);

    fclose(f);
    printf("Generated: src/test/e2e/hello.elf (%u bytes)\n", 52 + 32 + filesz);
    printf("  Entry: 0x%08x\n", entry);
    printf("  Instructions: 9 (36 bytes)\n");
    printf("  Message: \"%s\" (14 bytes)\n", msg);
    return 0;
}
