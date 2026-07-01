# CPU 模块设计文档

> 作者：李特 | 最后更新：2026-07-01
> 
> **架构决策**：团队已选定**两层内存架构**（PhysicalMemory + MMUState），详见 `docs/设计文档/共同部分/公共类型定义.md`。
> CPU 通过 `Simulator*` 访问 MMU 和 PhysicalMemory，**只调用 MMU 层函数，不直接调用 PhysicalMemory 层**。

---

## 1. 是什么（模块定位）

CPU 模块是 RISC-V 模拟器的**执行引擎**，处于数据流的**核心环节**。

在系统启动链路中：

```
ELF 文件 → Loader → PhysicalMemory + MMU → CPU → 执行结果
                                              │
                                           Debugger（旁路拦截）
```

- **上游**：Memory 子系统（两层）— CPU 通过 `mmu_read_32(mmu, pmem, pc, ...)` 取指令和读写数据。MMU 层负责 Sv32 地址翻译 + 权限校验，PhysicalMemory 层负责字节数组读写
- **下游**：无 — CPU 是数据流的终点
- **旁路**：Debugger — 挂在 CPU 旁边，读寄存器、控制执行节奏

CPU 模块的本质是一个**用 C 语言实现的 RISC-V 指令解释器**：用一个 `while(running)` 循环，不断重复"取指 → 译码 → 执行 → 访存 → 写回"，把 ELF 里的 RV32IM 指令一条条跑完。它不是真实的硬件 CPU，而是模拟器对 CPU 行为的软件仿真。

---

## 2. 做什么（功能与边界）

### 2.1 归我管的

| 功能 | 说明 |
|------|------|
| CPU 状态管理 | 32 个通用寄存器 (x0–x31)、程序计数器 (PC)、运行标志 (`running`) |
| 指令解码 | 将 32 位二进制指令解析为操作码、寄存器号、立即数等字段 |
| 指令执行 | RV32I 基本整数指令（约 38 条）+ M 扩展乘除指令（8 条）|
| 反汇编器 | 将二进制指令转为汇编字符串（提供给 Debugger 用于 `x` 命令显示） |
| x0 写保护 | `regs[0]` 硬连线为 0，任何写入操作对 x0 无效 |
| 系统调用触发 | 遇到 `ecall` 指令时调用 syscall 处理函数 |
| 断点检测 | 遇到 `ebreak` 指令时触发 Debugger 接管 |

### 2.2 不归我管的

| 功能 | 归属 | 为什么 |
|------|------|--------|
| ELF 解析与加载 | Loader（嘉华） | CPU 不读文件，只从 Memory 取指令 |
| 虚拟地址翻译 | Memory / MMU（焕聪） | CPU 传虚拟地址给 `mmu_read_32()`，MMU 内部做 Sv32 页表翻译和权限校验 |
| 物理内存管理 | Memory / PhysicalMemory（焕聪） | 字节数组 `data[]`、MemoryRegion 列表、边界检查全在 PhysicalMemory 层 |
| 页表管理 | Memory / MMU（焕聪） | 根页表分配、PTE 读写、`mmu_map_page` 全在 MMU 层 |
| 断点管理 | Debugger（嘉俊） | 断点列表的增删、指令替换全在 Debugger 侧 |
| 命令行交互 | Debugger（嘉俊） | REPL 提示符和命令解析 |

### 2.3 边界接口

CPU 处于模拟器的核心位置，它通过 `Simulator*` 指针访问所有其他模块：

```
          ┌─────────────────────────────────────────┐
          │              Simulator                   │
          │  ┌──────┐  ┌──────────┐  ┌───────────┐  │
          │  │ CPU  │  │ MMUState │  │PhysicalMemory│ │
          │  └──┬───┘  └────┬─────┘  └─────┬─────┘  │
          │     │           │              │         │
          │     └───调用────┴──────────────┘         │
          │        mmu_read_32(mmu, pmem, ...)       │
          │        mmu_write_32(mmu, pmem, ...)      │
          └─────────────────────────────────────────┘

CPU ──调用──► MMU:    mmu_read_32/16/8()  取指 + LOAD
                       mmu_write_32/16/8() STORE
                       mmu_read/write()    syscall 批量读写
                       （MMU 内部自动翻译 vaddr→paddr，再调 PhysicalMemory）

CPU ◄──读取── Debugger: 读 regs[], pc, CSR; 控制 running / single_step 标志
CPU ──触发──► Simulator: ebreak → 查 sim->breakpoints[] → 通知 Debugger
CPU ──触发──► syscall:   ecall → syscall_handler(sim) 处理系统调用
```

---

## 3. 怎么做

### 3.1 文件组织

```
src/include/cpu/          # CPU 模块头文件
├── cpu.h                 # CPU 状态结构体 + 初始化函数声明
├── decode.h              # 位域提取宏 + 立即数拼接宏 + DecodedInstr 结构体
└── execute.h             # cpu_execute 函数声明

src/src/cpu/              # CPU 模块源文件
├── cpu.c                 # cpu_init / cpu_reset 实现
├── decode.c              # cpu_decode / cpu_disasm 实现
└── execute.c             # 指令执行（最大的 switch-case）
```

### 3.2 对外接口

#### 3.2.1 CPU 状态结构体 (`cpu.h`)

##### 寄存器文件 `regs[32]` —— CPU 的"草稿纸"

`regs[32]` 是 RISC-V CPU 内部的 **32 个通用寄存器**，每个能存一个 32 位整数。CPU 做任何计算时，数据必须先装进这些寄存器，算完的结果也写回寄存器。**这是 CPU 能接触的最快存储**——比内存快几百倍。

RISC-V 给每个寄存器起了两个名字：**编号名**（x0–x31，指令编码里用）和 **ABI 名**（人类写汇编时用）。写模拟器代码时用编号下标访问数组。

**为什么是 32 个？** 这是 RISC 风格的典型设计——够编译器做寄存器分配优化，又不至于让指令编码太宽（5 位就能编码 0–31 号寄存器）。对比：ARM Cortex-M 有 13 个，x86-64 有 16 个。

##### 分四组来理解

**第一组：特殊用途寄存器（x0–x4）**

| 编号 | ABI 名 | 含义 | 在模拟器里怎么用 |
|------|--------|------|-----------------|
| x0 | `zero` | 硬连线为 0 | 永远读出来是 0，写进去的值直接丢弃。汇编里当常数来源：`addi x3, x0, 5` 就是把 5 搬进 x3。每条指令执行完后必须 `r[0] = 0` |
| x1 | `ra` | Return Address | `jal` 指令自动把"下一句的地址"写进 ra。函数返回时 `jalr x0, ra, 0` 跳回去 |
| x2 | `sp` | Stack Pointer | 永远指向栈顶。Loader 启动时设为 `0xC0000000`（栈顶），函数调用时向下增长 |
| x3 | `gp` | Global Pointer | 指向全局数据区中间位置，方便一条指令访问全局变量。简单程序可以不管 |
| x4 | `tp` | Thread Pointer | 指向线程局部存储 (TLS)。单线程模拟器基本不用 |

**第二组：函数参数和返回值（x10–x17）**

| 编号 | ABI 名 | 含义 |
|------|--------|------|
| x10 | `a0` | 第 1 个参数 / 返回值 |
| x11 | `a1` | 第 2 个参数 / 第二返回值 |
| x12 | `a2` | 第 3 个参数 |
| x13 | `a3` | 第 4 个参数 |
| x14 | `a4` | 第 5 个参数 |
| x15 | `a5` | 第 6 个参数 |
| x16 | `a6` | 第 7 个参数 |
| x17 | `a7` | 第 8 个参数 / **系统调用号** |

调用函数时，前 8 个参数放 a0–a7，超过 8 个的才压栈。这比 x86 全部压栈快得多。
在模拟器里，`a7` (x17) 是系统调用的关键——`ecall` 时靠它判断是什么系统调用（`SYS_exit=93`, `SYS_write=64`...）。

**第三组：临时寄存器（x5–x7, x28–x31）**

| 编号 | ABI 名 | 含义 |
|------|--------|------|
| x5–x7 | `t0`–`t2` | Temporary 0–2 |
| x28–x31 | `t3`–`t6` | Temporary 3–6 |

"临时"的意思是：调用子函数后，这些寄存器的值**可能被改掉**。如果主调函数有重要数据放在 t0 里，它必须在调用前自己保存。所以叫 **caller-saved**（调用者负责保存）。

**第四组：保存寄存器（x8–x9, x18–x27）**

| 编号 | ABI 名 | 含义 |
|------|--------|------|
| x8 | `s0` / `fp` | Saved 0 / Frame Pointer（帧指针） |
| x9 | `s1` | Saved 1 |
| x18–x27 | `s2`–`s11` | Saved 2–11 |

"保存"的意思是：调用子函数后，这些寄存器的值**保证不变**。如果子函数需要用到其中某个，必须先把它原来的值压栈、用完再弹回来。所以叫 **callee-saved**（被调用者负责保存）。

##### 一张图看清 32 个寄存器

```
regs[0]  zero ─── 永远为 0 ──────────────────────┐
regs[1]  ra   ─── 返回地址                         │
regs[2]  sp   ─── 栈指针                           │ 特殊用途 (5 个)
regs[3]  gp   ─── 全局指针                         │
regs[4]  tp   ─── 线程指针                      ───┘
regs[5]  t0                                     ───┐
regs[6]  t1      临时寄存器 (caller-saved)          │
regs[7]  t2                                         │
regs[8]  s0/fp   "函数调用后值可能被改"               │
regs[9]  s1                                         │
regs[10] a0      参数/返回值                         ├ 通用计算 (27 个)
regs[11] a1      "前 8 个参数, 其他压栈"             │
regs[12] a2                                         │
regs[13] a3                                         │
regs[14] a4                                         │
regs[15] a5                                         │
regs[16] a6                                         │
regs[17] a7      syscall 号                         │
regs[18] s2                                         │
...     ...     保存寄存器 (callee-saved)            │
regs[27] s11    "函数调用后值保证不变"                │
regs[28] t3     临时寄存器 (续)                      │
regs[29] t4                                         │
regs[30] t5                                         │
regs[31] t6                                      ───┘
```

##### 在模拟器里怎么用

```c
r[0]   // 永远是 0，每执行完一条指令就强行赋值为 0
r[1]   // ra，jal 指令会写它
r[2]   // sp，Loader 启动时设为 0xC0000000
r[10]  // a0，系统调用的第一个参数，也是返回值
r[17]  // a7，系统调用号（SYS_exit=93, SYS_write=64...）

// 解码阶段：指令里的 rd/rs1/rs2 字段就是 regs 的下标
// add x10, x5, x6    →  r[10] = r[5] + r[6]
// 反汇编就变成          →  add a0, t0, t1
```

##### CPU 结构体代码

```c
typedef struct {
    // ====== 通用寄存器 ======
    uint32_t regs[32];       // x0-x31，x0 硬连线为 0

    // ====== 程序计数器 ======
    uint32_t pc;             // 当前正在执行的指令地址

    // ====== 运行控制 ======
    bool     running;        // true = CPU 正在执行主循环

    // ====== 特权级 ======
    PrivilegeLevel priv;     // 当前特权级（模拟器始终为 PRIV_MACHINE）

    // ====== Machine 模式 CSR（控制和状态寄存器）======
    // 为什么需要 CSR？处理异常时必须：
    //   1. 保存异常 PC → mepc
    //   2. 记录异常原因 → mcause
    //   3. 记录异常相关信息 → mtval
    //   4. 跳转到异常处理程序 → mtvec
    //   5. 保存/恢复特权级和中断使能 → mstatus

    uint32_t mstatus;        // Machine Status
                             //   MIE  (bit[3]):  机器态中断使能
                             //   MPIE (bit[7]):  异常前的中断使能（异常返回时恢复）
                             //   MPP  (bits[12:11]): 异常前的特权级（异常返回时恢复）

    uint32_t mtvec;          // Machine Trap Vector
                             //   BASE (bits[31:2]): 异常处理程序入口地址（4 字节对齐）
                             //   MODE (bits[1:0]):  0=直接模式，1=向量模式（本阶段用 0）

    uint32_t mepc;           // Machine Exception PC
                             //   异常发生时，自动保存触发异常的指令 PC

    uint32_t mcause;         // Machine Cause
                             //   bit[31]: 0=异常(Exception)，1=中断(Interrupt)
                             //   bits[30:0]: 异常/中断编号（如 3=IllegalInst, 5=LoadAccessFault）

    uint32_t mtval;          // Machine Trap Value
                             //   异常相关信息：页错误为错误地址，非法指令为指令编码
} CPU;

void cpu_init(CPU *cpu);    // 初始化：全部置零，priv = PRIV_MACHINE，running = false
void cpu_reset(CPU *cpu);   // 重置：同 init
```

##### CSR 字段详解

**mstatus —— 机器状态寄存器**

```
  31    13  12  11  10   9   8   7   6   5   4   3   2   1   0
┌─────────┬─────┬────┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┐
│   ...   │ MPP │ ...│   │   │   │MPIE│   │   │   │MIE│   │   │
│         │[1:0]│    │   │   │   │    │   │   │   │   │   │   │
└─────────┴─────┴────┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┘
```

| 位 | 名称 | 含义 |
|----|------|------|
| [3] | MIE | Machine Interrupt Enable — 机器态中断使能（用 WRITE_CSR/READ_CSR 指令读写） |
| [7] | MPIE | Machine Previous Interrupt Enable — 异常发生前 MIE 的值（异常返回时恢复） |
| [12:11] | MPP | Machine Previous Privilege — 异常发生前的特权级（11=M, 1=S, 0=U；MRET 时恢复） |

**mtvec —— 陷阱向量基址**

```
  31                                    2   1   0
┌────────────────────────────────────────┬─────┐
│               BASE[31:2]               │ MODE│
│         异常处理程序入口地址             │ 0/1 │
└────────────────────────────────────────┴─────┘
```

- MODE=0（Direct）：所有异常跳转到 `BASE << 2`
- MODE=1（Vectored）：不同异常跳转到 `BASE << 2 + 4 × cause`（本阶段不实现）

**mepc —— 异常程序计数器**

异常发生时，硬件自动将触发异常的指令 PC 写入 mepc。MRET 指令从 mepc 恢复 PC。

**mcause —— 异常原因**

```
  31    30                                0
┌──────┬──────────────────────────────────┐
│ Int  │          Exception Code          │
│ 1=中断│     0=异常（如 3=IllegalInst）    │
└──────┴──────────────────────────────────┘
```

本阶段关注的异常码：

| Code | 枚举名 | 触发场景 |
|------|------|---------|
| 0 | `EXC_NONE` | 正常执行 |
| 1 | `EXC_INST_ADDR_MISALIGNED` | 指令地址未对齐 |
| 2 | `EXC_INST_ACCESS_FAULT` | 取指时 MMU 翻译失败 |
| 3 | `EXC_ILLEGAL_INST` | 未知操作码 |
| 4 | `EXC_BREAKPOINT` | ebreak 指令（断点） |
| 5 | `EXC_LOAD_ADDR_MISALIGNED` | Load 地址未对齐 |
| 6 | `EXC_LOAD_ACCESS_FAULT` | Load 指令访存失败 |
| 7 | `EXC_STORE_ADDR_MISALIGNED` | Store 地址未对齐 |
| 8 | `EXC_STORE_ACCESS_FAULT` | Store 指令访存失败 |
| 11 | `EXC_ECALL_M` | ecall 指令（系统调用） |
| 12 | `EXC_PAGE_FAULT_INST` | 取指页错误（Sv32 模式下 MMU 翻译失败） |
| 13 | `EXC_PAGE_FAULT_LOAD` | Load 页错误（Sv32 模式） |
| 15 | `EXC_PAGE_FAULT_STORE` | Store 页错误（Sv32 模式） |

**mtval —— 陷阱值**

| 异常类型 | mtval 的值 |
|---------|-----------|
| 页错误 / 访问错误 | 出错的虚拟地址 |
| 非法指令 | 非法指令的 32 位编码 |
| 地址未对齐 | 未对齐的地址 |
| 其他 | 0 |

##### 为什么寄存器用 `uint32_t` 而不是 `int` 或 `int32_t`

- **寄存器无类型**：硬件里就是 32 个二进制位，`uint32_t` 是纯粹的无类型位容器
- **无符号溢出合法**：C 标准规定无符号溢出是确定性的 wrap，用 `int32_t` 反而触发未定义行为
- **地址运算天然无符号**：`r[rs1] + imm` 算内存地址时不存在负数地址
- 需要符号语义时（如 `BLT` 比较），临时强转 `(int32_t)` 即可

##### 写模拟器时你只需要记住两条规则

1. **`regs[0]` 永远是 0** —— 每条指令执行完后强制执行 `r[0] = 0`，不管什么指令试图写 x0 都无效
2. **其他 31 个寄存器** —— 对模拟器来说就是普通的 `uint32_t` 变量，指令里 rd/rs1/rs2 字段指向谁就读谁写谁。唯一需要注意的是 Loader 和 Debugger 会按 ABI 名称来访问它们（sp、a0、a7 等）

#### 3.2.2 解码接口 (`decode.h`)

**位域提取宏：**

RISC-V 每条指令固定 32 位，不同的位段对应不同的含义。这 6 个宏就是"裁纸刀"——从 32 位里切出对应的字段。

```
一条 32 位指令的内存布局：
31        25 24    20 19    15 14    12 11      7 6        0
┌───────────┬────────┬────────┬────────┬─────────┬──────────┐
│  funct7   │  rs2   │  rs1   │ funct3 │   rd    │  opcode  │
│   7 位     │  5 位  │  5 位  │  3 位  │  5 位   │   7 位   │
└───────────┴────────┴────────┴────────┴─────────┴──────────┘
  [31:25]    [24:20]  [19:15]  [14:12]  [11:7]    [6:0]
```

举例 `add x10, x5, x6` → 编码 `0x00B302B3`，二进制逐段拆开：

```
0000000 00110 00101 000 01010 0110011
 funct7  rs2   rs1  f3  rd    opcode
  0x00   x6    x5   add x10   OP=0x33
```

```c
#define OPCODE(instr)   ((instr) & 0x7F)           // 取最低 7 位 [6:0]   → 0x33 (这是 OP 类)
#define RD(instr)       (((instr) >> 7) & 0x1F)    // 右移 7, 取低 5 位 [11:7] → 10 (x10/a0)
#define FUNCT3(instr)   (((instr) >> 12) & 0x7)    // 右移 12, 取低 3 位 [14:12] → 0 (加法类)
#define RS1(instr)      (((instr) >> 15) & 0x1F)   // 右移 15, 取低 5 位 [19:15] → 5 (x5/t0)
#define RS2(instr)      (((instr) >> 20) & 0x1F)   // 右移 20, 取低 5 位 [24:20] → 6 (x6/t1)
#define FUNCT7(instr)   (((instr) >> 25) & 0x7F)   // 右移 25, 取低 7 位 [31:25] → 0 (ADD)
```

`opcode` 决定指令大类和指令格式，`funct3` + `funct7` 在同类指令里进一步区分具体操作：

```
opcode = 0x33 (OP R-type) → "这是寄存器间运算"
    ├── funct3=0x0, funct7=0x00 → ADD  (加)
    ├── funct3=0x0, funct7=0x20 → SUB  (减)
    ├── funct3=0x0, funct7=0x01 → MUL  (乘)
    ├── funct3=0x4, funct7=0x00 → XOR  (异或)
    ├── funct3=0x4, funct7=0x01 → DIV  (除)
    └── ...
```

##### 宏是怎么工作的：`>>` 移位 + `&` 掩码

以 `OPCODE(instr)` 为例，`instr = 0x00B302B3`（`add x10, x5, x6` 的编码）：

```
instr  = 0000 0000 1011 0011 0000 0010 1011 0011   ← 32 位完整指令
0x7F  = 0000 0000 0000 0000 0000 0000 0111 1111   ← 只有低 7 位是 1
───────────────────────────────────────────────  &
结果  = 0000 0000 0000 0000 0000 0000 0011 0011   ← 只保留了低 7 位
                                              └── = 0x33
```

`0x7F` 是一把"筛子"——二进制低 7 位全是 1，其余位全是 0。和 `instr` 做按位与 (`&`)，只有两个都是 1 的位才留 1，其余全部清零。于是低 7 位被筛出来，其余 25 位全部归零。

其他 5 个宏同理，只是目标字段不在最低位，需要先**右移**让目标字段对齐到低位，再用掩码筛出：

```
RD(instr):    instr >> 7   → 把 [11:7] 挪到 [4:0]   → 再用 0x1F (低5位全1) 筛
RS1(instr):   instr >> 15  → 把 [19:15] 挪到 [4:0]  → 再用 0x1F 筛
FUNCT7(instr): instr >> 25 → 把 [31:25] 挪到 [6:0]  → 再用 0x7F (低7位全1) 筛
```

| 掩码 | 二进制 | 用于提取几位 |
|------|--------|-------------|
| `0x7` | `0111` | funct3（3 位） |
| `0x1F` | `0001 1111` | rd / rs1 / rs2（5 位） |
| `0x7F` | `0111 1111` | opcode / funct7（7 位） |

**一句话：`>>` 把要取的那一段挪到右边，`&` 一刀切掉左边不需要的部分。**

**6 种立即数拼接宏（最容易写错）：**

```c
// I-type: 12 位立即数，位于 [31:20]，符号扩展到 32 位
#define IMM_I(instr)  ((int32_t)((instr) >> 20))

// S-type: 12 位立即数，拆成两段
// 高位 [31:25] + 低位 [11:7]
#define IMM_S(instr)  ((int32_t)((((instr) >> 25) << 5) | (((instr) >> 7) & 0x1F)))

// B-type: 13 位立即数，bit[0] 恒为 0（地址 2 字节对齐），分散在 4 段
// [31] [7] [30:25] [11:8]
#define IMM_B(instr)  ((int32_t)((((instr) >> 31) << 12) | \
                                ((((instr) >> 7) & 1) << 11) | \
                                ((((instr) >> 25) & 0x3F) << 5) | \
                                ((((instr) >> 8) & 0xF) << 1)))

// U-type: 高 20 位，低 12 位为 0
#define IMM_U(instr)  ((int32_t)((instr) & 0xFFFFF000))

// J-type: 21 位立即数，bit[0] 恒为 0，分散在 4 段
// [31] [19:12] [20] [30:21]
#define IMM_J(instr)  ((int32_t)((((instr) >> 31) << 20) | \
                                ((((instr) >> 12) & 0xFF) << 12) | \
                                ((((instr) >> 20) & 1) << 11) | \
                                ((((instr) >> 21) & 0x3FF) << 1)))
```

##### 为什么立即数被拆得这么碎

核心思想：**所有指令格式的 `rs1`、`rs2`、`rd`、`funct3` 永远在同一个位段**，硬件译码器不用先判断格式就能直接读寄存器。立即数塞到剩下的空位里，格式差异只影响拼接电路。

| 格式 | 立即数位数 | 在指令里的分布 | 为什么搞成这样 |
|------|-----------|---------------|---------------|
| **I-type** | 12 位，连续 | `[31:20]` | 最简单，直接右移 20 位拿 |
| **S-type** | 12 位，拆成 2 段 | `[31:25]` + `[11:7]` | 让 `rs2` 位置和 R-type 对齐，硬件复用读寄存器电路 |
| **B-type** | 13 位，拆成 4 段，bit0 恒为 0 | `[31]` `[7]` `[30:25]` `[11:8]` | 让 `rs1`/`rs2` 位置不变，分支目标地址永远 2 字节对齐所以 bit0 不用存 |
| **U-type** | 20 位，连续，低 12 位为 0 | `[31:12]` | 给大常数（LUI/AUIPC），直接筛高 20 位 |
| **J-type** | 21 位，拆成 4 段，bit0 恒为 0 | `[31]` `[19:12]` `[20]` `[30:21]` | 和 B-type 原理相同，跳转地址 2 字节对齐 |

##### 每个宏拆解

**I-type**（用于 `addi` / `lw` / `jalr` 等）—— 12 位连续，最简单：
```
[31:20] = 12 位立即数，直接右移 20 位再用 (int32_t) 符号扩展
```

**S-type**（用于 `sw` / `sh` / `sb`）—— 拆成两段，中间被 rs2/rs1/funct3 隔开：
```
高位 [11:5] (instr >> 25) << 5  +  低位 [4:0] (instr >> 7) & 0x1F
        ╲                                ╲
   取出 [31:25]，左移到位置          取出 [11:7]（但这里存的是 rs2！实际上 [11:7] 在 S-type 里复用为 imm[4:0]）
```

**B-type**（用于 `beq` / `bne` 等分支）—— 最碎，分散在 4 段：
```
bit[12] ← instr[31]
bit[11] ← instr[7]
bit[10:5] ← instr[30:25]
bit[4:1] ← instr[11:8]
bit[0] = 0（分支目标永远 2 字节对齐，不用存）
```

**U-type**（用于 `lui` / `auipc`）—— 最简单，直接筛高位：
```
(instr) & 0xFFFFF000 → 取 [31:12] 共 20 位，低 12 位为 0
```

**J-type**（用于 `jal`）—— 和 B-type 类似很碎：
```
bit[20] ← instr[31]
bit[19:12] ← instr[19:12]
bit[11] ← instr[20]
bit[10:1] ← instr[30:21]
bit[0] = 0（跳转目标 2 字节对齐）
```

**解码结果结构体：**

```c
typedef struct {
    uint8_t  opcode;   // 指令类型大类
    uint8_t  rd;       // 目标寄存器号 (0-31)
    uint8_t  rs1;      // 源寄存器1号  (0-31)
    uint8_t  rs2;      // 源寄存器2号  (0-31)
    uint8_t  funct3;   // 3 位功能码
    uint8_t  funct7;   // 7 位功能码
    int32_t  imm;      // 立即数（已符号扩展）
} DecodedInstr;

// 解码一条 32 位指令
DecodedInstr cpu_decode(uint32_t instr);

// 反汇编：instr → 人类可读的汇编字符串
void cpu_disasm(uint32_t instr, uint32_t pc, char *buf, size_t bufsz);
```

#### 3.2.3 执行接口 (`execute.h`)

```c
struct Simulator;  // 前置声明

// 执行一条已解码的指令
//   sim      — 模拟器对象（通过它访问 mmu、pmem、breakpoints）
//   d        — 已解码的指令
//   next_pc  — 下一 PC，默认 pc+4，跳转/分支会修改
// 返回 true 表示正常，false 表示异常（非法指令等，CPU 内部已处理 CSR 写入）
bool cpu_execute(struct Simulator *sim, DecodedInstr *d, uint32_t *next_pc);
```

**CPU 访问 Memory 的方式（两层架构）：**

CPU 不直接调 `mem_*` 函数，始终通过 MMU 层访问。通过 `sim` 指针拿到 `mmu` 和 `pmem`：

```c
// 在 cpu_execute() 内部的典型调用：
ExceptionType exc = EXC_NONE;

// Fetch 取指令
uint32_t Instr;
if (!mmu_read_32(&sim->mmu, &sim->pmem, pc, &Instr, sim->cpu.priv, &exc)) {
    // 取指失败 → 设置 CSR，跳转异常处理
    cpu_trap(sim, exc, pc);
    return true;  // 异常已被处理，继续执行
}

// LOAD 指令
uint32_t val;
if (!mmu_read_32(&sim->mmu, &sim->pmem, addr, &val, sim->cpu.priv, &exc)) {
    cpu_trap(sim, exc, addr);
    return true;
}
r[rd] = val;

// STORE 指令
if (!mmu_write_32(&sim->mmu, &sim->pmem, addr, r[rs2], sim->cpu.priv, &exc)) {
    cpu_trap(sim, exc, addr);
    return true;
}

// syscall 批量读写
if (!mmu_read(&sim->mmu, &sim->pmem, buf_vaddr, buf, len, sim->cpu.priv, &exc)) {
    cpu_trap(sim, exc, buf_vaddr);
    return true;
}
```

**异常处理函数 `cpu_trap()`：**

```c
// 当 MMU 操作失败或 CPU 内部检测到异常时调用
// 按照 RISC-V 特权规范填写 CSR 并跳转到 mtvec
void cpu_trap(Simulator *sim, ExceptionType exc, uint32_t tval) {
    CPU *cpu = &sim->cpu;

    // 1. 保存当前 PC 到 mepc
    cpu->mepc = cpu->pc;

    // 2. 保存异常原因到 mcause
    cpu->mcause = (uint32_t)exc;

    // 3. 保存异常相关信息到 mtval
    cpu->mtval = tval;

    // 4. 更新 mstatus：保存当前 MIE → MPIE，保存当前特权级 → MPP，清 MIE
    uint32_t mpp = ((uint32_t)cpu->priv) << 11;
    uint32_t mpie = (cpu->mstatus & (1 << 3)) ? (1 << 7) : 0;
    cpu->mstatus = (cpu->mstatus & ~(3 << 11 | 1 << 7 | 1 << 3)) | mpp | mpie;

    // 5. 跳转到异常处理程序
    uint32_t trap_addr = cpu->mtvec & ~0x3u;  // MODE=0 直接模式
    cpu->pc = trap_addr;
}
```

### 3.3 内部实现思路

#### 3.3.1 `cpu.c` — 状态初始化

```c
void cpu_init(CPU *cpu) {
    memset(cpu, 0, sizeof(CPU));
    cpu->priv = PRIV_MACHINE;  // 模拟器始终运行在 M-mode
}

void cpu_reset(CPU *cpu) {
    cpu_init(cpu);  // 等价
}
```

逻辑极简。x0 的保护不在这里做，而是在每条指令执行完毕后 `regs[0] = 0`。

#### 3.3.2 `decode.c` — 指令解码

RISC-V 指令只有 6 种格式，解码逻辑是一个按 opcode 分发的 switch：

| opcode (hex) | 格式 | 指令类型 | 说明 |
|-------------|------|---------|------|
| `0x33` | R-type | OP | 寄存器间运算 (add, sub, sll, slt, xor, srl, sra, or, and) |
| `0x33` | R-type | OP (M扩展, funct7=0x01) | 乘除法 (mul, mulh, div, rem 等) — 注意：M 扩展复用 OP 操作码，通过 funct7=0x01 区分 |
| `0x13` | I-type | OP-IMM | 立即数运算 (addi, slli, slti, xori, ori, andi 等) |
| `0x03` | I-type | LOAD | 内存读 (lb, lh, lw, lbu, lhu) |
| `0x67` | I-type | JALR | 间接跳转 |
| `0x73` | I-type | SYSTEM | ecall / ebreak / CSR |
| `0x23` | S-type | STORE | 内存写 (sb, sh, sw) |
| `0x63` | B-type | BRANCH | 条件分支 (beq, bne, blt, bge, bltu, bgeu) |
| `0x37` | U-type | LUI | 加载高位立即数 |
| `0x17` | U-type | AUIPC | PC 加高位立即数 |
| `0x6F` | J-type | JAL | 跳转并链接 |
| `0x0F` | I-type | FENCE | 内存屏障（模拟器中为 nop） |

解码函数 `cpu_decode()` 的核心逻辑：
1. 取出 `opcode = instr & 0x7F`
2. 按 opcode 进入对应的格式分支，提取 rd / rs1 / rs2 / funct3 / funct7 / imm

反汇编器 `cpu_disasm()` 需要：
- **指令名表**：opcode + funct3 + funct7 → 指令名字符串
- **寄存器名表**：`x0→zero, x1→ra, x2→sp, x3→gp, x4→tp, x5→t0, x6→t1, x7→t2, x8→s0, x9→s1, x10→a0, x11→a1, x12→a2, x13→a3, x14→a4, x15→a5, x16→a6, x17→a7, x18→s2, x19→s3, x20→s4, x21→s5, x22→s6, x23→s7, x24→s8, x25→s9, x26→s10, x27→s11, x28→t3, x29→t4, x30→t5, x31→t6`
- 例如 `0x00300513, pc=0x10074` → 输出 `"addi a0, zero, 3"`

#### 3.3.3 `execute.c` — 指令执行

这是整个 CPU 模块最大的 switch-case。核心结构是一个双层 switch：

```c
bool cpu_execute(Simulator *sim, DecodedInstr *d, uint32_t *next_pc) {
    uint32_t *r = sim->cpu.regs;

    switch (d->opcode) {
        case 0x33:  // OP (R-type，含基础整数和 M 扩展)
            // funct7=0x00: 基础整数运算; funct7=0x20: SUB/SRA; funct7=0x01: M 扩展乘除法
            switch (d->funct3) {
                case 0x0:  // ADD / SUB / MUL
                    if      (d->funct7 == 0x00) r[d->rd] = r[d->rs1] + r[d->rs2];
                    else if (d->funct7 == 0x20) r[d->rd] = r[d->rs1] - r[d->rs2];
                    else if (d->funct7 == 0x01) r[d->rd] = r[d->rs1] * r[d->rs2];
                    break;
                case 0x1:  // SLL / MULH
                    ...
                // ... funct3 0x2 ~ 0x7
            }
            break;

        case 0x13:  // OP-IMM
            ...

        case 0x03:  // LOAD
            ...

        case 0x23:  // STORE
            ...

        case 0x63:  // BRANCH
            ...

        case 0x6F:  // JAL
        case 0x67:  // JALR
        case 0x37:  // LUI
        case 0x17:  // AUIPC
        case 0x73:  // SYSTEM (ecall / ebreak / CSR)
        case 0x0F:  // FENCE (nop)
            ...
    }

    r[0] = 0;  // x0 硬连线，必须在每条指令后置零
    return true;
}
```

**M 扩展乘除法实现要点：**

```c
// mulh: 有符号 × 有符号 → 高 32 位
int32_t mulh(int32_t a, int32_t b) {
    return (int32_t)(((int64_t)a * (int64_t)b) >> 32);
}

// mulhu: 无符号 × 无符号 → 高 32 位
uint32_t mulhu(uint32_t a, uint32_t b) {
    return (uint32_t)(((uint64_t)a * (uint64_t)b) >> 32);
}

// mulhsu: 有符号 × 无符号 → 高 32 位
int32_t mulhsu(int32_t a, uint32_t b) {
    return (int32_t)(((int64_t)a * (uint64_t)b) >> 32);
}

// 除法边界处理：
// - 除数为 0：DIV/DIVU 返回 -1 (全 1)，REM/REMU 返回被除数
// - DIV 溢出：INT32_MIN / -1 返回 INT32_MIN
```

**关键防错清单：**

| 规则 | 在哪里做 |
|------|----------|
| `regs[0]` 永远是 0 | `cpu_execute()` 末尾，每条指令执行后 |
| PC 默认 +4 | `sim_step()` 中 `*next_pc = pc + 4`，跳转指令覆盖 |
| JALR 目标地址最低位清零 | `(r[rs1] + imm) & ~1u` |
| LOAD 需要符号扩展 | LB→`(int8_t)`, LH→`(int16_t)`；LBU/LHU 不做扩展 |
| 除数为 0 的处理 | DIV/DIVU 返回 -1，REM/REMU 返回被除数 |

### 3.4 模块交互时序

#### 时序 1：正常指令执行

```
sim_step() 被调用:
    ┌──────────────────────────────────────────────────┐
    │ ① Fetch:                                        │
    │    CPU ──mmu_read_32(mmu, pmem, PC)──► MMU      │
    │                                          │       │
    │    MMU ──mmu_translate(PC)────────► 页表遍历    │
    │       │   (Bare模式→paddr=PC; Sv32→两级查表)    │
    │       ▼                                          │
    │    MMU ──mem_read_32(pmem, paddr)──► PhysicalMemory │
    │                                          │       │
    │    PhysicalMemory → data[paddr:paddr+3] 小端拼接 │
    │    CPU ←──32位指令────────────────────── MMU     │
    │                                                  │
    │ ② Decode:                                       │
    │    d = cpu_decode(instr)                         │
    │                                                  │
    │ ③ Execute:                                      │
    │    *next_pc = PC + 4  (默认)                     │
    │    ├─ LOAD ──mmu_read_32(mmu, pmem, addr)──► MMU→PhysicalMemory  │
    │    ├─ STORE ──mmu_write_32(mmu, pmem, addr, val)──► MMU→PhysicalMemory │
    │    └─ 其他 ── 纯寄存器操作                       │
    │                                                  │
    │ ④ PC 更新:                                      │
    │    CPU.pc = *next_pc                             │
    │    CPU.regs[0] = 0                               │
    └──────────────────────────────────────────────────┘
```

#### 时序 2：系统调用 ECALL

```
    CPU 译码: opcode=0x73, funct3=0x0, imm=0 (ecall)
        │
        ├── a7 (x17) == SYS_exit (93)
        │       → cpu.running = false，主循环退出
        │
        ├── a7 (x17) == SYS_write (64)
        │       → CPU ──mmu_read(mmu, pmem, buf_vaddr, buf, len)──► MMU→PhysicalMemory
        │         将读取到的数据 write() 到 stdout
        │         a0 (x10) = 实际写入字节数
        │
        ├── a7 (x17) == SYS_read (63)
        │       → 从 stdin read() 到临时缓冲区
        │         CPU ──mmu_write(mmu, pmem, buf_vaddr, data, len)──► MMU→PhysicalMemory
        │         a0 (x10) = 实际读取字节数
        │
        └── a7 (x17) == SYS_brk (214)
                → 调用 mmu_brk(&sim->mmu, &sim->pmem, new_brk) 扩展堆
                （brk 最终操作物理内存，但 CPU 通过 MMU 层包装间接调用，保持规则绝对性）
                a0 (x10) = 新的 brk 地址
```

#### 时序 3：断点拦截 EBREAK

```
    CPU 译码: opcode=0x73, funct3=0x0, imm=1 (ebreak)
        │
        ├── 遍历 sim->breakpoints[]
        │   ├── 当前 PC 在断点列表中
        │   │       → 恢复原始指令: mmu_write_32(addr, bp->original_Instr)
        │   │       → *next_pc = pc（不前进 PC，停在断点地址）
        │   │       → cpu.running = false
        │   │       → 控制权交给 Debugger REPL
        │   │       （Debugger continue 时：单步执行原始指令 → PC 自然越过断点 → 重新插入 ebreak）
        │   │
        │   └── 当前 PC 不在断点列表中（程序自身的 ebreak）
        │           → cpu_trap(sim, EXC_BREAKPOINT, pc)
        │            （若 mtvec=0 → fallback: printf + cpu->running = false）
```

#### 时序 4：完整生命周期

```
启动阶段:
    main.c
    ├── sim_init()
    │   ├── cpu_init(&sim->cpu)              // CPU: 寄存器清零, priv = MACHINE
    │   ├── mmu_init(&sim->mmu)              // MMU: 分配根页表, satp=0 (Bare模式)
    │   └── mem_init(&sim->pmem, 128MB)      // PhysicalMemory: calloc 字节数组
    │
    ├── sim_load_elf("hello.elf")
    │   ├── elf_load(&sim->pmem, &sim->mmu, &entry)
    │   │   ├── 遍历 ELF Program Headers (PT_LOAD)
    │   │   │   ├── mem_map(&pmem, paddr, size, flags, name)   // 注册物理区域
    │   │   │   ├── mem_load(&pmem, paddr, data, size)          // 拷段数据
    │   │   │   └── mmu_map_page(&mmu, vaddr, paddr, flags)     // 建立虚拟→物理映射
    │   │   └── mem_map(&pmem, 0xBFFC0000, 0x40000, MEM_READ | MEM_WRITE, "stack")  // 栈区域（256KB）
    │   ├── sim->cpu.pc = entry                                // Loader → CPU
    │   └── sim->cpu.regs[2] = 0xC0000000                      // 栈顶 sp
    │
    └── sim_run()
        cpu.running = true
        while (cpu.running) {
            sim_step()                    // ← cpu_execute 在这里被调用
        }
        输出统计：指令数、周期数
```

---

## 4. 实现优先级

| 优先级 | 内容 | 预估代码量 | 验证方式 |
|--------|------|-----------|---------|
| **P0** | CPU 状态结构体 + init/reset | ~20 行 | 编译通过，结构体能访问 |
| **P0** | 解码（位域宏 + 立即数宏 + cpu_decode） | ~120 行 | 手工输入已知指令逐条验证 |
| **P1** | RV32I 基本执行（LUI/AUIPC/JAL/JALR/BRANCH/LOAD/STORE/OP-IMM/OP） | ~200 行 | 跑 hello 程序（加 `-s` 单步看寄存器） |
| **P1** | ECALL / EBREAK / FENCE | ~40 行 | hello 程序能正常退出 |
| **P2** | M 扩展（乘除法） | ~60 行 | 跑含乘除的测试 ELF |
| **P2** | 反汇编器 | ~100 行 | 在 Debugger 中 `x addr` 看到汇编 |
| **P3** | 选做：五级流水线 | ~400 行 | CPI 统计 + 和基础版对比正确性 |

---

## 5. 与队友的接口约定

> **架构决策**：采用**两层内存架构**（PhysicalMemory + MMUState），详细约定见 `docs/设计文档/共同部分/公共类型定义.md`。
> CPU 通过 `Simulator*` 访问 MMU 和 PhysicalMemory，**只调 MMU 层函数，不直接调 `mem_*`**。

### 5.1 我依赖的接口

| 模块（负责人） | 接口 | 用途 |
|--------------|------|------|
| Memory/MMU（焕聪） | `bool mmu_read_32(MMUState*, PhysicalMemory*, uint32_t vaddr, uint32_t *val, PrivilegeLevel, ExceptionType*)` | 取指令 + LW |
| Memory/MMU（焕聪） | `bool mmu_read_16(MMUState*, PhysicalMemory*, uint32_t vaddr, uint16_t *val, PrivilegeLevel, ExceptionType*)` | LH/LHU |
| Memory/MMU（焕聪） | `bool mmu_read_8(MMUState*, PhysicalMemory*, uint32_t vaddr, uint8_t *val, PrivilegeLevel, ExceptionType*)` | LB/LBU |
| Memory/MMU（焕聪） | `bool mmu_write_32(MMUState*, PhysicalMemory*, uint32_t vaddr, uint32_t val, PrivilegeLevel, ExceptionType*)` | SW |
| Memory/MMU（焕聪） | `bool mmu_write_16(MMUState*, PhysicalMemory*, uint32_t vaddr, uint16_t val, PrivilegeLevel, ExceptionType*)` | SH |
| Memory/MMU（焕聪） | `bool mmu_write_8(MMUState*, PhysicalMemory*, uint32_t vaddr, uint8_t val, PrivilegeLevel, ExceptionType*)` | SB |
| Memory/MMU（焕聪） | `bool mmu_read(MMUState*, PhysicalMemory*, uint32_t vaddr, uint8_t *buf, uint32_t len, PrivilegeLevel, ExceptionType*)` | syscall write 批量读 |
| Memory/MMU（焕聪） | `bool mmu_write(MMUState*, PhysicalMemory*, uint32_t vaddr, const uint8_t *buf, uint32_t len, PrivilegeLevel, ExceptionType*)` | syscall read 批量写 |
| Loader（嘉华） | 设置 `sim->cpu.pc = elf_entry` | 程序入口地址 |
| Loader（嘉华） | 设置 `sim->cpu.regs[2] = 0xC0000000` | 栈指针 (sp) |
| Debugger（嘉俊） | 读 `sim->cpu.regs[]` / `sim->cpu.pc` / `sim->cpu.mstatus` 等 | 寄存器查看 |
| Debugger（嘉俊） | 控制 `sim->cpu.running` / `sim->single_step` | 执行控制 |
| Debugger（嘉俊） | 查询 `sim->breakpoints[]` | ebreak 时判断是否是断点 |

### 5.2 CPU 内部对 Memory 的调用约定

- **返回值**：`bool` — `true` 成功，`false` 失败（失败时 `*exc` 被填充异常类型）
- **地址**：CPU 传虚拟地址（`vaddr`），MMU 内部翻译为物理地址
- **特权级**：传 `sim->cpu.priv`（始终为 `PRIV_MACHINE`）
- **异常处理**：MMU 返回 `false` 时 → 调用 `cpu_trap(sim, exc, bad_addr)` 填写 CSR 并跳转

### 5.3 我暴露给别人的接口

| 谁依赖我 | 接口 | 用途 |
|----------|------|------|
| Simulator 主循环 | `cpu_decode(uint32_t instr)` → `DecodedInstr` | 译码 |
| Simulator 主循环 | `cpu_execute(Simulator*, DecodedInstr*, uint32_t *next_pc)` → `bool` | 执行 |
| Debugger | `cpu_disasm(uint32_t instr, uint32_t pc, char *buf, size_t bufsz)` | 反汇编显示 |
| Debugger | `cpu_init(CPU*)` / `cpu_reset(CPU*)` | 状态初始化/重置 |

### 5.4 异常处理流程

```
mmu_read_32() 返回 false
        │
        ▼
cpu_execute() 检测到 exc != EXC_NONE
        │
        ▼
cpu_trap(sim, exc, bad_addr)
  ├── mepc   = 当前 pc          // 保存异常地址
  ├── mcause = exc               // 保存异常原因
  ├── mtval  = bad_addr          // 保存错误地址
  ├── mstatus: MPP ← priv        // 保存旧特权级
  │            MPIE ← MIE        // 保存旧中断使能
  │            MIE  ← 0          // 关中断
  └── pc     = mtvec             // 跳转异常处理程序
```

**错误处理流程**：MMU 操作失败时返回 `false` + 填充 `*exc`，CPU 检测到后调用 `cpu_trap()` 统一处理：
  1. 填写 CSR（mepc = pc, mcause = exc, mtval = bad_addr, 更新 mstatus）
  2. 跳转到 mtvec 指向的异常处理程序（`pc = mtvec & ~0x3u`）
  3. 若 mtvec 未初始化（为 0）或阶段 1 无异常处理程序，`cpu_trap()` 内 fallback 为 `printf` + `cpu->running = false` 退出

---

## 6. 参考资料

- [RISC-V 非特权规范 (Volume I)](https://github.com/riscv/riscv-isa-manual/releases) — 第 2 章 RV32I 指令集
- [RISC-V 特权规范 (Volume II)](https://github.com/riscv/riscv-isa-manual/releases) — ecall/ebreak 行为和系统调用约定
- `docs/参考项目结构2/src/cpu/` — 参考骨架代码
- [RISC-V 指令编码速查](https://www.cl.cam.ac.uk/teaching/2016/ECAD+Arch/files/docs/RISCVGreenCardv8-20151013.pdf) — 一张卡片涵盖所有指令编码
