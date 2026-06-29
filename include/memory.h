#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

/* ============================================================
 * memory.h — 物理内存管理接口
 *
 * 需要编写的内容：
 * 1. mem_init()        — 分配物理内存空间 (默认 128MB)
 * 2. mem_read_8/16/32() — 按字节/半字/字读取物理内存
 * 3. mem_write_8/16/32()— 按字节/半字/字写入物理内存
 * 4. mem_map()         — 在指定物理地址范围建立映射区段
 *    (类似 mmap，维护已映射区域列表)
 * 5. mem_find_free()   — 查找空闲物理页 (用于分配新映射)
 * 6. mem_dump()        — 十六进制打印指定地址范围 (调试用)
 *
 * 设计要点：
 * - 物理内存用 uint8_t 数组模拟
 * - 维护一个 region_list 记录已映射区域 (基址+大小+权限)
 * - 边界检查：读写超出物理内存范围应触发异常
 * - 对齐检查：半字访问地址必须 2 字节对齐，字访问 4 字节对齐
 * - 小端序存储 (RISC-V 默认)
 * - 支持 mmap 区域类型：代码段 (RX)、数据段 (RW)、栈 (RW)、堆 (RW)
 * ============================================================
 */

#define MEM_SIZE_DEFAULT    (128 * 1024 * 1024)  // 128 MB

/* 内存区域权限 */
#define MEM_READ   (1 << 0)
#define MEM_WRITE  (1 << 1)
#define MEM_EXEC   (1 << 2)

/* 内存区域描述符 */
typedef struct {
    uint32_t base;      // 起始物理地址
    uint32_t size;      // 区域大小 (字节)
    uint8_t  flags;     // 权限标志
    char     name[32];  // 区域名称 (用于调试: "code", "data", "stack", "heap")
} MemoryRegion;

/* 内存管理器 */
typedef struct {
    uint8_t  *data;         // 物理内存字节数组
    uint32_t  size;         // 总大小 (字节)
    uint32_t  brk_start;    // brk 起始地址
    uint32_t  brk_current;  // 当前 brk 边界

    MemoryRegion *regions;  // 已映射区域列表
    int          region_count;
    int          region_capacity;
} PhysicalMemory;

/* 初始化物理内存 */
void mem_init(PhysicalMemory *pmem, uint32_t size);

/* 释放物理内存 */
void mem_destroy(PhysicalMemory *pmem);

/* 读取物理内存 */
bool mem_read_8 (PhysicalMemory *pmem, uint32_t addr, uint8_t  *val);
bool mem_read_16(PhysicalMemory *pmem, uint32_t addr, uint16_t *val);
bool mem_read_32(PhysicalMemory *pmem, uint32_t addr, uint32_t *val);

/* 写入物理内存 */
bool mem_write_8 (PhysicalMemory *pmem, uint32_t addr, uint8_t  val);
bool mem_write_16(PhysicalMemory *pmem, uint32_t addr, uint16_t val);
bool mem_write_32(PhysicalMemory *pmem, uint32_t addr, uint32_t val);

/* 映射区域 */
bool mem_map(PhysicalMemory *pmem, uint32_t base, uint32_t size,
             uint8_t flags, const char *name);

/* 查找空闲内存区域 */
uint32_t mem_find_free(PhysicalMemory *pmem, uint32_t size, uint32_t align);

/* brk 系统调用支持 */
uint32_t mem_brk(PhysicalMemory *pmem, uint32_t new_brk);

/* 十六进制 dump */
void mem_dump(PhysicalMemory *pmem, uint32_t addr, uint32_t len);

/* 加载二进制数据到内存 */
bool mem_load(PhysicalMemory *pmem, uint32_t addr, const uint8_t *data,
              uint32_t size);

#endif // MEMORY_H
