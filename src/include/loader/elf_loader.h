/* ============================================================================
 * elf_loader.h — ELF32 可执行文件加载器（对外接口）
 * ============================================================================
 *
 * ─── 本文件的作用 ──────────────────────────────────────────────
 *
 *   1. 定义 ELF32 文件格式的 C 语言结构体（Elf32_Ehdr、Elf32_Phdr）
 *   2. 定义 ELF 规范要求的常量（magic、类型、权限位）
 *   3. 声明 Loader 模块的唯一对外入口 elf_load()
 *
 *   外部模块（Simulator / main.c）只需要 #include 本文件即可调用 elf_load()。
 *   内部实现细节（校验、段加载、栈初始化）在 src/src/loader/ 下的 .c 文件中，
 *   外部不可见，不需要也不应该直接调用。
 *
 * ─── 对齐的团队结论 ──────────────────────────────────────────
 *
 *   【结论 1 — 两层内存架构】（团队讨论清单 §结论1）
 *     Loader 同时操作两层：
 *       - PhysicalMemory 层：mem_map() 注册区域 → mem_load() 写入段数据
 *       - MMU 层（完善版）：mmu_map_page() 建立虚拟地址 → 物理地址的页表映射
 *     当前简化版用恒等映射（paddr = p_vaddr），绕开 mem_find_free()。
 *     完善版需要 mem_find_free() 动态分配物理页 + mmu_map_page() 逐页映射。
 *
 *   【结论 2 — Memory 接口规范】（团队讨论清单 §结论2）
 *     - 返回值统一为 bool（true=成功, false=失败）
 *     - CPU 只调 mmu_* 层，不直接调 mem_*
 *     - Loader 调 mem_map + mem_load + mmu_map_page（跨两层）
 *
 *   【结论 7 — Loader → CPU 参数传递】（团队讨论清单 §结论7）
 *     - 栈顶地址：0xC0000000（和参考项目2 一致，接近真实 Linux 3GB 用户空间顶部）
 *     - 栈大小：256KB（0x40000），范围 0xBFFC0000 ~ 0xC0000000
 *     - Loader 通过输出参数 *entry 和 *stack_top 返回两个值
 *     - 由 main.c 或 sim_load_elf() 负责写入 sim->cpu.pc 和 sim->cpu.regs[2]
 *     - Loader 不传 argc/argv/envp，只设 sp
 *
 *   【结论 5 — syscall 归属】（不影响 Loader 接口，仅作参考）
 *     syscall 在 cpu_execute() 的 ecall case 中内联处理，不独立为模块。
 *     Loader 不参与系统调用，只在加载阶段设置初始栈。
 *
 *   【结论 9 — 文件组织】
 *     头文件按模块分目录：src/include/{cpu,memory,loader,debugger}/
 *     #include 写法：#include "types.h" 或 #include "memory/memory.h"
 *     编译时 -Isrc/include
 *
 * ─── 权限转换链路（重要）─────────────────────────────────────
 *
 *   ELF 文件中的 p_flags 使用的是 PF_* 位掩码（PF_R=4, PF_W=2, PF_X=1），
 *   这和模拟器内部 MEM_*（MEM_READ=1, MEM_WRITE=2, MEM_EXEC=4）的 bit 位置不同，
 *   也和 MMU 页表用的 PTE_*（PTE_READ=2, PTE_WRITE=4, PTE_EXEC=8）不同。
 *
 *   转换流程（elf_load_segment 内部完成）：
 *
 *     ELF p_flags (PF_*)             MEM_*              PTE_*（完善版用）
 *     ──────────────────    ──────────────────    ─────────────────────
 *     PF_R=4  (0100)   →    MEM_READ=1  (001)  →  PTE_VALID|PTE_READ  = 0x03
 *     PF_W=2  (0010)   →    MEM_WRITE=2 (010)  →  PTE_VALID|PTE_WRITE = 0x05
 *     PF_X=1  (0001)   →    MEM_EXEC=4  (100)  →  PTE_VALID|PTE_EXEC  = 0x09
 *
 *   使用 types.h 中的 mem_perm_to_pte_flags() 完成 MEM_* → PTE_* 的转换。
 *   这个 inline 函数保证 PTE 始终设置 VALID 位（PTE_VALID=1），
 *   并根据 MEM 权限位设置对应的 PTE R/W/X 位。
 *
 * ─── 依赖关系 ────────────────────────────────────────────────
 *
 *   编译期依赖（#include 链）：
 *     elf_loader.h → types.h   (PrivilegeLevel, ExceptionType, MEM_*, PTE_*,
 *                                mem_perm_to_pte_flags())
 *                  → memory.h  (PhysicalMemory, MemoryRegion, mem_* 系列函数)
 *                  → mmu.h     (MMUState, mmu_map_page)
 *
 *   运行时依赖（函数调用链，详见设计文档 §3.3）：
 *     main.c / sim_load_elf()
 *       └─ elf_load()                         ← 本接口
 *            ├─ elf_validate_header()          ← src/src/loader/elf_validate.c
 *            ├─ elf_load_segment() × N         ← src/src/loader/elf_segment.c
 *            │    ├─ mem_map(pmem, ...)        ← memory 模块（焕聪）
 *            │    ├─ mem_load(pmem, ...)       ← memory 模块
 *            │    └─ mmu_map_page(mmu, ...)    ← MMU 模块 [完善版]
 *            └─ elf_setup_stack()              ← src/src/loader/elf_stack.c
 *                 └─ mem_map(pmem, ...)        ← memory 模块
 *
 * ─── 使用示例 ────────────────────────────────────────────────
 *
 *   #include "elf_loader.h"
 *
 *   int main(void) {
 *       PhysicalMemory pmem;
 *       MMUState       mmu;
 *       uint32_t       entry, stack_top;
 *
 *       mem_init(&pmem, 128 * 1024 * 1024);   // 分配 128MB 物理内存
 *       mmu_init(&mmu);                       // 初始化 MMU（页表为空，Bare 模式）
 *
 *       if (!elf_load("hello.elf", &pmem, &mmu, &entry, &stack_top)) {
 *           fprintf(stderr, "Failed to load ELF\n");
 *           return 1;
 *       }
 *
 *       // 调用者负责设置 CPU 初始状态（团队结论 7）
 *       sim->cpu.pc = entry;                  // 程序入口地址
 *       sim->cpu.regs[2] = stack_top;         // sp = 栈顶 0xC0000000
 *       // 其他 31 个寄存器保持 0，由程序自己初始化
 *
 *       sim_run(&sim);                        // 开始执行
 *       return 0;
 *   }
 *
 * ─── ELF 文件结构总览 ────────────────────────────────────────
 *
 *   ┌──────────────────────┐ ← 文件开头（偏移 0x00）
 *   │   ELF Header (52B)   │    魔数 0x7F 'E' 'L' 'F'
 *   │                      │    e_type, e_machine, e_entry
 *   │                      │    e_phoff → 指向 Program Headers
 *   │                      │    e_phnum → Program Header 的数量
 *   ├──────────────────────┤ ← ehdr.e_phoff
 *   │ Program Header [0]   │    p_type=PT_LOAD, p_offset, p_vaddr
 *   │ Program Header [1]   │    p_filesz, p_memsz, p_flags
 *   │ ...                  │    每个 32 字节
 *   ├──────────────────────┤ ← phdr.p_offset
 *   │ .text 段数据         │    代码段：p_flags = PF_R|PF_X (5)
 *   │ .data 段数据         │    数据段：p_flags = PF_R|PF_W (6)
 *   │ .bss（不占文件空间） │    p_memsz > p_filesz 的部分
 *   └──────────────────────┘
 *
 * ─── 设计要点 & 限制 ─────────────────────────────────────────
 *
 *   1. 只支持 ELF32 / RV32 / 小端序。64 位或大端序 ELF 会在 elf_validate_header()
 *      中被拒绝并打印错误信息。
 *   2. 只加载 PT_LOAD 类型的段。其他段（PT_DYNAMIC, PT_NOTE, PT_GNU_STACK 等）
 *      在遍历时跳过。
 *   3. p_memsz > p_filesz 时，多出部分是 .bss 段（未初始化全局变量），
 *      通过 calloc + mem_load 写入零缓冲区实现清零。
 *   4. 当前简化版使用恒等映射（物理地址 = 虚拟地址），mem_find_free() 和
 *      mmu_map_page() 留到完善版。
 *   5. 栈放在 0xC0000000（高位地址），向下增长 256KB。不传 argc/argv。
 *   6. 错误处理：遇到任何问题打印 stderr 并返回 false，不抛异常。
 * ============================================================================
 */

#ifndef ELF_LOADER_H
#define ELF_LOADER_H

/* ── 标准库 ───────────────────────────────────────────────────
 * stdint.h  : uint8_t, uint16_t, uint32_t, int32_t — 确保跨平台固定宽度
 * stdbool.h : bool, true, false — 所有函数返回 bool
 * stdio.h   : FILE* — elf_load_segment 内部需要 FILE 指针
 *             注意：FILE 只在内部 .c 中使用，头文件不暴露 FILE 参数
 * ─────────────────────────────────────────────────────────── */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* ── 公共类型 & 依赖模块（对齐并行开发方案 §0.8 合约）────────────
 *
 * 以下头文件位于 src/include/，编译时通过 -Isrc/include 找到。
 * 路径与并行开发方案 "文件总览" 中的目录结构一致：
 *
 *   types.h            — PrivilegeLevel, ExceptionType, MEM_*, PTE_*,
 *                        mem_perm_to_pte_flags()
 *   memory/memory.h    — PhysicalMemory, MemoryRegion, mem_* 接口
 *   memory/mmu.h       — MMUState, mmu_* 接口
 * ─────────────────────────────────────────────────────────── */

#include "types.h"
#include "memory/memory.h"
#include "memory/mmu.h"

/* ============================================================
 * elf_loader.h — ELF32/ELF64 文件解析与加载器
 *
 * 需要编写的内容：
 * 1. elf_load()           — 加载 ELF 文件，返回入口地址
 * 2. elf_parse_header()   — 解析 ELF Header (e_ident, e_type, e_machine, e_entry)
 * 3. elf_load_segments()  — 遍历 Program Header Table，加载 LOAD 段到内存
 * 4. elf_setup_stack()    — 设置用户栈 (argc, argv, envp, auxv)
 * 5. elf_validate()       — 校验 ELF 魔数 (0x7f 'E' 'L' 'F') 和架构 (EM_RISCV=243)
 *
 * ELF 结构概述：
 * ┌─────────────────────┐
 * │   ELF Header        │  ← e_ident[16], e_entry, e_phoff, e_shoff, ...
 * ├─────────────────────┤
 * │   Program Headers   │  ← p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags
 * ├─────────────────────┤
 * │   Section Headers   │  ← 调试信息，运行时不需要
 * ├─────────────────────┤
 * │   Segment Data      │  ← .text (RX), .rodata (R), .data/.bss (RW)
 * └─────────────────────┘
 *
 * 设计要点：
 * - 优先支持 ELF32 (RV32)，兼顾 ELF64 识别
 * - 只加载 PT_LOAD 类型的段，忽略其他 (NOTE, TLS, GNU_STACK 等)
 * - p_memsz > p_filesz 时，多余部分清零 (.bss 段)
 * - 使用 mmu_map_page() 为每个段建立虚拟地址映射
 * - 栈默认放在高地址 (如 0x7FFFF000)，向下增长
 * - 辅助向量 (auxv) 至少包含 AT_PHDR, AT_PHENT, AT_PHNUM, AT_PAGESZ
 * ============================================================
 */

typedef uint32_t Elf32_Addr;    // 32 位地址（虚拟地址或物理地址）
typedef uint32_t Elf32_Off;     // 32 位文件偏移（从文件开头算起的字节偏移）
typedef uint16_t Elf32_Half;    // 16 位半字（用于 e_type, e_machine 等小字段）
typedef uint32_t Elf32_Word;    // 32 位字（ELF 规范中的标准字大小）
typedef int32_t  Elf32_Sword;   // 32 位有符号字（极少使用，仅为了完整性）

/* ═══════════════════════════════════════════════════════════════
 * 第 2 部分：ELF 常量
 * ═══════════════════════════════════════════════════════════════
 *
 * 以下常量来自 ELF32 规范（System V ABI 和 RISC-V 补充规范）。
 * 带 ★ 的是加载过程中实际使用的关键常量。
 */

/* ── e_ident[] 字节数组索引 ──────────────────────────────────
 *
 * e_ident[16] 是 ELF Header 的第一个字段，描述文件的基本属性。
 * 前 4 字节是固定的魔数，用于识别 "这是一个 ELF 文件"。
 * 第 5 字节（EI_CLASS）告诉我们是 32 位还是 64 位。
 * 第 6 字节（EI_DATA）告诉我们是小端序还是大端序。
 *
 * 例：一个合法的 ELF32 小端 RISC-V 文件的 e_ident 开头是：
 *     7F 45 4C 46 01 01 01 00 ...
 *      ↑  ↑  ↑  ↑  ↑  ↑  ↑
 *      │  E  L  F  32 小 版本1
 *      魔数(0x7F)   位 端
 * ─────────────────────────────────────────────────────────── */

#define EI_MAG0      0          // 魔数第 0 字节：固定为 0x7F
#define EI_MAG1      1          // 魔数第 1 字节：固定为 'E' (0x45)
#define EI_MAG2      2          // 魔数第 2 字节：固定为 'L' (0x4C)
#define EI_MAG3      3          // 魔数第 3 字节：固定为 'F' (0x46)
#define EI_CLASS     4          // 文件类别：1 = 32 位, 2 = 64 位
#define EI_DATA      5          // 数据编码：1 = 小端序, 2 = 大端序
#define EI_VERSION   6          // ELF 版本：固定为 1（当前唯一合法版本）
#define EI_PAD       7          // 填充字节起点（7~15 共 9 字节，全部为 0）
#define EI_NIDENT    16         // e_ident 数组总长度（固定 16 字节）

/* ── EI_CLASS 取值（e_ident[4]）─────────────────────────────
 *
 * 我们只支持 32 位 ELF（ELFCLASS32），64 位 ELF 在检测到后
 * 会打印错误信息并拒绝加载。
 * ─────────────────────────────────────────────────────────── */

#define ELFCLASS32   1          // ★ 32 位 ELF（我们支持的唯一值）
#define ELFCLASS64   2          // 64 位 ELF（不支持，仅用于检测和报错）

/* ── EI_DATA 取值（e_ident[5]）──────────────────────────────
 *
 * RISC-V 和 x86 都是小端序（ELFDATA2LSB），所以模拟器可以直接
 * 读取 ELF 结构体而无需做端序转换。大端序 ELF 会被拒绝。
 * ─────────────────────────────────────────────────────────── */

#define ELFDATA2LSB  1          // ★ 小端序（我们支持的唯一值）
#define ELFDATA2MSB  2          // 大端序（不支持，仅用于检测和报错）

/* ── e_type 取值（文件类型，位于 Header 偏移 0x10 处）────────
 *
 * 我们只加载可执行文件（ET_EXEC = 2）。可重定位文件（.o）和共享库
 * （.so）在检测到后会报错拒绝。
 * ─────────────────────────────────────────────────────────── */

#define ET_NONE      0          // 无类型（无效文件）
#define ET_REL       1          // 可重定位文件（.o 目标文件，需要链接）
#define ET_EXEC      2          // ★ 可执行文件（我们加载的唯一类型）
#define ET_DYN       3          // 共享库（.so，需要动态链接器）

/* ── e_machine 取值（目标 CPU 架构，位于 Header 偏移 0x12 处）─
 *
 * EM_RISCV = 243 是 RISC-V 在 ELF 规范中注册的机器编号。
 * 如果 e_machine 不是 243，说明这个 ELF 是给 x86/ARM/其他 CPU 的，
 * 我们的 RISC-V 模拟器无法执行。
 * ─────────────────────────────────────────────────────────── */

#define EM_RISCV     243        // ★ RISC-V 架构（我们支持的唯一值）

/* ── Program Header p_type 取值（段类型）─────────────────────
 *
 * ELF 文件可以有多种类型的段（Program Header）。我们只关心 PT_LOAD：
 * 它的 p_offset 指向文件中的实际数据，这些数据需要加载到内存。
 * 其他类型（动态链接信息、解释器路径、注释段等）在遍历时直接跳过。
 * ─────────────────────────────────────────────────────────── */

#define PT_NULL      0          // 空段（占位，跳过）
#define PT_LOAD      1          // ★ 可加载段（我们处理的唯一类型）
#define PT_DYNAMIC   2          // 动态链接信息（跳过）
#define PT_INTERP    3          // 解释器路径（跳过）
#define PT_NOTE      4          // 注释段（跳过）
#define PT_PHDR      6          // Program Header 表自身的位置（跳过）
#define PT_GNU_STACK 0x6474e551 // GNU 栈权限标记（特殊：不加载段数据，
                                //   只通过 p_flags 指示栈是否可执行。
                                //   我们忽略它，因为栈总是 RW 的）

/* ── p_flags 权限位（Program Header 的权限标志）──────────────
 *
 * 重要：这些值和模拟器内部的 MEM_* / PTE_* 值不同！
 *
 *   PF_R=4, PF_W=2, PF_X=1        ← ELF 规范定义（本头文件）
 *   MEM_READ=1, MEM_WRITE=2, MEM_EXEC=4  ← 模拟器 PhysicalMemory 层（types.h）
 *   PTE_READ=2, PTE_WRITE=4, PTE_EXEC=8  ← 模拟器 MMU 页表层（types.h）
 *
 * Loader 在 elf_load_segment 中做转换：PF_* → MEM_* →（完善版）PTE_*
 * ─────────────────────────────────────────────────────────── */

#define PF_X         1          // 可执行（.text 段通常设这个位）
#define PF_W         2          // 可写（.data 段设这个位）
#define PF_R         4          // 可读（几乎所有段都设这个位）
                                // 常见组合：PF_R|PF_X=5 (.text), PF_R|PF_W=6 (.data)

/* ═══════════════════════════════════════════════════════════════
 * 第 3 部分：ELF32 结构体
 * ═══════════════════════════════════════════════════════════════
 *
 * 以下结构体直接映射 ELF 文件的二进制布局。字段顺序和偏移量由 ELF32
 * 规范严格规定，不能随意调整——fread(&ehdr, 52, 1, fp) 直接把 52 字节
 * 的文件数据填入结构体，字段顺序必须和文件中的顺序完全一致。
 *
 * ★ 标记的字段是加载过程中实际使用的；无标记的字段虽然读入，
 * 但在加载阶段不会被访问（仅用于完整性或调试输出）。
 */

/* ── Elf32_Ehdr：ELF 文件头（52 字节）─────────────────────────
 *
 * 这是 ELF 文件的最开头 52 个字节。elf_load() 第一步就是
 * fread(&ehdr, sizeof(Elf32_Ehdr), 1, fp) 把它读进来。
 *
 * 字段布局（按文件偏移排列）：
 *
 *   偏移   大小   字段          说明
 *   ─────────────────────────────────────────────────────────
 *   0x00    16    e_ident[]     ★ 魔数 + 类型信息
 *                               [0]=0x7F [1]='E' [2]='L' [3]='F'
 *                               [4]=1(32bit) [5]=1(小端序) [6]=1(版本)
 *                               [7..15] = 0（填充，忽略）
 *   0x10     2    e_type        ★ 文件类型：1=可重定位, 2=可执行, 3=共享库
 *   0x12     2    e_machine     ★ 目标架构：243 = EM_RISCV
 *   0x14     4    e_version     ELF 版本号（固定为 1）
 *   0x18     4    e_entry       ★ 程序入口虚拟地址（CPU 从这里开始执行）
 *   0x1C     4    e_phoff       ★ Program Header 表在文件中的偏移量
 *                                用 fseek(fp, ehdr.e_phoff, SEEK_SET) 跳过去
 *   0x20     4    e_shoff       Section Header 表偏移（加载时不需要）
 *   0x24     4    e_flags       处理器特定标志（RISC-V 通常为 0，忽略）
 *   0x28     2    e_ehsize      ELF Header 自身大小（固定为 52 字节）
 *                                可以用来验证：如果 e_ehsize != 52，说明文件损坏
 *   0x2A     2    e_phentsize   ★ 每个 Program Header 的大小（固定为 32 字节）
 *                                用于计算读取步长
 *   0x2C     2    e_phnum       ★ Program Header 的数量（通常 2~4 个）
 *                                遍历循环的上限
 *   0x2E     2    e_shentsize   Section Header 大小（加载时不需要）
 *   0x30     2    e_shnum       Section Header 数量（加载时不需要）
 *   0x32     2    e_shstrndx    Section 名称字符串表索引（加载时不需要）
 *
 * 重要：结构体中的 ★ 字段约占总字段数的 1/3。很多字段（Section 相关的）
 * 只在链接和调试阶段使用，运行时加载不需要它们。但 fread 必须读取完整
 * 52 字节——结构体大小必须精确匹配，否则后续字段偏移全错。
 * ─────────────────────────────────────────────────────────── */

/* ----- ELF32 类型定义 ----- */
typedef uint32_t Elf32_Addr;
typedef uint32_t Elf32_Off;
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Word;
typedef int32_t  Elf32_Sword;

/* e_ident 字段索引 */
#define EI_MAG0      0
#define EI_MAG1      1
#define EI_MAG2      2
#define EI_MAG3      3
#define EI_CLASS     4          // 1=32bit, 2=64bit
#define EI_DATA      5          // 1=小端, 2=大端
#define EI_VERSION   6
#define EI_PAD       7
#define EI_NIDENT    16

/* EI_CLASS 取值 */
#define ELFCLASS32   1
#define ELFCLASS64   2

/* EI_DATA 取值 */
#define ELFDATA2LSB  1
#define ELFDATA2MSB  2

/* e_type 取值 */
#define ET_NONE      0
#define ET_REL       1          // 可重定位文件 (.o)
#define ET_EXEC      2          // 可执行文件
#define ET_DYN       3          // 共享库 (.so)

/* e_machine 取值 */
#define EM_RISCV     243        // RISC-V

/* Program Header p_type 取值 */
#define PT_NULL      0
#define PT_LOAD      1          // 可加载段
#define PT_DYNAMIC   2
#define PT_INTERP    3
#define PT_NOTE      4
#define PT_PHDR      6
#define PT_GNU_STACK 0x6474e551

/* p_flags 权限位 */
#define PF_X         1
#define PF_W         2
#define PF_R         4

/* ELF32 Header */
typedef struct {
    uint8_t     e_ident[EI_NIDENT]; // ★ [0..3]=魔数, [4]=32/64, [5]=大小端
    Elf32_Half  e_type;             // ★ 文件类型：2=可执行文件
    Elf32_Half  e_machine;          // ★ 目标架构：243=RISC-V
    Elf32_Word  e_version;          // ELF 版本号，固定为 1
    Elf32_Addr  e_entry;            // ★ 程序入口虚拟地址
    Elf32_Off   e_phoff;            // ★ Program Header 表在文件中的偏移
    Elf32_Off   e_shoff;            // Section Header 表偏移（加载不用）
    Elf32_Word  e_flags;            // 处理器标志（RISC-V 为 0）
    Elf32_Half  e_ehsize;           // ELF Header 大小（52 字节），可用来校验
    Elf32_Half  e_phentsize;        // ★ 单个 Program Header 大小（32 字节）
    Elf32_Half  e_phnum;            // ★ Program Header 个数
    Elf32_Half  e_shentsize;        // 单个 Section Header 大小（加载不用）
    Elf32_Half  e_shnum;            // Section Header 个数（加载不用）
    Elf32_Half  e_shstrndx;         // Section 名称字符串表索引（加载不用）
} Elf32_Ehdr;

/* ── Elf32_Phdr：Program Header（32 字节）─────────────────────
 *
 * Program Header 表紧跟在 ELF Header 之后（不一定立即相邻，具体位置
 * 由 ehdr.e_phoff 指定）。表中每一项描述一个"段"——一组具有相同权限
 * 的连续内存区域。
 *
 * 一个典型的 "Hello World" ELF 通常有 2 个 PT_LOAD 段：
 *   [0] .text 段（代码）：p_flags = PF_R|PF_X = 5, p_filesz = 几百字节
 *   [1] .data 段（数据）：p_flags = PF_R|PF_W = 6, p_filesz = 几十字节
 *
 * 字段布局（按文件偏移排列）：
 *
 *   偏移   大小   字段          说明
 *   ─────────────────────────────────────────────────────────
 *   0x00     4    p_type        ★ 段类型：1 = PT_LOAD（可加载段）
 *                                判断依据：if (phdr.p_type != PT_LOAD) continue;
 *   0x04     4    p_offset      ★ 段数据在 ELF 文件中的字节偏移
 *                                用于 fseek(fp, phdr.p_offset, SEEK_SET)
 *   0x08     4    p_vaddr       ★ 虚拟地址（段应该加载到哪个虚拟地址）
 *                                当前简化版用恒等映射：paddr = p_vaddr
 *                                完善版：mmu_map_page(mmu, p_vaddr, paddr, flags)
 *   0x0C     4    p_paddr       物理地址（在只读存储器系统中的加载地址）
 *                                模拟器中用于恒等映射，完善版由 mem_find_free 决定
 *   0x10     4    p_filesz      ★ 段在 ELF 文件中的实际字节数
 *                                可能为 0（纯 .bss 段，文件中不占空间）
 *   0x14     4    p_memsz       ★ 段在内存中需要的字节数（≥ p_filesz）
 *                                如果 memsz > filesz，多出的部分是 .bss，
 *                                需要清零（C 标准保证未初始化的全局变量为 0）
 *   0x18     4    p_flags       ★ 权限位：PF_R=4, PF_W=2, PF_X=1
 *                                可组合：5=R|X(.text), 6=R|W(.data), 4=R(.rodata)
 *                                注意：这些值和 MEM_* / PTE_* 不同，需要转换！
 *   0x1C     4    p_align       对齐要求（通常是 0x1000 = 4KB 页对齐）
 *                                如果 p_align 是 0 或 1，表示不需要对齐
 *                                当前简化实现忽略此字段，完善版传给 mem_find_free
 *
 * 注意：p_vaddr 和 p_offset 的关系——
 *   文件中偏移 p_offset 处的数据，加载后应该出现在虚拟地址 p_vaddr。
 *   模拟器执行时，CPU 按 p_vaddr 取指/访存，通过 MMU（或恒等映射）
 *   找到物理内存中的实际数据。
 * ─────────────────────────────────────────────────────────── */

typedef struct {
    Elf32_Word  p_type;     // ★ 段类型：1 = PT_LOAD，跳过其他值
    Elf32_Off   p_offset;   // ★ 段数据在文件中的偏移（用于 fseek）
    Elf32_Addr  p_vaddr;    // ★ 虚拟地址（段的目标地址）
    Elf32_Addr  p_paddr;    // 物理地址（恒等映射时 = p_vaddr）
    Elf32_Word  p_filesz;   // ★ 文件中占多少字节（可能 < memsz）
    Elf32_Word  p_memsz;    // ★ 内存中占多少字节（≥ filesz，差值 = .bss）
    Elf32_Word  p_flags;    // ★ 权限位：PF_R/W/X 组合
    Elf32_Word  p_align;    // 对齐要求（通常 0x1000，0 或 1 表示无要求）
} Elf32_Phdr;

/* ═══════════════════════════════════════════════════════════════
 * 第 4 部分：Loader 对外接口
 * ═══════════════════════════════════════════════════════════════
 *
 * elf_load() 是 Loader 模块的唯一对外入口。
 * 外部调用者只需关心这个函数——打开文件、解析 ELF、校验、加载段数据、
 * 初始化栈、关闭文件，全部在这个函数内部完成。
 *
 * 内部实现分散在 src/src/loader/ 下的多个 .c 文件中
 *（elf_validate.c, elf_segment.c, elf_load.c, elf_stack.c），
 * 通过模块内部头文件 loader_internal.h 互相调用。外部不应该也不
 * 需要直接调用那些内部函数。
 */

/*
 * elf_load — 加载 ELF 可执行文件到模拟器物理内存
 *
 * ── 函数签名设计理由 ──────────────────────────────────────
 *
 *   为什么不传 Simulator* 而是传独立的 pmem/mmcu 指针？
 *     - 遵循"最小依赖"原则：Loader 只需要 PhysicalMemory 和 MMU，
 *       不需要 Simulator 的其他字段（CPU状态、断点、统计计数等）
 *     - 方便单元测试：测试时可以构造假的 pmem/mmcu 而不需要完整的 Simulator
 *     - 团队结论 1：Loader 操作 PhysicalMemory 层 + MMU 层，传这两者的指针
 *       能直观反映架构
 *
 *   为什么用输出参数（entry, stack_top）而不直接写 sim->cpu？
 *     - Loader 不持有 Simulator* 指针，不知道 CPU 结构体的字段名
 *     - 解耦：CPU 结构体字段名可能变（regs vs. regfile），Loader 不需要知道
 *     - 调用者（main.c / sim_load_elf）负责把输出值写入 CPU 状态
 *
 * ── 参数说明 ──────────────────────────────────────────────
 *
 *   filename  — [输入] ELF 可执行文件的路径（C 字符串，以 \0 结尾）
 *               相对路径或绝对路径均可，由 fopen 处理
 *               例："hello.elf", "tests/add_test.elf"
 *
 *   pmem      — [输入/输出] 指向已初始化的 PhysicalMemory 的指针
 *               调用前必须先调用 mem_init(pmem, size)！
 *               函数内部会往 pmem->data[] 写入段数据，
 *               并通过 mem_map() 注册内存区域。
 *               函数不会修改 pmem->size 和 pmem->brk_start。
 *
 *   mmu       — [输入/输出] 指向已初始化的 MMUState 的指针
 *               调用前必须先调用 mmu_init(mmu)！
 *               当前简化版不使用此参数（恒等映射，不需要页表），
 *               完善版通过 mmu_map_page() 建立页表映射。
 *
 *   entry     — [输出] 程序入口虚拟地址，即 ehdr.e_entry 的值
 *               调用者负责：sim->cpu.pc = *entry;
 *               典型值：0x000100B0（取决于链接器）
 *
 *   stack_top — [输出] 栈顶虚拟地址，固定为 0xC0000000（团队结论 7）
 *               调用者负责：sim->cpu.regs[2] = *stack_top;
 *               栈底 = 0xC0000000 - 256KB = 0xBFFC0000
 *
 * ── 返回值 ────────────────────────────────────────────────
 *
 *   true  — 加载成功。*entry 和 *stack_top 已设置为有效值，
 *           pmem->data[] 中已有段数据，pmem->regions[] 已注册相应区域。
 *           ELF 文件已关闭。
 *
 *   false — 加载失败。已通过 fprintf(stderr, ...) 输出具体错误原因。
 *           调用了 fclose(fp) 保证文件句柄不泄露。
 *           pmem 和 mmu 的状态可能部分修改（已加载的段不会回滚），
 *           调用者应将其视为无效状态。
 *
 * ── 可能失败的原因 ────────────────────────────────────────
 *
 *   文件层面：
 *     - 文件不存在或无读取权限（fopen 失败 → perror）
 *     - 文件小于 52 字节（fread ELF Header 失败）
 *
 *   校验层面（elf_validate_header）：
 *     - Magic 不匹配（不是 ELF 文件）
 *     - 64 位 ELF（e_ident[4] = 2）
 *     - 大端序 ELF（e_ident[5] = 2）
 *     - 不是 RISC-V 架构（e_machine != 243）
 *     - 不是可执行文件（e_type != 2，如 .o 文件或 .so 文件）
 *
 *   内存层面（elf_load_segment）：
 *     - 段大小超出物理内存（paddr + memsz > pmem->size）
 *     - malloc/calloc 分配临时缓冲区失败（系统内存耗尽）
 *     - mem_map 或 mem_load 失败（已映射区域冲突等）
 *     - 文件读取失败（fread 返回字节数不匹配，文件可能被截断）
 *
 * ── 内部实现流程（外部无需关心，仅作理解用）────────────────
 *
 *   elf_load(filename, pmem, mmu, entry, stack_top)
 *     │
 *     ├─ 1. fopen(filename, "rb")
 *     │     └─ 失败 → perror + return false
 *     │
 *     ├─ 2. fread(&ehdr, sizeof(Elf32_Ehdr), 1, fp)
 *     │     └─ 失败 → fprintf + fclose + return false
 *     │
 *     ├─ 3. elf_validate_header(&ehdr)       ← elf_validate.c
 *     │     五重检查：magic → 32bit → 小端 → RISC-V → 可执行
 *     │     └─ 任一失败 → fclose + return false
 *     │
 *     ├─ 4. for (i = 0; i < ehdr.e_phnum; i++):
 *     │       fseek(ehdr.e_phoff + i*32) → fread(&phdr, 32)
 *     │       if (phdr.p_type == PT_LOAD):
 *     │         elf_load_segment(fp, &phdr, pmem, mmu) ← elf_segment.c
 *     │           ├─ 权限: PF_* → MEM_*
 *     │           ├─ 恒等映射: paddr = p_vaddr
 *     │           ├─ mem_map(pmem, paddr, memsz, flags, "segment")
 *     │           ├─ malloc + fread + mem_load → 拷贝段数据
 *     │           ├─ calloc + mem_load → 清零 .bss
 *     │           └─ [完善版] mem_find_free + mmu_map_page
 *     │
 *     ├─ 5. fclose(fp)
 *     │
 *     ├─ 6. *entry = ehdr.e_entry
 *     │
 *     ├─ 7. elf_setup_stack(pmem, stack_top) ← elf_stack.c
 *     │     ├─ mem_map(pmem, 0xBFFC0000, 256KB, MEM_R|MEM_W, "stack")
 *     │     └─ *stack_top = 0xC0000000
 *     │
 *     └─ 8. printf 加载信息（entry, stack_top）→ return true
 *
 * ── 调用示例 ──────────────────────────────────────────────
 *
 *   详见文件开头的完整使用示例，或参考 docs/设计文档/嘉华/loader设计文档.md
 *   的 §3.3 模块交互时序。
 */

bool elf_load(const char *filename,
              PhysicalMemory *pmem,
              MMUState       *mmu,
              uint32_t       *entry,
              uint32_t       *stack_top);

#endif /* ELF_LOADER_H */
