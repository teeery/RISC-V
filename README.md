# RISC-V RV32IM 模拟器与调试器

一个面向教育用途的 RISC-V 指令集模拟器，支持 RV32I 基础整数指令集 + M 扩展（乘除法），
能够加载并运行真实的 ELF 二进制程序，并内置交互式调试器。

## 项目结构

```
RISC-V/
├── include/                # 头文件
│   ├── types.h             # 公共类型定义、指令/异常/CPU状态枚举
│   ├── cpu.h               # CPU 核心接口 (init/step/run/dump)
│   ├── memory.h            # 物理内存管理接口
│   ├── mmu.h               # 虚拟内存 (Sv32 MMU) 接口
│   ├── elf_loader.h        # ELF32 文件解析与加载器接口
│   ├── debugger.h          # 交互式调试器接口
│   └── syscall.h           # Linux 系统调用模拟接口
├── src/                    # 源文件
│   ├── main.c              # 入口点、命令行参数解析
│   ├── cpu/
│   │   ├── cpu.c           # CPU 核心 (初始化、取值→译码→执行循环、寄存器dump)
│   │   ├── decode.c        # 指令译码器 (6种格式 → 结构化解码)
│   │   ├── execute.c       # 指令执行单元 (47条RV32I + 8条M扩展)
│   │   └── csr.c           # 控制与状态寄存器读写 (CSRRW/CSRRS/CSRRC)
│   ├── memory/
│   │   ├── memory.c        # 物理内存管理 (读写/mmapped区域/brk/hexdump)
│   │   └── mmu.c           # Sv32 虚拟内存 (两级页表、地址翻译)
│   ├── elf_loader.c        # ELF32 可执行文件加载 (Header解析/段加载)
│   ├── debugger.c          # 交互式调试 REPL (break/step/continue/examine)
│   └── syscall.c           # 系统调用模拟 (write/read/exit/brk)
├── test/
│   └── hello.s             # 测试用 RISC-V 汇编程序
├── Makefile                # 构建系统
└── README.md               # 本文件
```

## 功能概述

### 基础要求
| 功能 | 状态 | 说明 |
|------|------|------|
| RV32I 指令集 (47条) | ✅ 骨架已就绪 | 全部指令的执行逻辑框架已写好，需补充细节 |
| M 扩展 (8条乘除法) | ✅ 骨架已就绪 | MUL/MULH/MULHSU/MULHU/DIV/DIVU/REM/REMU |
| 虚拟内存映射 (Sv32) | 🚧 骨架已就绪 | 两级页表结构已定义，需完成实际页遍历逻辑 |
| ELF32 加载器 | ✅ 骨架已就绪 | ELF Header解析+Program Header遍历+段加载 |
| 调试器 | ✅ 骨架已就绪 | break/step/continue/info registers/x examine |
| 系统调用 | ✅ 骨架已就绪 | write/read/exit/brk 已实现 |

### 调试器命令

```
(riscv-dbg) help
Commands:
  b/reak <addr>        Set breakpoint
  d/elete <id>         Delete breakpoint
  i/b nfo break        List breakpoints
  s/tep                Single step
  si/stepi [n]         Step n instructions
  c/ontinue            Continue execution
  i/r nfo registers    Show registers
  x/<N><F><U> <addr>   Examine memory
  q/uit                Exit
```

## 构建与运行

### 构建模拟器
```bash
# 构建 (Release)
make

# 构建 (Debug, 带调试符号)
make debug

# 清理
make clean
```

### 编译测试程序
需要 RISC-V GNU 工具链:
```bash
# 安装工具链 (Arch Linux)
pacman -S riscv64-unknown-elf-gcc

# 安装工具链 (Ubuntu/Debian)
apt install gcc-riscv64-unknown-elf

# 手动汇编+链接
riscv64-unknown-elf-as -march=rv32im -o test/hello.o test/hello.s
riscv64-unknown-elf-ld -o test/hello.elf test/hello.o
```

### 运行
```bash
# 直接运行
./riscv-sim test/hello.elf

# 调试模式
./riscv-sim -s test/hello.elf

# 调试+指令跟踪
./riscv-sim -t -s test/hello.elf

# 内存大小 256 MB
./riscv-sim -m 256 test/hello.elf
```

## 开发任务清单

各文件中标记了 `TODO` 项，以下是按优先级排列的开发任务：

### Phase 1: 核心指令执行
1. [ ] **decode.c** — 验证所有 6 种指令格式的位域提取和立即数符号扩展
2. [ ] **execute.c** — 完善所有 RV32I 指令的执行逻辑 (LOAD/STORE 已基本完成)
3. [ ] **cpu.c** — 完善 cpu_step() 的异常处理逻辑 (cpu_take_trap)

### Phase 2: 内存与 ELF
4. [ ] **memory.c** — 完善 mem_find_free() 的空闲区域分配算法
5. [ ] **elf_loader.c** — 完善段加载循环 (遍历 Program Header + 读取数据)
6. [ ] **mmu.c** — 完成 Sv32 两级页表遍历 (mmu_translate 核心逻辑)

### Phase 3: 系统调用与调试
7. [ ] **syscall.c** — 添加更多系统调用 (openat/close/fstat)
8. [ ] **debugger.c** — 完善 x 命令的格式解析 (x/NxFxU)、地址表达式求值
9. [ ] **csr.c** — 完善特权级检查、补充 S 模式 CSR

### Phase 4: 扩展功能
10. [ ] **main.c** — 添加指令统计计数器、反汇编模式 (`-d`)
11. [ ] 支持 ELF64 (用于 RV64 程序)
12. [ ] 支持 C 扩展 (压缩指令, RV32IC)
13. [ ] 支持 A 扩展 (原子操作)

## 参考资料

- [RISC-V 非特权规范 (ISA Manual)](https://github.com/riscv/riscv-isa-manual/releases/latest)
- [RISC-V 特权规范](https://github.com/riscv/riscv-isa-manual/releases/latest)
- [RISC-V ELF psABI Specification](https://github.com/riscv-non-isa/riscv-elf-psabi-doc)
- [Linux RISC-V Syscall Table](https://github.com/torvalds/linux/blob/master/arch/riscv/kernel/syscall_table.c)
