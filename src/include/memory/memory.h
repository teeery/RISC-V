/* ============================================================================
 * memory.h — PhysicalMemory 接口（合约头文件）
 * ============================================================================
 *
 * 对齐：并行开发方案 §0.2
 * 实现者：焕聪
 */

#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

#define MEM_SIZE_DEFAULT    (128 * 1024 * 1024)  // 128 MB

/* 内存区域描述符 */
typedef struct {
    uint32_t base;
    uint32_t size;
    uint8_t  flags;
    char     name[32];
} MemoryRegion;

/* 物理内存管理器 */
typedef struct {
    uint8_t      *data;
    uint32_t      size;
    uint32_t      brk_start;
    uint32_t      brk_current;
    MemoryRegion *regions;
    int           region_count;
    int           region_capacity;
} PhysicalMemory;

/* 初始化 / 销毁 */
void mem_init(PhysicalMemory *pmem, uint32_t size);
void mem_destroy(PhysicalMemory *pmem);

/* 物理内存读写 */
bool mem_read_8 (PhysicalMemory *pmem, uint32_t addr, uint8_t  *val);
bool mem_read_16(PhysicalMemory *pmem, uint32_t addr, uint16_t *val);
bool mem_read_32(PhysicalMemory *pmem, uint32_t addr, uint32_t *val);

bool mem_write_8 (PhysicalMemory *pmem, uint32_t addr, uint8_t  val);
bool mem_write_16(PhysicalMemory *pmem, uint32_t addr, uint16_t val);
bool mem_write_32(PhysicalMemory *pmem, uint32_t addr, uint32_t val);

/* 区域管理 */
bool     mem_map(PhysicalMemory *pmem, uint32_t base, uint32_t size,
                 uint8_t flags, const char *name);
uint32_t mem_find_free(PhysicalMemory *pmem, uint32_t size, uint32_t align);
uint32_t mem_brk(PhysicalMemory *pmem, uint32_t new_brk);

/* 批量加载（Loader 用）*/
bool mem_load(PhysicalMemory *pmem, uint32_t addr, const uint8_t *data,
              uint32_t size);

/* 调试 dump */
void mem_dump(PhysicalMemory *pmem, uint32_t addr, uint32_t len);

#endif /* MEMORY_H */
