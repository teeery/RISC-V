# Linux Syscall（系统调用）设计文档

> 作者：嘉华
> 模块：linux
> 目录：`src/include/linux/` + `src/src/linux/`

---

## 1. 是什么（模块定位）

Syscall 模块是模拟器的**"操作系统内核替身"**——当 RISC-V 用户程序执行 `ecall` 指令时，CPU 陷入内核态，本模块接管执行对应的 Linux 系统调用（write / read / exit / brk），让用户程序以为自己在真实的 Linux 上运行。

**在数据流中的位置：**

```
CPU 执行循环
  │
  ├─ Fetch → Decode → Execute
  │                    │
  │     ecall 指令 ────┘
  │         │
  │         ▼
  │    ★ syscall_handler(sim)    ← 本模块
  │         │
  │         ├─ sys_write ──→ mmu_read  ← 从内存读数据 → fwrite(stdout)
  │         ├─ sys_read  ──→ mmu_write ← fread(stdin) → 写入内存
  │         ├─ sys_exit  ──→ sim->cpu.running = false
  │         └─ sys_brk   ──→ mmu_brk  ← 调整堆边界
  │
  └─ PC += 4（或退出）
```

启动阶段 Loader 退场后，运行时完全是 CPU + Memory + Syscall 的循环。

---

## 2. 做什么（功能与边界）

### 我负责的

| 系统调用 | 调用号 | 功能 |
|----------|--------|------|
| `exit` | 93 | 终止程序，设置退出码。把 `cpu.running` 置 false。 |
| `read` | 63 | 从文件描述符读取。目前只支持 stdin（fd=0），从宿主 stdin 读入 → 写入用户内存。 |
| `write` | 64 | 向文件描述符写入。从用户内存读数据 → 输出到宿主 stdout/stderr（fd=1/2）。 |
| `brk` | 214 | 调整堆边界（program break）。首次调用设初始值（= data+bss 末尾），后续调用扩展堆。 |

### RISC-V syscall 调用约定

```
a7 (x17) = 系统调用号   ← 用来判断是 exit / read / write / brk
a0 (x10) = 参数 1       ← 也是返回值存放位置
a1 (x11) = 参数 2
a2 (x12) = 参数 3
触发方式：ecall           ← CPU 执行到 ecall 时调用本模块
```

### 不归我管的

| 事 | 归谁管 | 说明 |
|----|--------|------|
| ecall 指令的识别和分发 | 李特（cpu） | `cpu_execute()` 的 ecall case 负责识别 `ecall` 并调用 `syscall_handler(sim)` |
| 内存读写 | 焕聪（memory） | 只调 `mmu_read` / `mmu_write`，不过问页表或权限 |
| 堆的底层管理 | 焕聪（memory） | `mmu_brk` 是 MMU 包装，内部调 `mem_brk` |
| 文件系统操作 | 无（模拟器简化） | 只支持 stdin/stdout/stderr 三个宿主 fd，不支持打开真实文件 |

### 各 syscall 的参数和返回值

| syscall | a7 | a0 | a1 | a2 | 返回值（a0） |
|---------|----|----|----|----|-------------|
| exit | 93 | status | — | — | 无（程序终止） |
| read | 63 | fd | buf(vaddr) | len | 实际读取字节数 |
| write | 64 | fd | buf(vaddr) | len | 实际写入字节数 |
| brk | 214 | new_brk | — | — | 当前 brk 地址 |

---

## 3. 怎么做

### 3.0 文件规划

```
src/include/linux/syscall.h    ← 对外头文件：系统调用号常量 + syscall_handler() 声明

src/src/linux/
  └── syscall.c                ← 实现：syscall_handler() + 4 个内部处理函数
```

因为四个 syscall 逻辑都不长，一个 `.c` 足够，不需要像 loader 那样拆多个文件。

### 3.1 对外接口

**声明：** `src/include/linux/syscall.h`

```c
#include "types.h"
#include "simulator.h"

/* 系统调用号 */
#define SYS_exit    93
#define SYS_read    63
#define SYS_write   64
#define SYS_brk     214

/* 特殊文件描述符 */
#define STDIN_FILENO   0
#define STDOUT_FILENO  1
#define STDERR_FILENO  2

/* 唯一对外入口 */
void syscall_handler(Simulator *sim);
```

**调用方式**（CPU 侧，ecall case 中）：

```c
case 0x73:  // SYSTEM opcode
    if (funct3 == 0 && imm == 0) {  // ecall
        syscall_handler(sim);
    }
    break;
```

### 3.2 内部实现思路

```
syscall_handler(sim)
  │
  ├─ 读取 a7 = sim->cpu.regs[REG_A7]
  │
  ├─ switch (a7):
  │
  │   case SYS_exit (93):
  │     ├─ status = sim->cpu.regs[REG_A0]
  │     ├─ printf("exit(%d)\n", status)
  │     └─ sim->cpu.running = false
  │
  │   case SYS_write (64):
  │     ├─ fd  = sim->cpu.regs[REG_A0]
  │     ├─ buf = sim->cpu.regs[REG_A1]  ← 虚拟地址
  │     ├─ len = sim->cpu.regs[REG_A2]
  │     ├─ 循环 len 次：
  │     │   └─ mmu_read_8(mmu, pmem, buf+i, &byte, ...)
  │     │      └─ 失败 → sim->cpu.regs[REG_A0] = -1; return
  │     ├─ 按 fd 输出到宿主：
  │     │   ├─ 1(stdout) / 2(stderr) → fwrite(..., stdout/stderr)
  │     │   └─ 其他 → 返回 -1
  │     └─ sim->cpu.regs[REG_A0] = 实际写入字节数
  │
  │   case SYS_read (63):
  │     ├─ fd  = sim->cpu.regs[REG_A0]
  │     ├─ buf = sim->cpu.regs[REG_A1]  ← 虚拟地址
  │     ├─ len = sim->cpu.regs[REG_A2]
  │     ├─ 只有 fd=0(stdin) 支持：
  │     │   ├─ fread(..., stdin) 读入临时缓冲
  │     │   └─ 循环写入：mmu_write_8(mmu, pmem, buf+i, byte)
  │     │      └─ 失败 → sim->cpu.regs[REG_A0] = -1; return
  │     └─ sim->cpu.regs[REG_A0] = 实际读取字节数
  │
  │   case SYS_brk (214):
  │     ├─ new_brk = sim->cpu.regs[REG_A0]
  │     ├─ result = mmu_brk(&sim->mmu, &sim->pmem, new_brk)
  │     └─ sim->cpu.regs[REG_A0] = result
  │
  │   default:
  │     └─ fprintf(stderr, "Unknown syscall %d\n", a7)
  │        sim->cpu.regs[REG_A0] = -1
```

**关键设计决策：**

- **为什么 len 很大时不一次分配？** `mmu_read/write` 已支持批量操作，但 write 逐字节读、read 逐字节写只是简化实现。可以改为一次性批量操作。
- **为什么不支持 open/close？** 模拟器没有虚拟文件系统。stdin/stdout/stderr 直接映射到宿主标准流。
- **为什么 brk 第一次调用自动初始化？** Loader 没有设 brk 起点，首次 `brk(0)` 返回 `mem_brk` 的内部默认值（= 0 或 data 段末尾）。调用 `mmu_brk` 而不是直接 `mem_brk` 是为了保持"CPU 只调 mmu_*"的约定。

### 3.3 模块交互时序

```
CPU 执行 addi a7, zero, 64   ← 设 syscall 号 = write
     addi a0, zero, 1        ← fd = stdout
     lui  a1, 0x10000        ← buf = 0x10000000（要输出的字符串地址）
     addi a2, zero, 14       ← len = 14 字节
     ecall                   ← ★ 触发
        │
        ▼
cpu_execute()                ← CPU 模块
  │ ecall case
  └─ syscall_handler(sim)    ← ★ 本模块
       │
       ├─ 读 a7=64 → SYS_write
       ├─ fd=1, buf=0x10000000, len=14
       │
       ├─ for i in 0..13:
       │    └─ mmu_read_8(&sim->mmu, &sim->pmem, 0x10000000+i, &byte, ...)
       │         └─ mem_read_8(&sim->pmem, paddr, &byte)
       │              (MMU Bare 模式: paddr = vaddr)
       │
       ├─ fwrite(buf, 1, 14, stdout)  ← 输出到宿主终端
       │
       └─ sim->cpu.regs[REG_A0] = 14  ← 返回写入字节数
            │
            ▼
cpu_execute() 继续: PC += 4
```

---

## 4. 与队友的对齐清单

| 序号 | 事项 | 需要对齐的人 | 当前状态 |
|------|------|-------------|---------|
| 1 | `syscall_handler(sim)` 应该在 `cpu_execute()` 的 ecall case 中被调用 | 李特 | 李特需在 execute.c 中 `#include "linux/syscall.h"` 并调用 |
| 2 | `mmu_read/write` 批量接口已就绪（mmu.h） | 焕聪 | ✅ |
| 3 | `mmu_brk` 已就绪（mmu.h 声明，mmu.c 实现） | 焕聪 | ✅ |
| 4 | 是否需要支持 exit 返回退出码给 main.c？ | 李特 | 当前只需 `running=false`，退出码可后续加 |

---

## 5. 测试计划

### L1：纯逻辑测试（不需要 Simulator）

| 测试用例 | 说明 |
|----------|------|
| `test_brk` | 验证 brk(0) 返回初始值，brk(new) 正确扩展 |

### L2：集成测试（需 CPU + Memory）

| 测试用例 | 说明 |
|----------|------|
| 汇编 `ecall exit(42)` | 程序终止，`running=false`，退出码 42 |
| 汇编 `write(1, "Hello", 5)` | stdout 输出 "Hello"，返回 5 |
| 汇编 `read(0, buf, 10)` | stdin 读取 10 字节到 buf |
