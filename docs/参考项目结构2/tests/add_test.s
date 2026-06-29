# ============================================================================
# tests/add_test.s — 测试 RV32I 基本算术指令
# ============================================================================
#
# 测试指令: addi, add, sub, beq
#
# 汇编:
#   riscv64-linux-gnu-as -march=rv32im -mabi=ilp32 -o add_test.o add_test.s
#   riscv64-linux-gnu-ld -o add_test add_test.o
#
# 预期: 如果所有测试通过 → exit(0), 如果失败 → exit(1)

    .section .text
    .globl _start

_start:
    # ---- 测试 1: ADDI ----
    li    t0, 0
    addi  t1, t0, 42         # t1 应该 = 42
    li    t2, 42
    bne   t1, t2, fail

    # ---- 测试 2: ADD ----
    li    t0, 100
    li    t1, 200
    add   t2, t0, t1          # t2 应该 = 300
    li    t3, 300
    bne   t2, t3, fail

    # ---- 测试 3: SUB ----
    li    t0, 100
    li    t1, 30
    sub   t2, t0, t1          # t2 应该 = 70
    li    t3, 70
    bne   t2, t3, fail

    # ---- 测试 4: 符号扩展 ----
    li    t0, -1
    addi  t1, x0, -1          # t1 应该也是 -1 (0xFFFFFFFF)
    bne   t0, t1, fail

    # ---- 全部通过 ----
    li    a7, 93              # exit
    li    a0, 0               # 成功
    ecall

fail:
    li    a7, 93              # exit
    li    a0, 1               # 失败
    ecall
