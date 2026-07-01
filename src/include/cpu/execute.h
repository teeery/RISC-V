#ifndef EXECUTE_H
#define EXECUTE_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * execute.h — CPU 指令执行接口
 *
 * 核心函数：
 *   cpu_execute() — 执行一条已解码的指令
 *   cpu_trap()    — 异常处理（填写 CSR 并跳转到 mtvec）
 *
 * 依赖：前置声明 Simulator、DecodedInsn（避免循环 include）
 *       cpu_execute 内部通过 Simulator* 访问 mmu、pmem、breakpoints
 * ============================================================ */

/* 前置声明（真实类型定义在 simulator.h 和 decode.h 中）*/
struct Simulator;
typedef struct Simulator Simulator;

struct DecodedInsn;
typedef struct DecodedInsn DecodedInsn;

/* 执行一条已解码的指令
 *
 * 参数：
 *   sim      — 模拟器对象（通过它访问 mmu、pmem、breakpoints、cpu）
 *   d        — 已解码的指令（由 cpu_decode 产出）
 *   next_pc  — [out] 下一 PC 值（默认 pc+4，跳转/分支指令会覆写）
 *
 * 返回：
 *   true  = 正常执行
 *   false = 非法指令等致命错误（当前阶段极少发生，异常通常走 cpu_trap）
 *
 * CPU 内部调用约定：
 *   - 通过 sim->cpu 访问寄存器文件
 *   - 通过 sim->mmu + sim->pmem 访问内存（只调 mmu_read/write_*，不直接调 mem_*）
 *   - 每条指令执行后必须执行 sim->cpu.regs[0] = 0（x0 硬连线）
 */
bool cpu_execute(Simulator *sim, DecodedInsn *d, uint32_t *next_pc);

/* 异常处理：按 RISC-V 特权规范填写 CSR 并跳转到 mtvec
 *
 * 调用时机：
 *   - MMU 操作返回 false（取指失败、访存失败、页错误等）
 *   - CPU 内部检测到非法指令
 *   - ecall 系统调用
 *   - ebreak 断点（非 Debugger 管理的断点时）
 *
 * 行为：
 *   1. mepc   = pc           （保存异常地址）
 *   2. mcause = exc           （保存异常原因）
 *   3. mtval  = tval          （保存错误地址/指令编码）
 *   4. mstatus: MPP ← priv, MPIE ← MIE, MIE ← 0
 *   5. pc     = mtvec & ~0x3u （跳转异常处理程序）
 *
 * Fallback：若 mtvec == 0（未初始化异常处理程序），printf 错误信息后
 *           设置 cpu->running = false 退出模拟
 */
void cpu_trap(Simulator *sim, uint32_t exc, uint32_t tval);

#endif // EXECUTE_H
