# 设计文档

先搞清楚每个模块在整个系统中是什么、处于什么位置，模块之间怎么交互，再开始写自己的设计文档。

---

## 系统全貌

### 启动 → 运行 → 退出的完整链路

```
hello (ELF 文件)
    │
    ▼
 Loader ──► Memory ──► CPU ──► 结果
    ↑         ↑         │
    │         │         ▼
    │         │      Debugger（旁路拦截）
    │         │
    └── 都通过 Memory 打交道 ──┘
```

### 两阶段视角

| 阶段 | 谁在工作 | 做什么 |
|------|---------|--------|
| 启动时 | Loader → Memory | 解析 ELF，把代码和数据搬进虚拟内存，建栈 |
| 运行时 | CPU + Memory + Debugger | 取指→译码→执行→访存→写回，Debugger 挂在旁边随时拦截 |

---

## 四个模块是什么

### Loader —— 把 ELF 文件搬进内存

**是什么？** 系统的入口。启动时解析 ELF32 文件，提取代码段和数据段，写入模拟器的虚拟内存。

**在数据流中的位置：** ELF 文件（磁盘上的二进制）→ 模拟器虚拟内存（MemRegion[]）。

**只和 Memory 打交道**，不直接碰 CPU。启动完成后退场，运行时不再参与。

### Memory —— 模拟器的"假内存"

**是什么？** 内存管理模块。维护一组 MemRegion（代码区、数据区、栈区），提供统一的读写接口，支持 Sv32 两级页表的虚拟地址翻译。

**在数据流中的位置：** 横跨整个生命周期，是所有模块的底层基础设施。Loader 往里面写、CPU 从里面读写、Debugger 从里面读。

### CPU —— 执行引擎

**是什么？** 模拟器的核心。一个 `while(running)` 循环，不断做"取指→译码→执行→访存→写回"，把 RISC-V 指令一条条跑完。

**在数据流中的位置：** 从 Memory 取指令，执行计算，遇到 ecall 触发系统调用，遇到 ebreak 触发调试器。

### Debugger —— 旁路控制器

**是什么？** 交互式调试层。挂在 CPU 旁边，拦截 ebreak 让程序暂停，给用户提供断点、单步、查看寄存器/内存的能力。

**在数据流中的位置：** 不在主数据流中，挂在 CPU 旁边随时拦截。用户想停就停、想看就看。

---

## 模块间的联系与交互

### 谁依赖谁

```
Debugger ──控制──► CPU ──读写──► Memory ◄──写入── Loader
    │               │
    └────读─────────┘
```

| 调用方 | 被调用方 | 怎么调 | 什么时候 |
|--------|---------|--------|---------|
| Loader | Memory | `mem_map()` 分配区域，`mem_write()` 写入代码/数据 | 启动时 |
| CPU | Memory | `mem_read32(addr)` 取指令 / 读写数据 | 每条指令的执行阶段 |
| Debugger | CPU | 读 `regs[]`、`PC`，控制 `running`、`single_step` | 用户命令触发 |
| Debugger | Memory | `mem_read32(addr)` 显示内存内容 | 用户 `x` 命令 |

### 四个场景串起所有交互

#### 场景 1：启动（Loader → Memory → CPU）

```
1. Loader 打开 ELF 文件，读 Header，验证 Magic Number
2. 遍历 Program Headers，对每个 PT_LOAD 段：
   Loader 调用 Memory.mem_map(base, size, prot) 分配区域
   Loader 调用 Memory.mem_write(base, data, size) 写入代码/数据
3. Loader 调用 Memory.mem_map(0xBFFC0000, 0x40000, MEM_READ | MEM_WRITE) 建栈（256KB）
4. Loader 告诉 CPU：entry_point = elf_header.entry（设 PC 初值）
5. Loader 退场
```

#### 场景 2：指令执行（CPU → Memory）

```
while (running) {
    ① Fetch:   insn = Memory.mem_read32(PC)    // CPU → Memory
    ② Decode:  拆出 opcode / rd / rs1 / rs2
    ③ Execute: result = ALU(regs[rs1], regs[rs2])
    ④ Memory:  if (lw) regs[rd] = Memory.mem_read32(addr)   // CPU → Memory
               if (sw) Memory.mem_write32(addr, regs[rs2])  // CPU → Memory
    ⑤ Writeback: regs[rd] = result
    PC += 4
}
```

#### 场景 3：用户设断点（Debugger ↔ CPU）

```
用户: (rvsim) b 0x10074

1. Debugger 把 0x10074 加入断点列表
2. Debugger 通过 Memory 读出 0x10074 处的原始指令，保存起来
3. Debugger 把 0x10074 处的指令替换成 ebreak（0x00100073）
4. CPU 继续执行...
5. CPU 执行到 0x10074，读到 ebreak → 触发异常，暂停
6. Debugger 接管，显示 "(rvsim) " 提示符
```

#### 场景 4：用户查看内存/寄存器（Debugger → CPU / Memory）

```
用户: (rvsim) info registers
→ Debugger 直接读 CPU.regs[0..31] 和 PC 并打印

用户: (rvsim) x 0x12000
→ Debugger 调用 Memory.mem_read32(0x12000) 并打印
```

### 接口边界要点

- **Loader ↔ Memory**：Loader 通过 `mem_map` + `mem_write` 写入。Memory 负责校验地址合法性。
- **CPU ↔ Memory**：CPU 通过 `mem_read32` / `mem_write32` 访问。Memory 负责地址翻译和权限检查，访问越界或权限不对 → 抛段错误，CPU 捕获后停止执行。
- **Debugger ↔ CPU**：Debugger 不直接改 CPU 的内部状态（除了设 PC 和 regs），通过 `single_step` 和 `running` 标志控制执行节奏。
- **Debugger ↔ Memory**：只读不写。调试时显示内存内容，不修改。

---

## 如何写自己的设计文档

按三层结构来写，逐层递进：

```
是什么 → 做什么 → 怎么做
```

### 是什么

用一段话回答：这个模块在整个系统中起到什么作用？处于数据流的哪个环节？

参考上面的模块分析，看你的模块在哪个位置，和其他模块怎么连接。

### 做什么

这个模块对外要提供哪些功能？边界在哪里（什么归我管、什么不归我管）？

### 怎么做

想清楚了是什么、做什么之后，再落笔到具体方案：

| 方面 | 要写清楚的问题 |
|------|---------------|
| 对外接口 | 暴露哪些函数/数据结构？别人怎么调用我？ |
| 内部实现 | 用什么算法/状态机？关键逻辑是什么？ |
| 模块交互 | 谁调我、我调谁？时序是怎样的？ |

---

## 模板

```markdown
# <模块名> 设计文档

## 1. 是什么（模块定位）

> 这个模块在整个系统中起到什么作用？处于数据流的哪个环节？

## 2. 做什么（功能与边界）

> 对外提供哪些功能？什么归我管、什么不归我管？

## 3. 怎么做

### 3.1 对外接口

> 函数签名、数据结构。别人怎么调用我？

### 3.2 内部实现思路

> 算法、状态机、关键逻辑。

### 3.3 模块交互时序

> 谁调我、我调谁？调用顺序是怎样的？
```
