#include "simulator.h"
#include "debugger/debugger.h"
#include "debugger/web_server.h"
#include "default_elf.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* ================================================================
 * main.c — RISC-V 模拟器入口点
 *
 * 用法:
 *   ./riscv-sim <elf_file>        直接运行
 *   ./riscv-sim -s <elf_file>     调试模式（进入 Debugger REPL）
 *   ./riscv-sim -t <elf_file>     指令跟踪模式
 *   ./riscv-sim -t -s <elf_file>  调试 + 跟踪
 *   ./riscv-sim -w <port> <elf>   Web 调试器模式
 * ================================================================
 */

static void print_usage(const char *prog)
{
    printf("Usage: %s [options] [elf_file]\n", prog);
    printf("Options:\n");
    printf("  -m <model>  CPU model: single (default), multi, pipeline\n");
    printf("  -s          Debug mode (enter interactive debugger)\n");
    printf("  -t          Instruction trace mode\n");
    printf("  -w <port>   Web debugger mode (HTTP server on given port)\n");
    printf("  -h          Show this help\n");
    printf("\nIf no ELF file is specified, a built-in demo program is loaded.\n");
}

static CpuModel parse_cpu_model(const char *s)
{
    if (strcmp(s, "multi")    == 0) return MODEL_MULTI_CYCLE;
    if (strcmp(s, "pipeline") == 0) return MODEL_PIPELINE;
    if (strcmp(s, "single")   == 0) return MODEL_SINGLE_CYCLE;
    fprintf(stderr, "Warning: unknown CPU model '%s', using single-cycle\n", s);
    return MODEL_SINGLE_CYCLE;
}

int main(int argc, char *argv[])
{
    bool debug_mode = false;
    bool trace_mode = false;
    bool web_mode   = false;
    int  web_port   = 8080;
    CpuModel cpu_model = MODEL_SINGLE_CYCLE;
    const char *elf_path = NULL;

    /* 解析命令行参数 */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0) {
            debug_mode = true;
        } else if (strcmp(argv[i], "-t") == 0) {
            trace_mode = true;
        } else if (strcmp(argv[i], "-m") == 0) {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                cpu_model = parse_cpu_model(argv[++i]);
            } else {
                fprintf(stderr, "Error: -m requires a model name (single/multi/pipeline)\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-w") == 0) {
            web_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                char *endptr;
                long val = strtol(argv[++i], &endptr, 10);
                if (*endptr != '\0' || val <= 0 || val > 65535) {
                    fprintf(stderr, "Error: Invalid port number '%s'\n", argv[i]);
                    return 1;
                }
                web_port = (int)val;
            }
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
        /* 未指定 ELF 文件：使用内置 demo 程序 */
        const char *default_elf = "builtin_hello.elf";
        FILE *fp = fopen(default_elf, "wb");
        if (fp) {
            fwrite(DEFAULT_ELF_DATA, 1, DEFAULT_ELF_SIZE, fp);
            fclose(fp);
            elf_path = default_elf;
            printf("No ELF specified, using built-in demo program.\n");
        } else {
            fprintf(stderr, "Error: Failed to write built-in ELF\n");
            return 1;
        }
    }

    /* 初始化模拟器 */
    Simulator sim;
    sim_init(&sim);
    sim.cpu_model = cpu_model;

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
    if (web_mode) {
        /* Web 调试器模式：阻塞直到服务器退出 */
        printf("Starting web debugger on port %d...\n", web_port);
        int ret = web_server_start(&sim, web_port, elf_path);
        if (ret != EXIT_SUCCESS) {
            sim_destroy(&sim);
            return 1;
        }
    } else if (debug_mode) {
        debugger_run(&sim);
    } else {
        sim_run(&sim);
    }

    /* 清理 */
    sim_destroy(&sim);
    return 0;
}
