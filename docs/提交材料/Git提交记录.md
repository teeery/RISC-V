# Git 提交记录

> 导出日期：2026-07-11
> 总提交数：117
> 分支：main, lite, jiahua, xhc, jiajun, feature/*

## 贡献者统计

| 贡献者 | Git 用户名 | 提交数 |
|--------|-----------|--------|
| Terry | Terry, teeery | 59 |
| 杨嘉华 | 12dsadaf | 23 |
| 香焕聪 | ZERONE-xhc | 22 |
| 刘嘉俊 | iyhgfgyhgftyuhgge | 13 |

## 完整提交历史

```
| 73207ba | 2026-07-11 | Terry | docs: v0.2.0 版本记录补充 release 链接 |
| f908020 | 2026-07-11 | Terry | docs: 版本记录更新 v0.2.0 — 三级CPU + Web调试器可视化 |
| 4769a70 | 2026-07-11 | Terry | fix: Web调试器流水线模式 Step 不可见 + 单周期/多周期 Step 不执行 |
| bf4044b | 2026-07-10 | Terry | docs: 截图移至简介下方，添加说明文字 |
| da157f9 | 2026-07-10 | Terry | docs: 精简 README 并添加 Web 调试器截图 |
| d4753e3 | 2026-07-10 | Terry | fix: debugger_step 流水线保持逐周期推进，仅多周期等待指令完成 |
| 548b29f | 2026-07-10 | Terry | fix: debugger step/continue 在多周期/流水线模式下提前停止 |
| 64a4f0a | 2026-07-10 | Terry | docs: 新增「实现成果」章节 — 76条指令、三级CPU、调试器、Web API、测试覆盖 |
| ae2705d | 2026-07-10 | Terry | docs: 更新 README — 三级CPU控制器、Web调试器、项目结构同步 |
| 6f6534c | 2026-07-10 | Terry | Merge pull request #16 from teeery/lite |
| c2162b6 | 2026-07-10 | Terry | refactor: 项目整理 + CPU多周期/流水线/Web调试器 |
| cbd3f2c | 2026-07-08 | 杨嘉华 | fix: E2E 测试 13/13 全通过 — gen_minimal_elf 加 a7=93 + e2e 适配新 ecall 路径 |
| 280a29a | 2026-07-08 | 香焕聪 | Merge branch 'main' of https://github.com/teeery/RISC-V |
| 8c752f1 | 2026-07-08 | 香焕聪 | docs: 指令解码表更新——M扩展8条全部完成, 72指令汇总 |
| d232f5d | 2026-07-08 | 刘嘉俊 | fix: Makefile添加syscall.c编译支持 |
| e937724 | 2026-07-08 | Terry | Merge pull request #15 from teeery/lite |
| dab59ca | 2026-07-08 | Terry | Merge branch 'main' into lite |
| 5d3bd55 | 2026-07-08 | Terry | Merge pull request #14 from teeery/jiahua |
| 6a69cdf | 2026-07-08 | 杨嘉华 | linux: 实现 syscall 模块 + 集成 ecall 流程 + 6/6 测试通过 |
| da258ed | 2026-07-08 | Terry | merge: origin/main → lite (F 扩展 opcode 修复 + fmaf) |
| 9ea7939 | 2026-07-07 | 香焕聪 | docs: F扩展设计文档修正——opcode 0x43→0x53, FMA fmaf单次舍入+fmt校验, 数据流图更新 |
| af630a2 | 2026-07-07 | 香焕聪 | fix: OP-FP opcode修正为标准0x53, 0x43归位FMADD.S; exec_fma加fmt校验+fmaf单次舍入 |
| 0aa9265 | 2026-07-03 | Terry | test: M+F 扩展单元测试 + decode.c 补全 F 扩展 opcode |
| 7f1f9dd | 2026-07-03 | Terry | refactor: exec_f.c 统一命名 d→dec, a/b/r/c→op_a/op_b/res/op_c |
| 99b9f5f | 2026-07-03 | Terry | merge: origin/main → lite (F 扩展 + Makefile + MMU 等) |
| 429b01d | 2026-07-03 | Terry | feat: M 扩展 8 条指令全部实现 + execute/ 模块统一命名 |
| 5eb042a | 2026-07-03 | Terry | Merge pull request #13 from teeery/xhc |
| 1d0f09d | 2026-07-03 | 香焕聪 | feat: F扩展26条指令完成——FLW/FSW/OP-FP(20条)+FMA(4条) |
| 304a826 | 2026-07-03 | 杨嘉华 | docs: 新增 Linux syscall 模块设计文档（三层结构） |
| 0dd9618 | 2026-07-03 | Terry | Merge pull request #12 from teeery/lite |
| cdd88f5 | 2026-07-03 | Terry | feat: syscall 处理 + execute/ 模块拆分 + E2E 测试 + 第二阶段规划 |
| 4f39395 | 2026-07-03 | 刘嘉俊 | feat: 添加Makefile — gcc -std=c11 -Wall -g -O0 |
| ce08979 | 2026-07-02 | 香焕聪 | Merge branch 'main' of https://github.com/teeery/RISC-V |
| a7fb7e2 | 2026-07-02 | 杨嘉华 | loader: 新增 Section Header 解析（elf_section.c）——不影响现有加载流程 |
| 3d7ea38 | 2026-07-02 | 香焕聪 | feat: mmu.h增加mmu_destroy+页表管理器; mmu_translate Sv32第二级通过pt_mgr查找; mmu_map_page完善PTE写入逻辑 |
| ec8e6c6 | 2026-07-02 | 杨嘉华 | test: loader 测试移入 src/test/loader/（对齐项目结构） |
| 1f0348b | 2026-07-02 | Terry | Merge pull request #11 from teeery/lite |
| 39143b7 | 2026-07-02 | Terry | feat: execute.c 完成全部 RV32I 指令执行 + CPU 单元测试 |
| 5177df9 | 2026-07-02 | Terry | Merge pull request #10 from teeery/lite |
| d2e66fd | 2026-07-02 | Terry | fix: 修复 DecodedInstr 匿名struct与前置声明类型冲突 |
| 2c96d3a | 2026-07-02 | Terry | 修改decod的结构体 |
| 5fb5a8b | 2026-07-02 | 杨嘉华 | docs: 共同部分 + README + memory 设计文档栈地址修正为 0x07F00000 |
| 0bd204e | 2026-07-02 | 杨嘉华 | docs: 栈地址从 0xC0000000 改为 0x07F00000（恒等映射可用，128MB 物理内存内） |
| 682e59b | 2026-07-02 | 杨嘉华 | loader: 栈地址调整到 128MB 物理内存内 + L2 加载测试通过 |
| fc9b157 | 2026-07-02 | Terry | Merge pull request #9 from teeery/fix |
| 6eca0de | 2026-07-02 | 香焕聪 | fix: ExceptionType统一使用INST缩写 |
| f51bfc9 | 2026-07-02 | Terry | fix(simulator): 统一缩写命名规范并修复编译错误 |
| b482823 | 2026-07-02 | Terry | Merge main into lite — sync with latest main |
| 4be8212 | 2026-07-02 | Terry | feat: add cpu.c and decode.c |
| cd04106 | 2026-07-01 | Terry | refactor: 统一缩写 insn → instr |
| a2cc369 | 2026-07-01 | Terry | Merge pull request #8 from teeery/jiahua |
| 2308bf9 | 2026-07-01 | Terry | Merge branch 'main' into jiahua |
| 8fd7bb1 | 2026-07-01 | Terry | Merge pull request #7 from teeery/xhc |
| b67b50f | 2026-07-01 | Terry | Merge pull request #6 from teeery/lite |
| 2f46d4c | 2026-07-01 | 杨嘉华 | Merge branch 'main' into jiahua |
| 6551b27 | 2026-07-01 | 杨嘉华 | fix: 修复 loader 全部爆红 — 补 stdio.h + 补 ELF 常量 + 消 unused 警告 |
| 27dbea0 | 2026-07-01 | 杨嘉华 | fix: 补上 jiahua 分支缺失的合约头文件 types.h / memory.h / mmu.h |
| ca3a104 | 2026-07-01 | 杨嘉华 | loader: 完成实现 + L1 校验测试 + ELF 生成器 |
| b32549f | 2026-07-01 | 香焕聪 | fix: zero-warning编译——suppress未使用参数，memory.c补PAGE_SIZE依赖，mmu_map_page清理死代码 |
| 60a1e6b | 2026-07-01 | Terry | refactor: 精简 types.h，消除重复定义 |
| 11b3a00 | 2026-07-01 | Terry | refactor: 整理头文件，消除重复定义，收敛公共类型到types.h |
| ac96ea3 | 2026-07-01 | 刘嘉俊 | feat: 添加simulator.c胶水层和main.c入口点 |
| b1fe48f | 2026-07-01 | 香焕聪 | feat: 添加memory.c/mmu.c实现，删除loader重复头文件；mmu.h调整include顺序 |
| 2806019 | 2026-07-01 | 刘嘉俊 | fix: simulator.h添加cpu/cpu.h依赖,适配CPU定义从types.h移除 |
| 8ee06bf | 2026-07-01 | Terry | Merge branch 'main' of github.com:teeery/RISC-V |
| 14d51aa | 2026-07-01 | Terry | 修改头文件中重复的定义 |
| 251b089 | 2026-07-01 | 杨嘉华 | chore: 删除 loader 目录下重复的 memory.h 和 types.h |
| 99d2446 | 2026-07-01 | 刘嘉俊 | feat: 创建simulator.h, 修复debugger代码include引用 |
| bca9a1c | 2026-07-01 | 刘嘉俊 | feat: 添加Debugger模块源码 (debugger.h + debugger.c + breakpoint.c) |
| a027de6 | 2026-07-01 | Terry | Merge branch 'main' of github.com:teeery/RISC-V |
| d2d8b48 | 2026-07-01 | Terry | CPU头文件定义 |
| 4c32751 | 2026-07-01 | 刘嘉俊 | docs: 完善3.1.1节——完整类型依赖链、Breakpoint/Simulator字段操作说明 |
| 666b02f | 2026-07-01 | 杨嘉华 | loader: 对齐并行开发方案 — 合约头文件路径 + 测试策略 |
| 5e2ca8e | 2026-07-01 | 香焕聪 | Merge branch 'xhc' |
| c9f2961 | 2026-07-01 | 香焕聪 | refactor: 将memory.h和mmu.h移入src/include/memory/子目录，对齐模块化文件组织 |
| 46e3ed5 | 2026-07-01 | 香焕聪 | Merge branch 'main' of https://github.com/teeery/RISC-V |
| a31969b | 2026-07-01 | 香焕聪 | feat: 添加公共头文件 types.h/memory.h/mmu.h，对齐讨论结论 |
| 9ebb0c9 | 2026-07-01 | 杨嘉华 | Merge remote main: 团队讨论结论 + loader 设计文档冲突解决 |
| c25e830 | 2026-07-01 | Terry | docs: 新增并行开发与验收方案，更新设计文档 |
| 98007a3 | 2026-07-01 | 刘嘉俊 | docs: 完善3.1.1节——完整类型依赖链、Breakpoint/Simulator字段操作说明 |
| 4671d80 | 2026-07-01 | Terry | Merge branch 'main' of github.com:teeery/RISC-V |
| a91f8b6 | 2026-07-01 | Terry | 更新讨论清单 |
| 86fa02d | 2026-07-01 | 香焕聪 | merge: 合并main分支，解决冲突——保留xhc侧最新讨论结论 |
| 6542275 | 2026-07-01 | 香焕聪 | docs: memory文档对齐讨论结论——返回值bool、异常ExceptionType*exc、批量读写接口 |
| 4fe84c1 | 2026-07-01 | 刘嘉俊 | docs: 对齐栈地址(0xBFFC0000)与Memory/CPU设计文档保持一致 |
| ff3abf8 | 2026-07-01 | Terry | docs: CPU与Memory文档全面对齐 |
| 2949a04 | 2026-07-01 | 刘嘉俊 | Merge branch 'jiajun' — Debugger设计文档 |
| 650d29b | 2026-07-01 | 杨嘉华 | loader: 对齐焕聪 memory 设计接口 |
| d3562f1 | 2026-07-01 | 刘嘉俊 | docs: 对齐Debugger设计文档至团队讨论结论 |
| 31b5b66 | 2026-07-01 | Terry | 修改设计文档与Memory对其 |
| 0e9dc75 | 2026-07-01 | 杨嘉华 | Merge branch 'main' of https://github.com/teeery/RISC-V into jiahua |
| 9f82cbe | 2026-07-01 | Terry | docs: 添加公共类型定义，完善CPU设计文档 |
| 087a91d | 2026-07-01 | 杨嘉华 | loader: 迁移到 src/src/loader/，更新设计文档路径和 VSCode 配置 |
| 271aca5 | 2026-07-01 | Terry | docs: 添加共同部分的缩写约定和团队讨论清单 |
| f2a09e8 | 2026-07-01 | 刘嘉俊 | Merge pull request #4 from teeery/jiajun |
| bce9ea5 | 2026-07-01 | 杨嘉华 | chore: 添加 VSCode C/C++ include 路径配置，解决头文件爆红 |
| 80e0e0b | 2026-07-01 | 刘嘉俊 | docs: 添加嘉俊的Debugger设计文档 |
| 84058b4 | 2026-07-01 | Terry | chore: add .gitkeep placeholders for empty src directories |
| d72b620 | 2026-06-30 | Terry | Merge pull request #2 from teeery/lite |
| 3ece62f | 2026-06-30 | Terry | Merge pull request #1 from teeery/jiahua |
| e338f83 | 2026-06-30 | 香焕聪 | Merge pull request #3 from teeery/xhc |
| 1f71661 | 2026-06-30 | Terry | docs: 添加李特的CPU设计文档与分阶段实现方案 |
| 0dfe1a9 | 2026-06-30 | 香焕聪 | docs: 添加焕聪 memory 模块设计文档 |
| 2021766 | 2026-06-30 | 杨嘉华 | docs: 修正设计文档中的相对路径（文档移入嘉华目录后多了一层） |
| 2773b8b | 2026-06-30 | 杨嘉华 | docs: 将 loader 设计文档移入嘉华目录 |
| 9050958 | 2026-06-30 | 杨嘉华 | Merge remote-tracking branch 'origin/main' into jiahua |
| 814fbb4 | 2026-06-30 | Terry | docs: 添加嘉俊、嘉华、焕聪的设计文档目录占位 |
| 057ff7c | 2026-06-30 | 杨嘉华 | loader: 拆分为模块化结构 + 设计文档 |
| 13ced7e | 2026-06-30 | Terry | 完善设计文档README，补充模块定位与交互关系 |
| 90eff55 | 2026-06-30 | Terry | 设计文档按「是什么→做什么→怎么做」三层结构重写，模板放入设计文档目录 |
| a078a8c | 2026-06-30 | Terry | 完善根目录 README，移除多余的子目录占位 readme |
| ad8669b | 2026-06-29 | Terry | 重构项目结构，添加完整文档说明 |
| 2c0072c | 2026-06-29 | 香焕聪 | docs: update README |
| f495d12 | 2026-06-29 | 香焕聪 | docs: simplify README |
| 44d4dd8 | 2026-06-29 | 香焕聪 | merge: resolve README.md conflict, keep local version |
| b88f06d | 2026-06-29 | 香焕聪 | feat: RISC-V RV32IM simulator & debugger - initial project structure |
| 18061fe | 2026-06-29 | Terry | first commit |
```

## 项目文件变更总览

```
 .gitignore                                         |   22 +
 .vscode/c_cpp_properties.json                      |   20 +
 Makefile                                           |  277 +++
 README.md                                          |  169 ++
 docs/tools/add_overlay.py                          |  141 ++
 docs/tools/datapath_editor.html                    |  461 +++++
 docs/tools/embed_svg.py                            |   31 +
 src/include/cpu/controller/controller_internal.h   |   37 +
 src/include/cpu/cpu.h                              |   90 +
 src/include/cpu/datapath/alu.h                     |  106 ++
 src/include/cpu/decode.h                           |   72 +
 src/include/cpu/exec_internal.h                    |   62 +
 src/include/cpu/execute.h                          |   63 +
 src/include/debugger/debugger.h                    |   47 +
 src/include/debugger/web_server.h                  |   35 +
 src/include/linux/syscall.h                        |  114 ++
 src/include/loader/elf_loader.h                    |  666 +++++++
 src/include/memory/memory.h                        |   88 +
 src/include/memory/mmu.h                           |  154 ++
 src/include/simulator.h                            |  272 +++
 src/include/types.h                                |  106 ++
 src/src/cpu/controller/multi_cycle.c               |  712 ++++++++
 src/src/cpu/controller/pipeline.c                  |  759 ++++++++
 src/src/cpu/controller/single_cycle.c              |  207 +++
 src/src/cpu/cpu.c                                  |   44 +
 src/src/cpu/datapath/alu.c                         |  183 ++
 src/src/cpu/decode.c                               |  523 ++++++
 src/src/cpu/execute/exec_f.c                       |  315 ++++
 src/src/cpu/execute/exec_m.c                       |  104 ++
 src/src/cpu/execute/exec_rv32i.c                   |  305 ++++
 src/src/cpu/execute/execute.c                      |  146 ++
 src/src/debugger/breakpoint.c                      |  141 ++
 src/src/debugger/datapath_pipeline_dynamic.svg     |   76 +
 src/src/debugger/debugger.c                        |  570 ++++++
 src/src/debugger/debugger_internal.h               |   30 +
 src/src/debugger/web_server.c                      | 1875 ++++++++++++++++++++
 src/src/linux/syscall.c                            |  160 ++
 src/src/loader/elf_load.c                          |  183 ++
 src/src/loader/elf_section.c                       |  122 ++
 src/src/loader/elf_segment.c                       |  184 ++
 src/src/loader/elf_stack.c                         |  113 ++
 src/src/loader/elf_validate.c                      |  103 ++
 src/src/loader/loader_internal.h                   |  143 ++
 src/src/main.c                                     |  126 ++
 src/src/memory/memory.c                            |  236 +++
 src/src/memory/mmu.c                               |  418 +++++
 src/src/simulator.c                                |  269 +++
 src/test/cpu/decode_test.c                         |  159 ++
 src/test/cpu/execute_test.c                        | 1122 ++++++++++++
 src/test/cpu/f_test.c                              |  392 ++++
 src/test/cpu/m_test.c                              |  326 ++++
 src/test/cpu/multi_cycle_test.c                    |  113 ++
 src/test/cpu/pipeline_test.c                       |  172 ++
 src/test/debugger/debugger_test.c                  |  818 +++++++++
 src/test/e2e/e2e_test.c                            |  164 ++
 src/test/e2e/gen_hello_elf.c                       |  115 ++
 src/test/e2e/hello.elf                             |  Bin 0 -> 136 bytes
 src/test/linux/test_syscall.c                      |  263 +++
 src/test/loader/gen_minimal_elf.c                  |  126 ++
 src/test/loader/minimal.elf                        |  Bin 0 -> 96 bytes
 src/test/loader/test_load.c                        |  119 ++
 src/test/loader/test_validate.c                    |  171 ++
 docs/                                              |  设计文档与提交材料
 137 files changed, 27,968 insertions(+)
```
