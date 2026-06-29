#include <stdio.h>
#include <string.h>

/* ============================================================
 * cpu.c — CPU 核心模块：初始化、复位、运行循环、寄存器导出
 *
 * ─── 你需要实现的内容 ─────────────────────────────────────
 *
 * 1. cpu_init(CPUState *cpu, uint32_t entry, uint32_t stack)
 *    - 将所有通用寄存器清零
 *    - 设置 sp (x2) = stack (栈顶)
 *    - 设置 pc = entry (程序入口)
 *    - 设置特权级 priv = PRIV_MACHINE (M 模式)
 *    - 初始化 CSR: mstatus.MPP=3, 其他为 0
 *    - 设置 running = true
 *
 * 2. cpu_step(CPUState *cpu, void *mem, void *mmu)
 *    单步执行流程 (经典五级流水线简化为单周期模型)：
 *
 *    a) IF (取值): 从 PC 指向的虚拟地址读取 32 位指令
 *       - 调用 mmu_read_32(mmu, pmem, cpu->pc, &raw, cpu->priv)
 *       - 如果读取失败 → 异常处理
 *
 *    b) ID (译码): 调用 decode(raw, &dec)
 *       - 如果译码失败 → 抛出 IllegalInstruction
 *
 *    c) EX (执行): 调用 execute(&dec, cpu, pmem, mmu)
 *
 *    d) WB (写回): 在 execute() 内部已完成寄存器写入
 *
 *    e) PC 更新:
 *       - 默认 cpu->pc = cpu->next_pc (= pc + 4)
 *       - 跳转指令已在 execute() 中修改 next_pc
 *       - 注意：next_pc 初始化应在 step 开始前设为 pc+4
 *
 *    f) 异常处理: 如果任何步骤失败:
 *       - 设置 mcause, mepc, mtval
 *       - PC = mtvec
 *       需要实现 cpu_take_trap() 函数
 *
 * 3. cpu_run(CPUState *cpu, void *mem, void *mmu)
 *    while (cpu->running) { cpu_step(cpu, mem, mmu); }
 *    断点检查和外部停止条件由调用者 (debugger) 处理
 *
 * 4. cpu_dump_regs()
 *    格式化打印所有 32 个寄存器 + PC + 部分 CSR
 *    格式: x0  (zero) = 0x00000000
 *          x1  (ra)   = 0x80000100
 *          ...
 *          pc           = 0x80000000
 *
 * 5. cpu_take_trap() — 异常处理
 *    - mcause = 异常号
 *    - mepc = 当前 PC (或下一条指令地址，视异常类型而定)
 *    - mtval = 附加信息 (如错误地址)
 *    - mstatus.MPIE = mstatus.MIE
 *    - mstatus.MIE = 0 (禁用中断)
 *    - mstatus.MPP = 当前特权级
 *    - PC = mtvec (陷阱向量基址)
 *
 * ─── 调试信息 ──────────────────────────────────────────────
 * 可在 cpu_step() 中添加 printf 输出每条指令的执行跟踪：
 *   [TRACE] 0x80000000: addi x5, x0, 42   | x5 = 42
 * 这对调试非常有用，可通过 -t 参数开启
 * ============================================================
 */

#include "types.h"
#include "memory.h"
#include "mmu.h"

/* 前向声明 (来自 decode.c 和 execute.c) */
extern bool decode(uint32_t raw, DecodedInstruction *dec);
extern bool execute(DecodedInstruction *dec, CPUState *cpu,
                    PhysicalMemory *pmem, MMUState *mmu);

/* 全局跟踪开关 */
bool g_trace_enabled = false;

/* ── 寄存器 ABI 名称 ─────────────────────────────────────── */
static const char *reg_names[] = {
    "zero", "ra", "sp", "gp", "tp",
    "t0", "t1", "t2",
    "s0", "s1",
    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
    "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11",
    "t3", "t4", "t5", "t6"
};

/* ── 异常处理 ────────────────────────────────────────────── */
static void cpu_take_trap(CPUState *cpu, ExceptionType cause, uint32_t tval)
{
    cpu->mcause  = cause;
    cpu->mepc    = cpu->pc;       // 保存当前 PC
    cpu->mtval   = tval;
    // 保存 MIE → MPIE, 清除 MIE, 设置 MPP
    cpu->mstatus = (cpu->mstatus & ~0x1880) |
                   ((cpu->mstatus & 0x8) << 4) |  // MPIE ← MIE
                   ((cpu->priv & 3) << 11);        // MPP ← 当前特权级
    cpu->mstatus &= ~0x8;         // MIE = 0
    cpu->priv     = PRIV_MACHINE; // 进入 M 模式
    cpu->next_pc  = cpu->mtvec;   // 跳转到陷阱向量
}

/* ── 初始化 ──────────────────────────────────────────────── */
void cpu_init(CPUState *cpu, uint32_t entry_addr, uint32_t stack_addr)
{
    memset(cpu, 0, sizeof(*cpu));
    cpu->regs[REG_SP] = stack_addr;    // 栈指针
    cpu->pc           = entry_addr;
    cpu->next_pc      = entry_addr + 4;
    cpu->priv         = PRIV_MACHINE;
    cpu->running      = true;
    // mstatus: MPP=3 (Machine)
    cpu->mstatus      = (3 << 11);
}

void cpu_reset(CPUState *cpu)
{
    memset(cpu->regs, 0, sizeof(cpu->regs));
    cpu->pc      = 0;
    cpu->next_pc = 4;
    cpu->priv    = PRIV_MACHINE;
    cpu->running  = true;
    cpu->mstatus = (3 << 11);
    cpu->mtvec   = 0;
    cpu->mepc    = 0;
    cpu->mcause  = 0;
    cpu->mtval   = 0;
}

/* ── 单步执行 ────────────────────────────────────────────── */
bool cpu_step(CPUState *cpu, PhysicalMemory *pmem, MMUState *mmu)
{
    uint32_t raw_insn;
    DecodedInstruction dec;

    cpu->next_pc = cpu->pc + 4;  // 默认顺序执行

    /* IF: 取值 */
    if (!mmu_read_32(mmu, pmem, cpu->pc, &raw_insn, cpu->priv)) {
        cpu_take_trap(cpu, EXC_INSTR_ACCESS_FAULT, cpu->pc);
        cpu->pc = cpu->next_pc;
        return false;
    }

    /* ID: 译码 */
    if (!decode(raw_insn, &dec)) {
        cpu_take_trap(cpu, EXC_ILLEGAL_INSTRUCTION, raw_insn);
        cpu->pc = cpu->next_pc;
        return false;
    }

    /* 跟踪输出 */
    if (g_trace_enabled) {
        printf("[TRACE] 0x%08x: 0x%08x\n", cpu->pc, raw_insn);
    }

    /* EX + MEM + WB: 执行 */
    bool ok = execute(&dec, cpu, pmem, mmu);

    /* 更新 PC */
    cpu->pc = cpu->next_pc;

    return ok;
}

/* ── 循环运行 ────────────────────────────────────────────── */
void cpu_run(CPUState *cpu, PhysicalMemory *pmem, MMUState *mmu)
{
    while (cpu->running) {
        cpu_step(cpu, pmem, mmu);
    }
}

/* ── 寄存器 dump ─────────────────────────────────────────── */
void cpu_dump_regs(const CPUState *cpu)
{
    printf("========================================\n");
    printf("  RISC-V CPU Registers\n");
    printf("========================================\n");
    printf("  pc      = 0x%08x\n", cpu->pc);
    printf("  priv    = %d (", cpu->priv);
    switch (cpu->priv) {
        case PRIV_USER:       printf("User"); break;
        case PRIV_SUPERVISOR: printf("Supervisor"); break;
        case PRIV_MACHINE:    printf("Machine"); break;
    }
    printf(")\n");
    printf("----------------------------------------\n");
    for (int i = 0; i < REG_COUNT; i++) {
        printf("  x%-2d (%s)\t= 0x%08x", i, reg_names[i], cpu->regs[i]);
        if (i % 2 == 1) printf("\n");
        else            printf("\t");
    }
    printf("\n----------------------------------------\n");
    printf("  mcause  = 0x%08x  mepc    = 0x%08x\n", cpu->mcause, cpu->mepc);
    printf("  mtval   = 0x%08x  mstatus = 0x%08x\n", cpu->mtval, cpu->mstatus);
    printf("  mtvec   = 0x%08x\n", cpu->mtvec);
    printf("========================================\n");
}
