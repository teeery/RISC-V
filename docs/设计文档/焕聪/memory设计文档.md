# memory 设计文档

> 作者：焕聪 | 最后更新：2026-07-01
> 
> **架构决策已确定**：采用两层架构（PhysicalMemory + MMUState）。
> 公共类型定义见 `docs/设计文档/共同部分/公共类型定义.md`，讨论结论见 `讨论清单.md`。
> 
> 接口签名已全员对齐：返回值 `bool`，错误通过 `ExceptionType*` 输出参数传递，
> CPU 只调 MMU 层（不直接调 `mem_*`），Loader 调两层。

## 1. 是什么（模块定位）

memory 子系统是 RISC-V 模拟器的**存储基石**——它在宿主机的堆内存中模拟出一块 "假内存"，让被模拟的 RISC-V 程序以为自己运行在真实的物理内存之上。

在完整数据流中，memory 的位置：

```
ELF 文件（磁盘）
    │
    ▼ ELF 加载器 把段数据拷进虚拟内存
┌──────────────────────┐
│   memory 子系统       │  ← 本文档的范围
│  ┌────────────────┐  │
│  │  MMU (虚拟层)   │  │  ← 虚拟地址 → 物理地址转换 (Sv32 页表)
│  │  mmu_translate  │  │
│  └───────┬────────┘  │
│          ▼            │
│  ┌────────────────┐  │
│  │  PhysicalMemory │  │  ← 物理内存字节数组 + 区域管理
│  │  mem_read/write │  │
│  └────────────────┘  │
└──────────────────────┘
    ▲         │
    │         ▼
  CPU      syscall 处理
 (取指/访存)  (read/write 等)
```

**核心职责**：把 RISC-V 程序的访存请求（虚拟地址）最终映射到宿主机 `malloc` 出来的字节数组（物理内存）上，并在这个过程中完成权限检查和边界检查。

设计上分为两层：
- **MMU 层**：负责 Sv32 虚拟地址翻译、页表管理、PTE 权限校验
- **PhysicalMemory 层**：负责物理内存的字节级读写、区域映射记录、堆管理

---

## 2. 做什么（功能与边界）

### 2.1 功能清单

#### PhysicalMemory（物理内存层）

| 功能 | 说明 |
|------|------|
| 物理内存分配/释放 | 用 `calloc` 申请一块连续字节数组（默认 128MB），模拟线性物理地址空间 |
| 字节级读写 | `mem_read/write_8/16/32` — 支持 1/2/4 字节的小端序读写 |
| 区域映射管理 | `mem_map` 注册已映射区间（基址+大小+权限+名称），动态扩容 regions 列表 |
| 空闲区域查找 | `mem_find_free` 遍历 regions 找到未占用的地址空间，用于新映射分配 |
| 堆管理 (brk) | `mem_brk` 支持 brk 系统调用，扩展/收缩堆边界 |
| 批量数据加载 | `mem_load` 将外部数据 `memcpy` 到物理内存指定位置（ELF 加载器用） |
| 调试 dump | `mem_dump` 以标准 hexdump 格式打印内存内容（地址 + hex + ASCII） |
| 边界检查 | 所有读写操作检查 `addr + size <= pmem->size`，越界返回 false |

#### MMU（虚拟内存层）

| 功能 | 说明 |
|------|------|
| 地址翻译 | `mmu_translate` — Sv32 两级页表遍历，虚拟地址 → 物理地址 |
| 虚拟地址读写 | `mmu_read/write_8/16/32` — 先翻译再访问物理内存 |
| 页表映射 | `mmu_map_page` — 建立虚拟页到物理页的映射，按需分配二级页表 |
| 权限校验 | 检查 PTE 的 V/R/W/X/U 标志位，不通过时抛出对应异常 |
| Bare 模式 | satp.MODE=0 时直接使用物理地址（恒等映射），等价于无 MMU |

### 2.2 边界——什么归我管，什么不归我管

| 归 memory 管 | 不归 memory 管 |
|-------------|---------------|
| ✅ 物理内存的分配和字节读写 | ❌ ELF 文件的解析和读取（那是 ELF 加载器的事） |
| ✅ 虚拟地址 → 物理地址的 Sv32 翻译 | ❌ 指令取指逻辑（CPU 通过 mmu_read_32 来取指） |
| ✅ 页表数据结构的管理和遍历 | ❌ satp CSR 的读写（那是 CSR 模块的事，MMU 只读 satp 值） |
| ✅ 读写时的权限检查（PTE 级别） | ❌ 系统调用的语义处理（syscall 模块调我读写，我不管业务逻辑） |
| ✅ 物理内存边界检查 | ❌ 栈的自动增长（操作系统做的事，模拟器不自动扩栈） |
| ✅ 小端序字节序处理 | ❌ 端序转换（RISC-V 本身就是小端序，与 x86 一致） |
| ✅ 区域映射的记录和查询 | ❌ 决定内存布局（布局由 ELF 加载器和初始化代码决定） |

### 2.3 典型内存布局

```
RISC-V 程序眼中的虚拟地址空间（32位）：
═══════════════════════════════════════════════
0x00000000 ──────────── NULL 区域（访问触发段错误）
0x00010000 ┌────────── 代码段 (.text)   R+X  ← Region[0]
           │
0x00012000 ├────────── 数据段 (.data/.bss) R+W ← Region[1]
           │
           ├────────── 堆 (brk，向上增长)
           │     ↓↓↓
           │  （空闲空间）
           │     ↑↑↑
           │
0xBFFC0000 ├────────── 栈（向下增长）  R+W    ← Region[2]
0xC0000000 └────────── 栈顶 (初始 sp)
═══════════════════════════════════════════════
```

---

## 3. 怎么做

### 3.1 对外接口

#### 3.1.1 数据结构

```c
// ==================== PhysicalMemory 层 ====================

#define MEM_SIZE_DEFAULT    (128 * 1024 * 1024)  // 128 MB
#define MEM_READ   (1 << 0)
#define MEM_WRITE  (1 << 1)
#define MEM_EXEC   (1 << 2)

/* 内存区域描述符 */
typedef struct {
    uint32_t base;      // 起始物理地址
    uint32_t size;      // 区域大小（字节）
    uint8_t  flags;     // 权限标志 (MEM_READ | MEM_WRITE | MEM_EXEC)
    char     name[32];  // 区域名称 ("code", "data", "stack", "heap")
} MemoryRegion;

/* 物理内存管理器 */
typedef struct {
    uint8_t  *data;         // 物理内存字节数组 (calloc 分配)
    uint32_t  size;         // 总大小
    uint32_t  brk_start;    // brk 起始地址
    uint32_t  brk_current;  // 当前 brk 边界

    MemoryRegion *regions;  // 动态数组
    int          region_count;
    int          region_capacity;
} PhysicalMemory;

// ==================== MMU 层 ====================

#define PAGE_SIZE        4096
#define PAGE_SHIFT       12
#define PTE_VALID        (1 << 0)
#define PTE_READ         (1 << 1)
#define PTE_WRITE        (1 << 2)
#define PTE_EXEC         (1 << 3)
#define PTE_USER         (1 << 4)
#define PTE_GLOBAL       (1 << 5)
#define PTE_ACCESSED     (1 << 6)
#define PTE_DIRTY        (1 << 7)
#define SATP_MODE_OFF    0
#define SATP_MODE_SV32   1

/* MMU 状态 */
typedef struct {
    uint32_t satp;              // satp CSR 值
    uint32_t *root_page_table;  // 第一级页表 (1024 项 × 4B)
    bool     enabled;           // 是否启用地址翻译
} MMUState;
```

#### 3.1.2 函数接口

```c
// ==================== PhysicalMemory 层 (13 个) ====================

void     mem_init(PhysicalMemory *pmem, uint32_t size);
void     mem_destroy(PhysicalMemory *pmem);

bool     mem_read_8 (PhysicalMemory *pmem, uint32_t addr, uint8_t  *val);
bool     mem_read_16(PhysicalMemory *pmem, uint32_t addr, uint16_t *val);
bool     mem_read_32(PhysicalMemory *pmem, uint32_t addr, uint32_t *val);

bool     mem_write_8 (PhysicalMemory *pmem, uint32_t addr, uint8_t  val);
bool     mem_write_16(PhysicalMemory *pmem, uint32_t addr, uint16_t val);
bool     mem_write_32(PhysicalMemory *pmem, uint32_t addr, uint32_t val);

bool     mem_map(PhysicalMemory *pmem, uint32_t base, uint32_t size,
                 uint8_t flags, const char *name);
uint32_t mem_find_free(PhysicalMemory *pmem, uint32_t size, uint32_t align);
uint32_t mem_brk(PhysicalMemory *pmem, uint32_t new_brk);
bool     mem_load(PhysicalMemory *pmem, uint32_t addr, const uint8_t *data,
                  uint32_t size);
void     mem_dump(PhysicalMemory *pmem, uint32_t addr, uint32_t len);

// ==================== MMU 层 (11 个) ====================

void     mmu_init(MMUState *mmu);
bool     mmu_translate(MMUState *mmu, uint32_t vaddr, uint32_t *paddr,
                       bool is_write, bool is_exec, PrivilegeLevel priv,
                       ExceptionType *exc);

bool     mmu_read_8 (MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                     uint8_t *val, PrivilegeLevel priv, ExceptionType *exc);
bool     mmu_read_16(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                     uint16_t *val, PrivilegeLevel priv, ExceptionType *exc);
bool     mmu_read_32(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                     uint32_t *val, PrivilegeLevel priv, ExceptionType *exc);
bool     mmu_write_8 (MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                      uint8_t val, PrivilegeLevel priv, ExceptionType *exc);
bool     mmu_write_16(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                      uint16_t val, PrivilegeLevel priv, ExceptionType *exc);
bool     mmu_write_32(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                      uint32_t val, PrivilegeLevel priv, ExceptionType *exc);

bool     mmu_read (MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                   uint8_t *buf, uint32_t len, PrivilegeLevel priv, ExceptionType *exc);
bool     mmu_write(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                   const uint8_t *buf, uint32_t len, PrivilegeLevel priv, ExceptionType *exc);

bool     mmu_map_page(MMUState *mmu, uint32_t vaddr, uint32_t paddr,
                      uint8_t flags);
```

> **注意**：批量读写 `mmu_read` / `mmu_write` 是会议决定新增的接口，用于 syscall `write`/`read` 场景——从虚拟地址连续读取/写入 `len` 字节。内部实现逐字节调用 `mmu_read_8` / `mmu_write_8`，或优化为按页批量 `memcpy`。

### 3.2 内部实现思路

#### 3.2.1 mem_init — 初始化物理内存

```
1. calloc(size, 1) 分配 data 字节数组
2. 初始化 regions 动态数组（初始 capacity=16，count=0）
3. brk_start = brk_current = 0
```

#### 3.2.2 mem_read/write — 小端序读写

按 RISC-V 小端序规范：**低地址存低字节**。

```
写 32 位值 0xAABBCCDD 到地址 addr:
  data[addr + 0] = 0xDD  (最低字节)
  data[addr + 1] = 0xCC
  data[addr + 2] = 0xBB
  data[addr + 3] = 0xAA  (最高字节)

读同理，按位或拼接:
  val = data[addr] | (data[addr+1] << 8) | (data[addr+2] << 16) | (data[addr+3] << 24)
```

边界检查：`addr + size > pmem->size` 时返回 false。物理内存层不做权限检查（那是 MMU 的职责）。

#### 3.2.3 mem_map — 区域映射注册

```
1. 如果 region_count >= region_capacity → realloc 扩容 (capacity *= 2)
2. 在 regions 末尾添加新条目：base, size, flags, name
3. region_count++
4. 注意：不检查区域重叠（由调用者保证），简化实现
```

#### 3.2.4 mem_find_free — 空闲地址查找

```
简单策略：找到所有已映射区域的末尾最大值，对齐后返回

1. max_end = 0
2. for each region: end = region.base + region.size; max_end = max(max_end, end)
3. 按 align 对齐: max_end = (max_end + align - 1) & ~(align - 1)
4. 返回 max_end
```

#### 3.2.5 mem_brk — 堆管理

```
1. 首次调用时，如果 brk_start == 0，初始化为 data 段末尾
2. new_brk == 0 → 返回当前 brk_current
3. new_brk >= brk_start 且在物理内存范围内 → 更新 brk_current
4. 返回 brk_current
```

#### 3.2.6 mmu_translate — Sv32 地址翻译（核心）

这是整个 memory 子系统最关键的逻辑，完整流程如下：

```
Sv32 虚拟地址结构:
  31        22 21        12 11         0
 ┌────────────┬────────────┬────────────┐
 │  VPN[1]    │  VPN[0]    │  offset    │
 │  10 bits   │  10 bits   │  12 bits   │
 └────────────┴────────────┴────────────┘

翻译流程:

  ┌─────────────────┐
  │ satp.MODE == 0? │──是──→ paddr = vaddr (Bare 模式，恒等映射)
  └────────┬────────┘
           │ 否 (MODE == 1, Sv32)
           ▼
  ┌─────────────────────────────────────┐
  │ 1. 提取 VPN[1] = vaddr[31:22]       │
  │ 2. 读第一级页表: PDE = root_table[VPN[1]] │
  │ 3. 检查 PDE.V 有效吗?               │
  │    ├─ V=0 → 页错误                  │
  │    └─ V=1 → 继续                    │
  │ 4. 检查 PDE 是否为叶子节点           │
  │    ├─ R|W|X != 0 → 可能是 4MB 大页   │
  │    └─ R=W=X=0 → 非叶子，指向二级页表  │
  │ 5. 提取 VPN[0] = vaddr[21:12]       │
  │ 6. 读第二级页表: PTE = l2_table[VPN[0]] │
  │ 7. 检查 PTE 权限:                    │
  │    ├─ V=0 → 页错误                  │
  │    ├─ 写操作 且 W=0 → 页错误         │
  │    ├─ 执行 且 X=0 → 页错误           │
  │    └─ U模式 且 U=0 → 页错误          │
  │ 8. 计算物理地址:                     │
  │    paddr = PTE.PPN × 4096 + offset  │
  │ 9. 更新 A/D 位 (Accessed / Dirty)    │
  └─────────────────────────────────────┘
```

PTE 格式（32 位）：

```
  31         20 19        10 9 8 7 6 5 4 3 2 1 0
 ┌─────────────┬────────────┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┐
 │ PPN[1]      │ PPN[0]     │ - │D│A│G│U│X│W│R│V│
 └─────────────┴────────────┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┘
   PPN = {PPN[1], PPN[0]} → 物理页号，共 22 位（最大支持 4GB 物理内存）
```

#### 3.2.7 mmu_read/write — 虚拟地址读写

```
1. 调用 mmu_translate(vaddr, ..., &paddr, &exc)
2. 如果翻译失败 (返回 false):
   → *exc 携带具体异常类型（EXC_LOAD_ACCESS_FAULT / EXC_STORE_ACCESS_FAULT 等）
   → 函数返回 false，调用者（CPU）根据 *exc 写 mcause/mtval 并跳转 mtvec
3. 如果翻译成功:
   → 调用 mem_read/write_*(pmem, paddr, ...) 完成物理内存访问
   → *exc = EXC_NONE（无异常）
   → 返回 mem_* 的结果
```

> **ExceptionType *exc 的语义**：`exc` 是输出参数，调用者传入 `&exc` 的地址。成功时写 `EXC_NONE`，失败时写具体异常码。PhysicalMemory 层的 `mem_*` 函数不设 exc（只返回 bool），异常类型由 MMU 层统一填充。可用的异常码参见 `types.h`（`EXC_LOAD_ACCESS_FAULT`、`EXC_STORE_ACCESS_FAULT`、`EXC_LOAD_ADDR_MISALIGNED` 等）。

#### 3.2.7b mmu_read / mmu_write（批量）— 批量虚拟地址读写

```
mmu_read(mmu, pmem, vaddr, buf, len, priv, exc):
  for i = 0 .. len-1:
    if (!mmu_read_8(mmu, pmem, vaddr + i, &buf[i], priv, exc))
      return false;   // *exc 携带第一个失败字节的异常类型
  *exc = EXC_NONE;
  return true;

mmu_write(mmu, pmem, vaddr, buf, len, priv, exc):
  同上，逐字节调用 mmu_write_8
```

> **使用场景**：`sys_write(fd, buf, len)` 需要从用户虚拟地址 `buf` 连续读 `len` 字节 → 调用 `mmu_read`；`sys_read` 同理调用 `mmu_write`。简单实现逐字节循环；后续可优化为检测连续物理页后直接 `memcpy`。

#### 3.2.8 mmu_map_page — 页表映射

```
1. 提取 VPN[1] = vaddr[31:22], VPN[0] = vaddr[21:12]
2. 读 PDE = root_table[VPN[1]]
3. 如果 PDE.V == 0 → 分配第二级页表 (calloc 1024×4B)，设置 PDE 指向它
4. 计算 PTE = (paddr >> 12) << 10 | flags（PPN + 权限位）
5. 写入 l2_table[VPN[0]] = PTE
```

#### 3.2.9 mem_dump — 调试输出

```
标准 hexdump 格式:

  00010000: 97 01 00 00 93 81 01 80  13 05 00 00 67 80 00 00  ............
  00010010: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  ................

  格式: <8位地址>: <16个hex字节，中间第8个后多一个空格> <ASCII区>
  ASCII 区：可打印字符直接显示，不可打印字符显示为 '.'
```

### 3.3 模块交互时序

#### 3.3.1 模拟器启动时的内存初始化

```
main()
  │
  ├─ mem_init(&pmem, 128MB)         ← 分配物理内存空间
  │
  ├─ mmu_init(&mmu)                 ← 初始化 MMU (satp=0, 分配根页表)
  │
  ├─ elf_load(elf_file, &pmem, &mmu)
  │   │
  │   ├─ 解析 ELF Header → 获取入口地址
  │   ├─ 遍历 Program Headers (PT_LOAD):
  │   │   ├─ mem_map(&pmem, vaddr, memsz, flags, name)  ← 注册区域
  │   │   └─ mem_load(&pmem, vaddr, data, filesz)       ← 拷贝段数据
  │   └─ mem_map(&pmem, 0xBFFC0000, 0x40000, MEM_READ | MEM_WRITE, "stack") ← 设置栈区域（256KB）
  │
  ├─ cpu.pc = elf_entry
  └─ cpu.regs[sp] = 栈顶
```

#### 3.3.2 CPU 取指 / 访存流程

```
CPU 执行循环
  │
  ├─ Fetch: mmu_read_32(&mmu, &pmem, pc, &insn, priv)
  │           │
  │           ├─ mmu_translate(pc, &paddr, is_exec=true)
  │           │   ├─ Bare 模式 → paddr = pc
  │           │   └─ Sv32 模式 → 两级页表遍历
  │           │        └─ 权限检查: X 位必须为 1
  │           │
  │           └─ mem_read_32(&pmem, paddr, &insn)
  │               └─ 边界检查 → data[paddr:paddr+3] 小端序拼接
  │
  ├─ Decode → Execute
  │
  ├─ Memory (如果是 load/store 指令):
  │   └─ mmu_read/write_*(&mmu, &pmem, addr, &val, priv)
  │        └─ 同上流程，权限检查 R/W 位
  │
  └─ Writeback → 更新 PC → 循环
```

#### 3.3.3 系统调用中的访存

```
syscall_handler()
  │
  ├─ sys_write(fd, buf_vaddr, len):
  │   └─ for i in 0..len:
  │       └─ mmu_read_8(&mmu, &pmem, buf_vaddr + i, &byte, priv)
  │           └─ 从虚拟地址逐字节读出 → 宿主 fwrite 输出
  │
  ├─ sys_read(fd, buf_vaddr, len):
  │   └─ 宿主 read 数据 → 逐字节 mmu_write_8 写入虚拟地址
  │
  └─ sys_brk(new_brk):
      └─ mem_brk(&pmem, new_brk)
```

#### 3.3.4 调试器查看内存

```
debugger REPL
  │
  ├─ "x /16x 0x10074" 命令:
  │   └─ mmu_read_32 × 16 次
  │       └─ mmu_translate(0x10074+i*4, &paddr) → mem_read_32
  │   └─ 格式化输出
  │
  └─ "dump 0x10000 256" 命令:
      └─ mem_dump(&pmem, 0x10000, 256)
```

### 3.4 关键设计决策

| 决策 | 选择 | 理由 |
|------|------|------|
| 物理内存 vs 虚拟内存分层 | **分层**（参考项目结构1） | 贴近真实硬件，物理层和 MMU 职责清晰；方便后续在 MMU 层加入 TLB 缓存等扩展 |
| 页表存储方式 | **独立 malloc**（非复用物理内存） | 简化实现，避免页表自身占用物理地址空间导致的管理复杂度 |
| 区域重叠检查 | **不做检查** | 由 ELF 加载器保证不重叠；减少运行时开销 |
| 跨页访存 | **4/2 字节访问不跨页检查**（简化） | 对齐的 4 字节访问天然不跨页；未对齐访问在 RISC-V 中是合法但可选的，简化实现假设对齐 |
| 大页支持 | **第一版不支持** | 4MB 大页在第一级 PDE 设置 R/W/X 即可，但标准测试程序不使用大页 |
| 线程安全 | **不考虑** | 单核模拟器，所有访问串行化 |
