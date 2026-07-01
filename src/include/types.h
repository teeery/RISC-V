#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * types.h — 公共类型定义
 *
 * 所有模块共享的基础类型：寄存器索引、异常类型、特权级、
 * 指令解码结构体、CPU 状态结构体。
 *
 * 设计决策（见 讨论清单.md 结论 4）：
 *   - CPU 结构体名统一为 CPU（非 CPUState）
 *   - 包含最小 CSR 集合（mstatus / mtvec / mepc / mcause / mtval）
 *   - 暂不包含 mie / mip（单核无外设不需要中断）
 *   - 暂不包含 mscratch（非必需）
 *   - next_pc 通过 cpu_execute 的输出参数传递，不存入 CPU 状态
 * ============================================================
 */

/* ============================================================
 * 1. 寄存器 ABI 名称
 * ============================================================
 */
typedef enum {
    REG_ZERO = 0,   // x0  - 恒为 0
    REG_RA   = 1,   // x1  - 返回地址
    REG_SP   = 2,   // x2  - 栈指针
    REG_GP   = 3,   // x3  - 全局指针
    REG_TP   = 4,   // x4  - 线程指针
    REG_T0   = 5,   // x5  - 临时寄存器
    REG_T1   = 6,   // x6
    REG_T2   = 7,   // x7
    REG_S0   = 8,   // x8  - 帧指针 (fp)
    REG_S1   = 9,   // x9
    REG_A0   = 10,  // x10 - 函数参数 / 返回值
    REG_A1   = 11,  // x11
    REG_A2   = 12,  // x12
    REG_A3   = 13,  // x13
    REG_A4   = 14,  // x14
    REG_A5   = 15,  // x15
    REG_A6   = 16,  // x16
    REG_A7   = 17,  // x17 - syscall 编号
    REG_S2   = 18,  // x18
    REG_S3   = 19,  // x19
    REG_S4   = 20,  // x20
    REG_S5   = 21,  // x21
    REG_S6   = 22,  // x22
    REG_S7   = 23,  // x23
    REG_S8   = 24,  // x24
    REG_S9   = 25,  // x25
    REG_S10  = 26,  // x26
    REG_S11  = 27,  // x27
    REG_T3   = 28,  // x28
    REG_T4   = 29,  // x29
    REG_T5   = 30,  // x30
    REG_T6   = 31,  // x31
    REG_COUNT = 32,
} RegisterID;

/* ============================================================
 * 2. 特权级
 * ============================================================
 */
typedef enum {
    PRIV_USER       = 0,
    PRIV_SUPERVISOR = 1,
    PRIV_MACHINE    = 3,
} PrivilegeLevel;

/* ============================================================
 * 3. 异常 / 中断类型（RISC-V 特权规范）
 *
 * MMU 层通过 ExceptionType *exc 输出参数报告错误：
 *   - EXC_NONE              (-1)  无异常
 *   - EXC_LOAD_ACCESS_FAULT  (5)  页错误 / 越权（读）
 *   - EXC_STORE_ACCESS_FAULT (7)  页错误 / 越权（写）
 *   - EXC_INSTR_ACCESS_FAULT (1)  取指页错误
 *   - EXC_ILLEGAL_INSTRUCTION(2)  非法指令
 *   - EXC_BREAKPOINT         (3)  ebreak / 断点
 * ============================================================
 */
typedef enum {
    EXC_NONE                    = -1,
    EXC_INSTR_ADDR_MISALIGNED   = 0,
    EXC_INSTR_ACCESS_FAULT      = 1,
    EXC_ILLEGAL_INSTRUCTION     = 2,
    EXC_BREAKPOINT              = 3,
    EXC_LOAD_ADDR_MISALIGNED    = 4,
    EXC_LOAD_ACCESS_FAULT       = 5,
    EXC_STORE_ADDR_MISALIGNED   = 6,
    EXC_STORE_ACCESS_FAULT      = 7,
    EXC_ECALL_FROM_U            = 8,
    EXC_ECALL_FROM_S            = 9,
    EXC_ECALL_FROM_M            = 11,
} ExceptionType;

/* ============================================================
 * 4. 指令解码
 * ============================================================
 */

/* 指令格式 */
typedef enum {
    FMT_R,      // R-type:  ADD rd, rs1, rs2
    FMT_I,      // I-type:  ADDI rd, rs1, imm  (含 load / jalr / ecall)
    FMT_S,      // S-type:  SW rs2, imm(rs1)
    FMT_B,      // B-type:  BEQ rs1, rs2, imm
    FMT_U,      // U-type:  LUI rd, imm
    FMT_J,      // J-type:  JAL rd, imm
} InstructionFormat;

/* 解码后的指令 */
typedef struct {
    uint32_t raw;           // 原始 32 位指令
    uint8_t  opcode;        // 低 7 位操作码
    uint8_t  rd;            // 目标寄存器  (5 bits)
    uint8_t  rs1;           // 源寄存器 1  (5 bits)
    uint8_t  rs2;           // 源寄存器 2  (5 bits)
    uint8_t  funct3;        // funct3 字段 (3 bits)
    uint8_t  funct7;        // funct7 字段 (7 bits, R-type)
    int32_t  imm;           // 解码后的立即数（已符号扩展）
    InstructionFormat fmt;  // 指令格式
} DecodedInstruction;

/* ============================================================
 * 5. CPU 状态（结论 4：最小 CSR 集合）
 *
 * 注意：
 *   - 结构体名统一为 CPU（非 CPUState）
 *   - next_pc 不在此处，通过 cpu_execute 的 uint32_t *next_pc 输出参数传递
 *   - x0 硬连线保护在 cpu_execute 末尾强制执行 regs[0] = 0
 * ============================================================
 */
typedef struct {
    uint32_t regs[REG_COUNT];   // 32 个通用寄存器 (x0-x31)
    uint32_t pc;                // 程序计数器
    bool     running;           // 运行标志
    PrivilegeLevel priv;        // 当前特权级

    /* M 模式 CSR（最小集合，结论 4） */
    uint32_t mstatus;   // 机器状态 (MIE, MPIE, MPP)
    uint32_t mtvec;     // 陷阱向量基址
    uint32_t mepc;      // 异常返回地址
    uint32_t mcause;    // 异常原因
    uint32_t mtval;     // 异常附加信息
} CPU;

/* ============================================================
 * 6. 内存访问宽度
 * ============================================================
 */
typedef enum {
    WIDTH_BYTE   = 1,
    WIDTH_HALF   = 2,
    WIDTH_WORD   = 4,
} MemAccessWidth;

#endif // TYPES_H
