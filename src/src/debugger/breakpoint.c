#include "debugger/debugger.h"
#include "simulator.h"       // Simulator, Breakpoint
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/* ================================================================
 * breakpoint.c — 软件断点管理
 *
 * 断点方案: ebreak 替换 (讨论结论 6)
 *   设断点: mmu_read_32 保存原始指令 → mmu_write_32 写入 0x00100073
 *   命中:   CPU 执行到 ebreak → 遍历 sim->breakpoints[] 找到匹配项
 *   恢复:   mmu_write_32 写回原始指令
 *
 * 断点数组: sim->breakpoints[] 动态分配，下标=编号
 *   初始容量: 16（由 sim_init 初始化）
 *   扩容:     bp_count >= bp_capacity 时 realloc ×2
 *   删除:     末尾元素移到被删位置 (O(1), 不保留空洞)
 * ================================================================
 */

#define EBREAK_INSTR  0x00100073

int debugger_add_breakpoint(struct Simulator *sim, uint32_t addr)
{
    /* ① 地址 4 字节对齐检查 */
    if (addr & 0x3) {
        fprintf(stderr, "Error: Breakpoint address 0x%08x is not word-aligned\n", addr);
        return -1;
    }

    /* ② 去重检查 */
    for (int i = 0; i < sim->bp_count; i++) {
        if (sim->breakpoints[i].addr == addr) {
            fprintf(stderr, "Error: Breakpoint already exists at 0x%08x (index %d)\n",
                    addr, i);
            return -1;
        }
    }

    /* ③ 容量检查 & 扩容 */
    if (sim->bp_count >= sim->bp_capacity) {
        int new_cap = sim->bp_capacity * 2;
        Breakpoint *new_bp = realloc(sim->breakpoints,
                                      (size_t)new_cap * sizeof(Breakpoint));
        if (!new_bp) {
            fprintf(stderr, "Error: Failed to allocate memory for breakpoints\n");
            return -1;
        }
        sim->breakpoints  = new_bp;
        sim->bp_capacity  = new_cap;
    }

    /* ④ 保存原始指令（通过 MMU 读虚拟地址） */
    ExceptionType exc = EXC_NONE;
    uint32_t orig_instr;
    if (!mmu_read_32(&sim->mmu, &sim->pmem, addr, &orig_instr,
                     sim->cpu.priv, &exc)) {
        fprintf(stderr, "Error: Cannot read memory at 0x%08x (exc=%d)\n",
                addr, exc);
        return -1;
    }

    /* ⑤ 写入 EBREAK */
    if (!mmu_write_32(&sim->mmu, &sim->pmem, addr, EBREAK_INSTR,
                      sim->cpu.priv, &exc)) {
        fprintf(stderr, "Error: Cannot write EBREAK to 0x%08x (exc=%d)\n",
                addr, exc);
        return -1;
    }

    /* ⑥ 记录断点信息 */
    int index = sim->bp_count;
    sim->breakpoints[index].addr          = addr;
    sim->breakpoints[index].original_instr = orig_instr;
    sim->breakpoints[index].enabled       = true;
    sim->bp_count++;

    printf("Breakpoint %d set at 0x%08x\n", index, addr);
    return index;
}

bool debugger_del_breakpoint(struct Simulator *sim, int index)
{
    if (index < 0 || index >= sim->bp_count) {
        fprintf(stderr, "Error: No breakpoint number %d\n", index);
        return false;
    }

    Breakpoint *bp = &sim->breakpoints[index];

    /* 恢复原始指令 */
    if (bp->enabled) {
        ExceptionType exc = EXC_NONE;
        if (!mmu_write_32(&sim->mmu, &sim->pmem, bp->addr,
                          bp->original_instr, sim->cpu.priv, &exc)) {
            fprintf(stderr, "Warning: Failed to restore instruction at 0x%08x\n",
                    bp->addr);
        }
    }

    printf("Breakpoint %d at 0x%08x deleted\n", index, bp->addr);

    /* 将末尾元素移到被删位置 (O(1)，不保留空洞) */
    if (index < sim->bp_count - 1) {
        sim->breakpoints[index] = sim->breakpoints[sim->bp_count - 1];
    }
    sim->bp_count--;

    return true;
}

void debugger_list_breakpoints(const struct Simulator *sim)
{
    if (sim->bp_count == 0) {
        printf("No breakpoints set.\n");
        return;
    }

    printf("Num\tType\t\tEnb\tAddress\n");
    for (int i = 0; i < sim->bp_count; i++) {
        const Breakpoint *bp = &sim->breakpoints[i];
        printf("%d\tbreakpoint\t%s\t0x%08x\n",
               i,
               bp->enabled ? "y" : "n",
               bp->addr);
    }
}

bool debugger_check_breakpoint(struct Simulator *sim, uint32_t pc)
{
    for (int i = 0; i < sim->bp_count; i++) {
        if (sim->breakpoints[i].enabled && sim->breakpoints[i].addr == pc) {
            return true;
        }
    }
    return false;
}
