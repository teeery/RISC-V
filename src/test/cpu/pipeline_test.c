/* ============================================================
 * pipeline_test.c — 五级流水线控制器正确性验证
 *
 * 验证：
 *   1. 寄存器值正确（与单周期/多周期结果一致）
 *   2. 指令数正确（3 条指令）
 *   3. 周期数正确（5 级流水线，前 5 周期填满，之后每周期 1 条）
 *   4. 单周期/多周期/流水线 三种模型结果一致
 *   5. CPI 低于多周期（流水线重叠执行的优势）
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
        printf("  " "\xe2\x9c\x85" " " fmt "\n", ##__VA_ARGS__); \
        passed++;                                    \
    } else {                                         \
        printf("  " "\xe2\x9d\x8c" " " fmt "\n", ##__VA_ARGS__); \
        failed++;                                    \
    }                                                \
} while(0)

int main(void)
{
    printf("\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\n");
    printf("  Pipeline CPU Verification\n");
    printf("\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\n\n");

    /* ── Test: Pipeline mode with minimal.elf ──────────── */
    printf("── Pipeline: minimal.elf ──\n");
    {
        Simulator sim;
        sim_init(&sim);
        sim.cpu_model = MODEL_PIPELINE;

        bool ok = sim_load_elf(&sim, "src/test/loader/minimal.elf");
        CHECK(ok, "sim_load_elf");
        sim.cpu.priv = PRIV_MACHINE;

        sim_run(&sim);

        printf("    instructions = %" PRIu64 ", cycles = %" PRIu64 "\n",
               sim.instr_count, sim.cycle_count);

        CHECK(sim.instr_count == 3,
              "instr_count == 3 (was %" PRIu64 ")", sim.instr_count);

        /* 5 级流水线：3 条指令需要 7 周期（5 填满 + 2 后续） */
        CHECK(sim.cycle_count == 7,
              "cycles == 7 (5-stage pipeline: 5 fill + 2 more): %" PRIu64,
              sim.cycle_count);

        CHECK(sim.cpu.regs[REG_A0] == 42,
              "a0 == 42 (was %u)", sim.cpu.regs[REG_A0]);
        CHECK(sim.cpu.regs[REG_A7] == 93,
              "a7 == 93 (was %u)", sim.cpu.regs[REG_A7]);
        CHECK(sim.cpu.regs[0] == 0,
              "x0 == 0 (hardwired zero)");
        CHECK(sim.cpu.mcause == EXC_ECALL_M,
              "mcause == EXC_ECALL_M");

        /* 流水线统计 */
        printf("    stall_cycles  = %" PRIu64 "\n", sim.pipe.stall_cycles);
        printf("    flush_cycles  = %" PRIu64 "\n", sim.pipe.flush_cycles);

        CHECK(sim.pipe.stall_cycles == 0,
              "stall_cycles == 0 (no load-use hazards in minimal.elf)");
        CHECK(sim.pipe.flush_cycles == 1,
              "flush_cycles == 1 (ecall flushes IF/ID + ID/EX)");

        sim_destroy(&sim);
    }

    /* ── Test: Pipeline vs Single-cycle consistency ────── */
    printf("\n── Three-Model Consistency ──\n");
    {
        uint64_t sc_instr = 0, sc_cycles = 0;
        uint64_t mc_instr = 0, mc_cycles = 0;
        uint64_t pl_instr = 0, pl_cycles = 0;

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

        /* Pipeline */
        {
            Simulator sim;
            sim_init(&sim);
            sim.cpu_model = MODEL_PIPELINE;
            sim_load_elf(&sim, "src/test/loader/minimal.elf");
            sim.cpu.priv = PRIV_MACHINE;
            sim_run(&sim);
            pl_instr  = sim.instr_count;
            pl_cycles = sim.cycle_count;
            sim_destroy(&sim);
        }

        printf("    Single:   %" PRIu64 " instr, %" PRIu64 " cycles (CPI=%.2f)\n",
               sc_instr, sc_cycles,
               sc_instr ? (double)sc_cycles / sc_instr : 0);
        printf("    Multi:    %" PRIu64 " instr, %" PRIu64 " cycles (CPI=%.2f)\n",
               mc_instr, mc_cycles,
               mc_instr ? (double)mc_cycles / mc_instr : 0);
        printf("    Pipeline: %" PRIu64 " instr, %" PRIu64 " cycles (CPI=%.2f)\n",
               pl_instr, pl_cycles,
               pl_instr ? (double)pl_cycles / pl_instr : 0);

        CHECK(sc_instr == mc_instr && mc_instr == pl_instr,
              "same instr count: single=%" PRIu64 " multi=%" PRIu64 " pipe=%" PRIu64,
              sc_instr, mc_instr, pl_instr);

        /* 周期数关系：单周期 < 流水线 < 多周期 */
        CHECK(sc_cycles < pl_cycles,
              "single-cycle < pipeline cycles: %" PRIu64 " < %" PRIu64,
              sc_cycles, pl_cycles);
        CHECK(pl_cycles < mc_cycles,
              "pipeline < multi-cycle cycles: %" PRIu64 " < %" PRIu64,
              pl_cycles, mc_cycles);
    }

    /* ── Test: Pipeline state initialization ───────────── */
    printf("\n── Pipeline State Init ──\n");
    {
        Simulator sim;
        sim_init(&sim);

        CHECK(!sim.pipe.if_id.valid, "IF/ID invalid at init");
        CHECK(!sim.pipe.id_ex.valid, "ID/EX invalid at init");
        CHECK(!sim.pipe.ex_mem.valid, "EX/MEM invalid at init");
        CHECK(!sim.pipe.mem_wb.valid, "MEM/WB invalid at init");
        CHECK(sim.pipe.stall_cycles == 0, "stall_cycles == 0 at init");
        CHECK(sim.pipe.flush_cycles == 0, "flush_cycles == 0 at init");

        sim_destroy(&sim);
    }

    printf("\n\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\n");
    printf("  Results: %d passed, %d failed\n", passed, failed);
    printf("\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\n");

    return failed ? 1 : 0;
}
