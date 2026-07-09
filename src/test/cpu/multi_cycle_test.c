/* ============================================================
 * multi_cycle_test.c — 多周期控制器正确性验证
 *
 * 验证：
 *   1. 寄存器值正确（与单周期结果一致）
 *   2. CPI > 1（多周期：不同指令不同周期数）
 *   3. instr_count 不变（指令条数相同）
 * ============================================================ */
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
    printf("  Multi-Cycle CPU Verification\n");
    printf("═══════════════════════════════════════\n\n");

    /* ── Test: Multi-cycle mode with minimal.elf ──────────── */
    printf("── Multi-Cycle: minimal.elf ──\n");
    {
        Simulator sim;
        sim_init(&sim);
        sim.cpu_model = MODEL_MULTI_CYCLE;

        bool ok = sim_load_elf(&sim, "src/test/loader/minimal.elf");
        CHECK(ok, "sim_load_elf");
        sim.cpu.priv = PRIV_MACHINE;

        sim_run(&sim);

        printf("    instructions = %" PRIu64 ", cycles = %" PRIu64 "\n",
               sim.instr_count, sim.cycle_count);

        CHECK(sim.instr_count == 3,
              "instr_count == 3 (was %" PRIu64 ")", sim.instr_count);
        CHECK(sim.cycle_count > sim.instr_count,
              "cycles > instr (multi-cycle CPI>1): %" PRIu64 " > %" PRIu64,
              sim.cycle_count, sim.instr_count);
        CHECK(sim.cpu.regs[REG_A0] == 42,
              "a0 == 42 (was %u)", sim.cpu.regs[REG_A0]);
        CHECK(sim.cpu.regs[REG_A7] == 93,
              "a7 == 93 (was %u)", sim.cpu.regs[REG_A7]);
        CHECK(sim.cpu.regs[0] == 0,
              "x0 == 0 (hardwired zero)");
        CHECK(sim.cpu.mcause == EXC_ECALL_M,
              "mcause == EXC_ECALL_M");

        sim_destroy(&sim);
    }

    /* ── Test: Multi-cycle vs Single-cycle consistency ────── */
    printf("\n── Consistency: Multi vs Single ──\n");
    {
        uint64_t sc_instr, sc_cycles;
        uint64_t mc_instr, mc_cycles;

        /* Single-cycle */
        {
            Simulator sim;
            sim_init(&sim);
            sim.cpu_model = MODEL_SINGLE_CYCLE;
            sim_load_elf(&sim, "src/test/loader/minimal.elf");
            sim.cpu.priv = PRIV_MACHINE;
            sim_run(&sim);
            sc_instr  = sim.instr_count;
            sc_cycles = sim.cycle_count;
            sim_destroy(&sim);
        }

        /* Multi-cycle */
        {
            Simulator sim;
            sim_init(&sim);
            sim.cpu_model = MODEL_MULTI_CYCLE;
            sim_load_elf(&sim, "src/test/loader/minimal.elf");
            sim.cpu.priv = PRIV_MACHINE;
            sim_run(&sim);
            mc_instr  = sim.instr_count;
            mc_cycles = sim.cycle_count;
            sim_destroy(&sim);
        }

        CHECK(sc_instr == mc_instr,
              "same instr count: single=%" PRIu64 " multi=%" PRIu64,
              sc_instr, mc_instr);
        CHECK(sc_cycles < mc_cycles,
              "multi uses more cycles: single=%" PRIu64 " multi=%" PRIu64,
              sc_cycles, mc_cycles);
    }

    printf("\n═══════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", passed, failed);
    printf("═══════════════════════════════════════\n");

    return failed ? 1 : 0;
}
