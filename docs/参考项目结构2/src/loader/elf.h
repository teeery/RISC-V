/* ============================================================================
 * elf.h — ELF 文件格式结构体定义
 * ============================================================================
 *
 * 这个文件定义 ELF 文件格式的 C 语言结构体。
 * 只定义了加载器需要的字段，不是完整的 ELF 规范。
 *
 * ---- ELF Header (32-bit) ----
 *
 *   typedef struct {
 *       uint8_t  e_ident[16];  // Magic + 类型信息
 *                               // [0..3] = 0x7F 'E' 'L' 'F'
 *                               // [4]    = 1(32bit) / 2(64bit)
 *                               // [5]    = 1(小端) / 2(大端)
 *                               // [6]    = 1(当前版本)
 *       uint16_t e_type;       // 1=可重定位, 2=可执行, 3=共享库
 *       uint16_t e_machine;    // 243 = EM_RISCV
 *       uint32_t e_version;    // 1
 *       uint32_t e_entry;      // ★ 程序入口虚拟地址
 *       uint32_t e_phoff;      // ★ Program Header 表在文件中的偏移
 *       uint32_t e_shoff;      // Section Header 表偏移（加载时不需要）
 *       uint32_t e_flags;
 *       uint16_t e_ehsize;     // ELF Header 大小
 *       uint16_t e_phentsize;  // ★ 每个 Program Header 的大小
 *       uint16_t e_phnum;      // ★ Program Header 的数量
 *       uint16_t e_shentsize;  // Section Header 大小
 *       uint16_t e_shnum;      // Section Header 数量
 *       uint16_t e_shstrndx;   // Section 名称字符串表索引
 *   } Elf32_Ehdr;
 *
 * ---- Program Header (32-bit) ----
 *
 *   typedef struct {
 *       uint32_t p_type;       // ★ 1 = PT_LOAD（需要加载的段）
 *                               //   其他值可以跳过
 *       uint32_t p_offset;     // ★ 在文件中的偏移
 *       uint32_t p_vaddr;      // ★ 虚拟地址（加载到哪里）
 *       uint32_t p_paddr;      // 物理地址（对模拟器无用）
 *       uint32_t p_filesz;     // ★ 文件中的大小
 *       uint32_t p_memsz;      // ★ 内存中的大小（可能 > filesz）
 *       uint32_t p_flags;      // ★ 权限: PF_R=4, PF_W=2, PF_X=1
 *                               //   可组合：5=R|X, 6=R|W, 7=R|W|X
 *       uint32_t p_align;      // 对齐要求
 *   } Elf32_Phdr;
 *
 * ---- 常量 ----
 *
 *   #define ELF_MAGIC0   0x7F
 *   #define ELF_MAGIC1   'E'
 *   #define ELF_MAGIC2   'L'
 *   #define ELF_MAGIC3   'F'
 *   #define ELFCLASS32   1
 *   #define ELFCLASS64   2
 *   #define ELFDATA2LSB  1    // 小端序
 *   #define EM_RISCV     243
 *   #define ET_EXEC      2    // 可执行文件
 *   #define PT_LOAD      1
 *   #define PF_X         1
 *   #define PF_W         2
 *   #define PF_R         4
 *
 * ---- 函数声明 ----
 *
 *   // 加载 ELF 文件到模拟器内存
 *   // 返回 0 成功，返回 -1 失败（文件不存在、不是 ELF、不是 RISC-V 等）
 *   int elf_load(Simulator *sim, const char *filename);
 *
 *   // 验证 ELF Header 的 magic 和 machine 字段
 *   bool elf_validate(Elf32_Ehdr *ehdr);
 */

#ifndef ELF_H
#define ELF_H

#include "common.h"

/* ---- 在这里定义 Elf32_Ehdr, Elf32_Phdr 和加载函数 ---- */

#endif /* ELF_H */
