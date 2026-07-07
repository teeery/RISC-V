# F 扩展设计文档

> 作者：焕聪 | 最后更新：2026-07-06
>
> F 扩展（RV32F）为 RISC-V 添加 26 条单精度浮点指令。
> 本文档描述在模拟器中实现这些指令的编码格式、函数分发和实现细节。

---

## 1. 是什么

F 扩展让 RISC-V CPU 能够执行 `float`（单精度 IEEE 754）的 load/store/运算/转换指令。

**在项目中的位置：**

```
CPU 执行循环 (execute.c)
    │
    ├─ 0x07 (LOAD-FP)   → exec_load_fp()    ─┐
    ├─ 0x27 (STORE-FP)  → exec_store_fp()    │
    ├─ 0x43 (OP-FP)     → exec_fp_op()       ├── exec_f.c
    ├─ 0x47 (FMSUB)     → exec_fma()         │
    ├─ 0x4B (FNMSUB)    → exec_fma()         │
    └─ 0x4F (FNMADD)    → exec_fma()        ─┘
```

**依赖关系：**

```
exec_f.c
  ├── cpu/execute.h      — cpu_trap 异常处理
  ├── cpu/decode.h       — DecodedInstr (opcode/rd/rs1/rs2/rs3/funct3/funct7/imm)
  ├── cpu/exec_internal.h— exec_fma 声明
  ├── simulator.h        — Simulator* 访问 mmu/pmem/cpu
  ├── memory/mmu.h       — mmu_read_32 / mmu_write_32
  ├── types.h            — ExceptionType, PrivilegeLevel
  └── <math.h>           — sqrtf, fminf, fmaxf
```

**不依赖：** Loader、Debugger、exec_rv32i.c 等其他执行文件。

---

## 2. 做什么（26 条指令清单）

### 2.1 浮点访存（2 条）

| 指令 | opcode | 格式 | 说明 |
|------|--------|------|------|
| `FLW rd, imm(rs1)` | 0x07 | I-type | 从内存读 32 位浮点数到 `fregs[rd]` |
| `FSW rs2, imm(rs1)` | 0x27 | S-type | 将 `fregs[rs2]` 的 32 位写入内存 |

### 2.2 OP-FP 运算（20 条，opcode=0x43，funct7 区分）

| funct7 | 指令 | rs2 / funct3 | 说明 |
|--------|------|-------------|------|
| 0x00 | `FADD.S` | — | 浮点加法 |
| 0x04 | `FSUB.S` | — | 浮点减法 |
| 0x08 | `FMUL.S` | — | 浮点乘法 |
| 0x0C | `FDIV.S` | — | 浮点除法 |
| 0x2C | `FSQRT.S` | rs2=0 | 平方根 |
| 0x10 | `FSGNJ.S` | funct3=0 | 符号注入（rs2 的符号） |
| 0x10 | `FSGNJN.S` | funct3=1 | 符号注入（rs2 符号取反） |
| 0x10 | `FSGNJX.S` | funct3=2 | 符号注入（XOR 符号） |
| 0x14 | `FMIN.S` | funct3=0 | 最小值 |
| 0x14 | `FMAX.S` | funct3=1 | 最大值 |
| 0x50 | `FLE.S` | funct3=0 | rs1 ≤ rs2 → 1/0（结果写整数寄存器） |
| 0x50 | `FLT.S` | funct3=1 | rs1 < rs2 → 1/0 |
| 0x50 | `FEQ.S` | funct3=2 | rs1 == rs2 → 1/0 |
| 0x60 | `FCVT.W.S` | rs2=0 | float → 有符号 int |
| 0x60 | `FCVT.WU.S` | rs2≠0 | float → 无符号 int |
| 0x68 | `FCVT.S.W` | rs2=0 | 有符号 int → float |
| 0x68 | `FCVT.S.WU` | rs2≠0 | 无符号 int → float |
| 0x70 | `FMV.X.W` | funct3=0 | freg 位拷贝到整数 reg |
| 0x70 | `FCLASS.S` | funct3=1 | 返回浮点类型码 |
| 0x78 | `FMV.W.X` | funct3=0 | 整数 reg 位拷贝到 freg |

### 2.3 FMA 融合乘加（4 条，R4 格式）

| 指令 | opcode | 公式 |
|------|--------|------|
| `FMADD.S rd, rs1, rs2, rs3` | 0x43 | `rd = rs1 × rs2 + rs3` |
| `FMSUB.S rd, rs1, rs2, rs3` | 0x47 | `rd = rs1 × rs2 - rs3` |
| `FNMSUB.S rd, rs1, rs2, rs3` | 0x4B | `rd = -(rs1 × rs2) + rs3` |
| `FNMADD.S rd, rs1, rs2, rs3` | 0x4F | `rd = -(rs1 × rs2) - rs3` |

---

## 3. 怎么做

### 3.1 文件位置

- **主实现：** `src/src/cpu/execute/exec_f.c`（257 行）
- **内部声明：** `src/include/cpu/exec_internal.h`（4 个函数声明）
- **分发入口：** `src/src/cpu/execute/execute.c`（opcode case 分发）
- **解码支持：** `src/include/cpu/decode.h`（DecodedInstr.rs3, RS3 宏）
- **浮点寄存器：** `src/include/cpu/cpu.h`（CPU.fregs[32]）

### 3.2 uint32_t ↔ float 互转

```c
/* 定义在 exec_f.c 顶部 */
typedef union { uint32_t u; float f; } fp32_t;

/* 使用模式：
 *   fp32_t a, r;
 *   a.u = sim->cpu.fregs[d->rs1];   // 从 freg 读出 32 位模式
 *   r.f = a.f + b.f;                // 浮点运算
 *   sim->cpu.fregs[d->rd] = r.u;    // 32 位模式写回
 */
```

`a.u` 和 `a.f` 共享同一块 4 字节内存，零开销在整数位模式和浮点解释之间切换。

### 3.3 指令编码格式

FLW/FSW 使用标准 I-type / S-type（和 LW/SW 相同），OP-FP 使用 R-type 编码（funct7 区分操作），FMA 使用 R4 格式。

```
R-type（OP-FP，如 FADD.S）：
  31        25 24    20 19    15 14    12 11    7 6       0
 ┌───────────┬────────┬────────┬────────┬───────┬────────────┐
 │  funct7   │  rs2   │  rs1   │ funct3 │  rd   │  opcode   │
 └───────────┴────────┴────────┴────────┴───────┴────────────┘

R4 格式（FMA，如 FMADD.S）：
  31    27 26   25 24    20 19    15 14    12 11    7 6       0
 ┌───────┬──────┬────────┬────────┬────────┬───────┬──────────┐
 │  rs3  │ fmt  │  rs2   │  rs1   │ funct3 │  rd   │ opcode  │
 └───────┴──────┴────────┴────────┴────────┴───────┴──────────┘
   [31:27] [1:0]                                0x43/47/4B/4F
```

FMA 的 `rs3` 字段（bits [31:27]）在 R-type 中对应 `funct7[6:2]`，因此新增 `RS3` 宏而无须改变已有 decode 逻辑。`fmt` 字段（bits [26:25]）为 00 代表单精度 `.S`。

### 3.4 4 个分发函数

```c
// exec_load_fp  — FLW
bool exec_load_fp(Simulator *sim, DecodedInstr *d, uint32_t *next_pc)
{
    uint32_t addr = sim->cpu.regs[d->rs1] + (uint32_t)d->imm;
    uint32_t val;
    ExceptionType exc = EXC_NONE;
    if (mmu_read_32(&sim->mmu, &sim->pmem, addr, &val, sim->cpu.priv, &exc)) {
        sim->cpu.fregs[d->rd] = val;   // 直接存 IEEE 754 位模式
        return true;
    }
    cpu_trap(sim, (uint32_t)exc, addr);
    return false;
}

// exec_store_fp — FSW
bool exec_store_fp(Simulator *sim, DecodedInstr *d, uint32_t *next_pc)
{
    uint32_t addr = sim->cpu.regs[d->rs1] + (uint32_t)d->imm;
    uint32_t data = sim->cpu.fregs[d->rs2];
    ExceptionType exc = EXC_NONE;
    if (mmu_write_32(&sim->mmu, &sim->pmem, addr, data, sim->cpu.priv, &exc))
        return true;
    cpu_trap(sim, (uint32_t)exc, addr);
    return false;
}

// exec_fp_op — 20 条 OP-FP，switch(d->funct7) 分发
// exec_fma   — 4 条 FMA，  switch(d->opcode) 分发
```

### 3.5 FLW/FSW 与 LW/SW 的对比

| | LW (整数 Load) | FLW (浮点 Load) |
|---|---|---|
| 地址计算 | `rs1 + imm` | `rs1 + imm`（完全相同） |
| memory 调用 | `mmu_read_32` | `mmu_read_32`（同一函数） |
| 目标 | `cpu->regs[rd]` | `cpu->fregs[rd]` |
| memory 模块需改动 | — | **不需要** |

memory 模块将 4 字节作为位模式读回，不关心解释为 `int` 还是 `float`。

### 3.6 跨寄存器文件操作

以下 8 条 OP-FP 指令涉及浮点寄存器和整数寄存器之间的数据搬运：

| 指令 | 方向 | 类型转换 |
|------|------|---------|
| `FCVT.W.S` / `FCVT.WU.S` | freg → reg | ✅ float → int（截断向零） |
| `FCVT.S.W` / `FCVT.S.WU` | reg → freg | ✅ int → float |
| `FMV.X.W` | freg → reg | ❌ 纯位拷贝 |
| `FMV.W.X` | reg → freg | ❌ 纯位拷贝 |
| `FLE.S` / `FLT.S` / `FEQ.S` | freg×2 → reg | ❌ 比较结果 0/1 写整数寄存器 |

---

## 4. 实现优先级与完成状态

### 4.1 已全部完成

| 优先级 | 内容 | 指令数 | 代码量 |
|--------|------|--------|--------|
| P0 | FLW / FSW 访存 | 2 | ~30 行 |
| P0 | FADD / FSUB / FMUL / FDIV 四则 | 4 | ~20 行 |
| P1 | FSQRT 平方根 | 1 | ~5 行 |
| P1 | FSGNJ×3 符号注入 | 3 | ~12 行 |
| P1 | FMIN / FMAX 最值 | 2 | ~10 行 |
| P1 | FLE / FLT / FEQ 比较 | 3 | ~10 行 |
| P2 | FCVT.W.S / FCVT.WU.S（float→int） | 2 | ~10 行 |
| P2 | FCVT.S.W / FCVT.S.WU（int→float） | 2 | ~10 行 |
| P2 | FMV.X.W / FMV.W.X / FCLASS.S | 3 | ~25 行 |
| P3 | FMADD / FMSUB / FNMSUB / FNMADD | 4 | ~30 行 |

### 4.2 暂未实现

| 内容 | 说明 |
|------|------|
| fcsr 寄存器 | 舍入模式 + 异常标志；CPU.fcsr 字段空间已在 cpu.h 预留，exec_f.c 暂不写 fcsr |
| D 扩展（双精度） | 需 64 位 load/store/运算 + 新的 `mmu_read_64` / `mmu_write_64` |
| FMA 单次舍入 | 当前为 `a.f * b.f + c.f`（两次舍入），与真实硬件的单次舍入有 1 ulp 差异 |

---

## 5. 与其他模块的接口

| 调用方 | 使用的接口 | 说明 |
|--------|-----------|------|
| execute.c | `exec_load_fp / exec_store_fp / exec_fp_op / exec_fma` | opcode 分发 |
| exec_f.c → memory | `mmu_read_32 / mmu_write_32` | FLW/FSW 访存 |
| exec_f.c → CPU | `sim->cpu.fregs[]` / `sim->cpu.regs[]` | 寄存器读写 |
| exec_f.c → CPU | `cpu_trap(sim, exc, tval)` | 访存失败时触发异常 |
| decode.h | `DecodedInstr.rs3` / `RS3(instr)` | FMA 的第三源寄存器 |
