/* ============================================================================
 * syscall.h / syscall.c — Linux 系统调用模拟
 * ============================================================================
 *
 * 当 RISC-V 程序执行 ecall 指令时，模拟器截获并模拟 Linux 系统调用的行为。
 *
 * ---- RISC-V 系统调用约定 ----
 *
 *   触发指令: ecall
 *   系统调用号: a7 (x17)
 *   参数:  a0 (x10), a1 (x11), a2 (x12), a3 (x13), a4 (x14), a5 (x15)
 *   返回值: a0 (x10)
 *
 * ---- 需要实现的系统调用 ----
 *
 *   SYS_exit  (93):
 *     - 停止模拟器运行
 *     - 退出码 = a0
 *
 *   SYS_write (64):
 *     - write(fd, buf, count)
 *     - fd=1 → stdout, fd=2 → stderr
 *     - 从模拟器虚拟内存地址 buf 读取 count 字节
 *     - 在宿主机上调用 fwrite 输出
 *     - 返回值: 成功写入的字节数
 *
 *   SYS_read  (63):
 *     - read(fd, buf, count)
 *     - fd=0 → stdin
 *     - 从宿主机读取数据写入模拟器虚拟内存
 *     - 返回值: 成功读取的字节数
 *
 *   SYS_brk   (214):
 *     - brk(addr)
 *     - 如果 addr == 0: 返回当前 brk（程序初始堆顶）
 *     - 如果 addr != 0: 设置新 brk，扩展/收缩堆区域
 *     - 返回值: 新的 brk 地址
 *     - 简化实现: 维护一个 brk_start 变量，不需要真正分配堆，记录地址即可
 *
 * ---- 选做扩展（让更多程序能跑）----
 *
 *   SYS_openat  (56):  打开文件
 *   SYS_close   (57):  关闭文件
 *   SYS_fstat   (80):  获取文件信息
 *   SYS_lseek   (62):  文件定位
 *   SYS_mmap    (222): 内存映射（向 mmu 添加新 region）
 *   SYS_munmap  (215): 取消内存映射（从 mmu 删除 region）
 *   SYS_uname   (160): 返回系统信息（很多程序启动时调用，返回假的即可）
 *   SYS_gettimeofday (169): 获取时间
 *
 * ---- 函数声明 ----
 *
 *   // 根据当前 CPU 寄存器状态处理系统调用
 *   // 需要读取 a7 (x17) 确定 syscall 号
 *   // 需要读取 a0~a5 (x10~x15) 作为参数
 *   // 结果写入 a0 (x10)
 *   void syscall_handler(Simulator *sim);
 */

#ifndef SYSCALL_H
#define SYSCALL_H

#include "common.h"
// #include "../simulator.h"

/* ---- 在这里声明 syscall_handler ---- */

#endif /* SYSCALL_H */
