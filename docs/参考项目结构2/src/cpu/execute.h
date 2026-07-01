/* ============================================================================
 * execute.h — 指令执行
 * ============================================================================
 *
 * 这个文件声明指令执行的入口函数。
 *
 *   // 执行一条已解码的指令，*next_pc 默认为 pc+4，跳转/分支指令会修改
 *   // 返回 true 表示正常执行（包括 ecall），返回 false 表示异常（如未知 opcode）
 *   bool cpu_execute(Simulator *sim, DecodedInstr *d, uint32_t *next_pc);
 *
 *   参数说明：
 *     sim:     模拟器对象，可访问 regs、mmu、统计计数器
 *     d:       已解码的指令
 *     next_pc: 下一 PC 的指针，默认 pc+4，跳转/分支会修改
 *
 * 执行每条指令时需要做的事（以 ADD 为例）：
 *   1. 从 regs 读 rs1, rs2 的值
 *   2. 计算结果: regs[rd] = regs[rs1] + regs[rs2]
 *   3. 注意：如果是 LOAD 指令，需要通过 mmu_read32 等函数访问虚拟内存
 *   4. 注意：如果是 STORE 指令，需要通过 mmu_write32 等函数写入虚拟内存
 *   5. 注意：如果是 ECALL，调用 syscall_handler()
 *   6. 注意：如果是 EBREAK，检查是不是软件断点
 *   7. 注意：如果是 FENCE，什么都不做（模拟器不需要内存屏障）
 *   8. 注意：如果是 CSR 指令，读写对应的 CSR 寄存器
 */

#ifndef EXECUTE_H
#define EXECUTE_H

#include "common.h"
// #include "decode.h"  // DecodedInstr 结构体
// #include "../simulator.h"  // Simulator 结构体

/* ---- 在这里声明 cpu_execute 函数 ---- */

#endif /* EXECUTE_H */
