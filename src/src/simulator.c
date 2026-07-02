#include "simulator.h"
#include "debugger/debugger.h"   // debugger_check_breakpoint
#include "cpu/execute.h"         // cpu_execute, cpu_trap
#include "cpu/decode.h"          // DecodedInstr, cpu_decode
#include "loader/elf_loader.h"   // elf_load
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * simulator.c — 模拟器顶层胶水层
 *
 * 职责：
 *   1. sim_init     — 初始化所有模块并分配资源
 *   2. sim_destroy  — 释放所有资源
 *   3. sim_load_elf — 加载 ELF 并设置 CPU 初值
 *   4. sim_step     — 执行单条指令（取指→译码→执行→写回）
 *   5. sim_run      — 主执行循环
 *
 * 这是唯一知道所有模块如何协作的文件。
 * 各模块只知道自己和 Simulator 的接口。
 * ================================================================
 */

/* ── sim_init ──────────────────────────────────────────────────── */
void sim_init(Simulator *sim)
{
    /* 清零整个结构体 */
    memset(sim, 0, sizeof(Simulator));

    /* 初始化物理内存：128MB */
    mem_init(&sim->pmem, MEM_SIZE_DEFAULT);

    /* 初始化 MMU（satp=0 即 Bare 模式，分配根页表） */
    mmu_init(&sim->mmu);

    /* 初始化 CPU：寄存器清零，priv = PRIV_MACHINE */
    cpu_init(&sim->cpu);

    /* 初始化断点数组 */
    sim->bp_capacity = 16;
    sim->breakpoints = calloc((size_t)sim->bp_capacity, sizeof(Breakpoint));
    if (!sim->breakpoints) {
        fprintf(stderr, "Fatal: Failed to allocate breakpoint array\n");
        exit(1);
    }
    sim->bp_count = 0;

    /* 非调试模式 */
    sim->single_step = false;
    sim->debug_mode  = false;

    sim->instr_count  = 0;
    sim->cycle_count = 0;
}

/* ── sim_destroy ───────────────────────────────────────────────── */
void sim_destroy(Simulator *sim)
{
    /* 恢复所有断点的原始指令 */
    for (int i = 0; i < sim->bp_count; i++) {
        if (sim->breakpoints[i].enabled) {
            ExceptionType exc = EXC_NONE;
            mmu_write_32(&sim->mmu, &sim->pmem,
                         sim->breakpoints[i].addr,
                         sim->breakpoints[i].original_instr,
                         sim->cpu.priv, &exc);
        }
    }

    free(sim->breakpoints);
    sim->breakpoints  = NULL;
    sim->bp_count     = 0;
    sim->bp_capacity  = 0;

    mem_destroy(&sim->pmem);
    /* mmu 的根页表由 mmu_init 分配的 calloc，暂不单独释放 */
}

/* ── sim_load_elf ──────────────────────────────────────────────── */
bool sim_load_elf(Simulator *sim, const char *path)
{
    uint32_t entry, stack_top;
    if (!elf_load(path, &sim->pmem, &sim->mmu, &entry, &stack_top)) {
        return false;
    }

    /* 设置 CPU 初始状态 */
    sim->cpu.pc           = entry;
    sim->cpu.regs[REG_SP] = stack_top;

    printf("ELF loaded: entry=0x%08x, stack=0x%08x\n", entry, stack_top);
    return true;
}

/* ── sim_step ──────────────────────────────────────────────────── */
void sim_step(Simulator *sim)
{
    uint32_t pc = sim->cpu.pc;

    /* ① 取指：通过 MMU 读取 32 位指令 */
    ExceptionType exc = EXC_NONE;
    uint32_t instr;
    if (!mmu_read_32(&sim->mmu, &sim->pmem, pc, &instr,
                     sim->cpu.priv, &exc)) {
        /* 取指失败 → 触发异常 */
        cpu_trap(sim, (uint32_t)exc, pc);
        sim->instr_count++;
        sim->cycle_count++;
        return;
    }

    /* ② 译码 */
    DecodedInstr d = cpu_decode(instr);

    /* ③ 执行 */
    uint32_t next_pc = pc + 4;  /* 默认顺序执行 */
    bool ok = cpu_execute(sim, &d, &next_pc);

    /* ④ 写回：更新 PC */
    sim->cpu.pc = next_pc;

    /* ⑤ x0 硬连线保护 */
    sim->cpu.regs[0] = 0;

    /* ⑥ 统计 */
    sim->instr_count++;
    sim->cycle_count++;

    /* ⑦ 如果执行失败（非法指令），cpu_execute 内部已调 cpu_trap */
    (void)ok;
}

/* ── sim_run ───────────────────────────────────────────────────── */
void sim_run(Simulator *sim)
{
    sim->cpu.running = true;

    while (sim->cpu.running) {
        sim_step(sim);

        /* 单步模式：执行一条后暂停 */
        if (sim->single_step) {
            sim->single_step = false;
            break;
        }
    }

    printf("\nSimulation ended.\n");
    printf("Instructions: %" PRIu64 "  Cycles: %" PRIu64 "\n",
           sim->instr_count, sim->cycle_count);
}
