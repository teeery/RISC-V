# ============================================================================
# tests/hello.s — 最小 RISC-V 汇编测试程序（手动编写，不需链接 libc）
# ============================================================================
#
# 汇编方法:
#   riscv64-linux-gnu-as -march=rv32im -mabi=ilp32 -o hello.o hello.s
#   riscv64-linux-gnu-ld -o hello hello.o
#
# 或者一步:
#   riscv64-linux-gnu-gcc -march=rv32im -mabi=ilp32 -nostdlib -static -o hello hello.s
#
# 这个汇编程序直接使用 write 和 exit 系统调用，不依赖 C 库。
# 是调试你们模拟器 syscall 功能的最小测试用例。
#
# 预期行为:
#   在 stdout 输出 "Hello, RISC-V!\n" (15 字节)
#   退出码: 0
#
# 在模拟器中运行:
#   rvsim hello
#   预期输出: Hello, RISC-V!
#   预期退出码: 0

    .section .text
    .globl _start

_start:
    # ---- write(1, msg, 15) ----
    li   a7, 64         # syscall 号: write = 64
    li   a0, 1          # fd = 1 (stdout)
    la   a1, msg        # buf = msg 的地址
    li   a2, 15         # count = 15
    ecall               # 触发系统调用

    # ---- exit(0) ----
    li   a7, 93         # syscall 号: exit = 93
    li   a0, 0          # 退出码 = 0
    ecall               # 触发系统调用

    # 如果 exit 失败，无限循环
    j .

    .section .rodata
msg:
    .string "Hello, RISC-V!\n"
