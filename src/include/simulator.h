#ifndef SIMULATOR_H
#define SIMULATOR_H

#include "types.h"           // PrivilegeLevel, ExceptionType, RegisterID
#include "cpu/cpu.h"         // CPU (regs[32], pc, running, priv, CSR)
#include "memory/memory.h"   // PhysicalMemory, MemoryRegion, mem_*
#include "memory/mmu.h"      // MMUState, mmu_*
#include "cpu/decode.h"      // DecodedInstr, cpu_decode, cpu_disasm

/* ================================================================
 * simulator.h — 模拟器顶层结构体
 *
 * 聚合所有模块状态（CPU / MMU / PhysicalMemory / Breakpoint），
 * 供 main.c、debugger、cpu_execute 等统一使用。
 *
 * 断点数组：放在 Simulator 下（讨论结论 3）
 *   设断点：Debugger 操作 sim->breakpoints[]
 *   命中断点：CPU 执行 ebreak → 查 sim->breakpoints[] → 通知 Debugger
 *
 * 栈地址（讨论结论 7）：
 *   栈顶 0xC0000000，栈大小 256KB（0x40000）
 *   范围 0xBFFC0000 ~ 0xC0000000
 * ================================================================
 */

/* ── CPU 时序模型枚举 ────────────────────────────────────────────
 *
 * 三种模型对应 Patterson & Hennessy 教科书中三种 CPU 实现方式：
 *   MODEL_SINGLE_CYCLE — 单周期：1 条指令 = 1 个周期（CPI=1.0）
 *   MODEL_MULTI_CYCLE  — 多周期：5 状态 FSM，不同指令不同周期数
 *   MODEL_PIPELINE     — 五级流水线：IF/ID/EX/MEM/WB 重叠执行
 *
 * 数据通路（ALU/寄存器堆/内存接口）三种模型共享。
 * 控制器各自独立——改变时序模型只需替换 controller/ 下的文件。
 */
typedef enum {
    MODEL_SINGLE_CYCLE = 0,
    MODEL_MULTI_CYCLE  = 1,
    MODEL_PIPELINE     = 2,
} CpuModel;

/* ── 栈地址常量 ──────────────────────────────────────────────────
 *
 * 这些地址是 RISC-V 规范中用户态程序的典型栈布局：
 *   STACK_TOP  = 0xC0000000（3GB 位置，接近真实 Linux 用户空间顶部）
 *   STACK_SIZE = 0x40000   （256KB，足够运行标准测试程序）
 *   STACK_BASE = 0xBFFC0000（STACK_TOP - STACK_SIZE）
 *
 * 注意：这些宏由 Loader 定义并使用（elf_stack.c），Debugger 也引用它们
 */
#define STACK_TOP_DEFAULT   0xC0000000
#define STACK_SIZE_DEFAULT  (256 * 1024)   // 256KB
#define STACK_TOP           STACK_TOP_DEFAULT
#define STACK_BASE          (STACK_TOP_DEFAULT - STACK_SIZE_DEFAULT)

/* ── 断点结构体 ────────────────────────────────────────────────── */
typedef struct {
    uint32_t addr;              // 断点地址
    uint32_t original_instr;     // 被替换前的原始指令（用于恢复）
    bool     enabled;           // 是否启用
} Breakpoint;

/* ── 数据通路信号状态（Web 调试器 SVG 可视化用）───────────────── */
typedef struct {
    uint32_t pc;            /* 当前 PC */
    uint32_t instr;         /* 32 位指令字 */
    uint8_t  opcode;        /* [6:0] */
    uint8_t  rd, rs1, rs2;  /* 寄存器索引 */
    uint8_t  funct3, funct7;/* 功能码 */
    int32_t  imm;           /* 立即数 */
    uint32_t rs1_val;       /* rs1 值（执行前） */
    uint32_t rs2_val;       /* rs2 值（执行前） */
    uint32_t rd_val;        /* rd 新值（执行后） */
    char     alu_op[8];     /* ALU 操作名 */
    uint32_t alu_a, alu_b, alu_result; /* ALU 输入输出 */
    uint32_t mem_addr;      /* Load/Store 地址 */
    uint32_t mem_rdata;     /* Load 数据 */
    uint32_t mem_wdata;     /* Store 数据 */
    bool     mem_read, mem_write; /* 访存标志 */
    bool     branch_taken;  /* 分支跳转？ */
    uint32_t next_pc;       /* 下条 PC */
    bool     reg_write;     /* 写寄存器？ */
    char     disasm[64];    /* 反汇编 */
    bool     valid;         /* 数据有效（执行后置 true） */
} DatapathState;

/* ── 多周期控制器状态 ────────────────────────────────────────────
 *
 * 多周期 CPU 在执行一条指令期间，跨多个时钟周期保存中间结果。
 * 每个字段对应教科书图 4.39 中的流水线寄存器：
 *   IR      — 指令寄存器（IF 阶段锁存）
 *   A, B    — 寄存器值（ID 阶段锁存）
 *   ALUOut  — ALU 计算结果（EX 阶段锁存）
 *   MDR     — 内存读取数据（MEM 阶段锁存）
 */
typedef struct {
    uint8_t  state;         // 当前 FSM 状态：0=IF, 1=ID, 2=EX, 3=MEM, 4=WB
    uint32_t ir;            // 指令寄存器（取指结果）
    uint32_t pc;            // 当前指令的 PC
    uint32_t a, b;          // 寄存器值 rs1_val, rs2_val
    uint32_t alu_out;       // ALU 输出 / 地址计算结果
    uint32_t mdr;           // 内存数据寄存器（Load 结果）
    uint32_t next_pc;       // 下一条指令的 PC
    uint32_t temp_pc;       // ID 阶段预计算的跳转目标
} MultiCycleState;

/* ── 流水线寄存器（Patterson & Hennessy §4.7-4.8）─────────────────
 *
 * 五级流水线的段间寄存器。每周期结束时，前一级的输出锁存到后一级的
 * 输入寄存器中。valid=false 表示该寄存器包含气泡（NOP），不产生
 * 任何副作用。
 */
typedef struct {
    uint32_t pc;        /* IF 阶段 PC */
    uint32_t instr;     /* 32 位指令字 */
    bool     valid;     /* 有效？（气泡时为 false） */
} PipeIFID;

typedef struct {
    uint32_t     pc;
    DecodedInstr d;       /* 译码结果 */
    uint32_t     rs1_val; /* 源寄存器 1 的值 */
    uint32_t     rs2_val; /* 源寄存器 2 的值 */
    bool         valid;
} PipeIDEX;

typedef struct {
    uint32_t pc;
    uint32_t alu_result;    /* ALU 计算结果 / 有效地址 */
    uint32_t rs2_val;       /* Store 数据（转发后） */
    uint8_t  rd;            /* 目标寄存器 */
    uint8_t  opcode;
    uint8_t  funct3;
    bool     reg_write;     /* 写整数寄存器？ */
    bool     mem_read;      /* Load 指令？ */
    bool     mem_write;     /* Store 指令？ */
    bool     is_fp;         /* 浮点指令？ */
    bool     valid;
} PipeEXMEM;

typedef struct {
    uint32_t alu_result;    /* ALU 结果直通（非 Load） */
    uint32_t mem_data;      /* Load 数据（mem_read=true 时有效） */
    uint8_t  rd;
    uint8_t  opcode;
    uint8_t  funct3;
    bool     reg_write;     /* 写整数寄存器？ */
    bool     is_load;       /* 来自 Load？WB 阶段选 mem_data 而非 alu_result */
    bool     is_fp_load;    /* 浮点 Load？WB 阶段写 fregs 而非 regs */
    bool     valid;
} PipeMEMWB;

/* ── 流水线控制器状态（仅 MODEL_PIPELINE 使用）────────────────── */
typedef struct {
    PipeIFID   if_id;
    PipeIDEX   id_ex;
    PipeEXMEM  ex_mem;
    PipeMEMWB  mem_wb;
    uint64_t   stall_cycles;    /* Load-use 停顿周期数 */
    uint64_t   flush_cycles;    /* 分支/跳转冲刷周期数 */
} PipelineState;

/* ── 模拟器顶层结构体 ──────────────────────────────────────────── */
typedef struct Simulator {
    /* 核心模块 */
    CPU             cpu;            // CPU 状态（寄存器、PC、CSR）
    MMUState        mmu;            // MMU 页表 / satp
    PhysicalMemory  pmem;           // 物理内存

    /* 调试相关 */
    Breakpoint     *breakpoints;    // 断点动态数组（Debugger 管理）
    int             bp_count;       // 当前断点数量
    int             bp_capacity;    // 断点数组容量（初始 16，按需 x2）

    bool            single_step;    // 单步执行标志
    bool            debug_mode;     // 调试模式（Debugger 接管时设为 true）

    /* 线程安全（Web 调试器用，内部是 CRITICAL_SECTION*） */
    void           *sim_lock;

    /* 数据通路可视化（Web 调试器 SVG） */
    DatapathState   dp;             // 最新一条指令的数据通路信号

    /* 多周期控制器状态（仅 MODEL_MULTI_CYCLE 使用） */
    MultiCycleState mc;             // FSM 状态 + 跨周期中间结果

    /* 流水线控制器状态（仅 MODEL_PIPELINE 使用） */
    PipelineState   pipe;           // 4 组流水线寄存器 + 统计

    /* CPU 时序模型（单周期 / 多周期 / 流水线） */
    CpuModel        cpu_model;      // 选择 CPU 实现方式（默认 MODEL_SINGLE_CYCLE）

    /* 统计 */
    uint64_t        instr_count;    // 已执行指令数
    uint64_t        cycle_count;    // 周期计数（单周期 = instr_count，多周期/流水线 ≠）
} Simulator;

/* ================================================================
 * Simulator 级别接口
 * ================================================================
 */

/* 初始化模拟器（分配 pmem、mmu、breakpoints，清零 CPU） */
void sim_init(Simulator *sim);

/* 销毁模拟器（释放 pmem、breakpoints 等） */
void sim_destroy(Simulator *sim);

/* 执行单条指令或推进一个 CPU 周期
 *
 * 根据 sim->cpu_model 分发到对应控制器：
 *   MODEL_SINGLE_CYCLE → sim_step_single()  (1 调用 = 1 完整指令)
 *   MODEL_MULTI_CYCLE  → sim_step_multi()   (1 调用 = 1 个 FSM 状态)
 *   MODEL_PIPELINE     → sim_step_pipeline() (1 调用 = 1 个时钟周期)
 *
 * 对所有模式：内部检查 single_step 和 breakpoints 状态。
 */
void sim_step(Simulator *sim);

/* 加载 ELF 文件并设置 CPU 初始状态
 *
 * 内部调用 elf_load()，然后设置：
 *   sim->cpu.pc          = entry（ELF 入口地址）
 *   sim->cpu.regs[REG_SP] = STACK_TOP（栈顶 0xC0000000）
 *
 * 返回 true 加载成功，false 失败
 */
bool sim_load_elf(Simulator *sim, const char *path);

/* 重新初始化模拟器状态（保留 CPU 模型和线程锁）
 *
 * 用于在线编译后加载新 ELF 的场景：
 *   1. 恢复断点原始指令
 *   2. 清零 CPU 寄存器、MC/Pipeline 状态、统计数据
 *   3. 重建物理内存和 MMU
 *   4. 重置断点数组
 *
 * 调用后必须再调用 sim_load_elf() 加载新程序。
 */
void sim_reload(Simulator *sim);

/* 主执行循环：while (cpu.running) sim_step() */
void sim_run(Simulator *sim);

/* ── 线程安全锁（Web 调试器通过此接口同步访问 sim）────────────── */

/* 获取 sim 的排他锁（进入临界区） */
void sim_lock(Simulator *sim);

/* 释放 sim 的排他锁（退出临界区） */
void sim_unlock(Simulator *sim);

#endif /* SIMULATOR_H */
