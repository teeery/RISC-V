#ifndef CPU_H
#define CPU_H

#include "types.h"

/* ============================================================
 * cpu.h — CPU 状态结构体 + 初始化接口
 *
 * CPU 模块是 RISC-V 模拟器的执行引擎。
 * 职责：寄存器文件、PC、CSR、运行控制标志
 *
 * 依赖：只依赖 types.h（PrivilegeLevel）
 * 被依赖：simulator.h、execute.h、debugger 等各种读取 CPU 状态的模块
 * ============================================================ */

/* ── CPU 状态结构体 ── */
typedef struct {
    /* 通用寄存器 */
    uint32_t regs[32];       // x0-x31，x0 硬连线为 0（每条指令后强制执行 regs[0]=0）

    /* 程序计数器 */
    uint32_t pc;             // 当前正在执行的指令地址

    /* 运行控制 */
    bool     running;        // true = CPU 正在执行主循环

    /* 特权级 */
    PrivilegeLevel priv;     // 当前特权级（模拟器始终为 PRIV_MACHINE）
                             //   0 = 用户态，1 = 管理态，3 = 机器态

    /* ── Machine 模式 CSR（控制和状态寄存器）── */
    uint32_t mstatus;        // Machine Status 机器状态寄存器
                             //   MIE  (bit[3]):  机器态中断使能
                             //   MPIE (bit[7]):  异常前的中断使能
                             //   MPP  (bits[12:11]): 异常前的特权级

    uint32_t mtvec;          // Machine Trap Vector 机器陷阱向量基址
                             //   BASE (bits[31:2]): 异常处理程序入口地址
                             //   MODE (bits[1:0]):  0=直接模式

    uint32_t mepc;           // Machine Exception PC 
                             //   异常发生时保存触发异常的指令 PC

    uint32_t mcause;         // Machine Cause
                             //   bit[31]: 0=异常, 1=中断
                             //   bits[30:0]: 异常/中断编号

    uint32_t mtval;          // Machine Trap Value
                             //   异常相关信息（页错误=错误地址，非法指令=指令编码）
} CPU;

/* 初始化 CPU：寄存器清零，priv = PRIV_MACHINE，running = false */
void cpu_init(CPU *cpu);

/* 重置 CPU（等价于 cpu_init）*/
void cpu_reset(CPU *cpu);

#endif // CPU_H
