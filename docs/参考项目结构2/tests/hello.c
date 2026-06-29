/* ============================================================================
 * tests/hello.c — 最小 C 测试程序
 * ============================================================================
 *
 * 编译方法:
 *   riscv64-linux-gnu-gcc -march=rv32im -mabi=ilp32 -static -o hello hello.c
 *
 * 预期行为:
 *   输出 "Hello, RISC-V!" 并返回 0
 *
 * 在模拟器中运行:
 *   rvsim hello
 *   预期输出: Hello, RISC-V!
 *   预期退出码: 0
 *
 * 调试器测试:
 *   rvsim -d hello
 *   (rvsim) b main
 *   (rvsim) c
 *   (rvsim) s
 *   (rvsim) info registers
 */

#include <stdio.h>

int main() {
    printf("Hello, RISC-V!\n");
    return 0;
}
