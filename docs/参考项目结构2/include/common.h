/* ============================================================================
 * common.h — 项目全局公共定义
 * ============================================================================
 *
 * 所有 .c 文件都应该 #include "common.h"
 * 这个文件包含：
 *   - 标准库头文件
 *   - 固定宽度整数类型 (uint32_t, int32_t 等)
 *   - 项目全局宏定义
 *   - 内存权限常量
 *   - ELF 相关常量
 *   - RISC-V 系统调用号
 *
 * 实际实现时需要的内容：
 *   #include <stdint.h>    // uint32_t, int32_t, uint64_t, uint8_t
 *   #include <stdbool.h>   // bool, true, false
 *   #include <stdio.h>     // printf, fprintf, FILE, fopen, fread, fwrite
 *   #include <stdlib.h>    // malloc, free, exit
 *   #include <string.h>    // memset, memcpy, strcmp, strtoul
 *   #include <assert.h>    // assert
 *
 *   内存权限位掩码（用于 MemRegion.prot）:
 *     #define PROT_READ   0x01
 *     #define PROT_WRITE  0x02
 *     #define PROT_EXEC   0x04
 *
 *   ELF 段类型常量:
 *     #define PT_LOAD     0x01   // 需要加载到内存的段
 *     #define PT_NULL     0x00
 *
 *   ELF 段权限（p_flags）:
 *     #define PF_X        0x01   // 可执行
 *     #define PF_W        0x02   // 可写
 *     #define PF_R        0x04   // 可读
 *
 *   RISC-V ELF 机器标识:
 *     #define EM_RISCV    243
 *
 *   系统调用号（RISC-V Linux ABI）:
 *     #define SYS_exit    93
 *     #define SYS_read    63
 *     #define SYS_write   64
 *     #define SYS_open    1024
 *     #define SYS_close   57
 *     #define SYS_brk     214
 *
 *   EBREAK 指令编码:
 *     #define EBREAK_INSN 0x00100073
 */

#ifndef COMMON_H
#define COMMON_H

/* ---- 在这里添加你的 #include 和 #define ---- */

#endif /* COMMON_H */
