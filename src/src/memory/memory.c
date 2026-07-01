#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "memory/memory.h"
#include "memory/mmu.h"    // PAGE_SIZE 用于 mem_brk 对齐

/* ============================================================
 * memory.c — 物理内存管理实现
 *
 * 实现 memory.h 中声明的 13 个函数。
 *
 * 设计要点：
 *   - 物理内存用 uint8_t 数组模拟（calloc 分配，默认 128MB）
 *   - 边界检查：addr + size > pmem->size → 返回 false
 *   - 物理层不做权限检查——权限校验是 MMU 层的职责
 *   - 小端序读写（RISC-V 规范，x86 天然兼容）
 *   - 单核模拟，无需线程安全
 * ============================================================
 */

/* ============================================================
 * 初始化 / 销毁
 * ============================================================
 */

void mem_init(PhysicalMemory *pmem, uint32_t size)
{
    pmem->data  = (uint8_t *)calloc(size, 1);
    pmem->size  = size;
    pmem->brk_start   = 0;
    pmem->brk_current = 0;

    /* 初始化区域列表 */
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

/* ============================================================
 * 单次读取 — 小端序
 * ============================================================
 */

bool mem_read_8(PhysicalMemory *pmem, uint32_t addr, uint8_t *val)
{
    if (addr >= pmem->size) return false;
    *val = pmem->data[addr];
    return true;
}

bool mem_read_16(PhysicalMemory *pmem, uint32_t addr, uint16_t *val)
{
    if (addr + 1 >= pmem->size) return false;
    /* 小端序：低地址 = 低字节 */
    *val = pmem->data[addr]
         | ((uint16_t)pmem->data[addr + 1] << 8);
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

/* ============================================================
 * 单次写入 — 小端序
 * ============================================================
 */

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

/* ============================================================
 * 区域映射管理
 * ============================================================
 */

bool mem_map(PhysicalMemory *pmem, uint32_t base, uint32_t size,
             uint8_t flags, const char *name)
{
    /* 动态扩容 */
    if (pmem->region_count >= pmem->region_capacity) {
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

uint32_t mem_find_free(PhysicalMemory *pmem, uint32_t size, uint32_t align)
{
    /* 简单策略：找到所有已映射区域的最大末尾地址，对齐后返回 */
    uint32_t max_end = 0;
    for (int i = 0; i < pmem->region_count; i++) {
        uint32_t end = pmem->regions[i].base + pmem->regions[i].size;
        if (end > max_end) max_end = end;
    }
    /* 对齐 */
    if (align > 0) {
        max_end = (max_end + align - 1) & ~(align - 1);
    }
    return max_end;
}

uint32_t mem_brk(PhysicalMemory *pmem, uint32_t new_brk)
{
    /* 首次调用：以当前 region 末尾作为 brk 起点 */
    if (pmem->brk_start == 0 && pmem->brk_current == 0) {
        uint32_t data_end = mem_find_free(pmem, 0, PAGE_SIZE);
        pmem->brk_start   = data_end;
        pmem->brk_current = data_end;
    }

    /* new_brk == 0：仅查询当前 brk */
    if (new_brk == 0) {
        return pmem->brk_current;
    }

    /* 扩展或收缩堆 */
    if (new_brk >= pmem->brk_start && new_brk < pmem->size) {
        pmem->brk_current = new_brk;
    }
    return pmem->brk_current;
}

/* ============================================================
 * 批量数据加载（Loader 专用）
 * ============================================================
 */

bool mem_load(PhysicalMemory *pmem, uint32_t addr,
              const uint8_t *data, uint32_t size)
{
    if (addr + size > pmem->size) return false;
    memcpy(pmem->data + addr, data, size);
    return true;
}

/* ============================================================
 * 调试 dump
 * ============================================================
 */

void mem_dump(PhysicalMemory *pmem, uint32_t addr, uint32_t len)
{
    for (uint32_t i = 0; i < len; i += 16) {
        printf("%08x: ", addr + i);

        /* hex 字节 */
        for (uint32_t j = 0; j < 16; j++) {
            if (i + j < len) {
                printf("%02x ", pmem->data[addr + i + j]);
            } else {
                printf("   ");
            }
            if (j == 7) printf(" ");
        }

        /* ASCII */
        printf(" ");
        for (uint32_t j = 0; j < 16 && (i + j) < len; j++) {
            uint8_t c = pmem->data[addr + i + j];
            printf("%c", (c >= 32 && c < 127) ? c : '.');
        }
        printf("\n");
    }
}
