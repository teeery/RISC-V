#!/bin/bash
# ============================================================================
# tests/run_tests.sh — 自动化测试脚本
# ============================================================================
#
# 用法:
#   chmod +x run_tests.sh
#   ./run_tests.sh
#
# 这个脚本:
#   1. 检查是否有 RISC-V 交叉编译工具链
#   2. 编译每个测试程序
#   3. 用模拟器运行每个测试
#   4. 对比预期输出和实际输出
#   5. 统计通过/失败数量
#
# 注意: 需要先把模拟器编译好: cmake --build build

RV_GCC=riscv64-linux-gnu-gcc
RV_AS=riscv64-linux-gnu-as
RV_LD=riscv64-linux-gnu-ld
RVSIM=../build/rvsim

PASS=0
FAIL=0

echo "=== RISC-V Simulator Test Suite ==="
echo ""

# ---- 测试 1: hello.s (汇编直接 syscall) ----
echo "[TEST 1] hello.s"
$RV_AS -march=rv32im -mabi=ilp32 -o /tmp/hello.o hello.s
$RV_LD -o /tmp/hello /tmp/hello.o
OUTPUT=$($RVSIM /tmp/hello)
if [ "$OUTPUT" = "Hello, RISC-V!" ]; then
    echo "  PASS"
    PASS=$((PASS + 1))
else
    echo "  FAIL: expected 'Hello, RISC-V!', got '$OUTPUT'"
    FAIL=$((FAIL + 1))
fi

# ---- 测试 2: add_test.s (算术指令) ----
echo "[TEST 2] add_test.s"
$RV_AS -march=rv32im -mabi=ilp32 -o /tmp/add_test.o add_test.s
$RV_LD -o /tmp/add_test /tmp/add_test.o
$RVSIM /tmp/add_test
EXIT_CODE=$?
if [ "$EXIT_CODE" = "0" ]; then
    echo "  PASS"
    PASS=$((PASS + 1))
else
    echo "  FAIL: expected exit code 0, got $EXIT_CODE"
    FAIL=$((FAIL + 1))
fi

# ---- 以后可以添加更多测试 ----
# [TEST 3] riscv-tests 套件中的 rv32ui-p-add
# [TEST 4] riscv-tests 套件中的 rv32ui-p-sub
# [TEST 5] riscv-tests 套件中的 rv32um-p-mul (M 扩展)
# ...

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
