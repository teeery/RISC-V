/* ============================================================================
 * elf.c — ELF 加载器实现
 * ============================================================================
 *
 * 实现 elf_load(Simulator *sim, const char *filename)：
 *
 *   伪代码：
 *
 *   int elf_load(Simulator *sim, const char *filename) {
 *       // 1. 打开文件
 *       FILE *f = fopen(filename, "rb");
 *       if (!f) { perror(filename); return -1; }
 *
 *       // 2. 读取 ELF Header
 *       Elf32_Ehdr ehdr;
 *       fread(&ehdr, sizeof(ehdr), 1, f);
 *
 *       // 3. 验证 magic number
 *       if (ehdr.e_ident[0] != ELF_MAGIC0 ||
 *           ehdr.e_ident[1] != ELF_MAGIC1 ||
 *           ehdr.e_ident[2] != ELF_MAGIC2 ||
 *           ehdr.e_ident[3] != ELF_MAGIC3) {
 *           fprintf(stderr, "Not an ELF file\n");
 *           return -1;
 *       }
 *
 *       // 4. 验证 CPU 类型
 *       if (ehdr.e_machine != EM_RISCV) {
 *           fprintf(stderr, "Not a RISC-V binary\n");
 *           return -1;
 *       }
 *
 *       // 5. 验证位数（基础要求只做 32 位，选做可加 64 位）
 *       if (ehdr.e_ident[4] != ELFCLASS32) {
 *           fprintf(stderr, "Only 32-bit ELF supported\n");
 *           return -1;
 *       }
 *
 *       // 6. 遍历 Program Headers
 *       fseek(f, ehdr.e_phoff, SEEK_SET);
 *       for (int i = 0; i < ehdr.e_phnum; i++) {
 *           Elf32_Phdr phdr;
 *           fread(&phdr, sizeof(phdr), 1, f);
 *
 *           // 只处理 PT_LOAD 类型
 *           if (phdr.p_type != PT_LOAD) continue;
 *
 *           // 转换权限
 *           int prot = 0;
 *           if (phdr.p_flags & PF_R) prot |= PROT_READ;
 *           if (phdr.p_flags & PF_W) prot |= PROT_WRITE;
 *           if (phdr.p_flags & PF_X) prot |= PROT_EXEC;
 *
 *           // 分配虚拟内存区域
 *           // 注意：memsz 可能是 filesz 的整数倍（含 .bss）
 *           mmu_mmap(&sim->mmu, phdr.p_vaddr, phdr.p_memsz, prot);
 *
 *           // 从文件拷贝数据
 *           fseek(f, phdr.p_offset, SEEK_SET);
 *           // 逐块读取，写入 mmu
 *           uint8_t buf[4096];
 *           uint32_t remaining = phdr.p_filesz;
 *           uint32_t vaddr = phdr.p_vaddr;
 *           while (remaining > 0) {
 *               uint32_t chunk = (remaining > sizeof(buf)) ? sizeof(buf) : remaining;
 *               fread(buf, 1, chunk, f);
 *               mmu_write(&sim->mmu, vaddr, buf, chunk);
 *               vaddr += chunk;
 *               remaining -= chunk;
 *           }
 *
 *           // 清零 .bss 区域（memsz > filesz 的部分）
 *           if (phdr.p_memsz > phdr.p_filesz) {
 *               uint32_t bss_start = phdr.p_vaddr + phdr.p_filesz;
 *               uint32_t bss_size  = phdr.p_memsz - phdr.p_filesz;
 *               mmu_memset(&sim->mmu, bss_start, 0, bss_size);
 *           }
 *       }
 *
 *       // 7. 设置 CPU 入口地址
 *       sim->cpu.pc = ehdr.e_entry;
 *
 *       // 8. 初始化栈（简化：只设置 sp，不传 argc/argv）
 *       uint32_t stack_top = 0xC0000000;
 *       uint32_t stack_size = 0x00100000;  // 1MB
 *       mmu_mmap(&sim->mmu, stack_top - stack_size, stack_size,
 *                PROT_READ | PROT_WRITE);
 *       sim->cpu.regs[2] = stack_top;  // sp 指向栈顶
 *
 *       fclose(f);
 *       return 0;
 *   }
 *
 * 打印加载信息（调试用）：
 *   printf("Loaded %s:\n", filename);
 *   printf("  Entry: 0x%08x\n", ehdr.e_entry);
 *   遍历所有 PT_LOAD 段，打印：
 *   printf("  0x%08x - 0x%08x (%s)\n", base, base+size, prot_string);
 */

#include "elf.h"
// #include "../simulator.h"
// #include "../memory/memory.h"

/* ---- 在这里实现 elf_load ---- */
