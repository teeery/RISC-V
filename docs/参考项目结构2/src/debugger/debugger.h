/* ============================================================================
 * debugger.h / debugger.c — 调试器 REPL
 * ============================================================================
 *
 * 调试器提供一个命令行交互界面，让用户控制程序的执行。
 *
 * ---- 提示符 ----
 *
 *   (rvsim)
 *
 * ---- 支持的命令 ----
 *
 *   b / break <addr>    在指定地址设置软件断点
 *                       addr 可以写 0x 前缀的十六进制，或十进制数字
 *                       （选做）支持符号名: b main
 *
 *   d / delete <n>     删除第 n 号断点
 *
 *   info breakpoints   列出所有断点
 *   info registers     打印所有 32 个通用寄存器 + PC 的值
 *                      格式: x0  (zero) = 0x00000000 (0)
 *
 *   c / continue       继续执行，直到下一个断点或程序退出
 *
 *   s / step / stepi   单步执行一条指令，执行完后自动打印 PC 和下一条指令
 *
 *   x / examine <addr> 查看指定地址的内存内容
 *                      x 0x10000 → 显示 16 字节（默认）
 *                      x/32x 0x10000 → 显示 32 个 4 字节字
 *                      同时显示对应的汇编（如果能反汇编）
 *
 *   p / print <reg>    打印单个寄存器值
 *                      p $a0  → $a0 = 0x00000003 (3)
 *                      支持 ABI 名和 x 编号: p x10
 *
 *   disas <addr>       反汇编指定地址的指令
 *                      disas 0x10000 → 0x10000: 13 05 30 00  addi a0, zero, 3
 *
 *   q / quit           退出模拟器
 *
 *   h / help            显示帮助信息
 *
 * ---- 实现结构 ----
 *
 *   void debugger_repl(Simulator *sim) {
 *       char line[256];
 *       printf("RISC-V Simulator Debugger\n");
 *       printf("Loaded program, entry at 0x%08x\n", sim->cpu.pc);
 *
 *       while (true) {
 *           printf("(rvsim) ");
 *           fflush(stdout);
 *           if (!fgets(line, sizeof(line), stdin)) break;
 *
 *           char cmd[32], arg1[64], arg2[64];
 *           int n = sscanf(line, "%31s %63s %63s", cmd, arg1, arg2);
 *
 *           if (strcmp(cmd, "b") == 0 || strcmp(cmd, "break") == 0) {
 *               uint32_t addr = parse_addr(arg1);
 *               bp_set(sim, addr);
 *           }
 *           else if (strcmp(cmd, "c") == 0 || strcmp(cmd, "continue") == 0) {
 *               sim->cpu.running = true;
 *               // 注意：恢复被 ebreak 替换的指令！
 *               bp_restore_all(sim);
 *               cpu_run(sim);  // 会执行到下一个断点或 exit
 *               break;
 *           }
 *           // ... 其他命令
 *       }
 *   }
 *
 * ---- 寄存器 ABI 名称解析（辅助函数）----
 *
 *   int parse_reg_name(const char *name) {
 *       // 支持 $x10, x10, $a0, a0, zero, ra, sp, gp, tp,
 *       //        t0-t6, s0-s11, a0-a7, pc
 *       // 返回寄存器索引（0-31），或 -1 表示非法
 *
 *       // 可以用查表法: 一个 struct { char *name; int idx; } 数组
 *       // 或者 if-else 链
 *   }
 */

#ifndef DEBUGGER_H
#define DEBUGGER_H

#include "common.h"
// #include "../simulator.h"

/* ---- 在这里声明 debugger_repl 等调试器函数 ---- */

#endif /* DEBUGGER_H */
