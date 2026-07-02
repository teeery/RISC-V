/* ============================================================================
 * elf_load.c — ELF 加载器主入口
 * ============================================================================
 *
 * 职责：编排整个 ELF 加载流程。这是 Loader 模块对外唯一入口的实现。
 *
 * ── 位置 ───────────────────────────────────────────────────
 *
 *   在整个模拟器数据流中，elf_load() 是第一个干活的函数：
 *
 *     main.c → sim_init() → sim_load_elf() → ★ elf_load() → sim_run()
 *
 *   elf_load() 完成从"磁盘上的 ELF 文件"到"物理内存中的可执行镜像"的转换。
 *   返回后，CPU 就可以从 entry 地址开始取指执行了。
 *
 * ── 编排设计 ───────────────────────────────────────────────
 *
 *   本函数是"指挥者"而非"执行者"：
 *     - 不关心怎么校验 ELF → 委托 elf_validate_header()
 *     - 不关心怎么搬运段数据 → 委托 elf_load_segment()
 *     - 不关心怎么初始化栈   → 委托 elf_setup_stack()
 *
 *   编排者只负责：
 *     1. 按正确顺序调度上述函数
 *     2. 在失败时正确清理资源（关闭文件）
 *     3. 把各步骤的产物（entry, stack_top）汇总输出
 *
 * ── 加载顺序说明 ───────────────────────────────────────────
 *
 *   为什么先关文件再设栈？
 *     - fclose 只需要 FILE*，不依赖内存状态
 *     - elf_setup_stack 操作的是内存，不依赖文件
 *     - 先释放文件资源是最安全的做法（即使后续步骤失败，文件也不会泄露）
 *
 *   为什么 entry 在 fclose 前就取好？
 *     - ehdr 是栈上变量，读完 Header 后 e_entry 已经在内存中
 *     - fclose 不会影响 ehdr，所以先取后取都可以
 *     - 放在 fclose 前、校验后是为了让"解析 ELF → 取 entry"逻辑紧凑
 * ============================================================================
 */

#include "loader_internal.h"  // 已间接包含 elf_loader.h + stdio.h + 依赖模块 + 内部声明

bool elf_load(const char *filename, PhysicalMemory *pmem, MMUState *mmu,
              uint32_t *entry, uint32_t *stack_top)
{
    /*
     * ── 步骤 1：打开 ELF 文件 ──
     *
     * "rb" = 只读 + 二进制模式。
     * 二进制模式在 Linux 上无区别，但在 Windows 上不会做 \r\n 转换，
     * 保证读到的字节和文件中的完全一致。
     */
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("Failed to open ELF file");     // perror 会附加系统错误信息
        return false;
    }

    /*
     * ── 步骤 2：读取 ELF Header（文件开头 52 字节）──
     *
     * fread 直接把二进制数据灌入结构体。这要求：
     *   1. Elf32_Ehdr 的字段顺序和 ELF 规范完全一致 ✓（已在 elf_loader.h 验证）
     *   2. 编译器没有插入 padding（因为字段都是自然对齐的）✓
     *   3. 宿主机的端序和 ELF 一致（都是小端序）✓
     *
     * 如果文件不足 52 字节，fread 返回 < 1，说明文件损坏。
     */
    Elf32_Ehdr ehdr;
    if (fread(&ehdr, sizeof(ehdr), 1, fp) != 1) {
        fprintf(stderr, "Error: Failed to read ELF header "
                "(file too small?)\n");
        fclose(fp);
        return false;
    }

    /*
     * ── 步骤 3：校验 ELF Header ──
     *
     * 五重检查：magic → 32bit → 小端 → RISC-V → 可执行
     * 任一失败都会打印具体原因并返回 false。
     * 校验不通过的文件不能加载——这不是"尽力而为"，而是"不合法就拒绝"。
     */
    if (!elf_validate_header(&ehdr)) {
        fclose(fp);  // 校验失败也要关文件
        return false;
    }

    /*
     * ── 步骤 4：遍历 Program Headers，加载 PT_LOAD 段 ──
     *
     * Program Header Table 是一组连续的 32 字节条目，每个描述一个段。
     * 表中位置由 ehdr.e_phoff（文件偏移）和 ehdr.e_phnum（条目数）确定。
     *
     * 遍历逻辑：
     *   for i = 0 to e_phnum:
     *     1. 计算条目在文件中的偏移 = e_phoff + i * sizeof(Elf32_Phdr)
     *     2. fseek 跳过去 + fread 读 32 字节
     *     3. 如果 p_type == PT_LOAD → 加载它
     *     4. 其他类型（PT_NOTE, PT_GNU_STACK 等）跳过
     *
     * 一个典型 ELF 有 2 个 PT_LOAD 段（.text + .data），
     * 外加 1~2 个非 LOAD 段（NOTE, GNU_STACK）。
     */
    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf32_Phdr phdr;

        /* 计算 Program Header #i 在文件中的位置 */
        long offset = ehdr.e_phoff + i * sizeof(Elf32_Phdr);
        fseek(fp, offset, SEEK_SET);

        /* 读取 32 字节 Program Header */
        if (fread(&phdr, sizeof(phdr), 1, fp) != 1) {
            fprintf(stderr, "Error: Failed to read program header %d\n", i);
            fclose(fp);
            return false;
        }

        /*
         * 只处理 PT_LOAD 段（p_type == 1）。
         * 其他段类型（PT_DYNAMIC, PT_NOTE, PT_GNU_STACK 等）
         * 在加载阶段不需要——它们服务于动态链接器或调试器。
         */
        if (phdr.p_type == PT_LOAD) {
            if (!elf_load_segment(fp, &phdr, pmem, mmu)) {
                fclose(fp);  // 段加载失败 → 立即失败，不继续加载后续段
                return false;
            }
        }
    }

    /*
     * ── 步骤 5：关闭 ELF 文件 ──
     *
     * 所有段数据已经通过 mem_load 写入物理内存，不再需要文件。
     * 提前关闭避免占用文件句柄。
     */
    fclose(fp);

    /*
     * ── 步骤 6：设置程序入口地址 ──
     *
     * ehdr.e_entry 是链接器指定的程序入口虚拟地址。
     * 对于 C 程序，这是 _start 符号的地址（不是 main！）。
     * _start 在调用 main 之前会初始化 libc 环境。
     *
     * 这个值通过输出参数 *entry 返回给调用者，
     * 由 main.c 写入 sim->cpu.pc（团队结论 7）。
     */
    *entry = ehdr.e_entry;

    /*
     * ── 步骤 7：初始化用户栈 ──
     *
     * 栈独立于 ELF 段——它在 ELF 文件中没有对应数据。
     * Loader 负责在物理内存中分配栈空间（默认 256KB），
     * 并返回栈顶地址 0xC0000000（团队结论 7）。
     *
     * 调用者负责把这个值写入 sim->cpu.regs[2]（sp 寄存器）。
     */
    if (!elf_setup_stack(pmem, stack_top)) {
        return false;  // 栈分配失败——rare，通常是系统内存耗尽
    }

    /*
     * ── 步骤 8：打印加载信息 ──
     *
     * 调试用输出。格式：
     *   ELF loaded: hello.elf
     *     Entry:  0x000100b0
     *     Stack:  0xc0000000 (size: 256 KB)
     *
     * 具体的 entry 值取决于链接脚本；
     * 用 riscv32-unknown-elf-ld 默认链接时通常在 0x000100B0 附近。
     */
    printf("ELF loaded: %s\n", filename);
    printf("  Entry:  0x%08x\n", *entry);
    printf("  Stack:  0x%08x (size: %u KB)\n",
           *stack_top, STACK_SIZE_DEFAULT / 1024);

    return true;
}
