/* ============================================================================
 * syscall.h — Linux 系统调用接口（合约头文件）
 * ============================================================================
 *
 * ─── 定位 ──────────────────────────────────────────────────
 *
 *   本模块模拟 RISC-V Linux ABI 中最常用的 4 个系统调用：write / read /
 *   exit / brk。当 CPU 执行 ecall 指令时，cpu_execute() 调用本模块的
 *   syscall_handler() 完成具体逻辑。
 *
 * ─── RISC-V syscall 调用约定 ──────────────────────────────
 *
 *   a7 (x17) = 系统调用号（SYS_*）
 *   a0 (x10) = 参数 1 / 返回值
 *   a1 (x11) = 参数 2
 *   a2 (x12) = 参数 3
 *   触发方式：ecall 指令（CPU 侧检测并分发到此模块）
 *
 * ─── 依赖 ─────────────────────────────────────────────────
 *
 *   simulator.h → cpu.h + memory/mmu.h → types.h
 *   syscall_handler 通过 Simulator* 访问 CPU 寄存器和 MMU。
 *
 * ─── 对齐的团队结论 ──────────────────────────────────────
 *
 *   结论 5（syscall 归属）：放在 CPU 执行阶段，cpu_execute()
 *   的 ecall case 调用 syscall_handler(sim)。本模块只负责
 *   四个 syscall 的业务逻辑，不参与指令译码或分发。
 *
 *   结论 2（Memory 接口）：CPU 只调 mmu_* 层，write/read
 *   通过 mmu_read_8 / mmu_write_8 逐字节访问用户内存。
 *   brk 通过 mmu_brk 包装间接调用 mem_brk。
 * ============================================================================
 */

#ifndef SYSCALL_H
#define SYSCALL_H

/* ── 公共依赖 ───────────────────────────────────────────────
 * simulator.h 已包含：types.h, cpu/cpu.h, memory/memory.h,
 *                     memory/mmu.h, cpu/decode.h
 * 所以只需 include 它一个即可获得所有需要的类型和接口。
 * ─────────────────────────────────────────────────────────── */

#include "simulator.h"

/* ═══════════════════════════════════════════════════════════════
 * 第 1 部分：RISC-V Linux 系统调用号
 * ═══════════════════════════════════════════════════════════════
 *
 * 值来自 RISC-V Linux ABI（arch/riscv/include/uapi/asm/unistd.h）。
 * 目前仅实现模拟器需要的最小集合。
 */

#define SYS_exit    93       // 退出进程（int status）
#define SYS_read    63       // 读文件（fd, buf*, len）→ 返回实际读取字节数
#define SYS_write   64       // 写文件（fd, buf*, len）→ 返回实际写入字节数
#define SYS_open     1024    // 打开文件（path*, flags, mode）→ 返回 fd（保留，未实现）
#define SYS_close   57       // 关闭文件（fd）→ 0（保留，未实现）
#define SYS_brk     214      // 调整堆边界（addr）→ 返回新 brk 地址

/* ── 特殊文件描述符 ────────────────────────────────────────
 *
 * 模拟器没有虚拟文件系统，只支持以下三个宿主标准流：
 *   0 = stdin  → 宿主标准输入
 *   1 = stdout → 宿主标准输出
 *   2 = stderr → 宿主标准错误
 * 其他 fd 值调用 write/read 将返回 -1。
 * ─────────────────────────────────────────────────────────── */

#define STDIN_FILENO   0
#define STDOUT_FILENO  1
#define STDERR_FILENO  2

/* ═══════════════════════════════════════════════════════════════
 * 第 2 部分：对外接口
 * ═══════════════════════════════════════════════════════════════ */

/*
 * syscall_handler — 处理 ecall 触发的系统调用
 *
 * ── 调用方 ───────────────────────────────────────────────
 *
 *   cpu_execute() 的 ecall case（李特）：
 *
 *     case 0x73:  // SYSTEM opcode
 *         if (funct3 == 0 && imm == 0) {
 *             syscall_handler(sim);
 *         }
 *         break;
 *
 * ── 内部流程 ─────────────────────────────────────────────
 *
 *   1. 读 a7 → 确定系统调用号
 *   2. switch (a7):
 *        SYS_exit(93)  → 打印退出码，设 cpu.running = false
 *        SYS_write(64) → 从用户内存逐字节读出 → fwrite 到对应宿主流
 *        SYS_read(63)  → 从宿主 stdin 读 → 逐字节写入用户内存
 *        SYS_brk(214)  → 调 mmu_brk，更新堆边界
 *   3. 返回值写入 a0
 *
 * ── 参数 ─────────────────────────────────────────────────
 *
 *   sim — 模拟器实例（CPU 状态 + MMU + 物理内存）
 *         通过它访问：
 *           sim->cpu.regs[a7]    — 系统调用号
 *           sim->cpu.regs[a0~a2] — 参数
 *           sim->cpu.running     — exit 时置 false
 *           sim->mmu / sim->pmem — 内存读写
 * ─────────────────────────────────────────────────────────── */

void syscall_handler(Simulator *sim);

#endif /* SYSCALL_H */
