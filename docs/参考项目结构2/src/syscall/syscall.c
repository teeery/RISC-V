/* ============================================================================
 * syscall.c — 系统调用实现
 * ============================================================================
 *
 * 实现 syscall_handler(Simulator *sim)：
 *
 *   void syscall_handler(Simulator *sim) {
 *       uint32_t nr = sim->cpu.regs[17];  // a7 = syscall number
 *       uint32_t *r = sim->cpu.regs;
 *
 *       switch (nr) {
 *
 *       case SYS_exit:  // 93
 *           sim->cpu.running = false;
 *           printf("exit(%d)\n", r[10]);  // a0 = exit code
 *           break;
 *
 *       case SYS_write: // 64
 *           {
 *               int fd = r[10];           // a0
 *               uint32_t buf_addr = r[11]; // a1
 *               size_t count = r[12];     // a2
 *
 *               // 从虚拟内存读出数据
 *               uint8_t *buf = malloc(count + 1);
 *               mmu_read(&sim->mmu, buf_addr, buf, count);
 *               buf[count] = '\0';
 *
 *               if (fd == 1 || fd == 2) {
 *                   // 写到宿主机 stdout/stderr
 *                   fwrite(buf, 1, count, fd == 1 ? stdout : stderr);
 *                   fflush(fd == 1 ? stdout : stderr);
 *               }
 *               r[10] = count;  // 返回写入字节数
 *               free(buf);
 *           }
 *           break;
 *
 *       case SYS_read:  // 63
 *           {
 *               int fd = r[10];
 *               uint32_t buf_addr = r[11];
 *               size_t count = r[12];
 *
 *               if (fd == 0) {
 *                   // 从宿主机 stdin 读取
 *                   uint8_t *buf = malloc(count);
 *                   ssize_t n = read(0, buf, count);
 *                   if (n > 0) {
 *                       mmu_write(&sim->mmu, buf_addr, buf, n);
 *                   }
 *                   r[10] = (n >= 0) ? n : 0;
 *                   free(buf);
 *               } else {
 *                   r[10] = 0;  // 不支持的文件描述符
 *               }
 *           }
 *           break;
 *
 *       case SYS_brk:   // 214
 *           {
 *               uint32_t new_brk = r[10];
 *               if (new_brk == 0) {
 *                   r[10] = sim->brk_start;  // 返回当前 brk
 *               } else {
 *                   // 简化：直接记录新 brk，不真正扩展堆
 *                   sim->brk_start = new_brk;
 *                   r[10] = new_brk;
 *               }
 *           }
 *           break;
 *
 *       default:
 *           fprintf(stderr, "Unknown syscall %d at PC=0x%08x\n",
 *                   nr, sim->cpu.pc);
 *           r[10] = -1;  // 返回错误
 *           break;
 *       }
 *   }
 */

#include "syscall.h"

/* ---- 在这里实现 syscall_handler ---- */
