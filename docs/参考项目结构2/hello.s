# ============================================================
# hello.s — RISC-V RV32I 汇编测试程序
#
# 功能: 计算 1+2+3+...+10 = 55，然后调用 exit(0)
#
# 汇编与运行:
#   riscv64-unknown-elf-as  -march=rv32im -o hello.o hello.s
#   riscv64-unknown-elf-ld  -o hello.elf hello.o
#   或使用 clang:
#   clang --target=riscv32 -march=rv32im -nostdlib -o hello.elf hello.s
#
# 本程序不使用任何 libc，直接通过寄存器操作和 ECALL 退出
# ============================================================

    .section .text
    .globl _start
    .align 2

_start:
    # ----- 测试 1: 立即数加法 (ADDI) -----
    addi x5, x0, 0      # x5 = 0  (累加和 sum)
    addi x6, x0, 1      # x6 = 1  (循环变量 i)

loop:
    # ----- 测试 2: 加法 (ADD) -----
    add  x5, x5, x6     # sum = sum + i

    # ----- 测试 3: 立即数比较与分支 -----
    addi x6, x6, 1      # i = i + 1
    addi x7, x0, 10     # x7 = 10
    blt  x6, x7, loop   # if i <= 10: goto loop
    # 循环结束后 sum (x5) = 55 (1+2+...+10)

    # ----- 测试 4: 减法 (SUB) -----
    addi x10, x5, 0     # a0 = sum (55)
    addi x11, x0, 55    # x11 = 55
    sub  x12, x10, x11  # x12 = sum - 55 (应为 0)

    # ----- 测试 5: 逻辑运算 -----
    addi x13, x0, 0xFF  # x13 = 255
    andi x14, x13, 0x0F # x14 = 15
    ori  x15, x0,  0xAA # x15 = 0xAA
    xori x16, x15, 0xFF # x16 = 0xAA ^ 0xFF = 0x55

    # ----- 测试 6: 移位运算 -----
    addi x17, x0,  1
    slli x18, x17, 4    # x18 = 1 << 4 = 16
    srli x19, x18, 2    # x19 = 16 >> 2 = 4

    # ----- 测试 7: M 扩展乘法 -----
    # 取消注释以测试 M 扩展:
    # addi x20, x0, 6
    # addi x21, x0, 7
    # mul  x22, x20, x21  # x22 = 6 * 7 = 42

    # ----- 测试 8: LOAD/STORE -----
    # 取消注释以测试内存操作:
    # addi sp, x0, 0x1000   # 设置栈
    # addi x8, x0, 0xDEAD
    # sw   x8, 0(sp)        # 存储到栈
    # lw   x9, 0(sp)        # 从栈加载

    # ----- 退出 -----
    addi a0, x0, 0       # exit code = 0
    addi a7, x0, 93      # syscall number = SYS_exit (93)
    ecall                 # 触发系统调用

    # 如果 ecall 失败，无限循环
halt:
    j halt

    .section .data
    .align 2
result:
    .word 0               # 预留的结果存储空间
