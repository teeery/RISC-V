#include "simulator.h"
#include "debugger/debugger.h"
#include <stdio.h>
#include <string.h>

/* ================================================================
 * main.c — RISC-V 模拟器入口点
 *
 * 用法:
 *   ./riscv-sim <elf_file>        直接运行
 *   ./riscv-sim -s <elf_file>     调试模式（进入 Debugger REPL）
 *   ./riscv-sim -t <elf_file>     指令跟踪模式
 *   ./riscv-sim -t -s <elf_file>  调试 + 跟踪
 * ================================================================
 */

static void print_usage(const char *prog)
{
    printf("Usage: %s [options] <elf_file>\n", prog);
    printf("Options:\n");
    printf("  -s    Debug mode (enter interactive debugger)\n");
    printf("  -t    Instruction trace mode\n");
    printf("  -h    Show this help\n");
}

int main(int argc, char *argv[])
{
    bool debug_mode = false;
    bool trace_mode = false;
    const char *elf_path = NULL;

    /* 解析命令行参数 */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0) {
            debug_mode = true;
        } else if (strcmp(argv[i], "-t") == 0) {
            trace_mode = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            elf_path = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!elf_path) {
        fprintf(stderr, "Error: No ELF file specified\n");
        print_usage(argv[0]);
        return 1;
    }

    /* 初始化模拟器 */
    Simulator sim;
    sim_init(&sim);

    /* 加载 ELF */
    if (!sim_load_elf(&sim, elf_path)) {
        fprintf(stderr, "Error: Failed to load '%s'\n", elf_path);
        sim_destroy(&sim);
        return 1;
    }

    if (trace_mode) {
        printf("Trace mode enabled (not yet implemented)\n");
    }

    /* 运行 */
    if (debug_mode) {
        debugger_run(&sim);
    } else {
        sim_run(&sim);
    }

    /* 清理 */
    sim_destroy(&sim);
    return 0;
}
