# Debugger 设计文档

## 1. 是什么（模块定位）

> Debugger 是 RISC-V 模拟器的**交互式调试层**。它挂在 CPU 旁边，以"旁路拦截"的方式工作——不在主数据流（ELF → Memory → CPU → 结果）中，而是随时监听 CPU 状态，在用户需要时暂停执行、查看状态、修改控制流。

**在系统中的位置：**

```
                  ┌───────────┐
                  │  Debugger │  ← 旁路：拦截、查看、控制
                  └─────┬─────┘
                   控制  │  读取
                  ┌─────┴─────┐
ELF → Loader ──► Memory ◄──► CPU ──► 结果
```

**一句话概括：** Debugger 让用户能以"上帝视角"观察和控制程序的每一步执行——在哪停、什么时候走、每一步寄存器/内存里有什么，全由用户决定。

### 与其他模块的关系

| 关系 | 模块 | 说明 |
|------|------|------|
| 控制对象 | CPU | 读 PC / 寄存器，控制 `running` / `single_step`，设断点时修改内存中的指令 |
| 数据来源 | Memory | 读取内存内容（`x` 命令）、读取/写入断点处的指令 |
| 被谁触发 | 用户 或 CPU 异常 | 启动时加 `-s` 参数进入调试模式；CPU 遇到 ebreak 时自动暂停并进入 |

---

## 2. 做什么（功能与边界）

### 2.1 对外提供的功能

Debugger 提供一个交互式命令行界面（REPL），支持以下命令：

| 命令 | 缩写 | 功能 |
|------|------|------|
| `break <addr>` | `b <addr>` | 在指定地址设置软件断点 |
| `delete <id>` | `d <id>` | 删除指定编号的断点 |
| `info breakpoints` | `i b` | 列出所有断点 |
| `step` | `s` | 单步执行一条指令 |
| `stepi <n>` | `si <n>` | 执行 n 条指令 |
| `continue` | `c` | 继续运行，直到命中断点或程序退出 |
| `info registers` | `i r` | 显示所有 32 个通用寄存器 + PC 的值 |
| `x/<N><F><U> <addr>` | `x <addr>` | 查看指定地址的内存内容（hexdump 风格） |
| `print <reg>` | `p <reg>` | 打印单个寄存器的值（如 `p $a0`） |
| `backtrace` | `bt` | 打印调用栈（通过帧指针链回溯） |
| `help` | `h` | 显示帮助信息 |
| `quit` | `q` | 退出模拟器 |

### 2.2 断点功能详解

**软件断点原理：**

```
设断点前: 0x10074: [原始指令 addi sp, sp, -16]
         ↓ b 0x10074
设断点后: 0x10074: [EBREAK 0x00100073]
                 原始指令被保存到 breakpoint_list[i].original_insn
         ↓ CPU 执行到 0x10074
命中时:   CPU 读到 EBREAK → 触发异常 → 暂停
         Debugger 接管，显示 "Hit breakpoint 1 at 0x10074"
         ↓ 用户输入 c (continue)
继续前:   恢复原始指令到 0x10074
         单步执行原始指令 (PC 越过 0x10074)
         重新写入 EBREAK 到 0x10074（以便下次再次命中）
         继续正常执行
```

### 2.3 边界：什么归我管、什么不归我管

| 归 Debugger 管 | 不归 Debugger 管 |
|---------------|-----------------|
| 命令解析与分发 | ELF 文件的解析（Loader） |
| 断点的增删查 | 指令的取指、译码、执行（CPU） |
| 用户交互循环（REPL） | 虚拟地址翻译（MMU） |
| 寄存器/内存的**查看** | 物理内存的分配与管理（Memory） |
| 执行节奏的控制（暂停/单步/继续） | 系统调用的实现（Syscall） |
| 调用栈的回溯解析 | 异常处理的底层逻辑（CPU） |

**关键原则：** Debugger **只读不写** CPU 和 Memory 的核心状态（断点替换指令除外）。Debugger 通过 `single_step` 和 `running` 标志来控制 CPU 的执行节奏，而不是直接操纵 CPU 内部状态机。

---

## 3. 怎么做

### 3.1 对外接口

```c
// ========== 数据结构 ==========

#define MAX_BREAKPOINTS 256

/* 单个断点 */
typedef struct {
    int      id;              // 断点编号（自增，唯一）
    uint32_t addr;            // 断点地址
    uint32_t original_insn;   // 被替换前的原始指令（用于恢复）
    bool     enabled;         // 是否启用
} Breakpoint;

/* 调试器状态 */
typedef struct {
    Breakpoint breakpoints[MAX_BREAKPOINTS];
    int        bp_count;          // 当前断点数
    int        next_bp_id;        // 下一个断点编号
    bool       step_mode;         // 单步模式标志
    bool       running;           // 调试器 REPL 是否在运行
    CPUState  *cpu;               // 指向 CPU 状态
    PhysicalMemory *pmem;         // 指向物理内存
    MMUState  *mmu;               // 指向 MMU
} DebuggerState;

// ========== 函数接口 ==========

/* 初始化调试器 */
void debugger_init(DebuggerState *dbg, CPUState *cpu,
                   PhysicalMemory *pmem, MMUState *mmu);

/* 启动调试 REPL 主循环（阻塞，直到用户 quit） */
void debugger_run(DebuggerState *dbg);

/* 断点管理 */
int  debugger_add_breakpoint(DebuggerState *dbg, uint32_t addr);
bool debugger_del_breakpoint(DebuggerState *dbg, int id);
void debugger_list_breakpoints(const DebuggerState *dbg);
bool debugger_check_breakpoint(DebuggerState *dbg, uint32_t pc);

/* 执行控制 */
void debugger_step(DebuggerState *dbg);       // 单步一条指令
void debugger_continue(DebuggerState *dbg);    // 继续执行到断点

/* 状态查看 */
void debugger_print_registers(const CPUState *cpu);
void debugger_examine_memory(PhysicalMemory *pmem, MMUState *mmu,
                             uint32_t addr, int count,
                             char format, char unit);
```

### 3.2 内部实现思路

#### 3.2.1 REPL 主循环（`debugger_run`）

```
┌─────────────────────────────────────┐
│          debugger_run(dbg)          │
├─────────────────────────────────────┤
│  打印欢迎信息                        │
│  打印初始寄存器状态                   │
│                                     │
│  while (dbg->running) {             │
│    ① 打印提示符 "(rvsim) "           │
│    ② fgets() 读取用户输入            │
│    ③ strtok() 分词 (空格/制表符)     │
│    ④ 命令分发：                      │
│       "b"/"break" → 设断点           │
│       "d"/"delete" → 删断点          │
│       "s"/"step"   → 单步            │
│       "si"/"stepi" → 多步            │
│       "c"/"continue"→ 继续执行       │
│       "i r"        → 打印寄存器      │
│       "i b"        → 列出断点        │
│       "x"          → 查看内存        │
│       "bt"         → 调用栈回溯      │
│       "q"/"quit"   → 退出            │
│       "h"/"help"   → 帮助            │
│       default      → "Unknown cmd"  │
│  }                                  │
└─────────────────────────────────────┘
```

#### 3.2.2 断点设置流程（`debugger_add_breakpoint`）

```
① 参数校验：地址是否在已映射区域内？
② 去重检查：该地址是否已经设有断点？
③ 数量检查：bp_count >= MAX_BREAKPOINTS？
④ 保存原始指令：
   mmu_read_32(mmu, pmem, addr, &bp->original_insn, cpu->priv)
⑤ 写入 EBREAK：
   mmu_write_32(mmu, pmem, addr, 0x00100073, cpu->priv)
   （注意：代码段权限是 R+X，写入时需要临时放宽权限）
⑥ 记录到 breakpoints 数组，bp_count++
⑦ 返回断点编号
```

#### 3.2.3 断点命中 → 恢复 → 重新插入 流程

```
【CPU 执行到断点地址时】
CPU 读到 EBREAK (0x00100073) → 触发 EXC_BREAKPOINT 异常
  ↓
检查：当前 PC 是否在断点列表中？
  ├── 是 → 这是用户设的断点
  │      ① 恢复原始指令（写回原指令到该地址）
  │      ② 打印 "Hit breakpoint N at 0x..."
  │      ③ 打印当前寄存器状态
  │      ④ 暂停 CPU，回到 REPL 等待用户命令
  │
  └── 否 → 程序自身的 ebreak 指令，当作正常 ecall 处理或报错

【用户输入 continue 后】
① 单步执行断点处的原始指令（让 PC 越过断点地址）
② 重新写入 EBREAK 到断点地址（恢复断点）
③ 继续正常执行（while(running) 循环）
```

#### 3.2.4 x 命令（内存查看）格式解析

```
x/<N><F><U> <addr>

N = 显示数量（默认 16）
F = 显示格式：
    x → 十六进制 (hex)
    d → 有符号十进制 (dec)
    u → 无符号十进制 (unsigned)
    o → 八进制 (oct)
    t → 二进制 (bin)
U = 单位大小：
    b → 1 字节 (byte)
    h → 2 字节 (halfword)
    w → 4 字节 (word)

输出格式（hexdump 风格）：
  地址: 字节字节字节字节 字节字节字节字节  |可打印ASCII字符|
  例：
  0x10000: 97 01 00 00 93 81 01 80  13 05 00 00 13 06 00 00  |................|
```

#### 3.2.5 地址解析（`parse_addr`）

支持三种地址格式：
- **十六进制字面量：** `0x10074`、`0x8000_0000`
- **十进制字面量：** `1024`
- **寄存器引用：** `$x10`、`$a0`、`$sp`、`$pc`、`$ra`
- **表达式：** `$sp + 0x10`（简单加减）

ABI 名称映射表：
| ABI 名 | 寄存器 | ABI 名 | 寄存器 |
|--------|--------|--------|--------|
| zero | x0 | a0-a7 | x10-x17 |
| ra | x1 | s0-s11 | x8-x9, x18-x27 |
| sp | x2 | t0-t2 | x5-x7 |
| gp | x3 | t3-t6 | x28-x31 |
| tp | x4 | pc | (特殊) |

#### 3.2.6 调用栈回溯（`backtrace`）

```
算法：基于帧指针链 (Frame Pointer)
需要 ABI 约定：s0/fp (x8) 保存上一帧的 fp

struct StackFrame {
    uint32_t fp;        // 上一帧的 fp（存在当前 fp 指向的位置）
    uint32_t ra;        // 返回地址（存在 fp + 4 的位置）
};

backtrace 算法：
  ① current_fp = cpu->regs[REG_S0]  // x8 = fp
  ② current_pc = cpu->pc
  ③ 打印 "PC = 0x..."
  ④ while (current_fp != 0 且 current_fp 在栈区域内) {
        ra = mem_read_32(current_fp + 4)
        打印 "  frame: fp=0x..., ra=0x..."
        current_fp = mem_read_32(current_fp)
     }
  ⑤ 递归深度限制（如最大 64 层，防止死循环）
```

### 3.3 模块交互时序

#### 场景 1：启动调试（main → Debugger）

```
main.c:
  ① Loader 加载 ELF → Memory
  ② CPU 初始化（设置 PC = 入口地址）
  ③ 如果是调试模式（-s 参数）：
     debugger_init(&dbg, &cpu, &pmem, &mmu)
     debugger_run(&dbg)           ← 进入 REPL，等待用户命令
```

#### 场景 2：用户设断点并命中（Debugger ↔ CPU ↔ Memory）

```
用户: (rvsim) b 0x10074
        │
        ▼
Debugger: debugger_add_breakpoint(dbg, 0x10074)
  ① dbg → Memory: mmu_read_32(0x10074) → 得到原始指令
  ② dbg → Memory: mmu_write_32(0x10074, EBREAK)
  ③ 打印 "Breakpoint 1 set at 0x10074"
        │
用户: (rvsim) c
        │
        ▼
Debugger: debugger_continue(dbg)
  ① while (cpu.running) {
       cpu_step(cpu, pmem, mmu)
         CPU → Memory: mem_read_32(PC) // 取指令
         ...执行...
         PC += 4
     }
  ② 当 PC == 0x10074:
       CPU 读到 EBREAK → 触发 EXC_BREAKPOINT
       检查断点列表 → 命中！
       cpu.running = false
       恢复原始指令到 0x10074
  ③ 打印 "Hit breakpoint 1 at 0x10074"
  ④ 回到 REPL，显示 (rvsim) 提示符
```

#### 场景 3：单步执行（Debugger → CPU）

```
用户: (rvsim) s
        │
        ▼
Debugger: debugger_step(dbg)
  ① 处理断点恢复（如果当前在断点处）：
     - 恢复原始指令
     - cpu_step() 单步一次
     - 重新写入 EBREAK
  ② 否则：
     - cpu_step() 单步一次
  ③ 打印 PC 和刚执行的指令
  ④ 打印寄存器变化（高亮变化的寄存器）
  ⑤ 回到 REPL
```

#### 场景 4：查看内存/寄存器（Debugger → CPU / Memory）

```
用户: (rvsim) info registers
        │
        ▼
Debugger → CPU: 读取 cpu->regs[0..31]、cpu->pc
  打印:
    x0  (zero) = 0x00000000 (0)
    x1  (ra)   = 0x10000080 (268435584)
    x2  (sp)   = 0xBFFFFFF0 (3221225456)
    ...
    PC          = 0x00010074

用户: (rvsim) x/4xw 0x10000
        │
        ▼
Debugger → Memory: 
  for (i = 0; i < 4; i++)
    mmu_read_32(mmu, pmem, 0x10000 + i*4, &val, cpu->priv)
  打印:
    0x10000: 0x02a00513  0x00000593  0x00b50533  0x00100073
```

#### 场景 5：退出（Debugger → 系统）

```
用户: (rvsim) q
        │
        ▼
Debugger:
  ① 遍历所有断点，恢复原始指令（清理现场）
  ② debugger_del_breakpoint() × bp_count
  ③ dbg->running = false（退出 REPL 循环）
  ④ 返回 main.c → 正常退出流程
```

---

## 4. 实现要点总结

| 要点 | 说明 |
|------|------|
| **软件断点** | 使用 EBREAK (0x00100073) 替换原始指令，命中后恢复→单步→重新插入 |
| **命令缩写** | 前缀匹配即可（`b`→break, `c`→continue, `s`→step, `q`→quit） |
| **地址解析** | 支持 `0x` 十六进制、十进制、`$reg` 寄存器引用 |
| **权限处理** | 写 EBREAK 到代码段时，代码段通常是 R+X，模拟器中可以直接写物理内存绕过 |
| **错误处理** | 非法地址 → 提示"地址不在有效区域"；非法命令 → 提示"Unknown command" |
| **x0 硬连线** | 打印寄存器时 x0 始终显示 0，即使被错误写入 |
| **退出清理** | quit 时恢复所有断点的原始指令，避免内存残留 EBREAK |

---

## 5. 文件组织

```
src/debugger/
├── debugger.h       # DebuggerState 结构体 + 函数声明
├── debugger.c       # REPL 主循环 + 命令分发 + 地址解析
├── breakpoint.h     # Breakpoint 结构体 + 断点管理函数声明
└── breakpoint.c     # 断点增删查 + EBREAK 写入/恢复
```
