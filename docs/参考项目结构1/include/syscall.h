#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"
#include "memory.h"
#include "mmu.h"

/* ============================================================
 * syscall.h — Linux 系统调用模拟
 *
 * 需要编写的内容：
 * 1. syscall_handler()    — 分发系统调用 (根据 a7 寄存器中的调用号)
 * 2. sys_read()           — 从 fd 读取 (stdin=0)，写入 buf 指向的内存
 * 3. sys_write()          — 将 buf 指向的内存输出到 fd (stdout=1, stderr=2)
 * 4. sys_exit()           — 停止模拟器，返回退出码
 * 5. sys_brk()            — 调整程序 break (堆边界)，返回新的 brk
 * 6. sys_openat()         — 打开文件 (可选，用于运行更复杂的程序)
 * 7. sys_close()          — 关闭文件描述符 (可选)
 * 8. sys_fstat()          — 文件状态 (可选)
 *
 * RISC-V Linux 系统调用约定：
 * - 调用号放在 a7 (x17) 寄存器
 * - 参数放在 a0-a5 (x10-x15)
 * - 返回值放在 a0 (x10)
 * - 系统调用指令：ECALL (从 U 模式)
 *
 * RV32 Linux 系统调用号 (部分)：
 *   SYS_exit    = 93
 *   SYS_read    = 63
 *   SYS_write   = 64
 *   SYS_openat  = 56
 *   SYS_close   = 57
 *   SYS_brk     = 214
 *   SYS_fstat   = 80
 *   SYS_exit_group = 94
 *
 * 设计要点：
 * - read/write 需要从客户机虚拟地址读取/写入数据
 *   (通过 mmu_read/write 函数)
 * - brk() 维护堆边界，初始值为 data 段结束地址
 * - 文件描述符表：0=stdin, 1=stdout, 2=stderr, 3+=可选的打开文件
 * - 不支持的 syscall 打印警告并返回 -ENOSYS
 * - exit/exit_group 设置 cpu->running = false
 * ============================================================
 */

/* RISC-V Linux 系统调用号 */
#define SYS_IOGETEVENTS    29
#define SYS_IOSETEVENTS    30
#define SYS_OPENAT         56
#define SYS_CLOSE          57
#define SYS_LSEEK          62
#define SYS_READ           63
#define SYS_WRITE          64
#define SYS_WRITEV         66
#define SYS_READV          67
#define SYS_PREAD64        67
#define SYS_PWRITE64        68
#define SYS_FSTAT          80
#define SYS_EXIT           93
#define SYS_EXIT_GROUP     94
#define SYS_BRK            214

/* 处理系统调用 (由 ECALL 指令触发) */
bool syscall_handler(CPUState *cpu, PhysicalMemory *pmem, MMUState *mmu);

#endif // SYSCALL_H
