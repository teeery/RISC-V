#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ============================================================
 * syscall.c — Linux 系统调用模拟
 *
 * ─── 你需要实现的内容 ─────────────────────────────────────
 *
 * 1. syscall_handler(cpu, pmem, mmu)
 *    从 a7 (x17) 寄存器读取系统调用号，分发到对应的处理函数。
 *    RISC-V 系统调用通过 ECALL 指令触发 (从 U 模式)。
 *
 *    RISC-V Linux 调用约定:
 *    - a7 (x17)  = 系统调用号
 *    - a0 (x10)  = 第 1 参数 / 返回值
 *    - a1 (x11)  = 第 2 参数
 *    - a2 (x12)  = 第 3 参数
 *    - a3 (x13)  = 第 4 参数
 *    - a4 (x14)  = 第 5 参数
 *    - a5 (x15)  = 第 6 参数
 *    - 返回值写入 a0, 错误时返回 -errno
 *
 * 2. 必须实现的系统调用:
 *    SYS_write  (64):  fd=a0, buf=a1, count=a2
 *      将 buf 指向的 count 字节输出到 fd。
 *      fd=1 → stdout, fd=2 → stderr
 *      ★ 需要用 mmu_read 从客户机虚拟地址读取数据
 *      返回实际写入的字节数
 *
 *    SYS_read   (63):  fd=a0, buf=a1, count=a2
 *      从 fd (0=stdin) 读取 count 字节，写入 buf。
 *      ★ 需要用 mmu_write 写入客户机虚拟地址
 *      返回实际读取的字节数 (EOF=0, 错误=-1)
 *
 *    SYS_exit   (93):  status=a0
 *      停止模拟器: cpu->running = false
 *      打印退出信息和返回码
 *
 *    SYS_exit_group (94): 同 SYS_exit
 *      在多线程程序中用于终止所有线程，单核模拟器同 exit
 *
 *    SYS_brk    (214):  addr=a0
 *      调整程序 break (堆边界)
 *      如果 addr==0: 返回当前 brk
 *      如果 addr>0: 扩展或收缩堆 (不能低于初始 brk)
 *      返回新的 brk 地址
 *
 * 3. 可选实现的系统调用 (用于运行更复杂的程序):
 *    SYS_openat (56):  dirfd=a0, pathname=a1, flags=a2, mode=a3
 *      打开文件，返回文件描述符
 *      常用于 fopen/open 的底层实现
 *
 *    SYS_close  (57):  fd=a0
 *      关闭文件描述符
 *
 *    SYS_fstat  (80):  fd=a0, statbuf=a1
 *      获取文件状态 (isatty 等)
 *      ★ 需要构造 struct stat 并写入客户机内存
 *
 * 4. 文件描述符表:
 *    维护一个简单的映射: fd → FILE* 或文件描述符
 *    0=stdin, 1=stdout, 2=stderr 预分配
 *    可选: 支持更多打开的文件
 *
 * 5. 错误处理:
 *    - 不支持的系统调用: printf 警告 + 返回 -ENOSYS (38)
 *    - 无效 fd: 返回 -EBADF (9)
 *    - 无效地址: 返回 -EFAULT (14)
 *
 * ─── 重要细节 ──────────────────────────────────────────────
 * - read/write 的缓冲区地址是客户虚拟地址，
 *   必须通过 mmu_read/write 间接访问，不能直接使用主机指针
 * - 对于 write 系统调用，如果 count 很大，
 *   应该分块读取 (每次一页) 以避免性能问题
 * - brk 的初始值应该从 ELF 加载器获取 (通常是 data 段结束地址)
 * ============================================================
 */

#include "types.h"
#include "memory.h"
#include "mmu.h"
#include "syscall.h"

/* ── 各系统调用实现 ─────────────────────────────────────── */

static uint32_t sys_write(CPUState *cpu, PhysicalMemory *pmem, MMUState *mmu)
{
    uint32_t fd    = cpu->regs[REG_A0];
    uint32_t buf   = cpu->regs[REG_A1];  // 客户机虚拟地址
    uint32_t count = cpu->regs[REG_A2];

    if (fd != 1 && fd != 2) {
        // 非 stdout/stderr → 暂不支持
        return (uint32_t)-9;  // -EBADF
    }

    // 从客户机内存逐字节读取并输出
    // 注意：对于大 buffer，应该分页读取
    uint32_t written = 0;
    while (written < count) {
        uint8_t byte;
        if (!mmu_read_8(mmu, pmem, buf + written, &byte, cpu->priv)) {
            return (uint32_t)-14;  // -EFAULT
        }
        putchar(byte);
        written++;
    }
    fflush(fd == 1 ? stdout : stderr);
    return written;
}

static uint32_t sys_read(CPUState *cpu, PhysicalMemory *pmem, MMUState *mmu)
{
    uint32_t fd    = cpu->regs[REG_A0];
    uint32_t buf   = cpu->regs[REG_A1];  // 客户机虚拟地址
    uint32_t count = cpu->regs[REG_A2];

    if (fd != 0) {
        return (uint32_t)-9;  // -EBADF: 不支持 stdin 以外的输入
    }

    // 从 stdin 逐字节读取
    uint32_t nread = 0;
    while (nread < count) {
        int c = getchar();
        if (c == EOF) break;
        uint8_t byte = (uint8_t)c;
        if (!mmu_write_8(mmu, pmem, buf + nread, byte, cpu->priv)) {
            return (uint32_t)-14;  // -EFAULT
        }
        nread++;
        if (c == '\n') break;  // 行缓冲
    }
    return nread;
}

static uint32_t sys_exit(CPUState *cpu)
{
    uint32_t status = cpu->regs[REG_A0];
    printf("\n[EXIT] Program exited with code %d\n", status);
    cpu->running = false;
    return 0;   // 无返回值 (程序已终止)
}

static uint32_t sys_brk(CPUState *cpu, PhysicalMemory *pmem)
{
    uint32_t new_brk = cpu->regs[REG_A0];

    if (new_brk == 0) {
        return pmem->brk_current;
    }

    // 扩展/收缩堆
    if (new_brk >= pmem->brk_start && new_brk < pmem->size) {
        pmem->brk_current = new_brk;
    }
    return pmem->brk_current;
}

/* ── 主分发函数 ──────────────────────────────────────────── */

bool syscall_handler(CPUState *cpu, PhysicalMemory *pmem, MMUState *mmu)
{
    uint32_t syscall_num = cpu->regs[REG_A7];  // a7 = 系统调用号
    uint32_t ret = 0;

    switch (syscall_num) {
    case SYS_WRITE:
        ret = sys_write(cpu, pmem, mmu);
        break;

    case SYS_READ:
        ret = sys_read(cpu, pmem, mmu);
        break;

    case SYS_EXIT:
    case SYS_EXIT_GROUP:
        ret = sys_exit(cpu);
        break;

    case SYS_BRK:
        ret = sys_brk(cpu, pmem);
        break;

    default:
        printf("[SYSCALL] Unsupported syscall: %d\n", syscall_num);
        printf("          a0=0x%x a1=0x%x a2=0x%x a3=0x%x\n",
               cpu->regs[REG_A0], cpu->regs[REG_A1],
               cpu->regs[REG_A2], cpu->regs[REG_A3]);
        ret = (uint32_t)-38;  // -ENOSYS
        break;
    }

    // 写入返回值到 a0
    cpu->regs[REG_A0] = ret;
    return true;
}
