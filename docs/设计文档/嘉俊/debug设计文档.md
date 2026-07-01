# Debugger 设计文档

> 作者：嘉俊 | 最后更新：2026-07-01
>
> **架构决策已确定**：断点使用 ebreak 替换方案，断点数组放在 `Simulator` 下（CPU 和 Debugger 都可访问）。
> 公共类型定义见 `docs/设计文档/共同部分/公共类型定义.md`，讨论结论见 `讨论清单.md`。
>
> 接口签名已全员对齐：Memory 两层架构（PhysicalMemory + MMUState），返回值 `bool`，错误通过 `ExceptionType*` 输出参数传递。

---

## 1. 是什么（模块定位）

Debugger 是 RISC-V 模拟器的**交互式调试层**。它挂在 CPU 旁边，以"旁路拦截"的方式工作——不在主数据流（ELF → Memory → CPU → 结果）中，而是随时监听 CPU 状态，在用户需要时暂停执行、查看状态、控制执行节奏。

**在系统中的位置：**

```
                    ┌───────────┐
                    │  Debugger │  ← 旁路：拦截、查看、控制
                    └─────┬─────┘
                    控制   │  读取（通过 Simulator*）
           ┌──────────────┴──────────────┐
           │         Simulator           │
           │  ┌──────┐ ┌──────┐ ┌──────┐ │
           │  │ CPU  │ │ MMU  │ │Physical│ │
           │  │      │ │State │ │Memory │ │
           │  └──────┘ └──────┘ └──────┘ │
           │  ┌──────────────────────┐   │
           │  │ breakpoints[]       │   │  ← 断点数组（Debugger 管理，CPU 查询）
           │  └──────────────────────┘   │
           └─────────────────────────────┘
```

**一句话概括：** Debugger 让用户能以"上帝视角"观察和控制程序的每一步执行——在哪停、什么时候走、每一步寄存器/内存里有什么，全由用户决定。

### 与其他模块的关系

| 关系 | 模块 | 接口 | 说明 |
|------|------|------|------|
| 控制对象 | CPU | `sim->cpu.regs[]`, `sim->cpu.pc`, `sim->cpu.running`, `sim->single_step` | 读寄存器/PC、控制执行节奏 |
| 数据来源 | Memory | `mmu_read_32()`, `mem_dump()` | 查看内存（`x` 命令）、读/写断点指令 |
| 断点存储 | Simulator | `sim->breakpoints[]`, `sim->bp_count`, `sim->bp_capacity` | 断点数组（Debugger 增删，CPU 查询） |
| 被谁触发 | 用户 或 CPU 异常 | `-s` 参数 / ebreak 异常 | 启动时进入调试模式；CPU 遇到 ebreak 时自动暂停 |

---

## 2. 做什么（功能与边界）

### 2.1 对外提供的功能

Debugger 提供一个交互式命令行界面（REPL），提示符为 `(rvsim) `，支持以下命令：

| 命令 | 缩写 | 功能 |
|------|------|------|
| `break <addr>` | `b <addr>` | 在指定地址设置软件断点 |
| `delete <n>` | `d <n>` | 删除第 n 号断点（n = 数组下标） |
| `info breakpoints` | `i b` | 列出所有断点 |
| `step` / `stepi` | `s` / `si` | 单步执行一条指令（执行完后打印 PC + 寄存器） |
| `stepi <n>` | `si <n>` | 执行 n 条指令 |
| `continue` | `c` | 继续运行，直到命中断点或程序退出 |
| `info registers` | `i r` | 显示所有 32 个通用寄存器 + PC + 部分 CSR |
| `x/<N><F><U> <addr>` | `x <addr>` | 查看指定地址的内存内容（hexdump 风格） |
| `print <reg>` | `p <reg>` | 打印单个寄存器的值（如 `p $a0`） |
| `backtrace` | `bt` | 打印调用栈（通过帧指针链回溯） |
| `help` | `h` | 显示帮助信息 |
| `quit` | `q` | 退出模拟器 |

### 2.2 断点功能详解

**采用 ebreak 替换方案**（讨论结论 6），设断点 → 命中 → 恢复 → 重新插入的完整状态机如下：

```
设断点前: 0x10074: [原始指令 addi sp, sp, -16]
         ↓ b 0x10074
设断点后: 0x10074: [EBREAK 0x00100073]
                 原始指令被保存到 sim->breakpoints[i].original_Instr
         ↓ CPU 执行到 0x10074
命中时:   CPU 读到 EBREAK → 触发 EXC_BREAKPOINT
         CPU 遍历 sim->breakpoints[]，发现 PC 在列表中：
           ① 恢复原始指令到 0x10074（写回原指令）
           ② cpu.running = false，暂停执行
           ③ 打印 "Hit breakpoint at 0x10074"
           ④ 控制权交给 Debugger REPL
         ↓ 用户输入 c (continue)
继续前:   ① 单步执行断点处的原始指令（PC 越过 0x10074）
         ② 重新写入 EBREAK 到 0x10074（以便下次再次命中）
         ③ sim->cpu.running = true，继续正常执行
```

**断点列表在 `Simulator` 下**（讨论结论 3），原因：
- CPU 执行 ebreak 时需要查断点列表，区分"用户断点触发"还是"程序自身的 ebreak"
- 放在 Simulator 下，CPU 和 Debugger 都能访问，不破坏单向依赖
- Debugger 通过 `Simulator*` 指针操作断点数组，符合"上层依赖下层"

### 2.3 边界：什么归我管、什么不归我管

| 归 Debugger 管 | 不归 Debugger 管 |
|---------------|-----------------|
| 命令解析与分发 | ELF 文件的解析（Loader — 嘉华） |
| 断点的增删查（操作 `sim->breakpoints[]`） | 指令的取指、译码、执行（CPU — 李特） |
| 用户交互循环（REPL） | 虚拟地址翻译（MMU — 焕聪） |
| 寄存器/内存的**查看** | 物理内存的分配与管理（PhysicalMemory — 焕聪） |
| 执行节奏的控制（`running` / `single_step`） | 系统调用的实现（内联在 CPU `cpu_execute()` 中 — 李特） |
| 调用栈的回溯解析 | 异常处理的底层逻辑（`cpu_trap()` — 李特） |
| ebreak 指令的写入/恢复（通过 `mmu_write_32`） | ebreak 异常触发后的 CSR 填写（CPU — 李特） |

**关键原则：** Debugger **不修改程序的用户数据**（数据段、栈、堆内容），也**不直接修改 CPU 状态**。合法操作——① 断点替换时通过 `mmu_write_32` 临时修改代码段（写入/恢复 ebreak 指令）；② 通过 `single_step` 和 `running` 标志控制 CPU 执行节奏；③ 通过 `Simulator*` 读取寄存器和内存。

---

## 3. 怎么做

### 3.1 数据结构与对外接口

#### 3.1.1 类型依赖链（Debugger 不重复定义任何结构体，全部引用已有定义）

Debugger 模块**不定义任何自己的数据结构体**。所有状态通过 `Simulator*` 指针访问，类型定义分布在以下几个已对齐的头文件中。理解这条依赖链是 Debugger 实现的基础：

```
types.h                         ← 零依赖，基础枚举和宏
  │
  ├── PrivilegeLevel            ← { PRIV_USER=0, PRIV_SUPERVISOR=1, PRIV_MACHINE=3 }
  ├── ExceptionType             ← { EXC_NONE=0, EXC_BREAKPOINT=4, EXC_ECALL_M=11, ... }
  ├── MEM_READ/WRITE/EXEC       ← 内存权限标志（物理内存层用）
  ├── PAGE_SIZE=4096            ← 页大小常量
  └── PTE_VALID/READ/WRITE/...  ← PTE 权限标志（MMU 页表层用）
      │
      ├── memory.h              ← PhysicalMemory 结构体（焕聪）
      │     └── PhysicalMemory { uint8_t *data; uint32_t size; ... }
      │
      ├── mmu.h                 ← MMUState 结构体（焕聪）
      │     └── MMUState { uint32_t satp; uint32_t *root_page_table; bool enabled; }
      │
      ├── cpu.h                 ← CPU 结构体（李特）
      │     └── CPU { uint32_t regs[32]; uint32_t pc; bool running;
      │               PrivilegeLevel priv; uint32_t mstatus; uint32_t mtvec;
      │               uint32_t mepc; uint32_t mcause; uint32_t mtval; }
      │
      └── simulator.h           ← Simulator + Breakpoint（顶层聚合）
            │
            ├── Breakpoint { uint32_t addr; uint32_t original_Instr; bool enabled; }
            └── Simulator { CPU cpu; MMUState mmu; PhysicalMemory pmem;
                            Breakpoint *breakpoints; int bp_count; int bp_capacity;
                            bool single_step; bool debug_mode;
                            uint64_t inst_count; uint64_t cycle_count; }
                  │
                  └── debugger.h    ← Debugger 函数声明（只依赖 Simulator 前置声明）
```

**各类型 Debugger 访问方式（通过 `Simulator* sim` 指针）：**

| 类型 | 定义位置 | Debugger 中通过 | 用途 |
|------|---------|----------------|------|
| `Breakpoint` | `simulator.h` | `sim->breakpoints[i]` | 断点增删查，每个字段含义见下 |
| `Simulator` | `simulator.h` | `sim->` | 顶层入口，聚合所有模块状态 |
| `CPU` | `cpu.h` | `sim->cpu.regs[]`, `sim->cpu.pc`, `sim->cpu.running`, `sim->cpu.priv`, `sim->cpu.mstatus`, `sim->cpu.mcause`, `sim->cpu.mepc` | 寄存器查看、执行控制、栈回溯 |
| `MMUState` | `mmu.h` | `&sim->mmu`（传给 `mmu_read/write_32`） | x 命令读内存、断点指令读写 |
| `PhysicalMemory` | `memory.h` | `&sim->pmem`（传给 `mmu_read/write_32` 或 `mem_dump`） | x 命令物理内存 dump |
| `PrivilegeLevel` | `types.h` | `sim->cpu.priv`（传给 `mmu_read/write_32`） | MMU 权限检查 |
| `ExceptionType` | `types.h` | 局部变量 `exc`（传给 `mmu_read/write_32`） | 捕获 MMU 错误 |

**`Breakpoint` 字段说明：**

| 字段 | 类型 | Debugger 如何操作 |
|------|------|-----------------|
| `addr` | `uint32_t` | 设断点时赋值；删除/命中时比对 PC；退出时恢复原始指令 |
| `original_Instr` | `uint32_t` | 设断点时通过 `mmu_read_32` 读取并保存；命中后恢复时通过 `mmu_write_32` 写回 |
| `enabled` | `bool` | 设断点时设为 `true`；`info breakpoints` 显示 "y/n" |

**`Simulator` 中 Debugger 相关的字段：**

| 字段 | 类型 | Debugger 如何操作 |
|------|------|-----------------|
| `breakpoints` | `Breakpoint*` | 动态数组指针；设断点时通过下标写入，满时 `realloc` 扩容 |
| `bp_count` | `int` | 设断点时 `++`，删断点时 `--`（将末尾元素移到被删位置） |
| `bp_capacity` | `int` | 初始值由 `sim_init()` 设为 16；满时 ×2 扩容 |
| `single_step` | `bool` | `debugger_step()` 中设为 `true`；`sim_step()` 读它决定是否只执行一条 |
| `debug_mode` | `bool` | `debugger_run()` 入口设为 `true`；`quit` 时设为 `false` |

**断点数组管理约定：**
- 下标即编号：第 0 个断点 = `breakpoints[0]`，`info breakpoints` 显示 "0"
- 删除策略：将末尾元素移到被删位置（O(1)），`bp_count--`；不保留空洞
- 扩容策略：`bp_count >= bp_capacity` 时 `realloc` 扩容 2 倍，初始容量 16
- 退出清理：`quit` 时遍历恢复所有原始指令，`free(sim->breakpoints)` 在 `sim_destroy()` 中统一完成

#### 3.1.2 Debugger 自身的头文件（`src/include/debugger/debugger.h`）

```c
#ifndef DEBUGGER_H
#define DEBUGGER_H

#include "types.h"        // PrivilegeLevel, ExceptionType

struct Simulator;          // 前置声明（避免循环依赖，实现文件中再 #include "simulator.h"）

/* ========== REPL 入口 ========== */

/* 启动调试 REPL 主循环（阻塞，直到用户 quit 或程序正常退出） */
void debugger_run(struct Simulator *sim);

/* ========== 断点管理（操作 sim->breakpoints[]）========== */

/* 在 addr 处设置软件断点，返回下标（也是编号），失败返回 -1 */
int  debugger_add_breakpoint(struct Simulator *sim, uint32_t addr);

/* 删除指定下标的断点，返回 true 成功 */
bool debugger_del_breakpoint(struct Simulator *sim, int index);

/* 列出所有断点（info breakpoints） */
void debugger_list_breakpoints(const struct Simulator *sim);

/* 检查 pc 是否命中某个启用的断点（CPU 在 ebreak 时调用） */
bool debugger_check_breakpoint(struct Simulator *sim, uint32_t pc);

/* ========== 执行控制 ========== */

/* 单步执行一条指令，自动处理断点恢复/重新插入 */
void debugger_step(struct Simulator *sim);

/* 继续执行，直到命中断点或程序退出（内部循环调 sim_step） */
void debugger_continue(struct Simulator *sim);

/* ========== 状态查看 ========== */

/* 打印全部 32 个通用寄存器 + PC + 关键 CSR */
void debugger_print_registers(const struct Simulator *sim);

/* 查看虚拟地址内存内容（hexdump 风格，通过 mmu_read_*） */
void debugger_examine_memory(struct Simulator *sim, uint32_t vaddr,
                             int count, char format, char unit);

/* 打印调用栈（帧指针链回溯，通过 mmu_read_32 读栈帧） */
void debugger_print_backtrace(struct Simulator *sim);

#endif /* DEBUGGER_H */
```

**关键设计决策：Debugger 不需要 `DebuggerState` 结构体。**
- 所有状态已经从 `Simulator` 中获取：断点数组、单步标志、调试模式标志
- REPL 循环只需一个局部 `bool running` 变量（或用 `sim->debug_mode` 控制循环）
- 这样 CPU 和 Debugger 共享同一份断点数据，无需同步，不存在"谁的版本更新"的问题

### 3.2 内部实现思路

#### 3.2.1 REPL 主循环（`debugger_run`）

```
┌─────────────────────────────────────────┐
│          debugger_run(sim)              │
├─────────────────────────────────────────┤
│  sim->debug_mode = true                 │
│  打印欢迎信息                            │
│  打印初始寄存器状态                       │
│                                         │
│  while (sim->debug_mode) {              │
│    ① 打印提示符 "(rvsim) "              │
│    ② fgets() 读取用户输入               │
│    ③ strtok() 按空格/制表符分词         │
│    ④ 命令分发（前缀匹配）：             │
│       "b"/"break"    → 设断点           │
│       "d"/"delete"   → 删断点           │
│       "i b"          → 列出断点         │
│       "i r"          → 打印寄存器       │
│       "s"/"step"     → 单步             │
│       "si"/"stepi"   → 多步             │
│       "c"/"continue" → 继续执行         │
│       "x"            → 查看内存         │
│       "p"/"print"    → 打印单寄存器     │
│       "bt"           → 调用栈回溯       │
│       "q"/"quit"     → 退出             │
│       "h"/"help"     → 帮助             │
│       default        → "Unknown cmd"   │
│  }                                      │
│  sim->debug_mode = false                │
└─────────────────────────────────────────┘
```

**命令前缀匹配规则：** 用户输入的前缀只要能唯一匹配命令全名即可。`b` → `break`，`c` → `continue`，`s` → `step`，`q` → `quit`。不需要模糊搜索——直接 `strncmp` 或按首字符快速分发。

#### 3.2.2 断点设置流程（`debugger_add_breakpoint`）

```
① 参数校验：地址 4 字节对齐？地址在有效区域内？
② 去重检查：遍历 sim->breakpoints[0..bp_count-1]，已有同地址 → 提示并返回 -1
③ 容量检查 & 扩容：
     if (bp_count >= bp_capacity) {
         bp_capacity *= 2;
         sim->breakpoints = realloc(sim->breakpoints, bp_capacity * sizeof(Breakpoint));
     }
④ 保存原始指令（通过 MMU 读虚拟地址）：
     ExceptionType exc = EXC_NONE;
     mmu_read_32(&sim->mmu, &sim->pmem, addr,
                 &sim->breakpoints[bp_count].original_Instr,
                 sim->cpu.priv, &exc);
     失败则提示 "Cannot read memory at 0x..." 并返回 -1
⑤ 写入 EBREAK（通过 MMU 写虚拟地址）：
     mmu_write_32(&sim->mmu, &sim->pmem, addr, 0x00100073,
                  sim->cpu.priv, &exc);
     （注意：代码段权限 R+X，模拟器中可以绕过权限检查直接写物理内存）
⑥ 记录断点信息：
     sim->breakpoints[bp_count].addr    = addr;
     sim->breakpoints[bp_count].enabled = true;
     bp_count++;
⑦ 返回断点下标（同时也是编号）
```

#### 3.2.3 断点命中 → 恢复 → 重新插入 流程

```
【CPU 执行到断点地址时 —— 在 cpu_execute() 内部处理】

CPU 读到 EBREAK (0x00100073) → 触发 EXC_BREAKPOINT
  ↓
在 cpu_execute() 中检查：遍历 sim->breakpoints[0..bp_count-1]
  ├── PC 在断点列表中 → 用户设的断点（由 Debugger 的 ebreak 替换触发）
  │    ① 恢复原始指令：
  │       mmu_write_32(&sim->mmu, &sim->pmem, pc,
  │                     bp->original_Instr, sim->cpu.priv, &exc);
  │    ② *next_pc = pc（不前进 PC，停在断点地址）
  │    ③ cpu.running = false，暂停执行
  │    ④ 打印 "Hit breakpoint at 0x..."
  │    ⑤ 打印当前寄存器状态 → 回到 REPL
  │
  └── PC 不在断点列表中 → 程序自身的 ebreak 指令
        → cpu_trap(sim, EXC_BREAKPOINT, pc)（填入 CSR 后跳 mtvec，或 fallback exit）

【用户输入 c / continue 后 —— debugger_continue() 中处理】

① 当前是否停在断点处？
   是 → 单步执行一条原始指令（sim_step），PC 自然从断点地址变为断点地址+4
   否 → 直接继续
② 重新写入 EBREAK 到所有启用的断点地址（恢复断点，以便下次命中）
③ sim->cpu.running = true
④ while (cpu.running) {
       sim_step(sim);
   }
⑤ 回到 REPL
```

#### 3.2.4 单步执行（`debugger_step`）

```
debugger_step(sim):
  ① 如果当前 PC 在断点列表中：
       - 恢复该断点的原始指令
       - sim_step(sim)  // 单步一次
       - 重新写入 EBREAK
  ② 否则：
       - sim->single_step = true
       - sim_step(sim)
  ③ 打印 PC + 反汇编（调用 cpu_disasm）
  ④ 打印寄存器变化（可选：高亮变化的寄存器）
```

#### 3.2.5 x 命令（内存查看）格式解析

```
x/<N><F><U> <addr>

N = 显示数量（默认 16）
F = 显示格式：
    x → 十六进制 (hex, 默认)
    d → 有符号十进制 (dec)
    u → 无符号十进制 (unsigned)
    o → 八进制 (oct)
U = 单位大小：
    b → 1 字节 (byte)
    h → 2 字节 (halfword)
    w → 4 字节 (word, 默认)

示例：
  x/8xw 0x10000  → 从 0x10000 开始显示 8 个字（hex）
  x/16xb $sp     → 从 sp 开始显示 16 个字节（hex）
  x 0x10074      → 默认：显示 16 个字（hex）

实现：
  调用 mmu_read_8/16/32 逐单位读取，格式化为 hexdump 风格输出：
    0x10000: 97 01 00 00 93 81 01 80  13 05 00 00 13 06 00 00  |................|
    地址      字节字节字节字节 字节字节字节字节                    |可打印ASCII     |
```

#### 3.2.6 地址解析（`parse_addr`）

支持三种地址格式：

| 格式 | 示例 | 解析方式 |
|------|------|---------|
| 十六进制 | `0x10074`, `0x8000_0000` | `strtoul(str, NULL, 16)` |
| 十进制 | `1024` | `strtoul(str, NULL, 10)` |
| 寄存器引用 | `$x10`, `$a0`, `$sp`, `$pc`, `$ra` | 查 ABI 名称表 → `sim->cpu.regs[n]` |
| 简单表达式 | `$sp + 0x10` | 拆分后计算 |

ABI 名称映射表（与 CPU 模块一致）：

| ABI 名 | 寄存器 | ABI 名 | 寄存器 |
|--------|--------|--------|--------|
| zero | x0 | a0-a7 | x10-x17 |
| ra | x1 | s0/fp | x8 |
| sp | x2 | s1 | x9 |
| gp | x3 | s2-s11 | x18-x27 |
| tp | x4 | t0-t2 | x5-x7 |
| pc | (特殊：`sim->cpu.pc`) | t3-t6 | x28-x31 |

#### 3.2.7 调用栈回溯（`backtrace`）

基于帧指针链（Frame Pointer），利用 RISC-V ABI 约定：`s0/fp (x8)` 保存上一帧的 fp。

```
栈帧布局（RISC-V 标准 ABI）：
      高地址
      ┌────────────┐
      │   ra       │  ← fp + 4（返回地址）
      ├────────────┤
      │ old_fp     │  ← fp（指向上一个栈帧）
      ├────────────┤
      │  局部变量   │
      │   ...      │
      └────────────┘
      低地址

backtrace 算法：
  ① current_fp = sim->cpu.regs[8]  // x8 = s0/fp
  ② current_pc = sim->cpu.pc
  ③ 打印 "#0  PC = 0x..." + 反汇编
  ④ depth = 0
  ⑤ while (current_fp != 0 && current_fp >= STACK_BASE
            && current_fp < STACK_TOP && depth < 64) {
         // STACK_TOP = 0xC0000000, STACK_BASE = 0xBFFC0000
         // 使用与 Loader 一致的宏常量（见 loader_internal.h）
         ExceptionType exc = EXC_NONE;
         uint32_t ra, prev_fp;
         mmu_read_32(&sim->mmu, &sim->pmem, current_fp + 4, &ra,
                     sim->cpu.priv, &exc);
         mmu_read_32(&sim->mmu, &sim->pmem, current_fp, &prev_fp,
                     sim->cpu.priv, &exc);
         打印 "#%d  fp=0x%08x  ra=0x%08x" + ra 的反汇编
         current_fp = prev_fp;
         depth++;
     }
  ⑥ depth 上限 64 层（防止死循环，如栈被破坏时）
```

**注意事项：**
- 栈区域范围检查：当前 fp 必须在栈区域内（`STACK_BASE` ~ `STACK_TOP`，256KB 栈空间）。使用 Loader 模块定义的 `STACK_TOP_DEFAULT` 和 `STACK_SIZE_DEFAULT` 宏，与 Loader 保持同步
- 如果程序编译时未使用帧指针（`-fomit-frame-pointer`），回溯可能不准确——这是已知限制
- 读取栈帧时使用 `mmu_read_32`，走 MMU 翻译流程

### 3.3 模块交互时序

#### 场景 1：启动调试（main → Debugger）

```
main.c:
  ① sim_init(&sim)
       ├── cpu_init(&sim.cpu)          // 寄存器清零
       ├── mmu_init(&sim.mmu)          // MMU 初始化
       └── mem_init(&sim.pmem, 128MB)  // 物理内存分配
  ② sim_load_elf(&sim, "hello.elf")
       ├── Loader 解析 ELF，加载段数据
       ├── sim.cpu.pc = entry          // 设置入口地址
       └── sim.cpu.regs[2] = 0xC0000000  // 设置栈顶 sp
  ③ 如果是调试模式（-s 参数）：
       sim.debug_mode = true;
       debugger_run(&sim);            ← 进入 REPL，等待用户命令
```

#### 场景 2：用户设断点并命中（Debugger ↔ Simulator ↔ MMU）

```
用户: (rvsim) b 0x10074
        │
        ▼
debugger_add_breakpoint(sim, 0x10074)
  ① Debugger → MMU: mmu_read_32(&sim->mmu, &sim->pmem, 0x10074,
                                  &orig_Instr, sim->cpu.priv, &exc)
     读取原始指令，保存到 sim->breakpoints[0].original_Instr
  ② Debugger → MMU: mmu_write_32(&sim->mmu, &sim->pmem, 0x10074,
                                   0x00100073, sim->cpu.priv, &exc)
     写入 EBREAK 替换
  ③ sim->bp_count = 1
  ④ 打印 "Breakpoint 0 set at 0x00010074"
        │
用户: (rvsim) c
        │
        ▼
debugger_continue(sim)
  ① sim->cpu.running = true
  ② while (sim->cpu.running) {
         sim_step(sim);    // 内部调用 cpu_execute → mmu_read_32 取指
     }
  ③ 当 PC == 0x10074:
       mmu_read_32 → 读到 EBREAK
       cpu_execute → 检测到 EBREAK → 查 sim->breakpoints[]
       → 命中 sim->breakpoints[0]！
         恢复原始指令: mmu_write_32(0x10074, orig_Instr)
         sim->cpu.running = false
  ④ 打印 "Hit breakpoint 0 at 0x00010074"
  ⑤ 打印寄存器状态
  ⑥ 回到 REPL: (rvsim)
```

#### 场景 3：单步执行（Debugger → sim_step）

```
用户: (rvsim) s
        │
        ▼
debugger_step(sim)
  ① 检查当前 PC 是否在断点列表中
     在 → 恢复原始指令 → sim_step(sim) → 重新写入 EBREAK
     不在 → sim->single_step = true → sim_step(sim)
  ② sim_step 执行一条指令（取指→译码→执行→写回）
  ③ 调用 cpu_disasm 反汇编刚执行的指令
  ④ 打印: "0x00010074: 00b50533  add a0, a0, a1"
  ⑤ 打印寄存器（可选：高亮变化的寄存器）
  ⑥ 回到 REPL
```

#### 场景 4：查看内存/寄存器（Debugger → Simulator）

```
用户: (rvsim) info registers
        │
        ▼
debugger_print_registers(sim)
  → 读取 sim->cpu.regs[0..31], sim->cpu.pc, sim->cpu.mstatus, sim->cpu.mcause
  打印（格式对齐，每行 4 个寄存器）:
    x0  (zero) = 0x00000000    x1  (ra)   = 0x10000080
    x2  (sp)   = 0xBFFFFFF0    x3  (gp)   = 0x00000000
    ...
    x30 (t5)   = 0x00000000    x31 (t6)   = 0x00000000
    PC          = 0x00010074

用户: (rvsim) x/4xw 0x10000
        │
        ▼
debugger_examine_memory(sim, 0x10000, 4, 'x', 'w')
  for (i = 0; i < 4; i++) {
      mmu_read_32(&sim->mmu, &sim->pmem, 0x10000 + i*4,
                  &val, sim->cpu.priv, &exc);
  }
  打印:
    0x10000: 0x02a00513  0x00000593  0x00b50533  0x00100073
```

#### 场景 5：退出（Debugger → main）

```
用户: (rvsim) q
        │
        ▼
Debugger:
  ① 遍历 sim->breakpoints[0..bp_count-1]，恢复所有原始指令
       for (i = 0; i < sim->bp_count; i++) {
           mmu_write_32(&sim->mmu, &sim->pmem,
                        sim->breakpoints[i].addr,
                        sim->breakpoints[i].original_Instr,
                        sim->cpu.priv, &exc);
       }
  ② free(sim->breakpoints)（在 sim_destroy 中统一做）
  ③ sim->debug_mode = false（退出 REPL 循环）
  ④ 打印 "Exiting..."
  ⑤ 返回 main.c → sim_destroy → 正常退出
```

---

## 4. 与队友的接口约定

### 4.1 我依赖的接口

| 模块（负责人） | 接口 | 用途 |
|--------------|------|------|
| Memory/MMU（焕聪） | `bool mmu_read_32(MMUState*, PhysicalMemory*, uint32_t vaddr, uint32_t *val, PrivilegeLevel, ExceptionType*)` | x 命令读内存、读断点处原始指令、栈回溯读帧 |
| Memory/MMU（焕聪） | `bool mmu_write_32(MMUState*, PhysicalMemory*, uint32_t vaddr, uint32_t val, PrivilegeLevel, ExceptionType*)` | 设断点时写入 ebreak、删除断点时恢复原始指令 |
| Memory/PhysicalMemory（焕聪） | `void mem_dump(PhysicalMemory*, uint32_t addr, uint32_t len)` | x 命令物理内存 hexdump |
| CPU（李特） | `sim->cpu.regs[32]`, `sim->cpu.pc`, `sim->cpu.running`, `sim->cpu.priv`, `sim->cpu.mstatus`, `sim->cpu.mcause`, `sim->cpu.mepc` | 查看寄存器状态 |
| CPU（李特） | `void cpu_disasm(uint32_t instr, uint32_t pc, char *buf, size_t bufsz)` | 反汇编显示（x 命令、单步后显示、栈回溯） |
| Simulator（李特/焕聪） | `void sim_step(Simulator*)` | 单步执行一条指令 |
| Simulator（李特/焕聪） | `sim->breakpoints[]`, `sim->bp_count`, `sim->bp_capacity`, `sim->single_step`, `sim->debug_mode` | 断点管理 + 执行控制 |

### 4.2 我暴露给别人的接口

| 谁依赖我 | 接口 | 用途 |
|----------|------|------|
| main.c | `void debugger_run(Simulator*)` | 启动调试 REPL（调试模式下调用） |
| sim_step（李特） | `bool debugger_check_breakpoint(Simulator*, uint32_t pc)` | CPU 执行 ebreak 后判断是否是用户断点 |
| sim_step（李特） | `sim->single_step` / `sim->debug_mode` 标志 | CPU 判断是否在单步/调试模式 |

### 4.3 断点与 CPU 的交互约定

**CPU 侧（`cpu_execute` 内 ebreak 处理，李特负责）需要做的事情：**

```
当 cpu_execute 检测到 ebreak (opcode=0x73, imm=1) 时：
  ① 遍历 sim->breakpoints[0..bp_count-1]，看当前 PC 是否在列表中
  ② 如果在 → 用户断点：
       - 恢复原始指令: mmu_write_32(addr, bp->original_Instr)
       - *next_pc = pc（不前进 PC，停在断点地址）
       - cpu.running = false（暂停执行）
       - 打印 "Hit breakpoint at 0x..."
       - 如果 debug_mode 为 true → 回到 Debugger REPL
  ③ 如果不在 → 程序自身的 ebreak：
       - cpu_trap(sim, EXC_BREAKPOINT, pc)
       - （若 mtvec=0 则 fallback: printf + cpu->running = false）
```

**Debugger 侧（`debugger_continue` 内 continue 处理，嘉俊负责）需要做的事情：**

```
① 如果当前 PC 在断点列表中：
     - sim_step(sim)  // 单步执行一条原始指令，让 PC 越过断点
     - 重新写入 ebreak 到该地址
② sim->cpu.running = true
③ while (cpu.running) { sim_step(sim); }  // 继续执行
```

---

## 5. 实现要点总结

| 要点 | 说明 |
|------|------|
| **软件断点** | 通过 `mmu_write_32` 写入 EBREAK (0x00100073)，命中后恢复→单步→重新插入 |
| **断点存储** | 在 `Simulator` 下，动态数组 `Breakpoint*`，下标即为编号 |
| **命令缩写** | 前缀匹配（`b`→break, `c`→continue, `s`→step, `q`→quit） |
| **地址解析** | 支持 `0x` 十六进制、十进制、`$reg` 寄存器引用（ABI 名和 xN 编号） |
| **MMU 接口** | 所有访存通过 `mmu_read/write_32`，不直接调 `mem_*`，传 `PrivilegeLevel` + `ExceptionType*` |
| **x0 硬连线** | 打印寄存器时 x0 始终显示 0 |
| **退出清理** | quit 时遍历所有断点恢复原始指令（防止内存残留 EBREAK） |
| **错误处理** | MMU 操作失败 → 提示地址不可访问；非法命令 → 提示 "Unknown command" |
| **单步高亮** | 单步后可选高亮变化的寄存器（便于观察指令效果） |

---

## 6. 文件组织

```
src/include/debugger/
└── debugger.h              # 函数声明（debugger_run, debugger_add_breakpoint 等）

src/src/debugger/
├── debugger.c              # REPL 主循环 + 命令分发 + 地址解析
└── breakpoint.c            # 断点增删查 + EBREAK 写入/恢复实现
```

**依赖关系（#include 顺序）：**

```
types.h                         ← 零依赖，所有模块的基础
  └── simulator.h               ← Simulator 结构体（包含 Breakpoint, CPU, MMUState, PhysicalMemory）
        └── debugger.h          ← Debugger 函数声明（依赖 Simulator* 指针）
              └── debugger.c    ← 实现（include debugger.h + simulator.h）
              └── breakpoint.c  ← 实现（include debugger.h + simulator.h）
```

---

## 7. 参考

- `docs/设计文档/共同部分/公共类型定义.md` — Simulator / Breakpoint 结构体 + 全局类型
- `讨论清单.md` 结论 3/6/9 — 断点位置 / ebreak 方案 / 文件组织
- `docs/参考项目结构1/src/debugger.c` — 参考 REPL + 断点实现
- `docs/参考项目结构2/src/debugger/` — 参考命令解析 + 输出格式
- [RISC-V Debug Specification](https://github.com/riscv/riscv-debug-spec) — 硬件调试标准（参考，非完全实现）
