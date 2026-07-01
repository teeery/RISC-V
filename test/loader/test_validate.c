/* ============================================================================
 * test_validate.c — L1：ELF Header 校验测试（纯单元测试，无 Simulator 依赖）
 * ============================================================================
 *
 * 编译（从项目根目录）：
 *   gcc -std=c11 -Wall -Wextra -Isrc/include \
 *       test/loader/test_validate.c \
 *       src/src/loader/elf_validate.c \
 *       -o build/test_validate && ./build/test_validate
 *
 * 注意：elf_validate_header() 是 Loader 内部函数（声明在 loader_internal.h），
 * 测试直接 include 内部头文件来调用它。这是少数允许的例外。
 */

#include <stdio.h>
#include <string.h>  // memset

/* 直接引用 Loader 内部头文件以测试内部函数 */
#include "loader_internal.h"  // elf_validate_header()

/* ── 测试辅助宏 ── */
#define TEST(name) \
    static int test_##name(void)

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: %s\n", msg); \
        return 1; \
    } \
} while(0)

/*
 * 构造一个合法的最小 Elf32_Ehdr
 * e_ident, e_type, e_machine 后续根据测试用例覆盖
 */
static Elf32_Ehdr make_valid_ehdr(void) {
    Elf32_Ehdr ehdr;
    memset(&ehdr, 0, sizeof(ehdr));

    /* 设置合法默认值：32 位小端 RISC-V 可执行文件 */
    ehdr.e_ident[EI_MAG0] = 0x7F;
    ehdr.e_ident[EI_MAG1] = 'E';
    ehdr.e_ident[EI_MAG2] = 'L';
    ehdr.e_ident[EI_MAG3] = 'F';
    ehdr.e_ident[EI_CLASS] = ELFCLASS32;   // 32 位
    ehdr.e_ident[EI_DATA]  = ELFDATA2LSB;  // 小端序
    ehdr.e_ident[EI_VERSION] = 1;
    ehdr.e_type    = ET_EXEC;     // 可执行文件
    ehdr.e_machine = EM_RISCV;    // RISC-V
    ehdr.e_entry   = 0x00010000;

    return ehdr;
}

/* ───────────────────────────────────────────────────────────
 * 测试 1：合法的 ELF32 RISC-V 可执行文件 → 通过
 * ─────────────────────────────────────────────────────────── */
TEST(valid_elf_passes) {
    Elf32_Ehdr ehdr = make_valid_ehdr();
    CHECK(elf_validate_header(&ehdr) == true,
          "valid ELF32 RISC-V executable should pass");
    printf("  PASS: valid ELF accepted\n");
    return 0;
}

/* ───────────────────────────────────────────────────────────
 * 测试 2：magic number 错误 → 拒绝
 * ─────────────────────────────────────────────────────────── */
TEST(bad_magic_rejected) {
    Elf32_Ehdr ehdr = make_valid_ehdr();

    /* 破坏每个 magic byte 逐一测试 */
    ehdr.e_ident[EI_MAG0] = 0x00;
    CHECK(elf_validate_header(&ehdr) == false, "MAG0=0 should fail");
    ehdr.e_ident[EI_MAG0] = 0x7F;

    ehdr.e_ident[EI_MAG1] = 'X';
    CHECK(elf_validate_header(&ehdr) == false, "MAG1='X' should fail");
    ehdr.e_ident[EI_MAG1] = 'E';

    ehdr.e_ident[EI_MAG2] = 'X';
    CHECK(elf_validate_header(&ehdr) == false, "MAG2='X' should fail");
    ehdr.e_ident[EI_MAG2] = 'L';

    ehdr.e_ident[EI_MAG3] = 'X';
    CHECK(elf_validate_header(&ehdr) == false, "MAG3='X' should fail");

    printf("  PASS: bad magic rejected (4/4)\n");
    return 0;
}

/* ───────────────────────────────────────────────────────────
 * 测试 3：64 位 ELF → 拒绝
 * ─────────────────────────────────────────────────────────── */
TEST(class64_rejected) {
    Elf32_Ehdr ehdr = make_valid_ehdr();
    ehdr.e_ident[EI_CLASS] = ELFCLASS64;  // 2
    CHECK(elf_validate_header(&ehdr) == false,
          "64-bit ELF should be rejected");
    printf("  PASS: ELFCLASS64 rejected\n");
    return 0;
}

/* ───────────────────────────────────────────────────────────
 * 测试 4：大端序 ELF → 拒绝
 * ─────────────────────────────────────────────────────────── */
TEST(big_endian_rejected) {
    Elf32_Ehdr ehdr = make_valid_ehdr();
    ehdr.e_ident[EI_DATA] = ELFDATA2MSB;  // 2
    CHECK(elf_validate_header(&ehdr) == false,
          "big-endian ELF should be rejected");
    printf("  PASS: ELFDATA2MSB rejected\n");
    return 0;
}

/* ───────────────────────────────────────────────────────────
 * 测试 5：非 RISC-V 架构 → 拒绝
 * ─────────────────────────────────────────────────────────── */
TEST(wrong_machine_rejected) {
    Elf32_Ehdr ehdr = make_valid_ehdr();

    ehdr.e_machine = 3;   // EM_386 (x86)
    CHECK(elf_validate_header(&ehdr) == false,
          "x86 ELF should be rejected");
    printf("  PASS: x86 machine rejected\n");

    ehdr.e_machine = 62;  // EM_X86_64
    CHECK(elf_validate_header(&ehdr) == false,
          "x86-64 ELF should be rejected");
    printf("  PASS: x86-64 machine rejected\n");

    return 0;
}

/* ───────────────────────────────────────────────────────────
 * 测试 6：非可执行文件 → 拒绝
 * ─────────────────────────────────────────────────────────── */
TEST(wrong_type_rejected) {
    Elf32_Ehdr ehdr = make_valid_ehdr();

    ehdr.e_type = ET_REL;   // 1 (.o 文件)
    CHECK(elf_validate_header(&ehdr) == false,
          ".o relocatable should be rejected");
    printf("  PASS: ET_REL rejected\n");

    ehdr.e_type = ET_DYN;   // 3 (.so 共享库)
    CHECK(elf_validate_header(&ehdr) == false,
          ".so shared library should be rejected");
    printf("  PASS: ET_DYN rejected\n");

    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * 主入口
 * ═══════════════════════════════════════════════════════════ */
int main(void) {
    int failed = 0;

    printf("=== L1: ELF Header Validation Tests ===\n\n");

    failed += test_valid_elf_passes();
    failed += test_bad_magic_rejected();
    failed += test_class64_rejected();
    failed += test_big_endian_rejected();
    failed += test_wrong_machine_rejected();
    failed += test_wrong_type_rejected();

    printf("\n=== Results: %d test(s) failed ===\n", failed);
    return failed ? 1 : 0;
}
