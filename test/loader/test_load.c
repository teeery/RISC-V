/* ============================================================================
 * test_load.c — L2：ELF 加载测试（集成 Stub MMU + PhysicalMemory）
 * ============================================================================
 *
 * 编译（从项目根目录）：
 *   gcc -std=c11 -Wall -Wextra -Isrc/include -Isrc/src/loader \
 *       test/loader/test_load.c \
 *       src/src/loader/elf_validate.c src/src/loader/elf_load.c \
 *       src/src/loader/elf_segment.c src/src/loader/elf_stack.c \
 *       src/src/memory/memory.c src/src/memory/mmu.c \
 *       -o build/test_load && ./build/test_load
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "memory/memory.h"
#include "memory/mmu.h"
#include "loader/elf_loader.h"

#define FAIL(msg) do { \
    fprintf(stderr, "  FAIL: %s\n", msg); \
    return 1; \
} while(0)

#define PASS(msg) printf("  PASS: %s\n", msg)

int main(void) {
    PhysicalMemory pmem;
    MMUState       mmu;
    uint32_t       entry = 0, stack_top = 0;

    printf("=== L2: ELF Load Test ===\n\n");

    /* ── 初始化 ── */
    mem_init(&pmem, MEM_SIZE_DEFAULT);
    mmu_init(&mmu);

    /* ── 测试 1：加载合法 RISC-V ELF ── */
    {
        bool ok = elf_load("test/loader/minimal.elf", &pmem, &mmu,
                           &entry, &stack_top);
        if (!ok) FAIL("elf_load should succeed on valid ELF");
        PASS("elf_load succeeds on valid RISC-V ELF32");
    }

    /* ── 测试 2：验证入口地址 ── */
    {
        if (entry == 0) FAIL("entry should not be zero");
        printf("    entry = 0x%08x\n", entry);
        PASS("entry address is valid");
    }

    /* ── 测试 3：验证栈顶地址 ── */
    {
        if (stack_top == 0) FAIL("stack_top should not be zero");
        printf("    stack_top = 0x%08x\n", stack_top);
        PASS("stack_top is valid");
    }

    /* ── 测试 4：验证代码段已加载 ──
     *
     * minimal.elf 里第一条指令是 addi a0, zero, 42
     * 编码 = 0x02A00513
     * 在内存中（小端序）= 13 05 A0 02
     */
    {
        ExceptionType exc = EXC_NONE;
        uint32_t insn = 0;

        /* 通过 MMU 读取入口地址处的指令 */
        bool ok = mmu_read_32(&mmu, &pmem, entry, &insn,
                              PRIV_MACHINE, &exc);
        if (!ok) FAIL("mmu_read_32 at entry should succeed");
        if (exc != EXC_NONE) FAIL("exc should be EXC_NONE");

        printf("    insn @ 0x%08x = 0x%08x\n", entry, insn);
        /* 验证是 addi a0, zero, 42 */
        if (insn != 0x02A00513)
            FAIL("first instruction should be addi a0,zero,42 (0x02A00513)");
        PASS("first instruction = addi a0, zero, 42");
    }

    /* ── 测试 5：验证栈区域可读写 ── */
    {
        ExceptionType exc = EXC_NONE;
        /* 栈顶往下一个字 */
        uint32_t sp = stack_top - 4;  // 0xBFFFFFFC

        /* 写入 0xCAFEBABE */
        bool ok = mmu_write_32(&mmu, &pmem, sp, 0xCAFEBABE,
                               PRIV_MACHINE, &exc);
        if (!ok) FAIL("mmu_write_32 to stack should succeed");

        /* 读回验证 */
        uint32_t val = 0;
        ok = mmu_read_32(&mmu, &pmem, sp, &val, PRIV_MACHINE, &exc);
        if (!ok) FAIL("mmu_read_32 from stack should succeed");
        if (val != 0xCAFEBABE)
            FAIL("read-back should be 0xCAFEBABE");

        PASS("stack region is read-write");
    }

    /* ── 测试 6：文件不存在 ── */
    {
        bool ok = elf_load("nonexistent.elf", &pmem, &mmu,
                           &entry, &stack_top);
        if (ok) FAIL("should fail on nonexistent file");
        PASS("nonexistent file rejected");
    }

    mem_destroy(&pmem);

    printf("\n=== All L2 tests passed ===\n");
    return 0;
}
