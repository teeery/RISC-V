/* ============================================================================
 * elf_validate.c — ELF Header 校验
 * ============================================================================
 *
 * 职责：只做校验，不碰内存，不碰 MMU。
 *       对 ELF Header 进行五重检查，任一失败即返回 false。
 *
 * 五重检查：
 *   1. Magic number:  e_ident[0..3] == {0x7F, 'E', 'L', 'F'}
 *   2. 位数:          e_ident[4] == 1 (ELFCLASS32)
 *   3. 端序:          e_ident[5] == 1 (ELFDATA2LSB)
 *   4. 目标架构:      e_machine == EM_RISCV (243)
 *   5. 文件类型:      e_type == ET_EXEC (2, 可执行文件)
 */

#include <stdio.h>
#include "elf_loader.h"
#include "loader_internal.h"

bool elf_validate_header(Elf32_Ehdr *ehdr)
{
    /* 1. 魔数检查 */
    if (ehdr->e_ident[0] != 0x7f ||
        ehdr->e_ident[1] != 'E'  ||
        ehdr->e_ident[2] != 'L'  ||
        ehdr->e_ident[3] != 'F') {
        fprintf(stderr, "Error: Not a valid ELF file\n");
        return false;
    }

    /* 2. 32 位检查 */
    if (ehdr->e_ident[4] != 1) {  // ELFCLASS32
        fprintf(stderr, "Error: Only ELF32 is supported (got class %d)\n",
                ehdr->e_ident[4]);
        return false;
    }

    /* 3. 小端序检查 */
    if (ehdr->e_ident[5] != 1) {  // ELFDATA2LSB
        fprintf(stderr, "Error: Only little-endian is supported\n");
        return false;
    }

    /* 4. 目标架构检查 */
    if (ehdr->e_machine != EM_RISCV) {
        fprintf(stderr, "Error: Not a RISC-V binary (machine=%d)\n",
                ehdr->e_machine);
        return false;
    }

    /* 5. 可执行文件类型检查 */
    if (ehdr->e_type != ET_EXEC) {
        fprintf(stderr, "Error: Not an executable file (type=%d)\n",
                ehdr->e_type);
        return false;
    }

    return true;
}
