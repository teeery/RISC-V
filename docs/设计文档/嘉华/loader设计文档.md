# Loader（ELF 加载器）设计文档

> 作者：嘉华
> 模块：loader
> 参考项目结构：参考项目结构1
> 合约头文件：`src/include/loader/elf_loader.h`（对齐 [并行开发方案 §0.8](../共同部分/并行开发与验收方案.md#08-loaderelf_loaderh)）
> 并行开发轨道：[§2C](../共同部分/并行开发与验收方案.md#2c-嘉华loader-模块elf-解析与加载)

---

## 1. 是什么（模块定位）

Loader 是整个 RISC-V 模拟器的**"装填手"**——它把磁盘上的 ELF32 可执行文件解析、校验，然后"装填"到模拟器的物理内存中，并通过 MMU 建立虚拟地址映射，让 CPU 后续可以从虚拟地址取指、执行。

**在数据流中的位置：**

```
C 源码 → 汇编 → 机器码 → ELF 文件 → ★ Loader → 物理内存 + MMU映射 → CPU 执行
           ↑ 工具链负责（GCC/AS/LD）  ↑ 本模块负责这一段
```

编译器和链接器产出一个 ELF 文件后，Loader 是模拟器启动时第一个干活的模块。它搭起了"磁盘上的静态文件"和"CPU 运行时的虚拟地址空间"之间的桥梁。

### 在系统中的角色比喻

| 角色 | 对应 |
|------|------|
| **磁盘上的文件** | 快递包裹（ELF 文件 = Header + 程序段） |
| **Loader** | 拆包人——开包、验货、把东西摆到正确位置 |
| **PhysicalMemory** | 仓库货架（真实的存储空间，`pmem->data[]`） |
| **MMU** | 门牌号系统——把虚拟地址翻译成仓库里的实际位置 |
| **CPU** | 工人——只认门牌号（虚拟地址），不关心东西实际在哪 |

---

## 2. 做什么（功能与边界）

### 我负责的

| 功能 | 说明 |
|------|------|
| ELF Header 解析 | 读取并解析 52 字节 ELF Header，提取 `e_entry`、`e_phoff`、`e_phnum`、`e_phentsize` |
| ELF 合法性校验 | 五重检查：magic number → 32-bit → 小端序 → RISC-V 架构 → 可执行类型 |
| Program Header 遍历 | 跳过非 `PT_LOAD` 段，只处理需要加载到内存的段 |
| 物理内存分配 | 为每个 LOAD 段在 `PhysicalMemory` 中找到空闲空间（`mem_find_free`），建立区域映射（`mem_map`） |
| 段数据拷贝 | 从 ELF 文件分段读取数据，写入物理内存 `pmem->data[]` |
| .bss 段清零 | `p_memsz > p_filesz` 时，超出部分 `memset` 填 0 |
| 虚拟地址映射 | 调用 `mmu_map_page()` 为每个段建立虚拟地址 → 物理地址的页表映射 |
| 入口地址输出 | 通过输出参数 `*entry` 返回 `e_entry`，方便 main.c 设置 `cpu.pc` |
| 栈初始化 | 在物理内存顶端分配栈区域，通过输出参数 `*stack_top` 返回栈顶地址

### 不归我管的

| 事 | 归谁管 | 说明 |
|----|--------|------|
| 物理内存怎么分配/管理 | 焕聪（memory） | 我只调 `mem_find_free`、`mem_map`，内部维护 `MemoryRegion` 链表是 memory 模块的事 |
| 虚拟地址到物理地址的翻译 | 焕聪（memory/mmu） | 我只调 `mmu_map_page` 建立映射，Sv32 页表遍历、PTE 操作是 MMU 的事 |
| CPU 指令执行 | 李特（cpu） | 我只输出 `*entry`，谁来写 `cpu.pc`、怎么取指译码执行，我不管 |
| 系统调用处理 | 李特（CPU） | `exit`/`write` 等符号在 ELF 里只是字符串，运行时 `ecall` 触发由 `cpu_execute()` 中的 ecall case 处理 |
| 调试器 | 嘉俊（debugger） | 断点、单步、寄存器查看，我不参与 |

### 错误处理

| 错误场景 | 检测方式 | 行为 |
|----------|---------|------|
| 文件打不开 | `fopen` 返回 NULL | `perror` 打印系统错误，返回 `false` |
| 不是 ELF 文件 | `e_ident[0..3] != {0x7F,'E','L','F'}` | `fprintf` "Not a valid ELF file"，返回 `false` |
| 不是 32 位 | `e_ident[4] != 1` (ELFCLASS32) | `fprintf` "Only ELF32 is supported"，返回 `false` |
| 不是小端序 | `e_ident[5] != 1` (ELFDATA2LSB) | `fprintf` "Only little-endian is supported"，返回 `false` |
| 不是 RISC-V | `e_machine != EM_RISCV` (243) | `fprintf` "Not a RISC-V binary"，返回 `false` |
| 不是可执行文件 | `e_type != ET_EXEC` (2) | `fprintf` "Not an executable file"，返回 `false` |
| 段大小超物理内存 | `paddr + p_memsz > pmem->size` | `fprintf` "Segment exceeds physical memory"，返回 `false` |
| 文件读取失败 | `fread` 返回字节数不匹配 | `fprintf` "Failed to read segment data"，返回 `false` |

---

## 3. 怎么做

### 3.0 文件规划（重要：头文件与实现分离，模块内部解耦）

```
src/include/loader/elf_loader.h   ← 合约头文件（对齐并行方案 §0.8 + 文件总览）

src/src/loader/                   ← Loader 实现目录（对齐并行方案 §2C）
  ├── loader_internal.h           ← 内部头文件：各 .c 间共享的函数声明和常量
  ├── elf_validate.c              ← ELF Header 校验（纯逻辑，无副作用）
  ├── elf_segment.c               ← 段加载（权限转换、数据搬运、.bss 清零）
  ├── elf_load.c                  ← 主入口（编排调度其它文件）
  └── elf_stack.c                 ← 栈初始化（栈分配、auxv 布局）
```

**设计原则：**

| 原则 | 说明 |
|------|------|
| **头文件只放声明** | 结构体定义、常量 `#define`、函数签名。外部模块 `#include "elf_loader.h"` 即可调用。 |
| **实现文件不放声明** | `.c` 文件只有函数实现，不外泄类型或常量。 |
| **严禁 .h 和 .c 混在一起** | 头文件里不写实现代码，.c 文件里不放外部可见的类型定义。 |
| **每文件单一职责** | 校验归校验、搬运归搬运、编排归编排、栈归栈。改一个不碰其它。 |
| **内部通信用 `loader_internal.h`** | 模块内各 .c 之间的函数声明放内部头文件，不污染对外接口。 |

**各文件职责：**

| 文件 | 职责 | 依赖 |
|------|------|------|
| `loader_internal.h` | 模块内部共享的常量、函数声明 | `elf_loader.h`, `memory.h`, `mmu.h` |
| `elf_validate.c` | 五重校验：magic → 32-bit → 小端 → RISC-V → 可执行 | 只依赖 `elf_loader.h`（结构体+常量） |
| `elf_segment.c` | PF→MEM 权限转换、mem_map 映射、fread 搬运、memset 清零 | `memory.h`, `elf_loader.h` |
| `elf_load.c` | 编排流程：打开→读→校验→遍历段→设入口→设栈→打印 | 调用 validate、segment、stack |
| `elf_stack.c` | 栈区域分配、auxv 布局 | `memory.h`

### 3.1 对外接口

#### 核心函数

**声明位置：** [`include/elf_loader.h`](../../src/include/elf_loader.h)

```c
bool elf_load(const char *filename, PhysicalMemory *pmem, MMUState *mmu,
              uint32_t *entry, uint32_t *stack_top);
```

| 参数 | 方向 | 说明 |
|------|------|------|
| `filename` | 输入 | ELF 文件路径字符串 |
| `pmem` | 输入/输出 | 物理内存指针，段数据写入其 `data[]` 数组 |
| `mmu` | 输入/输出 | MMU 状态指针，用于建立虚拟地址页表映射 |
| `entry` | **输出** | 程序入口虚拟地址（`e_entry`），调用者写入 `cpu.pc` |
| `stack_top` | **输出** | 栈顶虚拟地址（`0x07F00000`），调用者写入 `cpu.regs[REG_SP]` |
| 返回值 | 输出 | `true` 加载成功，`false` 失败（已打印错误信息） |

**实现位置：** [`src/loader/elf_load.c`](../../src/src/loader/elf_load.c)

#### 模块内部函数

以下函数在 `src/loader/` 内部各文件间共享，声明在 [`loader_internal.h`](../../src/src/loader/loader_internal.h)，外部模块不可见：

| 函数 | 实现文件 | 说明 |
|------|----------|------|
| `elf_validate_header(Elf32_Ehdr *ehdr)` | `elf_validate.c` | 五重校验，返回 `bool` |
| `elf_load_segment(FILE*, Elf32_Phdr*, PhysicalMemory*, MMUState*)` | `elf_segment.c` | 段数据加载到物理内存 |
| `elf_setup_stack(PhysicalMemory*, uint32_t *stack_top)` | `elf_stack.c` | 栈区域分配 |

#### 数据结构

**定义位置：** [`include/elf_loader.h`](../../src/include/elf_loader.h)（已定义，不需要重新定义）

这些结构体只属于 loader 模块，放在 `elf_loader.h` 中。其他模块如果需要知道 `e_entry` 等信息，通过 `elf_load` 的输出参数获取，不需要直接引用这些结构体。

**ELF32 基础类型别名：**

```c
typedef uint32_t Elf32_Addr;    // 32 位地址
typedef uint32_t Elf32_Off;     // 32 位文件偏移
typedef uint16_t Elf32_Half;    // 16 位半字
typedef uint32_t Elf32_Word;    // 32 位字
typedef int32_t  Elf32_Sword;   // 32 位有符号字
```

**Elf32_Ehdr（52 字节 ELF Header），已在 elf_loader.h:58-72 定义：**

```
偏移   大小   字段          说明
0x00    16    e_ident[]     ★ Magic: [0]=0x7F,[1]='E',[2]='L',[3]='F'
                            [4]=1(32bit)/2(64bit), [5]=1(小端)/2(大端)
0x10    2     e_type        1=可重定位, 2=可执行(ET_EXEC), 3=共享库
0x12    2     e_machine     ★ 243=EM_RISCV
0x14    4     e_version     1
0x18    4     e_entry       ★ 程序入口虚拟地址
0x1C    4     e_phoff       ★ Program Header 表在文件中的偏移
0x20    4     e_shoff       Section Header 表偏移（加载时不用）
0x24    4     e_flags
0x28    2     e_ehsize      ELF Header 自身大小（52）
0x2A    2     e_phentsize   ★ 每个 Program Header 的大小（32）
0x2C    2     e_phnum       ★ Program Header 的数量
0x2E    2     e_shentsize   Section Header 大小（不用）
0x30    2     e_shnum       Section Header 数量（不用）
0x32    2     e_shstrndx    Section 名称字符串表索引（不用）
```

**Elf32_Phdr（32 字节 Program Header），已在 elf_loader.h:76-85 定义：**

```
偏移   大小   字段          说明
0x00    4     p_type        ★ 1=PT_LOAD（需要加载的段）
0x04    4     p_offset      ★ 段数据在文件中的偏移
0x08    4     p_vaddr       ★ 虚拟地址（加载到哪里）
0x0C    4     p_paddr       物理地址（用于建立 MMU 映射）
0x10    4     p_filesz      ★ 段在文件中的实际大小
0x14    4     p_memsz       ★ 段在内存中需要的大小（≥ filesz，多出的是 .bss）
0x18    4     p_flags       ★ 权限: PF_R=4, PF_W=2, PF_X=1，可组合
0x1C    4     p_align       对齐要求（通常是 0x1000 = 4KB）
```

#### 常量

**所有 ELF 相关常量定义在** [`include/elf_loader.h`](../../src/include/elf_loader.h)（已定义）：

```c
#define EI_NIDENT    16        // e_ident 数组长度
#define EM_RISCV     243       // RISC-V 机器标识
#define ET_EXEC      2         // 可执行文件类型
#define PT_LOAD      1         // 可加载段类型
#define PF_X         1         // 可执行
#define PF_W         2         // 可写
#define PF_R         4         // 可读
```

栈常量（在 `src/src/loader/loader_internal.h` 中定义）：
```c
// 简化版（恒等映射，128MB 物理内存内可用）
#define STACK_TOP_DEFAULT   0x07F00000      // 128MB 高位
// 完善版（MMU Sv32 后启用）
// #define STACK_TOP_DEFAULT 0xC0000000    // 团队结论 7，3GB 位置
#define STACK_SIZE_DEFAULT  (256 * 1024)     // 256KB (0x40000)
// 当前范围: 0x07EC0000 ~ 0x07F00000
// 完善版范围: 0xBFFC0000 ~ 0xC0000000
```

### 3.2 内部实现思路

#### 主流程（`elf_load` — 实现在 `src/loader/elf_load.c`，已部分完成）

```
elf_load(filename, pmem, mmu, entry, stack_top)
│
├─ 1. fopen(filename, "rb")
│     └─ 失败 → perror, return false
│
├─ 2. fread(&ehdr, sizeof(Elf32_Ehdr), 1, fp)
│     └─ 失败 → fprintf + fclose, return false
│
├─ 3. elf_validate_header(&ehdr)     ← ★ 已实现
│     ├─ e_ident[0..3] == {0x7F,'E','L','F'}
│     ├─ e_ident[4] == 1 (ELFCLASS32)
│     ├─ e_ident[5] == 1 (ELFDATA2LSB)
│     ├─ e_machine == EM_RISCV (243)
│     └─ e_type == ET_EXEC (2)
│     └─ 任一失败 → fclose, return false
│
├─ 4. 遍历 Program Headers           ← ★ 已实现框架（循环体已有）
│     for i = 0 .. ehdr.e_phnum:
│       offset = ehdr.e_phoff + i * sizeof(Elf32_Phdr)
│       fseek(fp, offset, SEEK_SET)
│       fread(&phdr, sizeof(Elf32_Phdr), 1, fp)
│
│       if phdr.p_type == PT_LOAD:
│         └─ elf_load_segment(fp, &phdr, pmem, mmu)   ← ★ 已实现框架，TODO 待完善
│
├─ 5. fclose(fp)
│
├─ 6. *entry = ehdr.e_entry          ← ★ 已实现
│
├─ 7. elf_setup_stack(pmem, stack_top) ← ★ 已实现（elf_stack.c）
│     ├─ mem_map(pmem, base, 256KB, RW, "stack")
│     │   其中 base = 0x07F00000 - 256KB = 0x07EC0000
│     └─ *stack_top = 0x07F00000
│
└─ 8. printf 加载信息, return true   ← ★ 已实现
```

#### elf_load_segment 详细流程（当前简化版，TODO 待完善）

当前简化实现：
```
elf_load_segment(fp, phdr, pmem, mmu)
│
│  // ============ 当前简化做法（恒等映射）============
│  paddr = phdr->p_vaddr    ← 虚拟地址直接当物理地址用
│
│  // 边界检查
│  if (paddr + phdr->p_memsz > pmem->size) → return false
│
│  // 建立物理内存映射
│  flags = 0
│  if (phdr->p_flags & PF_R) flags |= MEM_READ
│  if (phdr->p_flags & PF_W) flags |= MEM_WRITE
│  if (phdr->p_flags & PF_X) flags |= MEM_EXEC
│  mem_map(pmem, paddr, phdr->p_memsz, flags, "segment")
│
│  // 从文件拷贝数据（通过 memory 接口）
│  buf = malloc(phdr->p_filesz)
│  fseek(fp, phdr->p_offset, SEEK_SET)
│  fread(buf, 1, phdr->p_filesz, fp)
│  mem_load(pmem, paddr, buf, phdr->p_filesz)  ← 对齐焕聪 memory 接口
│  free(buf)
│
│  // .bss 清零
│  if (phdr->p_memsz > phdr->p_filesz):
│    zero_buf = calloc(1, bss_size)
│    mem_load(pmem, paddr + filesz, zero_buf, bss_size)
│    free(zero_buf)
│
│  // ============ TODO：完善版应该做的事 ============
│  // 1. 用 mem_find_free(pmem, p_memsz, p_align) 在物理内存中找空闲区域
│  //    paddr = mem_find_free(...)
│  // 2. 用 mmu_map_page(mmu, p_vaddr, paddr, flags) 建立虚拟→物理映射
│  //    for (offset = 0; offset < p_memsz; offset += PAGE_SIZE):
│  //      mmu_map_page(mmu, p_vaddr + offset, paddr + offset, flags)
│  // 3. 这样 CPU 就可以用 p_vaddr 取指，MMU 自动翻译到 paddr
│
└─ return true
```

#### 两个实现的对比

| 方面 | 当前简化版 | 完善版（TODO 目标） |
|------|-----------|-------------------|
| 物理地址分配 | `paddr = p_vaddr`（恒等映射） | `mem_find_free()` 动态分配 |
| MMU 映射 | **没做** | `mmu_map_page()` 逐页映射 |
| CPU 取指路径 | 直接读物理地址（绕开 MMU） | 走虚拟地址 → MMU 翻译 → 物理地址 |
| 优点 | 简单，快速跑通 | 真实模拟，支持多进程、页保护 |
| 缺点 | 地址必须不重叠，无页保护 | 需要 MMU 模块先实现好 |

> ⚠️ **重要限制**：简化版不调用 `mmu_map_page` 建立页表映射，因此 **只能在 Bare 模式（satp.MODE=0）下运行**。如果 MMU 启用了 Sv32 模式（satp.MODE=1），CPU 通过 `mmu_read_32` 取指时会因页表为空而失败（触发 `EXC_PAGE_FAULT_INST`）。在焕聪的 MMU 模块就绪前，确保 `mmu_init` 将 `satp` 设为 `SATP_MODE_OFF`（0），这样 `mmu_translate` 走 Bare 模式，`vaddr == paddr`，简化版即可正常工作。

**建议：先用简化版把流程跑通，等焕聪的 MMU 模块就绪后再切换到完善版。**

#### 关键设计决策

**为什么用 `mem_load()` 而不是直接写 `pmem->data[]`？**
对齐焕聪 memory 模块接口。`mem_load(pmem, addr, buf, size)` 是 PhysicalMemory 层提供的批量数据加载接口，Loader 只需把文件数据读到临时缓冲区再传入即可。这保证 Loader 不绕过 memory 模块直接操作内部数据结构（`data[]` 数组），接口更干净。

**为什么用 `malloc` 临时缓冲区 + `mem_load` 的组合？**
1. `malloc(phdr->p_filesz)` 分配临时缓冲区 → `fread` 读文件 → `mem_load(pmem, ...)` 写入物理内存
2. 不直接在栈上开大数组，避免段过大时栈溢出
3. 通过 `mem_load` 接口写入，不直接操作 `pmem->data[]`，保持模块边界清晰
4. 加载完成后 `free`，内存峰值 = 最大段大小（通常 < 1MB）

**为什么栈放在 `0x07F00000`？**
这是团队讨论结论（结论 7），和参考项目 2 一致。`0x07F00000` 接近 3GB 位置，更接近真实 Linux 用户空间布局。栈向下增长 256KB，范围 `0x07EC0000` ~ `0x07F00000`。

**为什么用输出参数而不是返回值？**
Loader 需要输出两个值（`entry` 和 `stack_top`），但返回值已被占用（表示成功/失败）。用指针输出是 C 的惯用做法，调用者（main.c）这样使用：
```c
uint32_t entry, stack_top;
if (!elf_load("hello.elf", &pmem, &mmu, &entry, &stack_top)) {
    return 1;
}
cpu.pc = entry;
cpu.regs[REG_SP] = stack_top;
```

### 3.3 模块交互时序

```
main.c
  │
  ├─ mem_init(&pmem, 128*1024*1024)     ← 初始化 128MB 物理内存
  ├─ mmu_init(&mmu)                     ← 初始化 MMU（页表为空）
  │
  ├─ elf_load("hello.elf", &pmem, &mmu, &entry, &stack_top)  ← ★ elf_load.c
  │    │
  │    ├─ fopen / fread(&ehdr) / fclose          ← C 标准库
  │    │
  │    ├─ elf_validate_header(&ehdr)             ← elf_validate.c
  │    │     ├─ magic: {0x7F,'E','L','F'}
  │    │     ├─ class: ELFCLASS32
  │    │     ├─ data:  ELFDATA2LSB
  │    │     ├─ machine: EM_RISCV
  │    │     └─ type: ET_EXEC
  │    │
  │    ├─ for each PT_LOAD segment:
  │    │    └─ elf_load_segment(fp, &phdr, pmem, mmu) ← elf_segment.c
  │    │         ├─ 权限转换: PF_* → MEM_*
  │    │         ├─ mem_map(pmem, paddr, size, flags, "segment")
  │    │         ├─ fread(pmem->data + paddr, 1, p_filesz, fp)
  │    │         ├─ memset(pmem->data + paddr + filesz, 0, bss)
  │    │         └─ [完善版] mmu_map_page(mmu, vaddr, paddr, flags)
  │    │
  │    ├─ *entry = ehdr.e_entry
  │    │
  │    └─ elf_setup_stack(pmem, stack_top)        ← elf_stack.c
  │         ├─ mem_map(pmem, base, 256KB, RW, "stack")
  │         └─ *stack_top = 0x07F00000
  │
  ├─ cpu.pc = entry                          ← CPU 模块拿到入口（李特）
  ├─ cpu.regs[REG_SP] = stack_top            ← CPU 模块拿到栈指针（李特）
  │
  └─ while (running) {                       ← CPU 主循环（李特）
       ├─ mmu_read_32(&mmu, &pmem, cpu.pc, ...)   ← 通过 MMU 取指令
       ├─ decode(instruction)
       └─ execute(...)
            ├─ 可能触发 ecall → cpu_execute() 内 syscall case 处理
            └─ 可能命中断点 → debugger 模块接管（嘉俊）
    }
```

### 依赖关系图

```
                              ┌──────────┐
                              │ types.h  │  ← CPUState, RegisterID 等
                              └────┬─────┘
                                   │
         ┌─────────────────────────┼─────────────────────────┐
         │                         │                         │
    ┌────┴────┐              ┌────┴────┐              ┌─────┴──────┐
    │memory.h │              │ mmu.h   │              │elf_loader.h│ ← 对外接口
    │mem_map  │              │mmu_map  │              │            │
    └────┬────┘              │_page()  │              └─────┬──────┘
         │                   └────┬────┘                    │
         │                        │          ┌─────────────┴──────────┐
         │                        │          │  loader_internal.h     │ ← 内部接口
         │                        │          └─────────────┬──────────┘
         │                        │                         │
         │              ┌─────────┴─────────┐  ┌────────────┼────────────┐
         │              │                   │  │            │            │
    ┌────┴──────┐  ┌────┴──────┐  ┌─────────┴──┴──┐  ┌─────┴──────┐ ┌──┴─────────┐
    │memory.c   │  │mmu.c     │  │ elf_validate.c │  │elf_segment │ │elf_stack.c │
    │           │  │          │  │                │  │    .c      │ │            │
    │ mem_map   │  │ mmu_map  │  │ 五重校验       │  │            │ │ 栈分配     │
    │           │  │ _page    │  │ 纯逻辑         │  │ mem_map    │ │ mem_map    │
    └───────────┘  └──────────┘  │                │  │ fread→data │ └────────────┘
                                 └───────┬────────┘  │ memset→bss │
                                         │           └──────┬──────┘
                                         │                  │
                                         │    ┌─────────────┴──────┐
                                         │    │   elf_load.c       │ ← ★ 编排层
                                         │    │                    │
                                         └────┤ elf_load():        │
                                              │  打开→验证→遍历段  │
                                              │  →设入口→设栈→打印 │
                                              └────────────────────┘
```

---

## 4. 当前代码状态

### 文件清单

```
src/include/elf_loader.h          ← 对外头文件：Elf32_Ehdr/Phdr、常量、elf_load() 声明

src/src/loader/
  ├── loader_internal.h           ← 内部头文件：各 .c 间共享的函数声明
  ├── elf_validate.c              ← ELF Header 校验（✓ 已实现）
  ├── elf_segment.c               ← 段加载（⚠ 简化版，恒等映射）
  ├── elf_load.c                  ← 主入口编排（✓ 已实现）
  └── elf_stack.c                 ← 栈初始化（✓ 已实现，TODO: auxv）
```

### 各文件完成状态

| 文件 | 状态 | 内容 |
|------|------|------|
| `include/elf_loader.h` | ✓ 已完成 | 类型定义（Elf32_Addr/Off/Half/Word/Sword）、结构体（Elf32_Ehdr/Phdr）、常量（EM_RISCV, ET_EXEC, PT_LOAD, PF_*）、函数声明（elf_load） |
| `loader_internal.h` | ✓ 已完成 | 内部常量（STACK_TOP_DEFAULT, STACK_SIZE_DEFAULT）、内部函数声明（validate_header, load_segment, setup_stack） |
| `elf_validate.c` | ✓ 已完成 | `elf_validate_header()` — 五重校验（magic → 32-bit → 小端 → RISC-V → 可执行） |
| `elf_segment.c` | ⚠ 简化版 | `elf_load_segment()` — 恒等映射（paddr = p_vaddr），TODO: `mem_find_free()` + `mmu_map_page()` |
| `elf_load.c` | ✓ 已完成 | `elf_load()` — 完整的编排流程 |
| `elf_stack.c` | ⚠ 简化版 | `elf_setup_stack()` — 只分配空间，TODO: 在栈上放置 auxv 辅助向量 |

---

## 5. 与队友的对齐清单

| 序号 | 待确认事项 | 需要对齐的人 | 当前状态 |
|------|-----------|-------------|----------|
| 1 | `MEM_READ/WRITE/EXEC` 常量已在 memory.h 定义（值 1/2/4），确认不变 | 焕聪 | 已定义 |
| 2 | `PF_R/W/X` 常量已在 elf_loader.h 定义（值 4/2/1），与 memory.h 的值不同（这是正确的，ELF 规范规定） | 我 | 已定义 |
| 3 | `mem_map(pmem, base, size, flags, name)` 签名确认 | 焕聪 | memory.h:72-73 |
| 4 | `mem_find_free(pmem, size, align)` 签名确认——完善版需要用到 | 焕聪 | memory.h:76 |
| 5 | `mmu_map_page(mmu, vaddr, paddr, flags)` 签名确认——完善版需要用到 | 焕聪 | mmu.h:78-79 |
| 6 | `cpu.pc` 和 `cpu.regs[REG_SP]` 字段确认存在 | 李特 | types.h:117-118 |
| 7 | `REG_SP` = 2 已在 types.h 枚举中定义 | 李特 | types.h:35 |
| 8 | `PhysicalMemory` 的 `size` 字段默认 128MB | 焕聪 | memory.h:28 |
| 9 | 栈基址 `0x07F00000`，大小 256KB，范围 `0x07EC0000` ~ `0x07F00000`（团队结论 7） | 全体讨论 | ✅ 已确定 |
| 10 | `mmu_map_page` 标志位使用 PTE_*（`PTE_VALID=1, PTE_READ=2, PTE_WRITE=4, PTE_EXEC=8, PTE_USER=16`），与 MEM_* 值不同，需通过 `mem_perm_to_pte_flags()` 转换（见公共类型定义） | 焕聪 | ✅ 已确定 |
| 11 | `elf_load` 输出了 `entry` 和 `stack_top`，main.c 里谁来写 `cpu.pc` / `cpu.regs[REG_SP]`？ | 李特 | ✅ 已确定：由 main.c 或 `sim_load_elf` 负责写入（见讨论清单结论 7） |

---

## 6. 测试计划

> 对齐 [并行开发方案 §2C](../共同部分/并行开发与验收方案.md#2c-嘉华loader-模块elf-解析与加载)，
> 使用 Stub PhysicalMemory + Stub MMU 独立测试，不依赖其他队友的模块。

### 测试目录

```
test/loader/
├── gen_minimal_elf.c     ← ELF 生成工具（在 x86 宿主机上运行，产出 minimal.elf）
├── test_validate.c       ← L1：校验测试
├── test_load.c           ← L2：加载测试
└── test_stack.c          ← L3：栈测试
```

### 测试 ELF 文件的生成（方式 A：手写最小 ELF）

不需要 RISC-V 交叉编译器。写一个 C 程序在宿主机上生成最小合法 ELF32 文件：

```
test/loader/gen_minimal_elf.c
  → 编译：gcc gen_minimal_elf.c -o gen_minimal_elf
  → 运行：./gen_minimal_elf
  → 产出：minimal.elf（内容：addi a0, zero, 42; ecall）
```

> 等交叉编译环境（`riscv64-unknown-elf-gcc`）搭好后，再用方式 B 生成复杂测试用例。

### L1：校验测试（test_validate.c）

**依赖**：无（纯单元测试，不需要 Simulator）

| 测试用例 | 输入 | 预期结果 |
|----------|------|---------|
| 合法 ELF32 RISC-V 可执行文件 | `minimal.elf` | `elf_validate_header()` 返回 `true` |
| 不是 ELF 文件（magic 错） | 伪造的 `e_ident` | 返回 `false`，"Not a valid ELF file" |
| 64 位 ELF | `e_ident[4] = 2` | 返回 `false`，"Only ELF32 is supported" |
| 大端序 ELF | `e_ident[5] = 2` | 返回 `false`，"Only little-endian is supported" |
| 不是 RISC-V | `e_machine = 62` (x86) | 返回 `false`，"Not a RISC-V binary" |
| 不是可执行文件 | `e_type = 1` (.o 文件) | 返回 `false`，"Not an executable file" |

### L2：加载测试（test_load.c）

**依赖**：Stub PhysicalMemory + Stub MMU（Bare 模式，vaddr = paddr）

| 测试用例 | 说明 | 验收方式 |
|----------|------|---------|
| 加载 minimal.elf | `elf_load()` 返回 `true` | `entry` 非零，`stack_top = 0x07F00000` |
| 段数据在内存正确位置 | `mmu_read_32(pmem, entry, &Instr)` | 读到的指令 = ELF 第一条指令的编码 |
| .bss 清零验证 | 写一个有 .bss 段的 ELF | .bss 区域全部为 0 |
| 栈区域可读写 | 向 `stack_top - 4` 写 `0xCAFEBABE` 再读回 | 读回值 = `0xCAFEBABE` |
| 文件不存在 | `elf_load("nonexistent", ...)` | 返回 `false` |
| 段超物理内存 | 段地址 + 大小 > 128MB | 返回 `false` |

### L3：栈测试（test_stack.c）

**依赖**：Stub PhysicalMemory + Stub MMU

| 测试用例 | 验收项 |
|----------|--------|
| `stack_top` 值 | `= 0x07F00000` |
| 栈区域大小 | `0x07F00000 - 0x07EC0000 = 256KB` |
| 栈区域映射 | `mem_map` 已注册名为 "stack" 的区域 |
| 栈区域权限 | `MEM_READ | MEM_WRITE`（不可执行） |

### 集成测试（阶段 3，等 CPU 就绪后）

| 步骤 | 说明 | 验收 |
|------|------|------|
| 李特用 `sim_load_elf` 替换 `test_load_Instrs` | ELF → Loader → MMU → CPU → exit | `./rvsim minimal.elf` 返回 42 |
| 端到端 hello 程序 | 用交叉编译器产出的 hello.elf | 程序正常 exit，输出 "Hello, RISC-V!" |

### 调试辅助

Loader 加载成功后应打印：
```
ELF loaded: minimal.elf
  Entry:  0x000100b0
  Stack:  0xc0000000 (size: 256 KB)
```
（具体 entry 值取决于 ELF 的链接脚本）
