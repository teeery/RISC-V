/* ============================================================================
 * memory.c — 虚拟内存管理实现
 * ============================================================================
 *
 * 实现 mmu_init, mmu_mmap, mmu_find_region,
 *       mmu_read8/16/32, mmu_write8/16/32,
 *       mmu_read, mmu_write, mmu_memset
 *
 * mmu_mmap 实现：
 *   1. 如果 regions 数组满了，realloc 扩容（capacity *= 2）
 *   2. （可选）检查新区域和已有区域是否重叠
 *   3. regions[count].base = base
 *   4. regions[count].size = size
 *   5. regions[count].data = malloc(size)
 *   6. regions[count].prot = prot
 *   7. count++
 *
 * 注意：
 *   - 基础版本中所有 malloc 出来的 data 不需要释放（模拟器退出时 OS 回收）
 *   - 所有读写函数必须在 region 内部才能成功，跨 region 访问应返回错误
 *   - 不需要实现真正的页表，区域数组就是你们的"页表"
 */

#include "memory.h"

/* ---- 在这里实现 ---- */
