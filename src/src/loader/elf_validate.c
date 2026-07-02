/* ============================================================================
 * elf_validate.c — ELF Header 校验
 * ============================================================================
 *
 * 职责：只做校验，不碰内存，不碰 MMU。纯函数，无副作用。
 *
 * 五重检查（按 ELF 规范规定的顺序）：
 *   1. Magic number  — e_ident[0..3] == {0x7F, 'E', 'L', 'F'}
 *      如果不是，说明文件根本不是 ELF 格式（可能是纯文本、图片等）
 *   2. 位数          — e_ident[EI_CLASS] == ELFCLASS32 (1)
 *      如果是 2（ELFCLASS64），说明是 64 位 ELF，我们的模拟器不支持
 *   3. 端序          — e_ident[EI_DATA] == ELFDATA2LSB (1)
 *      如果是 2（ELFDATA2MSB），说明是大端序，RISC-V 标准是小端序
 *   4. 目标架构      — e_machine == EM_RISCV (243)
 *      如果不是 243，说明这个 ELF 是给 x86/ARM/其他 CPU 的
 *   5. 文件类型      — e_type == ET_EXEC (2)
 *      如果是 1（可重定位.o），需要链接；如果是 3（共享库.so），需要动态链接器
 *      只有 2（可执行文件）能直接加载
 *
 * 设计原则：
 *   - 每个检查失败后打印具体原因（方便调试），然后立即返回 false
 *   - 不尝试修复或兼容非法文件（fail-fast 原则）
 *   - 检查顺序是从"最容易检测的格式错误"到"更具体的架构/类型错误"
 * ============================================================================
 */

#include "loader_internal.h"  // 已间接包含 elf_loader.h + 所有标准库 + 依赖模块

bool elf_validate_header(Elf32_Ehdr *ehdr)
{
    /*
     * 检查 1：魔数 {0x7F, 'E', 'L', 'F'}
     *
     * ELF 规范规定文件的前 4 字节必须是这 4 个值。
     * 0x7F 是 DEL 字符（ASCII 127），配合 'E' 'L' 'F' 组成独一无二的签名。
     * 绝大多数非 ELF 文件在这里就会被拦截。
     */
    if (ehdr->e_ident[EI_MAG0] != 0x7f ||
        ehdr->e_ident[EI_MAG1] != 'E'  ||
        ehdr->e_ident[EI_MAG2] != 'L'  ||
        ehdr->e_ident[EI_MAG3] != 'F') {
        fprintf(stderr, "Error: Not a valid ELF file\n");
        return false;
    }

    /*
     * 检查 2：必须是 32 位 ELF（ELFCLASS32 = 1）
     *
     * e_ident[EI_CLASS] 位于文件偏移 0x04 处：
     *   1 = ELFCLASS32（32 位地址空间，我们支持）
     *   2 = ELFCLASS64（64 位地址空间，不支持）
     * 64 位 ELF 的 Header 大小和字段偏移都不同，无法兼容。
     */
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS32) {
        fprintf(stderr, "Error: Only ELF32 is supported (got class %d)\n",
                ehdr->e_ident[EI_CLASS]);
        return false;
    }

    /*
     * 检查 3：必须是小端序（ELFDATA2LSB = 1）
     *
     * e_ident[EI_DATA] 位于文件偏移 0x05 处：
     *   1 = ELFDATA2LSB（小端序，低地址存低字节）
     *   2 = ELFDATA2MSB（大端序，低地址存高字节）
     * RISC-V 规范默认小端序，x86 宿主机也是小端序，匹配无需端序转换。
     */
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        fprintf(stderr, "Error: Only little-endian is supported\n");
        return false;
    }

    /*
     * 检查 4：必须是 RISC-V 架构（EM_RISCV = 243）
     *
     * e_machine 字段标识这个 ELF 是给哪种 CPU 的。
     * 常见的值：3=x86, 62=x86-64, 40=ARM, 243=RISC-V
     * 非 RISC-V 的二进制我们的模拟器无法执行（指令集不同）。
     */
    if (ehdr->e_machine != EM_RISCV) {
        fprintf(stderr, "Error: Not a RISC-V binary (machine=%d)\n",
                ehdr->e_machine);
        return false;
    }

    /*
     * 检查 5：必须是可执行文件（ET_EXEC = 2）
     *
     * e_type 字段标识文件类型：
     *   1 = ET_REL（可重定位 .o 文件，需要链接）
     *   2 = ET_EXEC（可执行文件，可以直接加载运行）
     *   3 = ET_DYN（共享库 .so，需要动态链接器）
     * 只有 ET_EXEC 可以直接加载——它的地址已经被链接器固定好了。
     */
    if (ehdr->e_type != ET_EXEC) {
        fprintf(stderr, "Error: Not an executable file (type=%d)\n",
                ehdr->e_type);
        return false;
    }

    /* 全部检查通过 */
    return true;
}
