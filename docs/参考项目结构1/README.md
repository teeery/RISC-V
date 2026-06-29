6/29 2023414290430香焕聪--------------------------------运用AI辅助完成代码目录架构
# RISC-V

面向 RISC-V 的模拟器与调试器，支持 RV32I 基础整数指令集及 M 扩展（乘除法），能够加载 ELF32 可执行文件并运行。实现了虚拟内存映射（Sv32 两级页表）、常用 Linux 系统调用模拟（write / read / exit / brk），以及一个支持断点、单步执行、寄存器/内存查看的交互式调试器。

## 构建

```bash
make          # 编译模拟器
make clean    # 清理
```

## 运行

```bash
./riscv-sim test/hello.elf      # 直接运行
./riscv-sim -s test/hello.elf   # 调试模式
./riscv-sim -t -s test/hello.elf # 调试 + 指令跟踪
```
