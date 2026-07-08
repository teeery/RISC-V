/* ============================================================================
 * test_syscall.c — Linux syscall 模块独立测试
 * ============================================================================
 *
 * 编译（从项目根目录）：
 *   gcc -std=c11 -Wall -Wextra -Isrc/include -Isrc/src/loader \
 *       src/test/linux/test_syscall.c \
 *       src/src/memory/memory.c \
 *       src/src/memory/mmu.c \
 *       src/src/linux/syscall.c \
 *       src/src/simulator.c \
 *       src/src/cpu/cpu.c \
 *       src/src/cpu/decode.c \
 *       -o build/test_linux && ./build/test_linux
 *
 * 测试目标：
 *   - exit(0) / exit(42)：验证 running 被设为 false
 *   - write(1, "Hello", 5)：验证 stdout 输出 + 返回值
 *   - brk(0)：验证返回当前 brk（不改变堆）
 *   - brk(new)：验证堆扩展
 *   - 未知 syscall：验证返回 -1
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "linux/syscall.h"

/* ── 辅助函数：构造一个最小可用的 Simulator ── */
static void init_sim(Simulator *sim)
{
    memset(sim, 0, sizeof(*sim));
    mem_init(&sim->pmem, MEM_SIZE_DEFAULT);
    mmu_init(&sim->mmu);
    sim->cpu.priv = PRIV_MACHINE;
    sim->cpu.running = true;
}

static void destroy_sim(Simulator *sim)
{
    mem_destroy(&sim->pmem);
    mmu_destroy(&sim->mmu);
}

/* ── 辅助函数：向虚拟内存写入字符串 ── */
static void write_string_to_memory(Simulator *sim, uint32_t vaddr, const char *s)
{
    ExceptionType exc = EXC_NONE;
    uint32_t len = (uint32_t)strlen(s);
    for (uint32_t i = 0; i < len; i++) {
        mmu_write_8(&sim->mmu, &sim->pmem, vaddr + i, (uint8_t)s[i],
                     PRIV_MACHINE, &exc);
    }
}

#define FAIL(msg) do { \
    fprintf(stderr, "  FAIL: %s\n", msg); \
    failed++; \
} while(0)

#define PASS(msg) printf("  PASS: %s\n", msg)

/* ═══════════════════════════════════════════════════════════════
 * 测试 1：exit(0) — running 变 false，a0 保持不变（不用设返回值）
 * ═══════════════════════════════════════════════════════════════ */
static int test_exit(void)
{
    int failed = 0;
    printf("\n--- test_exit ---\n");

    Simulator sim;
    init_sim(&sim);

    /* 设置 syscall 参数：exit(42) */
    sim.cpu.regs[REG_A7] = SYS_exit;
    sim.cpu.regs[REG_A0] = 42;
    sim.cpu.running = true;

    syscall_handler(&sim);

    /* exit 应该把 running 置 false */
    if (sim.cpu.running != false)
        FAIL("exit should set running=false");
    else
        PASS("exit(42) → running=false");

    destroy_sim(&sim);
    return failed;
}

/* ═══════════════════════════════════════════════════════════════
 * 测试 2：write(1, buf, len) — stdout 输出 + 返回值
 * ═══════════════════════════════════════════════════════════════ */
static int test_write(void)
{
    int failed = 0;
    printf("\n--- test_write ---\n");

    Simulator sim;
    init_sim(&sim);

    /* 在虚拟地址 0x10000 处写入测试字符串 */
    const char *msg = "Hello, RISC-V syscall!";
    uint32_t buf_addr = 0x00010000;
    write_string_to_memory(&sim, buf_addr, msg);

    /* 设置 syscall 参数：write(1, buf, len) */
    sim.cpu.regs[REG_A7] = SYS_write;
    sim.cpu.regs[REG_A0] = STDOUT_FILENO;
    sim.cpu.regs[REG_A1] = buf_addr;
    sim.cpu.regs[REG_A2] = (uint32_t)strlen(msg);

    printf("  Expected stdout output: \"%s\"\n", msg);
    printf("  Actual   stdout output: \"");

    /* 调用 syscall_handler——它会 fputc 到 stdout */
    syscall_handler(&sim);

    printf("\"\n");

    /* 验证返回值 = 实际写入字节数 */
    uint32_t result = sim.cpu.regs[REG_A0];
    if (result != (uint32_t)strlen(msg))
        FAIL("write should return actual byte count");
    else
        PASS("write returns correct byte count");

    destroy_sim(&sim);
    return failed;
}

/* ═══════════════════════════════════════════════════════════════
 * 测试 3：write 到非法 fd → 返回 -1
 * ═══════════════════════════════════════════════════════════════ */
static int test_write_bad_fd(void)
{
    int failed = 0;
    printf("\n--- test_write_bad_fd ---\n");

    Simulator sim;
    init_sim(&sim);

    sim.cpu.regs[REG_A7] = SYS_write;
    sim.cpu.regs[REG_A0] = 99;   // 非法 fd
    sim.cpu.regs[REG_A1] = 0x10000;
    sim.cpu.regs[REG_A2] = 10;

    syscall_handler(&sim);

    if ((int32_t)sim.cpu.regs[REG_A0] != -1)
        FAIL("write to bad fd should return -1");
    else
        PASS("write(bad fd) returns -1");

    destroy_sim(&sim);
    return failed;
}

/* ═══════════════════════════════════════════════════════════════
 * 测试 4：brk(0) — 查询当前堆边界
 * ═══════════════════════════════════════════════════════════════ */
static int test_brk_query(void)
{
    int failed = 0;
    printf("\n--- test_brk_query ---\n");

    Simulator sim;
    init_sim(&sim);

    /* brk(0) = 查询，不改变堆 */
    sim.cpu.regs[REG_A7] = SYS_brk;
    sim.cpu.regs[REG_A0] = 0;

    syscall_handler(&sim);

    /* 第一次 brk(0) 应该返回 mem_brk 的初始值 */
    uint32_t result = sim.cpu.regs[REG_A0];
    printf("  brk(0) = 0x%08x\n", result);

    /* 初始 brk 应该为 0（mem_init 后 brk_current = 0） */
    if (result != 0)
        FAIL("initial brk should be 0");
    else
        PASS("brk(0) returns initial brk = 0");

    destroy_sim(&sim);
    return failed;
}

/* ═══════════════════════════════════════════════════════════════
 * 测试 5：brk(new) — 扩展堆
 * ═══════════════════════════════════════════════════════════════ */
static int test_brk_extend(void)
{
    int failed = 0;
    printf("\n--- test_brk_extend ---\n");

    Simulator sim;
    init_sim(&sim);

    uint32_t new_brk = 0x20000000;  // 约 512MB

    sim.cpu.regs[REG_A7] = SYS_brk;
    sim.cpu.regs[REG_A0] = new_brk;

    syscall_handler(&sim);

    uint32_t result = sim.cpu.regs[REG_A0];
    printf("  brk(0x%08x) = 0x%08x\n", new_brk, result);

    /* 因为 new_brk > 128MB 物理内存，应该失败返回 0 */
    if (result == 0)
        PASS("brk beyond physical memory returns 0");
    else
        FAIL("brk beyond memory should fail");

    destroy_sim(&sim);
    return failed;
}

/* ═══════════════════════════════════════════════════════════════
 * 测试 6：未知 syscall → a0 设为 -1
 * ═══════════════════════════════════════════════════════════════ */
static int test_unknown_syscall(void)
{
    int failed = 0;
    printf("\n--- test_unknown_syscall ---\n");

    Simulator sim;
    init_sim(&sim);

    sim.cpu.regs[REG_A7] = 999;     // 不存在的 syscall
    sim.cpu.running = true;

    syscall_handler(&sim);

    if ((int32_t)sim.cpu.regs[REG_A0] != -1)
        FAIL("unknown syscall should return -1 in a0");
    else
        PASS("unknown syscall returns -1");

    destroy_sim(&sim);
    return failed;
}

/* ═══════════════════════════════════════════════════════════════
 * 主入口
 * ═══════════════════════════════════════════════════════════════ */
int main(void)
{
    int total = 0;
    printf("=== Linux Syscall Tests ===\n");

    total += test_exit();
    total += test_write();
    total += test_write_bad_fd();
    total += test_brk_query();
    total += test_brk_extend();
    total += test_unknown_syscall();

    printf("\n=== Results: %d test(s) failed ===\n", total);
    return total ? 1 : 0;
}
