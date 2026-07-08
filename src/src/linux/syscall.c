/* ============================================================================
 * syscall.c — Linux 系统调用实现
 * ============================================================================
 *
 * ── 入口 ──────────────────────────────────────────────────
 *
 *   syscall_handler(sim)   ← cpu_execute() 的 ecall case 调用
 *
 * ── 工作流程 ──────────────────────────────────────────────
 *
 *   1. 读 a7 → 系统调用号
 *   2. 读 a0~a2 → 参数
 *   3. switch (a7) → 执行对应逻辑
 *   4. 返回值写入 a0（exit 除外）
 *
 * ── 依赖 ─────────────────────────────────────────────────
 *
 *   - mmu_read_8 / mmu_write_8 → CPU 只调 mmu_* 层（结论 2）
 *   - mmu_brk → 堆管理，MMU 层包装 mem_brk
 *   - fwrite / fread / printf → 宿主标准 I/O
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include "linux/syscall.h"    // SYS_* 常量 + syscall_handler() 声明

/* ===================================================================
 * syscall_handler — 系统调用总入口
 * =================================================================== */
void syscall_handler(Simulator *sim)
{
    /* ── 读取系统调用号 ──
     *
     * RISC-V ABI: a7 (x17) = 系统调用号
     * types.h 中已定义 REG_A7 = 17
     */
    uint32_t a7 = sim->cpu.regs[REG_A7];

    /* ── 读取参数 ──
     *
     * a0~a2 是大多数 syscall 的参数，直接读出来方便各 case 使用。
     * 对于不需要某些参数的系统调用（如 exit 只用 a0），多余的读无害。
     */
    uint32_t a0 = sim->cpu.regs[REG_A0];
    uint32_t a1 = sim->cpu.regs[REG_A1];
    uint32_t a2 = sim->cpu.regs[REG_A2];

    /* ── 当前特权级 ──
     *
     * ecall 从 U-mode 触发时应该设 PRIV_USER，但从 M-mode 触发也不拦。
     * 当前一律用 PRIV_MACHINE（模拟器默认运行在 M-mode）。
     */
    PrivilegeLevel priv = PRIV_MACHINE;
    ExceptionType  exc  = EXC_NONE;

    switch (a7) {

    /* ── exit(status) — 终止程序 ────────────────────── */
    case SYS_exit: {
        /* 打印退出信息（调试用） */
        /*
        printf("[syscall] exit(%u)\n", a0);
        */
        sim->cpu.running = false;
        break;
    }

    /* ── write(fd, buf, len) — 向文件写数据 ─────────── */
    case SYS_write: {
        uint32_t fd  = a0;                 // 文件描述符
        uint32_t buf_vaddr = a1;           // 缓冲区虚拟地址
        uint32_t len = a2;                 // 要写入的字节数

        /* 只支持 stdout(1) 和 stderr(2) */
        if (fd != STDOUT_FILENO && fd != STDERR_FILENO) {
            sim->cpu.regs[REG_A0] = (uint32_t)-1;
            break;
        }

        FILE *out = (fd == STDOUT_FILENO) ? stdout : stderr;

        /* 逐字节从虚拟内存读出并写入宿主流
         *
         * 为什么逐字节而不是批量？
         *   mmu_read() 批量接口已存在，但这里用逐字节方式更简单、
         *   每个字节单独做异常检查。批量接口可后续优化。
         */
        uint32_t i;
        for (i = 0; i < len; i++) {
            uint8_t byte;
            if (!mmu_read_8(&sim->mmu, &sim->pmem,
                            buf_vaddr + i, &byte,
                            priv, &exc)) {
                /* 访问失败（地址越界 / 权限不足），返回已写入字节数 */
                sim->cpu.regs[REG_A0] = (int32_t)-1;
                return;  // 直接返回，不继续执行后续 case
            }
            fputc(byte, out);
        }
        fflush(out);                        // 立即刷新，保证调试输出实时可见

        /* 成功：返回实际写入字节数 */
        sim->cpu.regs[REG_A0] = i;
        break;
    }

    /* ── read(fd, buf, len) — 从文件读数据 ──────────── */
    case SYS_read: {
        uint32_t fd  = a0;
        uint32_t buf_vaddr = a1;
        uint32_t len = a2;

        /* 只支持 stdin(0) */
        if (fd != STDIN_FILENO) {
            sim->cpu.regs[REG_A0] = (uint32_t)-1;
            break;
        }

        /* 逐字节从宿主 stdin 读入并写入虚拟内存 */
        uint32_t i;
        for (i = 0; i < len; i++) {
            int ch = fgetc(stdin);
            if (ch == EOF) break;           // EOF 或输入结束

            if (!mmu_write_8(&sim->mmu, &sim->pmem,
                             buf_vaddr + i, (uint8_t)ch,
                             priv, &exc)) {
                /* 写入失败，返回已读取字节数 */
                sim->cpu.regs[REG_A0] = i;
                return;  // 直接返回，不继续执行后续 case
            }
        }

        sim->cpu.regs[REG_A0] = i;          // 实际读取字节数
        break;
    }

    /* ── brk(new_brk) — 调整堆边界 ──────────────────── */
    case SYS_brk: {
        uint32_t new_brk = a0;              // 新堆边界（0 表示查询）

        /*
         * mmu_brk 是 MMU 层对 mem_brk 的包装（结论 2：CPU 只调 mmu_*）
         * brk(0)  → 返回当前 brk 值（不改变堆）
         * brk(n)  → 将堆边界设为 n，返回新边界
         */
        uint32_t result = mmu_brk(&sim->mmu, &sim->pmem, new_brk);
        sim->cpu.regs[REG_A0] = result;
        break;
    }

    /* ── 未知系统调用 ───────────────────────────────── */
    default:
        fprintf(stderr, "[syscall] unknown syscall number: %u\n", a7);
        sim->cpu.regs[REG_A0] = (uint32_t)-1;
        sim->cpu.running = false;   // 未知 syscall → 停机（旧 cpu_trap 行为）
        break;
    }
}
