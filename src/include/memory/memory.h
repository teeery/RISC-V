#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

/* ============================================================
 * memory.h — 物理内存管理接口（PhysicalMemory 层）
 *
 * 职责：
 *   - 在宿主机堆上模拟 RISC-V 物理内存（字节数组）
 *   - 维护已映射区域列表（MemoryRegion[]）
 *   - 字节级小端序读写 + 边界检查
 *   - 区域映射管理、空闲地址查找、brk 堆管理
 *
 * 设计要点：
 *   - 物理内存用 uint8_t 数组模拟（calloc 分配，默认 128MB）
 *   - 边界检查：addr + size > pmem->size → 返回 false
 *   - 物理内存层不做权限检查——权限校验是 MMU 层的职责
 *   - 小端序存储（与 RISC-V 规范一致，x86 天然兼容）
 *   - 单核模拟，无需考虑线程安全
 *
 * 调用方：
 *   - Loader：mem_map / mem_find_free / mem_load / pmem->data[]
 *   - MMU 层：mem_read_* / mem_write_*（mmu_read/write 内部调用）
 *   - Debugger：mem_dump（物理内存 hexdump）
 * ============================================================
 */

#define MEM_SIZE_DEFAULT    (128 * 1024 * 1024)  // 128 MB

/* 内存区域权限标志 */
#define MEM_READ   (1 << 0)
#define MEM_WRITE  (1 << 1)
#define MEM_EXEC   (1 << 2)

/* 内存区域描述符 */
typedef struct {
    uint32_t base;      // 起始物理地址
    uint32_t size;      // 区域大小（字节）
    uint8_t  flags;     // 权限标志（MEM_READ | MEM_WRITE | MEM_EXEC）
    char     name[32];  // 区域名称（"text", "data", "stack", "heap"）
} MemoryRegion;

/* 物理内存管理器 */
typedef struct {
    uint8_t  *data;         // 物理内存字节数组（calloc 分配）
    uint32_t  size;         // 总大小（默认 128MB）
    uint32_t  brk_start;    // brk 起始地址
    uint32_t  brk_current;  // 当前 brk 边界

    MemoryRegion *regions;  // 已映射区域动态数组
    int          region_count;
    int          region_capacity;
} PhysicalMemory;

/* ============================================================
 * 接口（13 个）
 * ============================================================
 */

/* 初始化 / 销毁 */
void mem_init(PhysicalMemory *pmem, uint32_t size);
void mem_destroy(PhysicalMemory *pmem);

/* 单次读取 — 小端序，越界返回 false */
bool mem_read_8 (PhysicalMemory *pmem, uint32_t addr, uint8_t  *val);
bool mem_read_16(PhysicalMemory *pmem, uint32_t addr, uint16_t *val);
bool mem_read_32(PhysicalMemory *pmem, uint32_t addr, uint32_t *val);

/* 单次写入 — 小端序，越界返回 false */
bool mem_write_8 (PhysicalMemory *pmem, uint32_t addr, uint8_t  val);
bool mem_write_16(PhysicalMemory *pmem, uint32_t addr, uint16_t val);
bool mem_write_32(PhysicalMemory *pmem, uint32_t addr, uint32_t val);

/* 区域映射管理 */
bool     mem_map(PhysicalMemory *pmem, uint32_t base, uint32_t size,
                 uint8_t flags, const char *name);
uint32_t mem_find_free(PhysicalMemory *pmem, uint32_t size, uint32_t align);
uint32_t mem_brk(PhysicalMemory *pmem, uint32_t new_brk);

/* 批量数据加载（Loader 专用：将外部数据 memcpy 到物理内存） */
bool mem_load(PhysicalMemory *pmem, uint32_t addr, const uint8_t *data,
              uint32_t size);

/* 调试 dump：hexdump 格式打印指定地址范围 */
void mem_dump(PhysicalMemory *pmem, uint32_t addr, uint32_t len);

#endif // MEMORY_H
