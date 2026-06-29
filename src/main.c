#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ============================================================
 * main.c — RISC-V 模拟器入口点
 *
 * ─── 你需要实现的内容 ─────────────────────────────────────
 *
 * 1. 命令行参数解析:
 *    riscv-sim [options] <elf_file>
 *
 *    选项:
 *      -m SIZE    物理内存大小 (默认 128MB)，单位: MB
 *      -s         以调试模式启动 (直接进入 debugger REPL)
 *      -t         开启指令跟踪 (trace，打印每一条执行的指令)
 *      -d         反汇编模式：加载后打印所有指令并退出
 *      -h         显示帮助信息
 *
 *    示例:
 *      riscv-sim hello.elf          直接运行
 *      riscv-sim -s hello.elf       调试模式
 *      riscv-sim -t -s hello.elf    调试+跟踪
 *      riscv-sim -m 256 hello.elf   256MB 内存
 *
 * 2. 启动流程:
 *    a) 解析命令行参数
 *    b) 初始化物理内存 (mem_init)
 *    c) 初始化 MMU (mmu_init)
 *    d) 加载 ELF 文件 (elf_load → entry_addr, stack_addr)
 *    e) 初始化 CPU (cpu_init, 设置 PC=entry, SP=stack)
 *    f) 判断运行模式:
 *       - 调试模式 (-s): 启动 debugger_run()
 *       - 正常模式: 直接调用 cpu_run() 直到退出
 *    g) 程序退出时打印统计信息 (指令数、耗时等)
 *    h) 释放资源 (mem_destroy)
 *
 * 3. 统计信息 (可选):
 *    - 执行的指令总数
 *    - 各类型指令计数 (ALU, Load, Store, Branch, Jump, System)
 *    - 耗时 (可使用 clock() 粗略计时)
 *
 * 4. 错误处理:
 *    - ELF 文件不存在 → 打印错误并退出
 *    - 内存不足 → 打印错误并退出
 *    - 运行时异常 → 打印详细信息 (PC, 指令, 异常类型)
 *
 * ─── 程序结构图 ────────────────────────────────────────────
 *
 *   main()
 *   ├── 解析参数
 *   ├── mem_init()            ← 分配物理内存
 *   ├── mmu_init()            ← 初始化 MMU
 *   ├── elf_load()            ← 加载 ELF 到内存
 *   │   ├── 解析 ELF Header
 *   │   ├── 遍历 Program Headers
 *   │   └── 加载 LOAD 段 + 设置栈
 *   ├── cpu_init()            ← 初始化 CPU 状态
 *   ├── debugger_run() 或 cpu_run()
 *   │   ├── cpu_step()        ← 单步循环
 *   │   │   ├── IF: mmu_read_32  (取值)
 *   │   │   ├── ID: decode()     (译码)
 *   │   │   ├── EX: execute()    (执行)
 *   │   │   └── 更新 PC
 *   │   └── 处理异常/断点/syscall
 *   └── mem_destroy()         ← 释放资源
 * ============================================================
 */

#include "types.h"
#include "memory.h"
#include "mmu.h"
#include "elf_loader.h"
#include "cpu.h"
#include "debugger.h"
#include "syscall.h"

/* ── 全局变量 (来自其他 .c 文件) ───────────────────────── */
extern bool g_trace_enabled;

/* ── 使用说明 ────────────────────────────────────────────── */
static void print_usage(const char *prog)
{
    printf("RISC-V RV32IM Simulator & Debugger\n");
    printf("Usage: %s [options] <elf_file>\n\n", prog);
    printf("Options:\n");
    printf("  -m SIZE    Set physical memory size in MB (default: 128)\n");
    printf("  -s         Start in debug mode (interactive REPL)\n");
    printf("  -t         Enable instruction trace\n");
    printf("  -d         Disassemble mode: dump instructions and exit\n");
    printf("  -h         Show this help message\n\n");
    printf("Debugger Commands:\n");
    printf("  b/reak <addr>     Set breakpoint\n");
    printf("  d/elete <id>      Delete breakpoint\n");
    printf("  i/b nfo break     List breakpoints\n");
    printf("  s/tep             Single step\n");
    printf("  si/stepi [n]      Step n instructions\n");
    printf("  c/ontinue         Continue execution\n");
    printf("  i/r nfo registers Show registers\n");
    printf("  x/<N><F><U> <a>   Examine memory\n");
    printf("  q/uit             Exit\n\n");
    printf("Examples:\n");
    printf("  %s hello.elf              Run directly\n", prog);
    printf("  %s -s hello.elf           Debug mode\n", prog);
    printf("  %s -t -s hello.elf        Debug with trace\n", prog);
}

/* ── 入口点 ──────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    const char  *filename   = NULL;
    uint32_t     mem_size   = MEM_SIZE_DEFAULT;
    bool         debug_mode = false;
    bool         disasm_mode = false;

    // 解析参数
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-s") == 0) {
            debug_mode = true;
        } else if (strcmp(argv[i], "-t") == 0) {
            g_trace_enabled = true;
        } else if (strcmp(argv[i], "-d") == 0) {
            disasm_mode = true;
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            mem_size = (uint32_t)atoi(argv[++i]) * 1024 * 1024;
            if (mem_size == 0) mem_size = MEM_SIZE_DEFAULT;
        } else if (argv[i][0] != '-') {
            filename = argv[i];
        }
    }

    if (!filename) {
        fprintf(stderr, "Error: No ELF file specified\n");
        print_usage(argv[0]);
        return 1;
    }

    /* ── 初始化 ─────────────────────────────────────── */
    printf("RISC-V RV32IM Simulator\n");
    printf("Physical memory: %u MB\n", mem_size / (1024 * 1024));

    PhysicalMemory pmem;
    mem_init(&pmem, mem_size);

    MMUState mmu;
    mmu_init(&mmu);

    CPUState cpu;
    uint32_t entry_addr, stack_addr;

    /* ── 加载 ELF ──────────────────────────────────── */
    printf("Loading %s...\n", filename);
    if (!elf_load(filename, &pmem, &mmu, &entry_addr, &stack_addr)) {
        fprintf(stderr, "Failed to load ELF file\n");
        mem_destroy(&pmem);
        return 1;
    }

    /* ── 初始化 CPU ────────────────────────────────── */
    cpu_init(&cpu, entry_addr, stack_addr);
    printf("Entry point: 0x%08x, Stack: 0x%08x\n", entry_addr, stack_addr);
    printf("PC: 0x%08x, SP: 0x%08x\n\n", cpu.pc, cpu.regs[REG_SP]);

    if (disasm_mode) {
        // TODO: 反汇编模式 — 遍历代码段，逐条译码并打印
        printf("Disassembly mode not yet implemented\n");
    } else if (debug_mode) {
        /* ── 调试模式 ─────────────────────────── */
        DebuggerState dbg;
        debugger_init(&dbg, &cpu, &pmem, &mmu);
        debugger_run(&dbg);
    } else {
        /* ── 直接运行模式 ──────────────────────── */
        cpu_run(&cpu, &pmem, &mmu);
    }

    /* ── 清理 ─────────────────────────────────────── */
    printf("\nSimulation finished.\n");
    mem_destroy(&pmem);
    free(mmu.root_page_table);

    return 0;
}
