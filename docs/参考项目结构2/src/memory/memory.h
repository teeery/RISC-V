/* ============================================================================
 * memory.h / memory.c — 虚拟内存管理
 * ============================================================================
 *
 * 模拟器不能真的访问宿主机物理地址，所以需要自己管理一块"假内存"。
 * 实现方式是维护一个 MemRegion 数组，每个 region 对应一个虚拟地址区间。
 *
 * ---- 数据结构 ----
 *
 *   typedef struct {
 *       uint32_t base;       // 起始虚拟地址，如 0x00010000
 *       uint32_t size;       // 区域大小（字节）
 *       uint8_t  *data;      // 实际数据存储（malloc 分配）
 *       int      prot;       // 权限位掩码: PROT_READ|PROT_WRITE|PROT_EXEC
 *       char     name[32];   // 区域名称（调试用）: "text", "data", "stack"
 *   } MemRegion;
 *
 *   typedef struct {
 *       MemRegion *regions;  // 区域数组（动态扩容）
 *       int       count;     // 当前区域数量
 *       int       capacity;  // 数组容量
 *   } MMU;
 *
 * ---- 典型内存布局（32 位 RISC-V Linux）----
 *
 *   0x00000000 ──────────── NULL 区域（访问触发段错误）
 *   0x00010000 ┌────────── 代码段 (.text) → Region[0] R+X
 *              │
 *   0x00020000 ├────────── 只读数据 (.rodata) → 可能合并到 Region[0]
 *              │
 *   0x00030000 ├────────── 数据段 (.data + .bss) → Region[1] R+W
 *              │
 *              ├────────── 堆（brk，向上增长）
 *              │     ↓↓↓
 *              │  （空间空间）
 *              │     ↑↑↑
 *              │
 *   0xBFFF0000 ├────────── 栈（向下增长）→ Region[2] R+W
 *   0xC0000000 └────────── 栈顶
 *
 * ---- 需要实现的函数 ----
 *
 *   // 初始化 MMU
 *   void mmu_init(MMU *mmu);
 *
 *   // 分配一个虚拟内存区域
 *   // 返回 0 成功，-1 失败（如地址重叠）
 *   int mmu_mmap(MMU *mmu, uint32_t base, uint32_t size, int prot);
 *
 *   // 根据虚拟地址查找对应的 MemRegion
 *   MemRegion *mmu_find_region(MMU *mmu, uint32_t addr);
 *
 *   // ---- 读操作 ----
 *   // 每个函数都会做权限检查和地址范围检查
 *   int mmu_read8(MMU *mmu, uint32_t addr, uint32_t *value);
 *   int mmu_read16(MMU *mmu, uint32_t addr, uint32_t *value);
 *   int mmu_read32(MMU *mmu, uint32_t addr, uint32_t *value);
 *   // 返回值: 0=成功, -1=段错误
 *
 *   // 批量读取（用于 syscall write 等）
 *   int mmu_read(MMU *mmu, uint32_t addr, uint8_t *buf, uint32_t len);
 *
 *   // ---- 写操作 ----
 *   int mmu_write8(MMU *mmu, uint32_t addr, uint32_t value);
 *   int mmu_write16(MMU *mmu, uint32_t addr, uint32_t value);
 *   int mmu_write32(MMU *mmu, uint32_t addr, uint32_t value);
 *
 *   // 批量写入（用于 syscall read 等）
 *   int mmu_write(MMU *mmu, uint32_t addr, const uint8_t *buf, uint32_t len);
 *
 *   // 设置一块内存的值（用于 .bss 清零等）
 *   void mmu_memset(MMU *mmu, uint32_t addr, uint8_t val, uint32_t len);
 *
 * ---- 实现要点 ----
 *
 *   mmu_find_region:
 *       for (i = 0; i < mmu->count; i++) {
 *           if (addr >= regions[i].base &&
 *               addr < regions[i].base + regions[i].size)
 *               return &regions[i];
 *       }
 *       return NULL;  // 段错误
 *
 *   mmu_read32:
 *       1. MemRegion *r = mmu_find_region(mmu, addr);
 *       2. if (!r || !(r->prot & PROT_READ)) 返回错误
 *       3. 检查 addr+4 是否跨 region 边界（提示：一次 4 字节读可能跨越两个 region）
 *       4. uint32_t offset = addr - r->base;
 *       5. memcpy(value, r->data + offset, 4);  // 注意小端序
 *       6. 返回 0
 *
 *   跨页访存处理：
 *     如果一个 4 字节读的地址跨越了两个 region 的边界（如 0x0FFF 和 0x1000），
 *     需要分别读取两个 region 的数据再拼接。简化做法：要求所有 region 大小对齐，
 *     或者在发生跨 region 访问时报告错误。真实 CPU 会触发地址未对齐异常，
 *     但 RISC-V 实际上支持不对齐访问（通过多次内存访问完成）。
 *
 * ---- 已知限制 ----
 *   基础版本可以用线性查找实现。region 数量很少（通常 3~5 个），
 *   线性查找的性能完全够用。
 */

#ifndef MEMORY_H
#define MEMORY_H

#include "common.h"

/* ---- 在这里定义 MemRegion, MMU 和接口函数 ---- */

#endif /* MEMORY_H */
