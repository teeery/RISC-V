/* ============================================================================
 * e2e_test.c — 端到端集成测试
 *
 * 测试流程：
 *   加载 minimal.elf → 运行完整流水线（取指→译码→执行）→ 检查最终状态
 *
 * minimal.elf 内容（2 条指令）：
 *   addi a0, zero, 42  → a0 = 42（返回值寄存器）
 *   ecall              → 触发 EXC_ECALL_M → mtvec=0 → cpu_trap 停机
 *
 * 编译（从项目根目录）：
 *   gcc -std=c11 -Wall -Wextra -Isrc/include -Isrc/src/loader \
 *       -o build/e2e_test \
 *       src/test/e2e/e2e_test.c \
 *       src/src/simulator.c \
 *       src/src/cpu/cpu.c src/src/cpu/decode.c src/src/cpu/execute.c \
 *       src/src/memory/memory.c src/src/memory/mmu.c \
 *       src/src/loader/elf_validate.c src/src/loader/elf_load.c \
 *       src/src/loader/elf_segment.c src/src/loader/elf_stack.c
 *
 * 运行：
 *   ./build/e2e_test
 * ============================================================================ */

#include "simulator.h"
#include "loader/elf_loader.h"
#include "types.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

static int passed = 0;
static int failed = 0;

#define CHECK(cond, fmt, ...) do {                   \
    if (cond) {                                      \
        printf("  ✅ " fmt "\n", ##__VA_ARGS__);      \
        passed++;                                    \
    } else {                                         \
        printf("  ❌ " fmt "\n", ##__VA_ARGS__);      \
        failed++;                                    \
    }                                                \
} while(0)

int main(void)
{
    printf("═══════════════════════════════════════\n");
    printf("  RISC-V E2E Integration Test\n");
    printf("═══════════════════════════════════════\n\n");

    /* ================================================================
     * 测试 1：加载 ELF → 完整流水线运行 → 检查返回值
     *
     * 这是最核心的 E2E 测试：
     *   Loader  →  ELF 解析、段映射、栈初始化
     *   MMU     →  取指 / 读栈（Bare 模式 vaddr=paddr）
     *   CPU     →  译码 + 执行（addi + ecall）
     *   Trap    →  mtvec=0 → 正常停机
     * ================================================================ */
    printf("── Test 1: Full Pipeline (minimal.elf) ──\n");
    {
        Simulator sim;
        sim_init(&sim);

        /* 加载 ELF */
        bool ok = sim_load_elf(&sim, "src/test/loader/minimal.elf");
        CHECK(ok, "sim_load_elf(\"minimal.elf\")");

        printf("    entry = 0x%08" PRIx32 ", sp = 0x%08" PRIx32 "\n",
               sim.cpu.pc, sim.cpu.regs[REG_SP]);

        /* 设置为机器态（Loader 不设 priv，手动补齐） */
        sim.cpu.priv = PRIV_MACHINE;

        /* 运行整个程序（ecall → trap → running=false → 自动停止） */
        sim_run(&sim);

        printf("    instructions = %" PRIu64 ", cycles = %" PRIu64 "\n",
               sim.instr_count, sim.cycle_count);

        /* 验证：x10(a0) 应该是 42 */
        CHECK(sim.cpu.regs[REG_A0] == 42,
              "a0 == 42 (addi a0, zero, 42)");
        printf("    a0 = %" PRIu32 "\n", sim.cpu.regs[REG_A0]);

        /* x0 硬连线为 0 */
        CHECK(sim.cpu.regs[REG_ZERO] == 0,
              "x0 == 0 (hardwired zero)");

        /* 执行了恰好 3 条指令：addi a0 + addi a7 + ecall */
        CHECK(sim.instr_count == 3,
              "instruction count == 3 (addi a0 + addi a7 + ecall)");

        /* exit 后 CPU 应停止运行 */
        CHECK(sim.cpu.running == false,
              "cpu.running == false after exit(42)");

        sim_destroy(&sim);
    }

    /* ================================================================
     * 测试 2：栈运行时读写
     *
     * 加载 ELF 后，栈区域应可正常读写
     * ================================================================ */
    printf("\n── Test 2: Stack Read/Write at Runtime ──\n");
    {
        Simulator sim;
        sim_init(&sim);

        bool ok = sim_load_elf(&sim, "src/test/loader/minimal.elf");
        if (!ok) {
            CHECK(false, "sim_load_elf for stack test");
            sim_destroy(&sim);
            goto done;
        }

        uint32_t sp     = sim.cpu.regs[REG_SP] - 4;
        ExceptionType exc = EXC_NONE;

        /* 在栈上写入 */
        ok = mmu_write_32(&sim.mmu, &sim.pmem, sp, 0xCAFEBABE,
                          PRIV_MACHINE, &exc);
        CHECK(ok && exc == EXC_NONE,
              "write 0xCAFEBABE to stack[sp-4]");

        /* 读回验证 */
        uint32_t val = 0;
        ok = mmu_read_32(&sim.mmu, &sim.pmem, sp, &val,
                         PRIV_MACHINE, &exc);
        CHECK(ok && val == 0xCAFEBABE,
              "read back from stack == 0xCAFEBABE");

        sim_destroy(&sim);
    }

    /* ================================================================
     * 测试 3：模拟器统计信息
     *
     * sim_init 后、运行前检查初始状态
     * ================================================================ */
    printf("\n── Test 3: Initial Simulator State ──\n");
    {
        Simulator sim;
        sim_init(&sim);

        CHECK(sim.instr_count == 0, "instr_count == 0 at init");
        CHECK(sim.cycle_count == 0, "cycle_count == 0 at init");
        CHECK(sim.cpu.running == false, "cpu.running == false at init");
        CHECK(sim.cpu.priv == PRIV_MACHINE, "cpu.priv == PRIV_MACHINE at init");
        CHECK(sim.cpu.pc == 0, "cpu.pc == 0 at init");
        CHECK(sim.bp_count == 0, "bp_count == 0 at init");

        sim_destroy(&sim);
    }

done:
    printf("\n═══════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", passed, failed);
    printf("═══════════════════════════════════════\n");

    return failed > 0 ? 1 : 0;
}
