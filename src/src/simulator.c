#include "simulator.h"
#include "debugger/debugger.h"   // debugger_check_breakpoint
#include "cpu/execute.h"         // cpu_execute, cpu_trap
#include "cpu/decode.h"          // DecodedInstr, cpu_decode
#include "cpu/controller/controller_internal.h"  // sim_step_single/multi/pipeline
#include "loader/elf_loader.h"   // elf_load
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>             // CRITICAL_SECTION

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

    /* 默认 CPU 模型：单周期（兼容现有行为） */
    sim->cpu_model   = MODEL_SINGLE_CYCLE;

    /* 非调试模式 */
    sim->single_step = false;
    sim->debug_mode  = false;

    /* 线程安全锁 */
    sim->sim_lock = malloc(sizeof(CRITICAL_SECTION));
    if (sim->sim_lock)
        InitializeCriticalSection((CRITICAL_SECTION *)sim->sim_lock);

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

    /* 销毁线程锁 */
    if (sim->sim_lock) {
        DeleteCriticalSection((CRITICAL_SECTION *)sim->sim_lock);
        free(sim->sim_lock);
        sim->sim_lock = NULL;
    }

    mem_destroy(&sim->pmem);
    mmu_destroy(&sim->mmu);
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

/* ── sim_step ────────────────────────────────────────────────────
 *
 * CPU 周期推进：根据 cpu_model 分发到对应控制器。
 *
 * ┌─────────────────────┬────────────────────────────────┐
 * │ MODEL_SINGLE_CYCLE  │ 1 调用 = 1 条完整指令          │
 * │ MODEL_MULTI_CYCLE   │ 1 调用 = 1 个 FSM 状态         │
 * │ MODEL_PIPELINE      │ 1 调用 = 1 个时钟周期(5 级重叠) │
 * └─────────────────────┴────────────────────────────────┘
 *
 * 所有模式的共同保证：
 *   - x0 硬连线为 0（各控制器内部执行）
 *   - 异常通过 cpu_trap() 统一处理
 *   - 断点和单步在 sim_run() 层面处理
 * ─────────────────────────────────────────────────────────────── */
void sim_step(Simulator *sim)
{
    /* CPU 已停止 → 不执行任何指令（各控制器不再自行检查） */
    if (!sim->cpu.running) return;

    switch (sim->cpu_model) {

    case MODEL_SINGLE_CYCLE:
        sim_step_single(sim);
        break;

    case MODEL_MULTI_CYCLE:
        sim_step_multi(sim);
        break;

    case MODEL_PIPELINE:
        sim_step_pipeline(sim);
        break;

    default:
        /* 未知模型 → fallback 到单周期 */
        sim_step_single(sim);
        break;
    }
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

    /* 流水线排空 + 多周期排空：
     * 陷阱触发后，流水线中未完成指令仍需退休。
     * 多周期中可能在 MC_EX/MC_MEM/MC_WB 状态停止，需完成当前指令。 */
    if (sim->cpu_model == MODEL_PIPELINE) {
        while (1) {
            bool empty = !sim->pipe.if_id.valid && !sim->pipe.id_ex.valid &&
                         !sim->pipe.ex_mem.valid && !sim->pipe.mem_wb.valid;
            if (empty) break;
            sim_step_pipeline(sim);
        }
    } else if (sim->cpu_model == MODEL_MULTI_CYCLE) {
        /* 多周期：若没在 MC_IF（新指令开始），持续推进到指令完成 */
        while (sim->mc.state != 0) {  /* 0 = MC_IF */
            sim_step_multi(sim);
        }
    }

    printf("\nSimulation ended.\n");
    printf("Instructions: %" PRIu64 "  Cycles: %" PRIu64 "\n",
           sim->instr_count, sim->cycle_count);
}

/* ── sim_reload ───────────────────────────────────────────────────
 *
 * 重新初始化模拟器状态，保留 CPU 模型选择和线程锁。
 * 用于 Web 端"写 C → 编译 → 重新加载"的流程。
 * ─────────────────────────────────────────────────────────────── */
void sim_reload(Simulator *sim)
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

    /* 保存需要保留的字段 */
    CpuModel saved_model  = sim->cpu_model;
    bool    saved_debug   = sim->debug_mode;
    void   *saved_lock    = sim->sim_lock;   /* CRITICAL_SECTION，不能销毁 */

    /* 清零断点 */
    free(sim->breakpoints);
    sim->breakpoints  = NULL;
    sim->bp_count     = 0;
    sim->bp_capacity  = 0;

    /* 销毁并重建物理内存 */
    mem_destroy(&sim->pmem);
    mem_init(&sim->pmem, MEM_SIZE_DEFAULT);

    /* 重建 MMU */
    mmu_destroy(&sim->mmu);
    mmu_init(&sim->mmu);

    /* 重建断点数组 */
    sim->bp_capacity = 16;
    sim->breakpoints = calloc((size_t)sim->bp_capacity, sizeof(Breakpoint));
    if (!sim->breakpoints) {
        fprintf(stderr, "Fatal: Failed to allocate breakpoint array\n");
        exit(1);
    }

    /* 清零 CPU 和多周期/流水线状态 */
    memset(&sim->cpu,  0, sizeof(CPU));
    memset(&sim->mc,   0, sizeof(MultiCycleState));
    memset(&sim->pipe, 0, sizeof(PipelineState));
    memset(&sim->dp,   0, sizeof(DatapathState));

    /* 恢复 CPU 为 Machine 模式 */
    cpu_init(&sim->cpu);

    /* 恢复保留字段 */
    sim->cpu_model   = saved_model;
    sim->debug_mode  = saved_debug;
    sim->sim_lock    = saved_lock;
    sim->single_step = false;
    sim->instr_count = 0;
    sim->cycle_count = 0;

    printf("[sim] Reloaded: CPU/memory reset, ready for new ELF.\n");
}

/* ── sim_lock / sim_unlock ─────────────────────────────────────── */

void sim_lock(Simulator *sim)
{
    if (sim->sim_lock)
        EnterCriticalSection((CRITICAL_SECTION *)sim->sim_lock);
}

void sim_unlock(Simulator *sim)
{
    if (sim->sim_lock)
        LeaveCriticalSection((CRITICAL_SECTION *)sim->sim_lock);
}
