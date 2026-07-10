/* systub.h — RISC-V 模拟器最小系统调用封装
 *
 * 模拟器支持 4 个 Linux 兼容的 syscall：
 *   write(64), read(63), exit(93), brk(214)
 *
 * 用法：#include "systub.h"，然后直接调用 sys_write/sys_exit 等。
 * 编译时需要 -nostdlib -ffreestanding，入口点为 _start()。
 */

#ifndef SYSTUB_H
#define SYSTUB_H

/* ---- syscall 编号 ---- */
#define SYS_read   63
#define SYS_write  64
#define SYS_exit   93
#define SYS_brk    214

/* ---- write(fd, buf, len) ---- */
static inline long sys_write(int fd, const char *buf, long len)
{
    long ret;
    asm volatile(
        "mv a0, %1\n\t"
        "mv a1, %2\n\t"
        "mv a2, %3\n\t"
        "li a7, 64\n\t"
        "ecall\n\t"
        "mv %0, a0"
        : "=r"(ret) : "r"((long)fd), "r"(buf), "r"(len)
        : "a0", "a1", "a2", "a7"
    );
    return ret;
}

/* ---- exit(code) ---- */
static inline void sys_exit(int code)
{
    asm volatile(
        "mv a0, %0\n\t"
        "li a7, 93\n\t"
        "ecall"
        : : "r"((long)code) : "a0", "a7"
    );
    __builtin_unreachable();
}

/* ---- 打印字符串 ---- */
static void print(const char *s)
{
    long len = 0;
    while (s[len]) len++;
    sys_write(1, s, len);
}

/* ---- 整数转字符串（简易 itoa）---- */
static char *itoa(int n, char *buf)
{
    int i = 0, neg = 0;
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return buf; }
    if (n < 0) { neg = 1; n = -n; }
    while (n > 0) {
        buf[i++] = (char)((n % 10) + '0');
        n /= 10;
    }
    if (neg) buf[i++] = '-';
    buf[i] = '\0';
    /* 反转 */
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = t;
    }
    return buf;
}

#endif /* SYSTUB_H */
