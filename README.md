# RISC-V

面向 RISC-V 的模拟器与调试器，支持 RV32I 基础整数指令集、M 扩展（乘除法）及 F 扩展（单精度浮点），能够加载 ELF32 可执行文件并运行。实现了三种 CPU 执行模型（单周期 / 多周期 / 五级流水线）、数据通路可视化、虚拟内存映射（Sv32 两级页表）、常用 Linux 系统调用模拟（write / read / exit / brk），以及一个支持断点、单步执行、寄存器/内存查看的交互式调试器，同时提供 Web 端流水线数据通路实时可视化。

## 项目结构

```
risc-v/
├── src/
│   ├── include/                     # 公共头文件
│   │   ├── simulator.h              #   模拟器顶层接口
│   │   ├── types.h                  #   公共类型定义
│   │   ├── cpu/
│   │   │   ├── cpu.h                #   CPU 模拟
│   │   │   ├── decode.h             #   指令译码
│   │   │   ├── execute.h            #   指令执行
│   │   │   ├── exec_internal.h      #   执行器内部接口
│   │   │   ├── controller/
│   │   │   │   └── controller_internal.h  # 控制器接口（单/多/流水线）
│   │   │   └── datapath/
│   │   │       └── alu.h            #   ALU 数据通路
│   │   ├── debugger/
│   │   │   ├── debugger.h           #   交互式调试器
│   │   │   └── web_server.h         #   Web 调试器服务器
│   │   ├── linux/
│   │   │   └── syscall.h            #   系统调用模拟
│   │   ├── loader/
│   │   │   └── elf_loader.h         #   ELF 加载器
│   │   └── memory/
│   │       ├── memory.h             #   物理内存
│   │       └── mmu.h                #   MMU / Sv32 页表
│   ├── src/                         # 模块实现
│   │   ├── main.c                   #   入口 + 命令行解析
│   │   ├── simulator.c              #   模拟器主循环
│   │   ├── cpu/                     # CPU ——分工：李特
│   │   │   ├── cpu.c
│   │   │   ├── decode.c
│   │   │   ├── controller/          #   CPU 控制器
│   │   │   │   ├── single_cycle.c   #     单周期
│   │   │   │   ├── multi_cycle.c    #     多周期
│   │   │   │   └── pipeline.c       #     五级流水线
│   │   │   ├── datapath/
│   │   │   │   └── alu.c            #   ALU 运算单元
│   │   │   └── execute/             #   指令执行器
│   │   │       ├── execute.c        #     执行分派
│   │   │       ├── exec_rv32i.c     #     RV32I 基础指令
│   │   │       ├── exec_m.c         #     M 扩展（乘除法）
│   │   │       └── exec_f.c         #     F 扩展（浮点）
│   │   ├── debugger/                # 调试器 ——分工：嘉俊
│   │   │   ├── debugger.c
│   │   │   ├── debugger_internal.h
│   │   │   ├── breakpoint.c
│   │   │   ├── web_server.c         #   Web 调试器 HTTP 服务
│   │   │   └── datapath_pipeline_dynamic.svg  # 流水线 SVG 资源
│   │   ├── linux/
│   │   │   └── syscall.c            #   系统调用模拟
│   │   ├── loader/                  # ELF 加载器 ——分工：嘉华
│   │   │   ├── elf_load.c
│   │   │   ├── elf_segment.c
│   │   │   ├── elf_section.c
│   │   │   ├── elf_stack.c
│   │   │   ├── elf_validate.c
│   │   │   └── loader_internal.h
│   │   └── memory/                  # 内存管理 ——分工：焕聪
│   │       ├── memory.c
│   │       └── mmu.c
│   └── test/                        # 单元测试 & 测试夹具
│       ├── cpu/
│       │   ├── decode_test.c
│       │   ├── execute_test.c
│       │   ├── f_test.c             #   F 扩展测试
│       │   ├── m_test.c             #   M 扩展测试
│       │   ├── multi_cycle_test.c   #   多周期测试
│       │   └── pipeline_test.c      #   流水线测试
│       ├── debugger/
│       │   └── debugger_test.c      #   调试器测试
│       ├── loader/
│       │   ├── test_load.c
│       │   ├── test_validate.c
│       │   ├── gen_minimal_elf.c    #   测试用 ELF 生成器
│       │   └── minimal.elf
│       └── e2e/
│           ├── e2e_test.c           #   端到端测试
│           ├── gen_hello_elf.c      #   hello.elf 生成器
│           └── hello.elf
├── docs/                            # 文档与参考资料
│   ├── 前置知识/
│   │   ├── 计算机知识.md
│   │   ├── 数据流.md
│   │   └── RISC-V指令编码学习笔记.md
│   ├── 设计文档/
│   │   ├── 共同部分/                 #   公共约定
│   │   ├── 李特/                    #   CPU 设计文档
│   │   ├── 嘉俊/                    #   调试器设计文档
│   │   ├── 嘉华/                    #   Loader 设计文档
│   │   └── 焕聪/                    #   Memory 设计文档
│   ├── tools/                       #   文档工具
│   │   ├── add_overlay.py           #     SVG 叠加标注工具
│   │   ├── embed_svg.py             #     SVG→C 字符串转换
│   │   └── datapath_editor.html     #     数据通路交互编辑器
│   ├── 指令解码表.md                 #   指令编码速查
│   ├── 流水线.drawio.svg            #   流水线源图
│   ├── Web调试器使用文档.md
│   └── 参考项目结构1/ 参考项目结构2/
├── build/                           # 编译产物
├── tools/                           # 外部工具链
│   └── riscv-gcc/                   #   RISC-V GCC 14.2 交叉编译器
└── README.md
```

### 协作方式：文档先行

拿到分工后不要直接写代码，先写设计文档。设计文档的核心目的有两个：

**对自己** —— 写的过程就是理解的过程。把模块要做什么、怎么做写清楚，才能发现盲区和问题，避免"写到一半发现想错了"。

**对队友** —— 让别人能看懂你的模块怎么设计的、接口长什么样、数据怎么流转。每个人的模块不是孤立的，设计文档让其他人不用读代码就能理解你的模块。

每个人拿到自己的模块后，按以下步骤推进：

1. **读前置知识** — 先把 [`docs/前置知识/`](docs/前置知识/) 中的资料读一遍，搞懂项目全貌
2. **写设计文档** — 在 [`docs/设计文档/`](docs/设计文档/) 下写设计文档，单文件多文件自己设计。写清楚「是什么、做什么、怎么做」：

   **是什么** —— 这个模块在整个系统中起到什么作用？处于数据流的哪个环节？
   
   **做什么** —— 这个模块对外要提供哪些功能？边界在哪里（什么归我管、什么不归我管）？
   
   **怎么做** —— 想清楚了是什么、做什么之后，再落笔到具体方案：
   - 对外暴露什么接口（函数签名、数据结构），别人怎么调用我
   - 内部打算怎么实现（算法思路、状态机、关键逻辑）
   - 和其他模块怎么交互（谁调我、我调谁、时序是怎样的）
3. **团队讨论** — 把设计文档发出来，大家一起看接口对不对齐、有没有遗漏
4. **开始编码** — 讨论通过后再动手写代码


## 构建

使用 Visual Studio 打开 `src/` 文件夹（Folder 项目），直接生成即可。编译产物输出到 `build/` 目录。

| 产物 | 说明 |
|------|------|
| `build/riscv-sim.exe` | 模拟器主程序 |
| `build/execute_test.exe` | CPU 执行单元测试 |
| `build/decode_test.exe` | CPU 译码单元测试 |
| `build/f_test.exe` | F 扩展浮点测试 |
| `build/m_test.exe` | M 扩展乘除测试 |
| `build/multi_cycle_test.exe` | 多周期控制器测试 |
| `build/pipeline_test.exe` | 流水线控制器测试 |
| `build/debugger_test.exe` | 调试器单元测试 |
| `build/e2e_test.exe` | 端到端测试 |
| `build/test_load.exe` | Loader 测试 |

## 运行

```bash
# 基本运行
./riscv-sim src/test/e2e/hello.elf

# 指定 CPU 模型（single / multi / pipeline）
./riscv-sim -m pipeline src/test/e2e/hello.elf

# 交互式调试模式
./riscv-sim -s src/test/e2e/hello.elf

# Web 调试器模式（流水线数据通路实时可视化）
./riscv-sim -w 8080 src/test/e2e/hello.elf
# 浏览器访问 http://localhost:8080

# 指令跟踪模式（开发中）
./riscv-sim -t src/test/e2e/hello.elf
```

## 设计文档

### [从零开始的计算机底层知识](docs/前置知识/计算机知识.md)

按"如果我一无所知"的方式组织，从 CPU 只能看懂 0/1 开始，逐层构建到流水线。涵盖八层内容：

| 层次 | 内容 |
|------|------|
| 第一层 | 程序到底是什么 —— CPU 只认识数字，机器指令、寄存器、ISA |
| 第二层 | 汇编语言 —— 给人看的机器码，RISC-V 汇编风格、伪指令 |
| 第三层 | ELF 文件格式 —— 可执行文件的结构（Header + .text + .data + 符号表） |
| 第四层 | 模拟器原理 —— 用软件假扮硬件，while 循环执行指令 |
| 第五层 | 内存模型 —— MemRegion、虚拟地址到实际缓冲区的映射 |
| 第六层 | 系统调用 —— ecall 怎么让模拟器扮演操作系统 |
| 第七层 | 调试器 —— 软件断点（ebreak 替换指令）、单步执行 |
| 第八层 | 流水线 —— IF/ID/EX/MEM/WB 五级、数据冒险/控制冒险、Forwarding、CPI |

### [从 C 程序到模拟器执行的完整数据流追踪](docs/前置知识/数据流.md)

用一个极简 C 程序 `int main() { return 3; }` 逐步骤展示数据形态如何变化：

| 步骤 | 数据形态 | 工具/模块 |
|------|----------|-----------|
| 第 0 步 | `int main() { return 3; }`（文本） | 你写的 C 程序 |
| 第 1 步 | `addi x10, x0, 3`（汇编文本） | GCC 编译 |
| 第 2 步 | `0x00300513`（32 位机器码） | 汇编器 |
| 第 3 步 | 链接完整的 ELF 可执行文件 | 链接器 |
| 第 4 步 | ELF Header + Program Headers 详解 | 十六进制分析 |
| 第 5 步 | ELF 加载 → 模拟器虚拟内存（MemRegion[]） | 你们的 ELF 加载器 |
| 第 6 步 | CPU 执行循环：取指→译码→执行（addi / jalr） | 你们的 CPU 模拟 |
| 第 7 步 | ECALL 系统调用处理（exit） | 你们的 syscall 模块 |
| 第 8 步 | 调试器交互（断点/单步/查看寄存器） | 你们的调试器 |

### 参考项目结构

- **[参考项目结构1](docs/参考项目结构1/)** — 单层 `include/` + `src/` 目录，所有模块平铺放置。适合小规模快速开发。
- **[参考项目结构2](docs/参考项目结构2/)** — 模块化目录，每个子系统（cpu/debugger/loader/memory/syscall）独立文件夹。适合多人协作分模块开发。

## 版本记录

| 版本 | 日期 | 说明 |
|------|------|------|
| [v0.1.0](https://github.com/teeery/risc-v/releases/tag/v0.1.0) | 2026-07-02 | 首个可运行版本：完整 RV32I 指令执行、ELF 加载器、CPU 单元测试 |

## 参考资料

- [RISC-V 规范](https://riscv.org/technical/specifications/) — 官方指令集手册
- [RISC-V 汇编手册](https://github.com/riscv-non-isa/riscv-asm-manual) — 汇编语法与伪指令说明
