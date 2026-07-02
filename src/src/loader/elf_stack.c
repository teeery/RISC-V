/* ============================================================================
 * elf_stack.c — 用户栈初始化
 * ============================================================================
 *
 * 职责：在物理内存中为用户程序分配栈空间。
 *
 * ── 栈是什么 ────────────────────────────────────────────────
 *
 *   栈是一块"向下增长"的内存区域。CPU 的 sp 寄存器指向栈顶（高位地址），
 *   函数调用时 sp 减小来"分配"栈帧，函数返回时 sp 增大来"释放"栈帧。
 *
 *   我们的栈布局（从高地址到低地址）：
 *
 *     stack_top = 0x07F00000  ─┐
 *                              │ ← sp 寄存器指向这里（高地址）
 *                              │    函数调用向 ↓ 方向增长
 *                              │
 *                              │    当前简化版：栈内容是空的
 *                              │    用户程序的第一个函数调用
 *                              │    直接从 sp 开始写
 *                              │
 *     stack_base = 0x07EC0000  ─┘ ← 栈底（低地址）
 *            = 0x07F00000 - 256KB
 *
 * ── 完善版 TODO ───────────────────────────────────────────
 *
 *   真实 OS 在栈顶放置启动信息（从高到低排列）：
 *
 *     [ environment strings ]  ← 环境变量字符串（如 "PATH=/bin"）
 *     [ argument strings    ]  ← 命令行参数字符串（如 "./hello"）
 *     [ auxv array          ]  ← 辅助向量（AT_PHDR, AT_PHENT, AT_PHNUM,
 *                                  AT_PAGESZ=4096, AT_NULL=0 结束）
 *     [ argv array          ]  ← 指向 argument strings 的指针数组
 *     [ argc                ]  ← 命令行参数个数（sp 最终指向这里）
 *
 *   当前简化版不放置这些信息——对不接收命令行参数、不使用环境变量的
 *   简单程序（如 hello 程序）来说完全够用。
 *
 * ── 为什么栈区域不可执行 ──────────────────────────────────
 *
 *   权限设为 MEM_READ | MEM_WRITE（不设 MEM_EXEC）。
 *   这是现代 OS 的安全标准：栈上的数据不应该被执行。
 *   如果程序试图执行栈上的代码（如 buffer overflow 攻击），
 *   在完善版中 MMU 会因为 X 位未设置而抛出页错误。
 * ============================================================================
 */

#include "loader_internal.h"  // 已间接包含 elf_loader.h + stdio.h + 依赖模块 + 常量 + 声明

bool elf_setup_stack(PhysicalMemory *pmem, uint32_t *stack_top)
{
    /*
     * 计算栈基址（栈的起始物理地址）
     *
     * stack_top  = 0x07F00000（高位，128MB 物理内存内，栈向下增长）
     * stack_base = 0x07F00000 - 256KB = 0x07EC0000
     *
     * 两个地址都在 3GB 附近，给代码段/数据段/堆留下了
     * 0x00000000 ~ 0xBFFBFFFF 的广阔空间。
     */
    /* stack_base = 0x07F00000 - 256KB = 0x07EC0000（128MB 物理内存内）*/
    uint32_t stack_base = STACK_TOP_DEFAULT - STACK_SIZE_DEFAULT;

    /*
     * 在物理内存中注册栈区域
     *
     * mem_map 参数说明：
     *   pmem            — 物理内存管理器
     *   stack_base      — 栈区域起始地址（0xBFFC0000）
     *   STACK_SIZE_DEFAULT — 栈大小（256KB = 262144 字节）
     *   MEM_READ|MEM_WRITE — 可读可写，不可执行（安全）
     *   "stack"         — 区域名称，调试用（mem_dump 时会显示）
     *
     * 如果 mem_map 返回 false，说明区域映射失败（可能与其他区域重叠），
     * 这是一个严重错误——打印信息后返回 false 让调用者处理。
     */
    if (!mem_map(pmem, stack_base, STACK_SIZE_DEFAULT,
                 MEM_READ | MEM_WRITE, "stack")) {
        fprintf(stderr, "Error: Failed to allocate stack region "
                "(0x%08x ~ 0x%08x)\n",
                stack_base, STACK_TOP_DEFAULT);
        return false;
    }

    /*
     * 设置栈顶地址
     *
     * sp（stack pointer）= 0x07F00000，指向栈的最高地址。
     * 用户程序第一条使用栈的指令会从这个地址开始写。
     * 按照 RISC-V ABI，sp 应该保持 16 字节对齐——
     * 0x07F00000 正好是 16 的倍数 ✓
     *
     * 这个值通过输出参数返回，由调用者（main.c）写入 cpu.regs[2]。
     */
    *stack_top = STACK_TOP_DEFAULT;

    /*
     * TODO（完善版）：在栈上放置 argc / argv / auxv
     *
     * 如果后续需要支持 main(argc, argv) 参数传递，需要在此处
     * 从高位向低位依次写入 auxv 数组、argv 指针数组、argc 值，
     * 并把最终的 sp 调整到 argc 的位置。
     *
     * 需要的常量（来自 Linux ELF ABI）：
     *   #define AT_NULL     0    // auxv 结束标记
     *   #define AT_PAGESZ   6    // 系统页大小 = 4096
     *   #define AT_PHDR     3    // Program Header 表虚拟地址
     *   #define AT_PHENT    4    // Program Header 条目大小 = 32
     *   #define AT_PHNUM    5    // Program Header 数量
     */

    return true;
}
