/* ============================================================================
 * main.c — 程序入口
 * ============================================================================
 *
 * 用法：
 *   rvsim [options] <elf_file>
 *
 *   选项：
 *     -d          启动调试器模式
 *     -s          执行前进入单步模式
 *     -p          启用流水线模式（选做）
 *     --stats     运行结束后输出 CPI 统计
 *
 * 示例：
 *   rvsim hello              → 直接运行 hello 程序
 *   rvsim -d hello           → 启动调试器加载 hello
 *   rvsim -s -d hello        → 启动调试器，进入单步模式
 *
 * main() 需要做的事：
 *   1. 解析命令行参数
 *   2. 创建 Simulator 对象
 *   3. 根据参数设置 debug_mode, single_step, pipeline_mode
 *   4. 调用 sim_run(sim, elf_path)
 *   5. 输出程序退出码
 *   6. 如果 --stats，输出 CPI 统计数据
 *
 * 最小实现示例：
 *   int main(int argc, char *argv[]) {
 *       if (argc < 2) {
 *           fprintf(stderr, "Usage: rvsim [-d] <elf_file>\n");
 *           return 1;
 *       }
 *
 *       Simulator sim;
 *       sim_init(&sim);
 *
 *       const char *elf_path = argv[argc - 1];
 *       for (int i = 1; i < argc - 1; i++) {
 *           if (strcmp(argv[i], "-d") == 0) sim.debug_mode = true;
 *           if (strcmp(argv[i], "-s") == 0) sim.single_step = true;
 *           if (strcmp(argv[i], "-p") == 0) sim.pipeline_mode = true;
 *       }
 *
 *       int exit_code = sim_run(&sim, elf_path);
 *       printf("Program exited with code %d\n", exit_code);
 *       return 0;
 *   }
 */

#include "simulator.h"

int main(int argc, char *argv[]) {
    /* ---- 在这里实现命令行解析和模拟器启动 ---- */

    printf("RISC-V Simulator — not implemented yet\n");
    printf("Usage: rvsim [-d] [-s] [--stats] <elf_file>\n");

    return 0;
}
