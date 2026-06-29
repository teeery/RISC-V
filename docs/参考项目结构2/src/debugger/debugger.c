/* ============================================================================
 * debugger.c — 调试器 REPL 实现
 * ============================================================================
 *
 * 实现调试器的主循环和所有命令处理。
 *
 * 关键细节：
 *
 *   1. parse_addr 函数:
 *      - 如果以 0x 开头 → strtoul(..., NULL, 16)
 *      - 否则如果第一个字符是字母 → 可能是指令地址标签（搜索符号表）
 *      - 否则 → strtoul(..., NULL, 10)
 *
 *   2. info registers 输出格式:
 *      x0  (zero) = 0x00000000 (0)
 *      x1  (ra)   = 0x000100A0 (65696)
 *      ...
 *      PC          = 0x00010074
 *
 *   3. x 命令（查看内存）输出格式:
 *      (rvsim) x 0x10000
 *      0x10000: 97 01 00 00 93 81 01 80  13 05 00 00 13 06 00 00   |................|
 *      0x10010: 03 07 00 00 83 07 00 00  67 80 00 00 00 00 00 00   |........g.......|
 *      （左边是地址，中间是十六进制，右边是 ASCII 可打印字符）
 *
 *   4. disas 命令:
 *      调用 cpu_disasm() 函数，输出反汇编结果
 *
 *   5. 帮助信息:
 *      printf("Commands:\n");
 *      printf("  b/break <addr>     Set breakpoint\n");
 *      printf("  d/delete <n>       Delete breakpoint\n");
 *      printf("  info breakpoints   List breakpoints\n");
 *      printf("  info registers     Show all registers\n");
 *      printf("  c/continue         Continue execution\n");
 *      printf("  s/step/stepi       Single step\n");
 *      printf("  x <addr>           Examine memory\n");
 *      printf("  p/print <reg>      Print register\n");
 *      printf("  disas <addr>       Disassemble\n");
 *      printf("  q/quit             Quit\n");
 */

#include "debugger.h"
// #include "breakpoint.h"
// #include "../cpu/decode.h"
// #include "../simulator.h"

/* ---- 在这里实现 debugger_repl 和辅助函数 ---- */
