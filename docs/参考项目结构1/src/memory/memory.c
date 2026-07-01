#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>


/* ============================================================
 * memory.c — 物理内存管理
 *
 * ─── 你需要实现的内容 ─────────────────────────────────────
 *
 * 1. mem_init(PhysicalMemory *pmem, uint32_t size)
 *    - 分配 size 字节的物理内存 (calloc)
 *    - 初始化 regions 动态数组
 *    - 设置 brk_start / brk_current
 *
 * 2. mem_read_8/16/32(addr, *val) — 小端序读取
 *    - 边界检查 (addr+size <= pmem->size)
 *    - 权限检查: 基于 region_list 的 flags
 *      注意：物理内存层面的读写不强制权限检查，
 *      权限检查在 MMU 层面完成。此处只做边界检查。
 *    - 小端序：低地址 = 低字节
 *
 * 3. mem_write_8/16/32(addr, val) — 小端序写入
 *    - 同上
 *
 * 4. mem_map(pmem, base, size, flags, name)
 *    - 在 regions 数组中添加新条目
 *    - 检查是否与已有区域重叠（或允许重叠以支持多段映射）
 *
 * 5. mem_find_free(pmem, size, align)
 *    - 遍历 regions，找出一段未映射的空间
 *    - 返回满足 size 和 align 的空闲地址
 *    - 简单策略：从当前所有区域末尾之后分配
 *
 * 6. mem_brk(pmem, new_brk)
 *    - 如果 new_brk == 0: 返回当前 brk
 *    - 如果 new_brk > brk_current: 扩展堆
 *    - 如果 new_brk < brk_start: 不允许
 *    - 返回新的 brk 值
 *    堆区域通常从 data 段结束地址开始
 *
 * 7. mem_dump(pmem, addr, len)
 *    - 以十六进制格式打印内存内容
 *    - 标准 hexdump 格式：
 *      00000000: 7f 45 4c 46 01 01 01 00 00 00 00 00 00 00 00 00  .ELF............
 *
 * 8. mem_load(pmem, addr, *data, size)
 *    - 将外部二进制数据直接复制到物理内存指定地址
 *    - 用于 ELF 加载器写入段数据
 *
 * ─── 设计要点 ──────────────────────────────────────────────
 * - 物理内存是线性的字节数组，无虚拟地址概念
 * - 区域列表用于记录已分配空间，帮助 find_free 避免冲突
 * - 单核模拟：无需考虑多核一致性问题
 * - 所有写操作应当直接修改 pmem->data[]
 * ============================================================
 */

#include "types.h"
#include "memory.h"

/* ── 初始化 / 销毁 ───────────────────────────────────────── */
void mem_init(PhysicalMemory *pmem, uint32_t size)
{
    pmem->data  = (uint8_t *)calloc(size, 1);
    pmem->size  = size;
    pmem->brk_start   = 0;
    pmem->brk_current = 0;
    // 初始化动态区域列表
    pmem->region_capacity = 16;
    pmem->region_count    = 0;
    pmem->regions = (MemoryRegion *)calloc(pmem->region_capacity,
                                           sizeof(MemoryRegion));
}

void mem_destroy(PhysicalMemory *pmem)
{
    free(pmem->data);
    free(pmem->regions);
    pmem->data    = NULL;
    pmem->regions = NULL;
    pmem->size    = 0;
}

/* ── 读取 (小端序) ───────────────────────────────────────── */

bool mem_read_8(PhysicalMemory *pmem, uint32_t addr, uint8_t *val)
{
    if (addr >= pmem->size) return false;
    *val = pmem->data[addr];
    return true;
}

bool mem_read_16(PhysicalMemory *pmem, uint32_t addr, uint16_t *val)
{
    if (addr + 1 >= pmem->size) return false;
    // 小端序：低字节在低地址
    *val = pmem->data[addr] | ((uint16_t)pmem->data[addr + 1] << 8);
    return true;
}

bool mem_read_32(PhysicalMemory *pmem, uint32_t addr, uint32_t *val)
{
    if (addr + 3 >= pmem->size) return false;
    *val = pmem->data[addr]
         | ((uint32_t)pmem->data[addr + 1] << 8)
         | ((uint32_t)pmem->data[addr + 2] << 16)
         | ((uint32_t)pmem->data[addr + 3] << 24);
    return true;
}

/* ── 写入 (小端序) ───────────────────────────────────────── */

bool mem_write_8(PhysicalMemory *pmem, uint32_t addr, uint8_t val)
{
    if (addr >= pmem->size) return false;
    pmem->data[addr] = val;
    return true;
}

bool mem_write_16(PhysicalMemory *pmem, uint32_t addr, uint16_t val)
{
    if (addr + 1 >= pmem->size) return false;
    pmem->data[addr]     = (uint8_t)(val & 0xFF);
    pmem->data[addr + 1] = (uint8_t)((val >> 8) & 0xFF);
    return true;
}

bool mem_write_32(PhysicalMemory *pmem, uint32_t addr, uint32_t val)
{
    if (addr + 3 >= pmem->size) return false;
    pmem->data[addr]     = (uint8_t)(val & 0xFF);
    pmem->data[addr + 1] = (uint8_t)((val >> 8) & 0xFF);
    pmem->data[addr + 2] = (uint8_t)((val >> 16) & 0xFF);
    pmem->data[addr + 3] = (uint8_t)((val >> 24) & 0xFF);
    return true;
}

/* ── 区域映射 ────────────────────────────────────────────── */
bool mem_map(PhysicalMemory *pmem, uint32_t base, uint32_t size,
             uint8_t flags, const char *name)
{
    // TODO: 扩展区域列表，动态扩容
    if (pmem->region_count >= pmem->region_capacity) {
        // 扩容
        pmem->region_capacity *= 2;
        pmem->regions = (MemoryRegion *)realloc(
            pmem->regions, pmem->region_capacity * sizeof(MemoryRegion));
    }
    MemoryRegion *r = &pmem->regions[pmem->region_count++];
    r->base  = base;
    r->size  = size;
    r->flags = flags;
    strncpy(r->name, name, sizeof(r->name) - 1);
    r->name[sizeof(r->name) - 1] = '\0';
    return true;
}

/* ── 查找空闲区域 ────────────────────────────────────────── */
uint32_t mem_find_free(PhysicalMemory *pmem, uint32_t size, uint32_t align)
{
    // TODO: 简单策略 — 找到所有区域的最大末尾地址，对齐后返回
    uint32_t max_end = 0;
    for (int i = 0; i < pmem->region_count; i++) {
        uint32_t end = pmem->regions[i].base + pmem->regions[i].size;
        if (end > max_end) max_end = end;
    }
    // 对齐
    if (align > 0) {
        max_end = (max_end + align - 1) & ~(align - 1);
    }
    return max_end;
}

/* ── brk 系统调用 ────────────────────────────────────────── */
uint32_t mem_brk(PhysicalMemory *pmem, uint32_t new_brk)
{
    // TODO: 首次调用时初始化 brk_start/brk_current
    if (new_brk == 0) {
        return pmem->brk_current;
    }
    if (new_brk >= pmem->brk_start && new_brk < pmem->size) {
        pmem->brk_current = new_brk;
    }
    return pmem->brk_current;
}

/* ── 批量加载数据 ────────────────────────────────────────── */
bool mem_load(PhysicalMemory *pmem, uint32_t addr,
              const uint8_t *data, uint32_t size)
{
    if (addr + size > pmem->size) return false;
    memcpy(pmem->data + addr, data, size);
    return true;
}

/* ── Hexdump ──────────────────────────────────────────────── */
void mem_dump(PhysicalMemory *pmem, uint32_t addr, uint32_t len)
{
    for (uint32_t i = 0; i < len; i += 16) {
        printf("%08x: ", addr + i);
        // hex bytes
        for (uint32_t j = 0; j < 16; j++) {
            if (i + j < len) {
                printf("%02x ", pmem->data[addr + i + j]);
            } else {
                printf("   ");
            }
            if (j == 7) printf(" ");
        }
        // ASCII
        printf(" ");
        for (uint32_t j = 0; j < 16 && (i + j) < len; j++) {
            uint8_t c = pmem->data[addr + i + j];
            printf("%c", (c >= 32 && c < 127) ? c : '.');
        }
        printf("\n");
    }
}
