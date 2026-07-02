/* ============================================================================
 * decode_test.c — cpu_decode + cpu_disasm 单元测试
 *
 * 用法：
 *   1. 在项目根目录执行：
 *      gcc -I src/include -I src/include/cpu -o build/decode_test \
 *          src/src/cpu/decode.c src/test/cpu/decode_test.c
 *      ./build/decode_test
 *
 *   2. 或者用 CMake（后面再配）
 *
 * 测试策略：手工构造已知的 32 位指令 → 调用 decode/disasm → 打印结果
 * 你不需要"预知"正确答案——先跑起来，看输出是否合理，再对照规范验证。
 * ============================================================================ */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>    // PRIx64 等宏

#include "cpu/decode.h"  // DecodedInstr, cpu_decode, cpu_disasm

/* ── 辅助函数：打印一条指令的解码结果 ────────────────────────── */
void test_decode(uint32_t instr, const char *desc)
{
    DecodedInstr d = cpu_decode(instr);

    printf("──────────────────────────────────────────────────\n");
    printf("测试: %s\n", desc);
    printf("  原始指令: 0x%08X\n", instr);
    printf("  ── 解码字段 ──\n");
    printf("  opcode  = 0x%02X\n", d.opcode);
    printf("  rd      = x%d\n",   d.rd);
    printf("  rs1     = x%d\n",   d.rs1);
    printf("  rs2     = x%d\n",   d.rs2);
    printf("  funct3  = 0x%X\n",  d.funct3);
    printf("  funct7  = 0x%02X\n", d.funct7);
    printf("  imm     = %d  (0x%X)\n", d.imm, (uint32_t)d.imm);

    /* 反汇编 */
    char disasm[128];
    cpu_disasm(instr, 0x80000000, disasm, sizeof(disasm));
    printf("  ── 反汇编 ──\n");
    printf("  0x80000000: %08X  %s\n", instr, disasm);
    printf("\n");
}

/* ── 辅助函数：测试反汇编字符串格式 ─────────────────────────── */
void test_disasm(uint32_t instr, uint32_t pc, const char *desc,
                 const char *expected)
{
    char disasm[128];
    cpu_disasm(instr, pc, disasm, sizeof(disasm));

    printf("[%s] ", desc);
    if (strcmp(disasm, expected) == 0) {
        printf("✅ PASS: %s\n", disasm);
    } else {
        printf("❌ FAIL:\n");
        printf("     期望: %s\n", expected);
        printf("     实际: %s\n", disasm);
    }
}

/* ====================================================================
 * 主测试入口
 * ==================================================================== */
int main(void)
{
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║       decode.c 单元测试                          ║\n");
    printf("╚══════════════════════════════════════════════════╝\n\n");

    /* ================================================================
     * 第一部分：cpu_decode — 字段提取测试
     *
     * 每个测试的目标指令是通过 RISC-V 编码表手工构造的，
     * 你需要确认 opcode/rd/rs1/rs2/funct3/imm 是否正确。
     * ================================================================ */

    printf("━━━ 第一部分：cpu_decode 字段提取 ━━━\n\n");

    /* ADDI x1, x0, 42
     * opcode=0x13, rd=1, rs1=0, funct3=0, imm=42
     * 机器码: imm[11:0]=42 | rs1=0 | 000 | rd=1 | 0010011
     *        = 000000101010_00000_000_00001_0010011
     *        = 0000 0000 0010 1010 0000 0000 1000 1001 0011
     *        = 0x02A00093  (注意符号扩展后 imm=42)
     *
     *   → 这道题你来算：写出完整的 32 位二进制，再转成十六进制，验证 0x02A00093 */
    test_decode(0x02A00093, "ADDI x1, x0, 42");

    /* ADD x3, x1, x2
     * opcode=0x33, rd=3, rs1=1, rs2=2, funct3=0, funct7=0x00
     * R-type 格式: funct7 | rs2 | rs1 | funct3 | rd | opcode
     *             = 0000000_00010_00001_000_00011_0110011
     *             = 0x002081B3
     *
     *   → 你来验证：为什么是 0x002081B3？
     *     提示：把各字段转成二进制，从高位到低位拼接 */
    test_decode(0x002081B3, "ADD x3, x1, x2");

    /* LUI x5, 0x12345
     * opcode=0x37, rd=5, imm=0x12345000
     * U-type: imm[31:12] | rd | opcode
     *        = 0x12345_00101_0110111
     *        = 0x123452B7 (20 位 imm 在左 + 5 位 rd + 7 位 opcode)
     *
     *   → 注意：U-type 输出 imm 的时候应该显示 0x12345000，不是 0x12345 */
    test_decode(0x123452B7, "LUI x5, 0x12345");

    /* LW x5, 4(x0)
     * opcode=0x03, rd=5, rs1=0, funct3=2, imm=4
     * I-type: imm[11:0] | rs1 | funct3 | rd | opcode
     *        = 000000000100_00000_010_00101_0000011
     *        = 0x00402283
     *
     *   → 练习：画出这条指令的 32 位布局 */
    test_decode(0x00402283, "LW x5, 4(x0)");

    /* ================================================================
     * 第二部分：cpu_disasm — 反汇编字符串测试
     *
     * 这里用精确匹配的方式测试——已知期望的字符串，对比实际输出。
     * ================================================================ */

    printf("━━━ 第二部分：cpu_disasm 字符串匹配 ━━━\n\n");

    /* 所有指令假设在 PC = 0x80000000 处执行 */

    test_disasm(0x02A00093, 0x80000000,
                "ADDI", "addi    ra, zero, 42");

    test_disasm(0x002081B3, 0x80000000,
                "ADD",  "add     gp, ra, sp");

    test_disasm(0x123452B7, 0x80000000,
                "LUI",  "lui     t0, 0x12345000");

    test_disasm(0x00402283, 0x80000000,
                "LW",   "lw      t0, 4(zero)");

    /* ================================================================
     * 第三部分：边界情况
     * ================================================================ */

    printf("━━━ 第三部分：边界情况 ━━━\n\n");

    /* 非法指令（全零） */
    test_decode(0x00000000, "非法指令（全零）");

    /* 非法指令（全 1） */
    test_decode(0xFFFFFFFF, "非法指令（全 1）");

    printf("══════════════════════════════════════════════════\n");
    printf("测试完成！检查上面的 PASS/FAIL，红色 FAIL 需要修。\n");
    printf("══════════════════════════════════════════════════\n");

    return 0;
}
