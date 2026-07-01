# CPU 分阶段实现方案

> 作者：李特 | 最后更新：2026-06-30

---

## 核心原则

**每个阶段都可以独立于队友的模块进行开发和测试。** 需要依赖 Memory 模块的阶段，用一个极简的 Memory Stub（~20 行代码）替代。等焕聪的 Memory 就绪后，只需替换函数调用，CPU 核心逻辑一行不改。

---

## 阶段总览

```
阶段0 (骨架)
  └─► 阶段1 (解码) ─────────────────────────────┐
       └─► 阶段2 (寄存器运算) ───────────────────┤
             └─► 阶段3 (控制流) ────────────────┤  全部可以用
                   └─► 阶段4 (访存) ────────────┤  Memory Stub
                         └─► 阶段5 (系统指令) ──┤  独立完成
                               └─► 阶段6 (M扩展)┤
                                     └─► 阶段7 (反汇编)
                                           └─► 阶段8 (流水线，选做)
```

| 阶段 | 内容 | 预计时间 | 对外依赖 |
|------|------|---------|---------|
| 0 | CPU 骨架 | 0.5h | 无 |
| 1 | 指令解码 | 3–4h | 无 |
| 2 | 寄存器间运算指令 | 3–4h | Memory Stub（自写 ~15 行） |
| 3 | 控制流指令 | 3–4h | Memory Stub |
| 4 | 访存指令 | 2–3h | Memory Stub（扩展支持读写 ~25 行） |
| 5 | 系统指令 | 1–2h | Memory Stub |
| 6 | M 扩展（乘除法） | 2–3h | 无（纯 ALU） |
| 7 | 反汇编器 | 2–3h | 无 |
| 8 | 五级流水线（选做） | 6–8h | 无（内部重构） |

---

## 阶段 0：CPU 骨架

### 做什么

定义 CPU 状态结构体，实现 `cpu_init` 和 `cpu_reset`。

### 涉及文件

- `src/include/cpu/cpu.h` — CPU 结构体定义
- `src/src/cpu/cpu.c` — `cpu_init` / `cpu_reset` 实现

### 具体任务

1. 定义 `CPU` 结构体：
   ```c
   typedef struct {
       uint32_t regs[32];       // 通用寄存器 x0-x31
       uint32_t pc;             // 程序计数器
       bool     running;        // 运行标志
       PrivilegeLevel priv;     // 当前特权级（始终为 PRIV_MACHINE）
       uint32_t mstatus;        // 机器状态（MIE/MPIE/MPP）
       uint32_t mtvec;          // 陷阱向量基址
       uint32_t mepc;           // 异常返回地址
       uint32_t mcause;         // 异常原因
       uint32_t mtval;          // 异常相关信息
   } CPU;
   ```
2. 实现 `cpu_init(CPU *cpu)`：regs 清零，pc=0，running=false，priv=PRIV_MACHINE，CSR 清零
3. 实现 `cpu_reset(CPU *cpu)`：与 init 等价（预留后续扩展空间）

### 为什么放第一

结构体字段一旦确定，后续所有阶段就能直接引用 `regs[]` 和 `pc`。这是整个模块的地基。

### 验收标准

- 编译通过，无 warning（`-Wall -Wextra`）
- `cpu_init` 后可通过如下断言：

| 检查项 | 预期值 |
|--------|--------|
| `regs[0]` ~ `regs[31]` | 全部为 0 |
| `pc` | 0 |
| `running` | false |
| `priv` | `PRIV_MACHINE` (3) |
| `mstatus` / `mtvec` / `mepc` / `mcause` / `mtval` | 全部为 0 |

- `cpu_reset` 后的状态与 `cpu_init` 完全一致

### 独立性

✅ 零依赖。不依赖任何队友模块。

---

## 阶段 1：指令解码

### 做什么

实现 6 个位域提取宏、6 种立即数拼接宏、`cpu_decode()` 函数和 `DecodedInsn` 结构体。

### 涉及文件

- `src/include/cpu/decode.h` — 宏定义 + 结构体声明
- `src/src/cpu/decode.c` — `cpu_decode` 实现

### 具体任务

1. **6 个位域提取宏：** `OPCODE`, `RD`, `FUNCT3`, `RS1`, `RS2`, `FUNCT7`
2. **6 种立即数拼接宏：** `IMM_I`, `IMM_S`, `IMM_B`, `IMM_U`, `IMM_J`
3. **`DecodedInsn` 结构体：** opcode / rd / rs1 / rs2 / funct3 / funct7 / imm
4. **`cpu_decode(uint32_t insn)`：** 按 opcode 分发到 6 种格式，提取各字段

### 为什么放第二

解码是纯函数（输入 `uint32_t`，输出结构体），不涉及任何状态变化，最容易独立测试。解码阶段出错，后续所有阶段都会出错——先把它做对，后面才能放心。

### 验收标准

逐条手写测试用例，覆盖全部 6 种指令格式（R / I / S / B / U / J）：

| # | 测试指令 | 输入 hex | 预期输出 |
|---|---------|----------|---------|
| 1 | `add x10, x5, x6` | `0x00B302B3` | opcode=`0x33`, rd=10, rs1=5, rs2=6, funct3=0, funct7=0 |
| 2 | `addi x10, x0, 3` | `0x00300513` | opcode=`0x13`, rd=10, rs1=0, imm=3 |
| 3 | `lw x10, 4(x2)` | `0x00412503` | opcode=`0x03`, rd=10, rs1=2, imm=4 |
| 4 | `sw x10, 8(x2)` | `0x00A12423` | opcode=`0x23`, rs1=2, rs2=10, imm=8（S-type 拼接验证） |
| 5 | `beq x5, x6, +256` | `0x10628863` | opcode=`0x63`, rs1=5, rs2=6, imm=256（B-type 拼接验证） |
| 6 | `lui x10, 0x12345` | `0x12345537` | opcode=`0x37`, rd=10, imm=`0x12345000`（U-type） |
| 7 | `jal x1, 0x1000` | 手算编码 | opcode=`0x6F`, rd=1, imm=0x1000（J-type 拼接验证） |
| 8 | `addi x5, x0, -1` | `0xFFF00293` | imm=`-1`（符号扩展为 `0xFFFFFFFF`） |

全部 8 个 case 通过 → 解码阶段验收通过。

### 独立性

✅ 零依赖。纯 `uint32_t → DecodedInsn` 的 combinational 函数，不涉及任何状态或 I/O。

---

## 阶段 2：寄存器间运算指令

### 做什么

实现 R-type OP (`0x33`) + I-type OP-IMM (`0x13`) + LUI (`0x37`) + AUIPC (`0x17`)，约 20 条指令。

### 涉及文件

- `src/src/cpu/execute.c` — `cpu_execute` 主 switch-case（本阶段先实现一部分）

### 具体任务

实现以下指令的执行逻辑：

| opcode | 指令 | funct3 | funct7 |
|--------|------|--------|--------|
| `0x33` (OP) | ADD / SUB | 0x0 | 0x00 / 0x20 |
| `0x33` | SLL | 0x1 | 0x00 |
| `0x33` | SLT | 0x2 | 0x00 |
| `0x33` | SLTU | 0x3 | 0x00 |
| `0x33` | XOR | 0x4 | 0x00 |
| `0x33` | SRL / SRA | 0x5 | 0x00 / 0x20 |
| `0x33` | OR | 0x6 | 0x00 |
| `0x33` | AND | 0x7 | 0x00 |
| `0x13` (OP-IMM) | ADDI | 0x0 | — |
| `0x13` | SLLI | 0x1 | — |
| `0x13` | SLTI | 0x2 | — |
| `0x13` | SLTIU | 0x3 | — |
| `0x13` | XORI | 0x4 | — |
| `0x13` | SRLI / SRAI | 0x5 | 0x00 / 0x20 |
| `0x13` | ORI | 0x6 | — |
| `0x13` | ANDI | 0x7 | — |
| `0x37` | LUI | — | — |
| `0x17` | AUIPC | — | — |

每执行一条指令后，强制执行 `r[0] = 0`。

### Memory Stub（自己写，~15 行）

```c
// stub_memory.h — 极简假内存，只用来取指令
// 接口签名与真实的 MMU 层对齐，等焕聪的 Memory 就绪后只需替换函数名

#include "types.h"        // 提供 ExceptionType, PrivilegeLevel
#include "memory.h"       // 提供 PhysicalMemory

uint8_t fake_mem[4096];
PhysicalMemory stub_pmem; // 最小占位

// 把测试指令序列装入"内存"
static inline void stub_mem_load(uint32_t addr, const uint32_t *insns, int count) {
    memcpy(&fake_mem[addr], insns, count * 4);
}

// CPU 取指令时调这个（签名与 mmu_read_32 对齐）
bool stub_mmu_read_32(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                      uint32_t *val, PrivilegeLevel priv, ExceptionType *exc) {
    (void)mmu; (void)pmem; (void)priv; (void)exc;
    memcpy(val, &fake_mem[vaddr], 4);
    return true;
}
```

### 为什么放第三

这些指令只操作寄存器，不需要真正的访存。配合 Memory Stub 只提供指令 fetch，就能跑通完整的"取指→译码→执行→写回"循环。

### 验收标准

| # | 测试指令序列 | 验证项 | 预期值 |
|---|------------|--------|--------|
| 1 | `addi x5, x0, 42` | `regs[5]` | 42 |
| 2 | `addi x6, x5, 8` | `regs[6]` | 50 |
| 3 | `sub x7, x6, x5` | `regs[7]` | 8 |
| 4 | `addi x8, x0, -1` | `regs[8]` | `0xFFFFFFFF` |
| 5 | `slli x9, x5, 2` | `regs[9]` | 168 |
| 6 | `xori x10, x5, 0xFF` | `regs[10]` | `42 ^ 0xFF` |
| 7 | `lui x11, 0x12345` | `regs[11]` | `0x12345000` |
| 8 | `auipc x12, 0` | `regs[12]` | 当前指令的 PC 值 |
| 9 | 任意指令序列执行完 | `regs[0]` | 永远为 0 |

### 独立性

✅ 只依赖自写的 Memory Stub（~15 行），不依赖焕聪的 Memory 模块。

---

## 阶段 3：控制流指令

### 做什么

实现 JAL (`0x6F`) + JALR (`0x67`) + 6 条 BRANCH (`0x63`)，共 8 条指令。

### 具体任务

| opcode | 指令 | funct3 | 行为 |
|--------|------|--------|------|
| `0x6F` | JAL | — | `rd = PC+4`, `PC += imm`（J-type） |
| `0x67` | JALR | 0x0 | `rd = PC+4`, `PC = (rs1+imm) & ~1` |
| `0x63` | BEQ | 0x0 | `rs1 == rs2` → 跳转 |
| `0x63` | BNE | 0x1 | `rs1 != rs2` → 跳转 |
| `0x63` | BLT | 0x4 | 有符号 `rs1 < rs2` → 跳转 |
| `0x63` | BGE | 0x5 | 有符号 `rs1 >= rs2` → 跳转 |
| `0x63` | BLTU | 0x6 | 无符号 `rs1 < rs2` → 跳转 |
| `0x63` | BGEU | 0x7 | 无符号 `rs1 >= rs2` → 跳转 |

### 验收标准

| # | 测试场景 | 验证方法 |
|---|---------|---------|
| 1 | `jal x1, target` | `regs[1] == 返回地址(PC+4)`，`PC == target` |
| 2 | `jalr x0, x1, 0` | `PC == regs[1]`（JALR 目标地址最低位已清零） |
| 3 | `beq x5, x5, offset`（相等） | `PC == 原PC + offset`，不是 `PC+4` |
| 4 | `beq x5, x6, offset`（不等） | `PC == 原PC + 4`，不跳转 |
| 5 | `bne x5, x6, offset`（不等） | `PC == 原PC + offset` |
| 6 | `blt x0, x5, offset`（0 < 正数） | 条件成立，跳转 |
| 7 | `blt x5, x0, offset`（正数 < 0） | 条件不成立，不跳转 |
| 8 | `bge x5, x5, offset`（相等） | 条件成立，跳转 |
| 9 | **综合**：10 次循环 `for(i=10; i>0; i--)` | 循环体执行 10 次后退出，计数器归零 |

### 独立性

✅ 只依赖自写的 Memory Stub。

---

## 阶段 4：访存指令

### 做什么

实现 LOAD (`0x03`) + STORE (`0x23`)，共 8 条指令：LB / LH / LW / LBU / LHU + SB / SH / SW。

### 具体任务

| opcode | 指令 | funct3 | 行为 |
|--------|------|--------|------|
| `0x03` | LB | 0x0 | 读 1 字节，符号扩展到 32 位 |
| `0x03` | LH | 0x1 | 读 2 字节，符号扩展到 32 位 |
| `0x03` | LW | 0x2 | 读 4 字节 |
| `0x03` | LBU | 0x4 | 读 1 字节，零扩展 |
| `0x03` | LHU | 0x5 | 读 2 字节，零扩展 |
| `0x23` | SB | 0x0 | 写 1 字节 |
| `0x23` | SH | 0x1 | 写 2 字节 |
| `0x23` | SW | 0x2 | 写 4 字节 |

LOAD/STORE 的地址计算：`addr = regs[rs1] + imm`（其中 STORE 的 imm 使用 IMM_S 格式）。

### 扩展 Memory Stub（加入写入能力）

```c
// 在原有 stub_mmu_read_32 基础上，增加（签名全部与真实 MMU 层对齐）：
bool stub_mmu_read_16(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                      uint16_t *val, PrivilegeLevel priv, ExceptionType *exc) {
    (void)mmu; (void)pmem; (void)priv; (void)exc;
    uint16_t tmp;
    memcpy(&tmp, &fake_mem[vaddr], 2);
    *val = tmp;
    return true;
}
bool stub_mmu_read_8(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                     uint8_t *val, PrivilegeLevel priv, ExceptionType *exc) {
    (void)mmu; (void)pmem; (void)priv; (void)exc;
    *val = fake_mem[vaddr];
    return true;
}
bool stub_mmu_write_32(MMUState *mmu, PhysicalMemory *pmem, uint32_t vaddr,
                       uint32_t val, PrivilegeLevel priv, ExceptionType *exc) {
    (void)mmu; (void)pmem; (void)priv; (void)exc;
    memcpy(&fake_mem[vaddr], &val, 4);
    return true;
}
// write16, write8 同理
```

### 验收标准

| # | 测试场景 | 验证方法 |
|---|---------|---------|
| 1 | `sw x5, 0(x2)` → `lw x6, 0(x2)` | `regs[6] == regs[5]`，完整 4 字节读写 |
| 2 | `sh x5, 0(x2)` → `lhu x6, 0(x2)` | 低 2 字节正确，lhu 零扩展高位 |
| 3 | `sb x5, 0(x2)` → `lbu x6, 0(x2)` | 低 1 字节正确，lbu 零扩展高位 |
| 4 | `lb x6, addr`（内存中是 `0xFF`） | 符号扩展：`regs[6] == 0xFFFFFFFF` |
| 5 | `lbu x6, addr`（同位置） | 零扩展：`regs[6] == 0x000000FF` |
| 6 | `lh x6, addr`（内存中是 `0x8000`） | 符号扩展：`regs[6] == 0xFFFF8000` |
| 7 | **综合**：写一串数据到内存 → 逐字节读回 | 全部匹配，无遗漏无错位 |

### 独立性

⚠️ 需要 Memory Stub 支持读写（~25 行）。等焕聪的 Memory 就绪后，只需把 `stub_mmu_read_32` 替换为 `mmu_read_32` 等真实接口。

---

## 阶段 5：系统指令

### 做什么

实现 ECALL / EBREAK / FENCE，共 3 条指令。

### 具体任务

| opcode | 指令 | 行为 |
|--------|------|------|
| `0x73` + imm=0 | ECALL | 读 `a7` (x17) 获取 syscall 号，调用 syscall 处理 |
| `0x73` + imm=1 | EBREAK | 触发断点，暂停 CPU |
| `0x0F` | FENCE | 模拟器中为 NOP，直接继续下一条指令 |

ECALL 的 syscall 处理（先实现最小可用集）：

| a7 值 | syscall | 行为 |
|-------|---------|------|
| 93 | SYS_exit | `cpu.running = false`，退出码 = `a0` (x10) |
| 64 | SYS_write | 从 Memory 读 `a1` 地址处的 `a2` 字节，写到 stdout |
| 63 | SYS_read | 从 stdin 读 `a2` 字节，写入 Memory `a1` 地址 |
| 214 | SYS_brk | 扩展堆区（返回当前 brk 地址） |

### 验收标准

| # | 测试场景 | 验证方法 |
|---|---------|---------|
| 1 | `li a7, 93; li a0, 0; ecall` | CPU 停止，程序退出码 0 |
| 2 | `li a7, 93; li a0, 42; ecall` | CPU 停止，程序退出码 42 |
| 3 | `ebreak` | CPU 暂停，控制权交给调试器（本阶段可先打印一行日志） |
| 4 | `fence` | 什么也不做，顺利执行下一条指令 |
| 5 | **端到端**：跑 `hello.s`（汇编输出 "Hello" 后 exit） | 控制台输出 "Hello, RISC-V!"，正常退出 |

### 独立性

✅ syscall 处理是你自己写的，只在 ECALL 时被触发。SYS_write/SYS_read 需要 Memory 的读写接口（用 Stub），其余不依赖任何队友模块。

---

## 阶段 6：M 扩展（乘除法）

### 做什么

实现 8 条乘除法指令：MUL / MULH / MULHSU / MULHU / DIV / DIVU / REM / REMU。

### 具体任务

| opcode | 指令 | funct3 | funct7 | 行为 |
|--------|------|--------|--------|------|
| `0x33` | MUL | 0x0 | 0x01 | `rd = (int32_t)rs1 * (int32_t)rs2`（低 32 位） |
| `0x33` | MULH | 0x1 | 0x01 | 有符号×有符号 → 高 32 位 |
| `0x33` | MULHSU | 0x2 | 0x01 | 有符号×无符号 → 高 32 位 |
| `0x33` | MULHU | 0x3 | 0x01 | 无符号×无符号 → 高 32 位 |
| `0x33` | DIV | 0x4 | 0x01 | 有符号除法，向零舍入 |
| `0x33` | DIVU | 0x5 | 0x01 | 无符号除法 |
| `0x33` | REM | 0x6 | 0x01 | 有符号取余 |
| `0x33` | REMU | 0x7 | 0x01 | 无符号取余 |

### 关键边界处理

```c
// mulh: 有符号 × 有符号 → 高 32 位
int32_t mulh(int32_t a, int32_t b) {
    return (int32_t)(((int64_t)a * (int64_t)b) >> 32);
}

// mulhu: 无符号 × 无符号 → 高 32 位
uint32_t mulhu(uint32_t a, uint32_t b) {
    return (uint32_t)(((uint64_t)a * (uint64_t)b) >> 32);
}

// 除法边界：
// - 除数为 0：DIV/DIVU 返回 -1 (全 1)，REM/REMU 返回被除数
// - DIV 溢出：INT32_MIN / -1 返回 INT32_MIN
```

### 验收标准

| # | 测试 | 验证项 | 预期值 |
|---|------|--------|--------|
| 1 | `mul x5, x6, x7` (3 × 5) | `regs[5]` | 15 |
| 2 | `mulh x5, x6, x7`（`0x80000000 × 2`） | `regs[5]` | `((int64_t)a*b)>>32` |
| 3 | `mulhu x5, x6, x7`（`0xFFFFFFFF × 3`） | `regs[5]` | `((uint64_t)a*b)>>32` |
| 4 | `div x5, x6, x7` (10 / 3) | `regs[5]` | 3（向零舍入） |
| 5 | `div x5, x6, x7` (-10 / 3) | `regs[5]` | -3（向零舍入） |
| 6 | `div x5, x6, x0`（除数为 0） | `regs[5]` | -1（全 1） |
| 7 | `div x5, x6, x7`（INT32_MIN / -1） | `regs[5]` | INT32_MIN（溢出特例） |
| 8 | `rem x5, x6, x7` (10 % 3) | `regs[5]` | 1 |

### 独立性

✅ 乘除法是纯 ALU 操作，只读写寄存器。不涉及访存，不依赖任何队友模块。

---

## 阶段 7：反汇编器

### 做什么

实现 `cpu_disasm()` —— 二进制指令 → "人类可读的汇编字符串"。

### 涉及文件

- `src/src/cpu/decode.c` — `cpu_disasm` 实现

### 具体任务

1. **指令名表**：opcode + funct3 + funct7 → 指令名字符串（如 `"add"`, `"addi"`, `"lw"`, `"beq"`）
2. **寄存器名表**：x0→zero, x1→ra, x2→sp, x3→gp, x4→tp, x5→t0, ..., x31→t6
3. **格式化输出**：按指令类型拼接 "指令名 目标寄存器, 源操作数1, 源操作数2"

例：`0x00300513, pc=0x10074` → 输出 `"addi a0, zero, 3"`

### 验收标准

| # | 输入指令 | 输入 PC | 预期输出 |
|---|---------|---------|---------|
| 1 | `0x00300513` | `0x10074` | `"addi a0, zero, 3"` |
| 2 | `0x00B302B3` | `0x10078` | `"add t0, t1, a1"` |
| 3 | `0x00412503` | `0x1007C` | `"lw a0, 4(sp)"` |
| 4 | `0x00A12423` | `0x10080` | `"sw a0, 8(sp)"` |
| 5 | `0x00628863` | `0x10084` | `"beq t0, t1, 0x10094"`（显示绝对跳转地址） |
| 6 | `0x0000006F` | `0x10088` | `"jal zero, 0x10088"`（offset=0） |
| 7 | `0x12345537` | `0x1008C` | `"lui a0, 0x12345"` |
| 8 | `0x00100073` | `0x10090` | `"ebreak"` |

### 独立性

✅ 零依赖。纯函数 `(uint32_t insn, uint32_t pc) → 字符串`。

---

## 阶段 8（选做）：五级流水线

### 做什么

将基础的单周期 CPU 改造为五级流水线：IF → ID → EX → MEM → WB，含数据转发（Forwarding）和流水线停顿（Stall）。

### 为什么是选做

基本实现的顺序执行 CPU（阶段 0–7）已经能正确跑 RISC-V 程序。流水线是在此基础上的性能优化——**不改变程序执行结果，只提高吞吐率**。

### 具体任务

1. **五级流水寄存器**：定义 IF/ID、ID/EX、EX/MEM、MEM/WB 四组流水线寄存器
2. **数据转发（Forwarding）**：EX/MEM 或 MEM/WB 的结果直接旁路回 EX 阶段
3. **Load-Use 冒险检测**：`lw` 后紧跟使用该寄存器的指令 → 插入一个气泡（stall）
4. **控制冒险处理**：分支指令在 ID 阶段解析，未命中时冲刷 IF/ID
5. **CPI 统计**：`cycle_count / inst_count`

### 验收标准

| # | 测试 | 验证方法 |
|---|------|---------|
| 1 | 同一个测试程序，基础版 vs 流水线版 | **寄存器最终值完全一致** |
| 2 | 无数据冒险序列（如 `add x5,x6,x7; add x8,x9,x10`） | CPI ≈ 1.0 |
| 3 | 有数据冒险（如 `add x5,x6,x7; sub x8,x5,x9`） | Forwarding 生效，结果正确 |
| 4 | Load-Use 冒险（`lw x5,0(x2); add x6,x5,x7`） | 自动插入 1 个气泡，结果正确 |
| 5 | 分支指令 | 预测错误的指令不写入寄存器 |

### 独立性

✅ 纯 CPU 内部重构，对外接口不变。不依赖任何队友模块。

---

## 测试文件组织建议

```
test/
├── cpu/
│   ├── test_decode.c        # 阶段1：手写指令的解码测试
│   ├── test_alu.c           # 阶段2：寄存器运算指令测试
│   ├── test_branch.c        # 阶段3：分支跳转测试
│   ├── test_memory.c        # 阶段4：访存指令测试
│   ├── test_syscall.c       # 阶段5：ecall/ebreak 测试
│   ├── test_muldiv.c        # 阶段6：乘除法测试
│   ├── test_disasm.c        # 阶段7：反汇编输出比对
│   └── stub_memory.h        # 所有阶段共用的 Memory Stub
├── asm/
│   ├── hello.s              # 端到端：输出 "Hello, RISC-V!"
│   ├── add_test.s           # 算术指令综合测试
│   ├── branch_test.s        # 分支指令综合测试
│   └── muldiv_test.s        # 乘除法综合测试
└── riscv-tests/             # 官方 conformance test（阶段4后引入）
```

---

## 与队友接口的就绪节点

| 你的阶段 | 需要队友提供的接口 | 何时可对接 |
|----------|-------------------|-----------|
| 阶段 0–3 | 无 | 立即开始 |
| 阶段 4 | Memory: `mmu_read_32 / mmu_write_32 / mmu_read_16 / mmu_write_16 / mmu_read_8 / mmu_write_8` | Memory Stub → 替换为焕聪的真实接口 |
| 阶段 5 | 同上 + syscall 写 stdout 需要 Debugger 确认格式 | 先用自己的 syscall 实现 |
| 阶段 7 | Debugger: 确认 `cpu_disasm` 的输出格式是否匹配 `x` 命令 | 反汇编格式与 Debugger 约定一致 |
| 最终联调 | Loader 设置 `sim->cpu.pc` 和 `regs[2]`（栈指针） | 阶段 0–5 完成后可与嘉华 + 焕聪联调 |

---

## 参考资料

- [RISC-V 非特权规范 (Volume I)](https://github.com/riscv/riscv-isa-manual/releases) — 第 2 章 RV32I 指令集
- [RISC-V 特权规范 (Volume II)](https://github.com/riscv/riscv-isa-manual/releases) — ecall/ebreak 行为
- `docs/参考项目结构2/src/cpu/` — 参考骨架代码
- [RISC-V 指令编码速查](https://www.cl.cam.ac.uk/teaching/2016/ECAD+Arch/files/docs/RISCVGreenCardv8-20151013.pdf) — 一张卡片涵盖所有指令编码
- [riscv-tests](https://github.com/riscv-software-src/riscv-tests) — 官方指令集 conformance test 套件
