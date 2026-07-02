/* ============================================================================
 * execute_test.c — cpu_execute 单元测试（按指令组逐步添加）
 *
 * 用法：
 *   gcc -I src/include -I src/include/cpu -I src/include/memory \
 *       -o build/execute_test \
 *       src/src/cpu/cpu.c \
 *       src/src/cpu/decode.c \
 *       src/src/cpu/execute.c \
 *       src/src/simulator.c \
 *       src/src/memory/memory.c \
 *       src/src/memory/mmu.c \
 *       src/test/cpu/execute_test.c
 *   ./build/execute_test
 *
 * 测试策略：
 *   不经过 sim_step（避免依赖 MMU 取指），直接构造 DecodedInstr
 *   然后调用 cpu_execute()，再检查寄存器写入是否正确。
 *   这样能独立验证 execute 逻辑，不受取指/译码干扰。
 * ============================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "simulator.h"      // Simulator, sim_init, sim_destroy
#include "cpu/decode.h"     // cpu_decode, DecodedInstr
#include "cpu/execute.h"    // cpu_execute
#include "memory/mmu.h"     // mmu_read_32, mmu_write_32 (Load/Store 测试用)
#include "types.h"          // ExceptionType

/* ── 辅助：创建一个干净的模拟器实例 ──────────────────────────────── */
static Simulator *new_sim(void)
{
    Simulator *sim = calloc(1, sizeof(Simulator));
    if (!sim) { fprintf(stderr, "Fatal: calloc Simulator failed\n"); exit(1); }
    sim_init(sim);
    /* 映射一块物理内存供测试使用：地址 0，大小 64KB，可读写 */
    mem_map(&sim->pmem, 0, 64 * 1024, MEM_READ | MEM_WRITE, "test");
    return sim;
}

/* ── 辅助：执行单条指令（不经过取指），返回执行结果 ───────────────── */
static bool exec(Simulator *sim, uint32_t instr, uint32_t *next_pc)
{
    DecodedInstr d = cpu_decode(instr);
    return cpu_execute(sim, &d, next_pc);
}

/* ====================================================================
 * 测试用例
 * ==================================================================== */

/* ── 测试共用变量 ────────────────────────────────────────────────── */
static int passed = 0;
static int failed = 0;

#define CHECK(cond, fmt, ...) do {                              \
    if (cond) {                                                 \
        printf("  ✅ " fmt "\n", ##__VA_ARGS__);                \
        passed++;                                               \
    } else {                                                    \
        printf("  ❌ " fmt "\n", ##__VA_ARGS__);                \
        failed++;                                               \
    }                                                           \
} while(0)


/* ================================================================
 * 第一组：LUI + AUIPC
 * ================================================================ */
void test_lui(void)
{
    printf("\n─── LUI ───\n");

    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000;
    uint32_t next_pc;

    /* LUI x5, 0x12345 → x5 = 0x12345000
     * 机器码: imm[31:12]=0x12345 | rd=5 | opcode=0x37
     *        = 0x12345_00101_0110111 = 0x123452B7 */
    exec(sim, 0x123452B7, &next_pc);

    CHECK(sim->cpu.regs[5] == 0x12345000,
          "LUI x5, 0x12345   → x5=0x%08X (expect 0x12345000)", sim->cpu.regs[5]);
    CHECK(next_pc == 0x80000004,
          "next_pc           → 0x%08X (expect 0x80000004)", next_pc);

    sim_destroy(sim);
    free(sim);
}

void test_lui_zero(void)
{
    printf("\n─── LUI zero ───\n");

    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000;
    uint32_t next_pc;

    /* LUI x0, 0x1 → x0 = 0x1000（但 x0 会被 sim_step 清零，这里只测 execute）
     * 机器码: imm[31:12]=0x00001, rd=0, opcode=0x37
     *        = 0x00001_00000_0110111 = 0x00001037 */
    exec(sim, 0x00001037, &next_pc);

    /* execute 不负责清零 x0，sim_step 会在调用后做 */
    CHECK(sim->cpu.regs[0] == 0x00001000,
          "LUI x0, 0x1       → x0=0x%08X (execute 不负责清零 x0)", sim->cpu.regs[0]);

    sim_destroy(sim);
    free(sim);
}

void test_auipc(void)
{
    printf("\n─── AUIPC ───\n");

    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000;
    uint32_t next_pc;

    /* AUIPC x5, 0x10 → x5 = pc + 0x10000 = 0x80010000
     * 机器码: imm[31:12]=0x00010, rd=5, opcode=0x17
     *        = 0x00010_00101_0010111 = 0x00010297 */
    exec(sim, 0x00010297, &next_pc);

    CHECK(sim->cpu.regs[5] == 0x80010000,
          "AUIPC x5, 0x10    → x5=0x%08X (expect 0x80010000)", sim->cpu.regs[5]);
    CHECK(next_pc == 0x80000004,
          "next_pc           → 0x%08X (expect 0x80000004)", next_pc);

    sim_destroy(sim);
    free(sim);
}

void test_auipc_negative(void)
{
    printf("\n─── AUIPC negative offset ───\n");

    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000010;
    uint32_t next_pc;

    /* AUIPC x6, -0x1 → x6 = pc + 0xFFFFF000 = 0x7FFFF000 + 0x10
     *  imm = 0xFFFFF000（符号扩展），rd=6, opcode=0x17
     *  机器码: 0xFFFFF_00110_0010111 = 0xFFFFF317
     *  pc + imm = 0x80000010 + 0xFFFFF000 = 0x7FFFF010 (overflow wraps) */
    exec(sim, 0xFFFFF317, &next_pc);

    CHECK(sim->cpu.regs[6] == 0x80000010 + (uint32_t)(int32_t)0xFFFFF000,
          "AUIPC x6, -0x1    → x6=0x%08X (expect 0x7FFFF010)", sim->cpu.regs[6]);
    CHECK(next_pc == 0x80000014,
          "next_pc           → 0x%08X (expect 0x80000014)", next_pc);

    sim_destroy(sim);
    free(sim);
}

/* ================================================================
 * 第二组：ADDI + ADD + SUB
 * ================================================================ */

void test_addi_positive(void)
{
    printf("\n─── ADDI positive ───\n");

    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000;
    uint32_t next_pc;

    /* 先给 x1 设初值，后续指令要用 */
    sim->cpu.regs[1] = 100;

    /* ADDI x2, x1, 42 → x2 = 100 + 42 = 142
     * 机器码: imm=42, rs1=1, funct3=0, rd=2, opcode=0x13
     *        = 000000101010_00001_000_00010_0010011
     *        = 0x02A08113 */
    exec(sim, 0x02A08113, &next_pc);

    CHECK(sim->cpu.regs[2] == 142,
          "ADDI x2, x1, 42   → x2=%d (expect 142)", sim->cpu.regs[2]);
    CHECK(next_pc == 0x80000004,
          "next_pc           → 0x%08X (expect 0x80000004)", next_pc);

    sim_destroy(sim);
    free(sim);
}

void test_addi_negative(void)
{
    printf("\n─── ADDI negative ───\n");

    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000;
    uint32_t next_pc;

    sim->cpu.regs[1] = 100;

    /* ADDI x2, x1, -5 → x2 = 100 + (-5) = 95
     * 机器码: imm=-5=0xFFB(12-bit), rs1=1, funct3=0, rd=2, opcode=0x13
     *        = 111111111011_00001_000_00010_0010011
     *        = 0xFFB08113 */
    exec(sim, 0xFFB08113, &next_pc);

    CHECK(sim->cpu.regs[2] == 95,
          "ADDI x2, x1, -5   → x2=%d (expect 95)", sim->cpu.regs[2]);

    sim_destroy(sim);
    free(sim);
}

void test_addi_from_zero(void)
{
    printf("\n─── ADDI from zero ───\n");

    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000;
    uint32_t next_pc;

    /* ADDI x3, x0, 0 → x3 = 0 + 0 = 0
     * 机器码: imm=0, rs1=0, funct3=0, rd=3, opcode=0x13
     *        = 0x00000193 */
    exec(sim, 0x00000193, &next_pc);

    CHECK(sim->cpu.regs[3] == 0,
          "ADDI x3, x0, 0    → x3=%d (expect 0)", sim->cpu.regs[3]);

    sim_destroy(sim);
    free(sim);
}

void test_add(void)
{
    printf("\n─── ADD ───\n");

    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000;
    uint32_t next_pc;

    sim->cpu.regs[1] = 100;
    sim->cpu.regs[2] = 200;

    /* ADD x3, x1, x2 → x3 = 100 + 200 = 300
     * 机器码: funct7=0x00, rs2=2, rs1=1, funct3=0, rd=3, opcode=0x33
     *        = 0000000_00010_00001_000_00011_0110011
     *        = 0x002081B3 */
    exec(sim, 0x002081B3, &next_pc);

    CHECK(sim->cpu.regs[3] == 300,
          "ADD x3, x1, x2    → x3=%d (expect 300)", sim->cpu.regs[3]);
    CHECK(next_pc == 0x80000004,
          "next_pc           → 0x%08X (expect 0x80000004)", next_pc);

    sim_destroy(sim);
    free(sim);
}

void test_sub(void)
{
    printf("\n─── SUB ───\n");

    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000;
    uint32_t next_pc;

    sim->cpu.regs[1] = 200;
    sim->cpu.regs[2] = 50;

    /* SUB x3, x1, x2 → x3 = 200 - 50 = 150
     * 机器码: funct7=0x20, rs2=2, rs1=1, funct3=0, rd=3, opcode=0x33
     *        = 0100000_00010_00001_000_00011_0110011
     *        = 0x402081B3 */
    exec(sim, 0x402081B3, &next_pc);

    CHECK(sim->cpu.regs[3] == 150,
          "SUB x3, x1, x2    → x3=%d (expect 150)", sim->cpu.regs[3]);
    CHECK(next_pc == 0x80000004,
          "next_pc           → 0x%08X (expect 0x80000004)", next_pc);

    sim_destroy(sim);
    free(sim);
}

void test_sub_negative(void)
{
    printf("\n─── SUB negative result ───\n");

    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000;
    uint32_t next_pc;

    sim->cpu.regs[1] = 50;
    sim->cpu.regs[2] = 200;

    /* SUB x3, x1, x2 → x3 = 50 - 200 = -150 = 0xFFFFFF6A
     * 机器码: funct7=0x20, rs2=2, rs1=1, funct3=0, rd=3, opcode=0x33
     *        = 0x402081B3（同上，只是寄存器值不同） */
    exec(sim, 0x402081B3, &next_pc);

    CHECK(sim->cpu.regs[3] == (uint32_t)-150,
          "SUB x3, x1, x2    → x3=%d (expect -150, unsigned=0x%08X)",
          (int32_t)sim->cpu.regs[3], sim->cpu.regs[3]);
    CHECK(sim->cpu.regs[3] == 0xFFFFFF6A,
          "x3 as hex          → 0x%08X (expect 0xFFFFFF6A)", sim->cpu.regs[3]);

    sim_destroy(sim);
    free(sim);
}

/* ================================================================
 * 第三组：JAL + JALR
 * ================================================================ */

void test_jal(void)
{
    printf("\n─── JAL ───\n");

    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000;
    uint32_t next_pc;

    /* JAL x1, 0x20 → x1 = pc+4 = 0x80000004, next_pc = pc+0x20 = 0x80000020
     * J-type: imm[20]|imm[10:1]|imm[11]|imm[19:12]|rd|opcode
     * offset=0x20 → bit5=1 → imm[10:1]=0b0000010000 → instr[25]=1
     * 机器码 = 0x020000EF */
    exec(sim, 0x020000EF, &next_pc);

    CHECK(sim->cpu.regs[1] == 0x80000004,
          "JAL x1, 0x20      → x1=0x%08X (expect 0x80000004)", sim->cpu.regs[1]);
    CHECK(next_pc == 0x80000020,
          "next_pc           → 0x%08X (expect 0x80000020)", next_pc);

    sim_destroy(sim);
    free(sim);
}

void test_jalr(void)
{
    printf("\n─── JALR ───\n");

    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000;
    sim->cpu.regs[1] = 0x80000100;   // rs1 = x1, 跳转目标基址
    uint32_t next_pc;

    /* JALR x2, x1, 8 → x2 = pc+4 = 0x80000004,
     *                   next_pc = (x1 + 8) & ~1 = (0x80000100 + 8) & ~1 = 0x80000108
     * 机器码: imm=8, rs1=1, funct3=0, rd=2, opcode=0x67
     *        = 0x00808167 */
    exec(sim, 0x00808167, &next_pc);

    CHECK(sim->cpu.regs[2] == 0x80000004,
          "JALR x2, x1, 8   → x2=0x%08X (expect 0x80000004)", sim->cpu.regs[2]);
    CHECK(next_pc == 0x80000108,
          "next_pc           → 0x%08X (expect 0x80000108)", next_pc);

    sim_destroy(sim);
    free(sim);
}

void test_jalr_alignment(void)
{
    printf("\n─── JALR alignment ───\n");

    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000;
    /* rs1 + imm = 0x80000103, & ~1 应该对齐到 0x80000102 */
    sim->cpu.regs[2] = 0x80000100;
    uint32_t next_pc;

    /* JALR x1, x2, 3 → target = (0x80000100 + 3) & ~1 = 0x80000102
     * 机器码: imm=3, rs1=2, funct3=0, rd=1, opcode=0x67
     *        = 0x003100E7 */
    exec(sim, 0x003100E7, &next_pc);

    CHECK(next_pc == 0x80000102,
          "next_pc (aligned) → 0x%08X (expect 0x80000102)", next_pc);

    sim_destroy(sim);
    free(sim);
}

/* ================================================================
 * 第四组：B-type 分支指令（BEQ/BNE/BLT/BGE/BLTU/BGEU）
 * ================================================================ */

void test_beq_taken(void)
{
    printf("\n─── BEQ taken ───\n");

    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000;
    sim->cpu.regs[1] = 42;
    sim->cpu.regs[2] = 42;
    uint32_t next_pc;

    /* BEQ x1, x2, 0x10 → rs1==rs2 → 跳转: next_pc = pc + 0x10 = 0x80000010
     * 机器码: B-type, imm=0x10, rs2=2, rs1=1, funct3=0, opcode=0x63
     *        = 0x00208863 */
    exec(sim, 0x00208863, &next_pc);

    CHECK(next_pc == 0x80000010,
          "BEQ x1,x2,+16     → next_pc=0x%08X (expect 0x80000010)", next_pc);

    sim_destroy(sim);
    free(sim);
}

void test_beq_not_taken(void)
{
    printf("\n─── BEQ not taken ───\n");

    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000;
    sim->cpu.regs[1] = 42;
    sim->cpu.regs[2] = 99;
    uint32_t next_pc;

    /* BEQ x1, x2, 0x10 → rs1≠rs2 → 不跳转: next_pc = pc+4 = 0x80000004
     * 机器码同上 */
    exec(sim, 0x00208863, &next_pc);

    CHECK(next_pc == 0x80000004,
          "BEQ x1,x2,+16     → next_pc=0x%08X (expect 0x80000004, not taken)", next_pc);

    sim_destroy(sim);
    free(sim);
}

void test_bne_taken(void)
{
    printf("\n─── BNE taken ───\n");

    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000;
    sim->cpu.regs[1] = 10;
    sim->cpu.regs[2] = 20;
    uint32_t next_pc;

    /* BNE x1, x2, -8 → rs1≠rs2 → 跳转: next_pc = pc + (-8) = 0x7FFFFFF8
     * 机器码: B-type, imm=-8, rs2=2, rs1=1, funct3=1, opcode=0x63
     *        = 0xFE209CE3 */
    exec(sim, 0xFE209CE3, &next_pc);

    CHECK(next_pc == 0x7FFFFFF8,
          "BNE x1,x2,-8      → next_pc=0x%08X (expect 0x7FFFFFF8)", next_pc);

    sim_destroy(sim);
    free(sim);
}

void test_blt_taken(void)
{
    printf("\n─── BLT taken ───\n");

    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000;
    sim->cpu.regs[1] = (uint32_t)-10;   // signed: -10
    sim->cpu.regs[2] = 5;              // signed: 5
    uint32_t next_pc;

    /* BLT x1, x2, 0x0C → -10 < 5 → 跳转: next_pc = pc + 0x0C = 0x8000000C
     * 机器码: funct3=4, imm=0x0C, rs2=2, rs1=1, opcode=0x63
     *        = 0x0020C663 */
    exec(sim, 0x0020C663, &next_pc);

    CHECK(next_pc == 0x8000000C,
          "BLT x1,x2,+12     → next_pc=0x%08X (expect 0x8000000C)", next_pc);

    sim_destroy(sim);
    free(sim);
}

void test_blt_not_taken(void)
{
    printf("\n─── BLT not taken ───\n");

    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000;
    sim->cpu.regs[1] = 100;
    sim->cpu.regs[2] = (uint32_t)-5;   // signed: -5, unsigned: large
    uint32_t next_pc;

    /* BLT x1, x2, 0x10 → 100 < -5? No（有符号）→ 不跳转
     * 机器码: funct3=4, imm=0x10, rs2=2, rs1=1, opcode=0x63
     *        = 0x0020C863 */
    exec(sim, 0x0020C863, &next_pc);

    CHECK(next_pc == 0x80000004,
          "BLT x1,x2,+16     → next_pc=0x%08X (expect 0x80000004, not taken)", next_pc);

    sim_destroy(sim);
    free(sim);
}

void test_bge_taken(void)
{
    printf("\n─── BGE taken ───\n");

    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000;
    sim->cpu.regs[1] = 5;
    sim->cpu.regs[2] = (uint32_t)-10;   // signed: -10
    uint32_t next_pc;

    /* BGE x1, x2, 0x10 → 5 >= -10 → 跳转
     * 机器码: funct3=5, imm=0x10, rs2=2, rs1=1, opcode=0x63
     *        = 0x0020D863 */
    exec(sim, 0x0020D863, &next_pc);

    CHECK(next_pc == 0x80000010,
          "BGE x1,x2,+16     → next_pc=0x%08X (expect 0x80000010)", next_pc);

    sim_destroy(sim);
    free(sim);
}

void test_bltu_taken(void)
{
    printf("\n─── BLTU taken ───\n");

    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000;
    sim->cpu.regs[1] = 10;
    sim->cpu.regs[2] = (uint32_t)-5;   // unsigned: 0xFFFFFFFB ≈ 4.3 billion
    uint32_t next_pc;

    /* BLTU x1, x2, 0x0C → 10 < 4294967291 → 跳转（无符号比较）
     * 机器码: funct3=6, imm=0x0C, rs2=2, rs1=1, opcode=0x63
     *        = 0x0020E663 */
    exec(sim, 0x0020E663, &next_pc);

    CHECK(next_pc == 0x8000000C,
          "BLTU x1,x2,+12    → next_pc=0x%08X (expect 0x8000000C)", next_pc);

    sim_destroy(sim);
    free(sim);
}

void test_bgeu_taken(void)
{
    printf("\n─── BGEU taken ───\n");

    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000;
    sim->cpu.regs[1] = (uint32_t)-5;   // unsigned: large
    sim->cpu.regs[2] = 10;             // unsigned: small
    uint32_t next_pc;

    /* BGEU x1, x2, 0x10 → 4294967291 >= 10 → 跳转（无符号比较）
     * 机器码: funct3=7, imm=0x10, rs2=2, rs1=1, opcode=0x63
     *        = 0x0020F863 */
    exec(sim, 0x0020F863, &next_pc);

    CHECK(next_pc == 0x80000010,
          "BGEU x1,x2,+16    → next_pc=0x%08X (expect 0x80000010)", next_pc);

    sim_destroy(sim);
    free(sim);
}

void test_bne_not_taken(void)
{
    printf("\n─── BNE not taken ───\n");

    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000;
    sim->cpu.regs[1] = 42;
    sim->cpu.regs[2] = 42;
    uint32_t next_pc;

    /* BNE x1, x2, 0x10 → rs1==rs2 → 不跳转
     * 机器码: funct3=1, imm=0x10, rs2=2, rs1=1, opcode=0x63
     *        = 0x00209863 */
    exec(sim, 0x00209863, &next_pc);

    CHECK(next_pc == 0x80000004,
          "BNE x1,x2,+16     → next_pc=0x%08X (expect 0x80000004, not taken)", next_pc);

    sim_destroy(sim);
    free(sim);
}

/* ================================================================
 * 第五组：Load + Store（LW/SW/LH/LHU/LB/LBU/SH/SB）
 *
 * 需要真实内存，先用 mmu_write 预设数据，
 * 再执行 load/store 指令，最后用 mmu_read 验证。
 * ================================================================ */

/* ── 辅助：写 32 位值到模拟器物理内存 ───────────────────────── */
static void mem_poke(Simulator *sim, uint32_t addr, uint32_t val)
{
    ExceptionType exc = EXC_NONE;
    mmu_write_32(&sim->mmu, &sim->pmem, addr, val, sim->cpu.priv, &exc);
}

/* ── 辅助：从模拟器物理内存读 32 位值 ───────────────────────── */
static uint32_t mem_peek(Simulator *sim, uint32_t addr)
{
    ExceptionType exc = EXC_NONE;
    uint32_t val = 0;
    mmu_read_32(&sim->mmu, &sim->pmem, addr, &val, sim->cpu.priv, &exc);
    return val;
}

void test_lw(void)
{
    printf("\n─── LW ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t next_pc;

    /* 地址 0x1000 预设 0xDEADBEEF，x1=0x1000，LW x2,0(x1) */
    mem_poke(sim, 0x1000, 0xDEADBEEF);
    sim->cpu.regs[1] = 0x1000;

    /* LW x2, 0(x1): imm=0,rs1=1,funct3=2,rd=2,opcode=0x03 → 0x0000A103 */
    exec(sim, 0x0000A103, &next_pc);
    CHECK(sim->cpu.regs[2] == 0xDEADBEEF,
          "LW x2,0(x1)       → x2=0x%08X (expect 0xDEADBEEF)", sim->cpu.regs[2]);

    sim_destroy(sim); free(sim);
}

void test_lw_offset(void)
{
    printf("\n─── LW with offset ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t next_pc;

    mem_poke(sim, 0x1008, 0xCAFEBABE);
    sim->cpu.regs[1] = 0x1000;

    /* LW x3, 8(x1): imm=8,rs1=1,funct3=2,rd=3,opcode=0x03 → 0x0080A183 */
    exec(sim, 0x0080A183, &next_pc);
    CHECK(sim->cpu.regs[3] == 0xCAFEBABE,
          "LW x3,8(x1)       → x3=0x%08X (expect 0xCAFEBABE)", sim->cpu.regs[3]);

    sim_destroy(sim); free(sim);
}

void test_sw(void)
{
    printf("\n─── SW ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t next_pc;

    sim->cpu.regs[1] = 0x2000;
    sim->cpu.regs[2] = 0x12345678;

    /* SW x2,0(x1): imm[11:5]=0,rs2=2,rs1=1,funct3=2,imm[4:0]=0,opcode=0x23 → 0x0020A023 */
    exec(sim, 0x0020A023, &next_pc);
    CHECK(mem_peek(sim, 0x2000) == 0x12345678,
          "SW x2,0(x1)       → mem[0x2000]=0x%08X (expect 0x12345678)", mem_peek(sim, 0x2000));

    sim_destroy(sim); free(sim);
}

void test_lh(void)
{
    printf("\n─── LH (sign-extend) ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t next_pc;

    /* 0xFFFF 符号扩展 → 0xFFFFFFFF */
    mem_poke(sim, 0x1000, 0x0000FFFF);
    sim->cpu.regs[1] = 0x1000;

    /* LH x2,0(x1): imm=0,rs1=1,funct3=1,rd=2,opcode=0x03 → 0x00009103 */
    exec(sim, 0x00009103, &next_pc);
    CHECK(sim->cpu.regs[2] == 0xFFFFFFFF,
          "LH x2,0(x1)       → x2=0x%08X (expect 0xFFFFFFFF)", sim->cpu.regs[2]);

    sim_destroy(sim); free(sim);
}

void test_lhu(void)
{
    printf("\n─── LHU (zero-extend) ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t next_pc;

    mem_poke(sim, 0x2000, 0x0000FFFF);
    sim->cpu.regs[1] = 0x2000;

    /* LHU x2,0(x1): imm=0,rs1=1,funct3=5,rd=2,opcode=0x03 → 0x0000D103 */
    exec(sim, 0x0000D103, &next_pc);
    CHECK(sim->cpu.regs[2] == 0x0000FFFF,
          "LHU x2,0(x1)      → x2=0x%08X (expect 0x0000FFFF)", sim->cpu.regs[2]);

    sim_destroy(sim); free(sim);
}

void test_lb(void)
{
    printf("\n─── LB (sign-extend) ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t next_pc;

    /* 0x80 符号扩展 → 0xFFFFFF80 */
    mem_poke(sim, 0x3000, 0x00000080);
    sim->cpu.regs[1] = 0x3000;

    /* LB x2,0(x1): imm=0,rs1=1,funct3=0,rd=2,opcode=0x03 → 0x00008103 */
    exec(sim, 0x00008103, &next_pc);
    CHECK(sim->cpu.regs[2] == 0xFFFFFF80,
          "LB x2,0(x1)       → x2=0x%08X (expect 0xFFFFFF80)", sim->cpu.regs[2]);

    sim_destroy(sim); free(sim);
}

void test_lbu(void)
{
    printf("\n─── LBU (zero-extend) ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t next_pc;

    mem_poke(sim, 0x4000, 0x00000080);
    sim->cpu.regs[1] = 0x4000;

    /* LBU x2,0(x1): imm=0,rs1=1,funct3=4,rd=2,opcode=0x03 → 0x0000C103 */
    exec(sim, 0x0000C103, &next_pc);
    CHECK(sim->cpu.regs[2] == 0x00000080,
          "LBU x2,0(x1)      → x2=0x%08X (expect 0x00000080)", sim->cpu.regs[2]);

    sim_destroy(sim); free(sim);
}

void test_sb(void)
{
    printf("\n─── SB ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t next_pc;

    /* 先写 0xFFFFFFFF，SB 覆盖最低字节为 0x42 */
    mem_poke(sim, 0x5000, 0xFFFFFFFF);
    sim->cpu.regs[1] = 0x5000;
    sim->cpu.regs[2] = 0x42;

    /* SB x2,0(x1): imm[11:5]=0,rs2=2,rs1=1,funct3=0,imm[4:0]=0,opcode=0x23 → 0x00208023 */
    exec(sim, 0x00208023, &next_pc);
    CHECK(mem_peek(sim, 0x5000) == 0xFFFFFF42,
          "SB x2,0(x1)       → mem[0x5000]=0x%08X (expect 0xFFFFFF42)", mem_peek(sim, 0x5000));

    sim_destroy(sim); free(sim);
}

void test_sh(void)
{
    printf("\n─── SH ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t next_pc;

    /* 先写 0xFFFFFFFF，SH 覆盖低 2 字节为 0xABCD */
    mem_poke(sim, 0x6000, 0xFFFFFFFF);
    sim->cpu.regs[1] = 0x6000;
    sim->cpu.regs[2] = 0xABCD;

    /* SH x2,0(x1): imm[11:5]=0,rs2=2,rs1=1,funct3=1,imm[4:0]=0,opcode=0x23 → 0x00209023 */
    exec(sim, 0x00209023, &next_pc);
    CHECK(mem_peek(sim, 0x6000) == 0xFFFFABCD,
          "SH x2,0(x1)       → mem[0x6000]=0x%08X (expect 0xFFFFABCD)", mem_peek(sim, 0x6000));

    sim_destroy(sim); free(sim);
}

/* ================================================================
 * 第六组：剩余 ALU 指令（16 条）
 *   OP-IMM: SLLI/SLTI/SLTIU/XORI/SRLI/SRAI/ORI/ANDI
 *   OP:     SLL/SLT/SLTU/XOR/SRL/SRA/OR/AND
 * ================================================================ */

void test_slli(void)
{
    printf("\n─── SLLI ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t np;
    sim->cpu.regs[1] = 0x01;  /* 0x01 << 4 = 0x10 */
    /* SLLI x2,x1,4: imm=4,rs1=1,funct3=1,rd=2,funct7=0,opcode=0x13 → 0x00409113 */
    exec(sim, 0x00409113, &np);
    CHECK(sim->cpu.regs[2] == 0x10, "SLLI x2,x1,4    → x2=0x%08X (expect 0x10)", sim->cpu.regs[2]);
    sim_destroy(sim); free(sim);
}

void test_slti_true(void)
{
    printf("\n─── SLTI true ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t np;
    sim->cpu.regs[1] = (uint32_t)-5;  /* signed: -5 */
    /* SLTI x2,x1,10: -5<10→1, imm=10,rs1=1,funct3=2,rd=2,opcode=0x13 → 0x00A0A113 */
    exec(sim, 0x00A0A113, &np);
    CHECK(sim->cpu.regs[2] == 1, "SLTI x2,x1,10   → x2=%d (expect 1)", sim->cpu.regs[2]);
    sim_destroy(sim); free(sim);
}

void test_slti_false(void)
{
    printf("\n─── SLTI false ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t np;
    sim->cpu.regs[1] = 10;
    /* SLTI x2,x1,5: 10<5→0, imm=5,rs1=1,funct3=2,rd=2,opcode=0x13 → 0x0050A113 */
    exec(sim, 0x0050A113, &np);
    CHECK(sim->cpu.regs[2] == 0, "SLTI x2,x1,5    → x2=%d (expect 0)", sim->cpu.regs[2]);
    sim_destroy(sim); free(sim);
}

void test_sltiu(void)
{
    printf("\n─── SLTIU ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t np;
    sim->cpu.regs[1] = (uint32_t)-5;  /* unsigned: huge (~4.3B) */
    /* SLTIU x2,x1,10: 4294967291<10→0, imm=10,rs1=1,funct3=3,rd=2,opcode=0x13 → 0x00A0B113 */
    exec(sim, 0x00A0B113, &np);
    CHECK(sim->cpu.regs[2] == 0, "SLTIU x2,x1,10  → x2=%d (expect 0)", sim->cpu.regs[2]);
    sim_destroy(sim); free(sim);
}

void test_xori(void)
{
    printf("\n─── XORI ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t np;
    sim->cpu.regs[1] = 0x0FF0;
    /* XORI x2,x1,0x00F: 0x0FF0 ^ 0xF = 0x0FFF (0xF 是正数不触发符号扩展)
     * imm=0xF,rs1=1,funct3=4,rd=2,opcode=0x13 → (0xF<<20)|(1<<15)|(4<<12)|(2<<7)|0x13 = 0x00F0C113 */
    exec(sim, 0x00F0C113, &np);
    CHECK(sim->cpu.regs[2] == 0x0FFF, "XORI x2,x1,0x0F  → x2=0x%08X (expect 0x0FFF)", sim->cpu.regs[2]);
    sim_destroy(sim); free(sim);
}

void test_srli(void)
{
    printf("\n─── SRLI ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t np;
    sim->cpu.regs[1] = 0x80000000;
    /* SRLI x2,x1,4: 0x80000000 >> 4 = 0x08000000 (逻辑右移), imm=4,rs1=1,funct3=5,rd=2,funct7=0,opcode=0x13 → 0x0040D113 */
    exec(sim, 0x0040D113, &np);
    CHECK(sim->cpu.regs[2] == 0x08000000, "SRLI x2,x1,4    → x2=0x%08X (expect 0x08000000)", sim->cpu.regs[2]);
    sim_destroy(sim); free(sim);
}

void test_srai(void)
{
    printf("\n─── SRAI ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t np;
    sim->cpu.regs[1] = 0x80000000;
    /* SRAI x2,x1,4: 0x80000000 >>a 4 = 0xF8000000 (算术右移), funct7=0x20 → 0x4040D113 */
    exec(sim, 0x4040D113, &np);
    CHECK(sim->cpu.regs[2] == 0xF8000000, "SRAI x2,x1,4    → x2=0x%08X (expect 0xF8000000)", sim->cpu.regs[2]);
    sim_destroy(sim); free(sim);
}

void test_ori(void)
{
    printf("\n─── ORI ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t np;
    sim->cpu.regs[1] = 0x0F00;
    /* ORI x2,x1,0x0F: 0x0F00 | 0x0F = 0x0F0F, imm=0xF,rs1=1,funct3=6,rd=2,opcode=0x13 → 0x00F0E113 */
    exec(sim, 0x00F0E113, &np);
    CHECK(sim->cpu.regs[2] == 0x0F0F, "ORI x2,x1,0x0F  → x2=0x%08X (expect 0x0F0F)", sim->cpu.regs[2]);
    sim_destroy(sim); free(sim);
}

void test_andi(void)
{
    printf("\n─── ANDI ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t np;
    sim->cpu.regs[1] = 0xFF0F;
    /* ANDI x2,x1,0x0FF: 0xFF0F & 0xFF = 0x0F, imm=0xFF,rs1=1,funct3=7,rd=2,opcode=0x13 → 0x0FF0F113 */
    exec(sim, 0x0FF0F113, &np);
    CHECK(sim->cpu.regs[2] == 0x0F, "ANDI x2,x1,0xFF  → x2=0x%08X (expect 0x0F)", sim->cpu.regs[2]);
    sim_destroy(sim); free(sim);
}

/* ── OP（寄存器-寄存器）─────────────────────────────────────── */

void test_sll(void)
{
    printf("\n─── SLL ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t np;
    sim->cpu.regs[1] = 7; sim->cpu.regs[2] = 3;
    /* SLL x3,x1,x2: 7<<3=56, funct3=1 → 0x002091B3 */
    exec(sim, 0x002091B3, &np);
    CHECK(sim->cpu.regs[3] == 56, "SLL x3,x1,x2     → x3=%d (expect 56)", sim->cpu.regs[3]);
    sim_destroy(sim); free(sim);
}

void test_slt_true(void)
{
    printf("\n─── SLT true ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t np;
    sim->cpu.regs[1] = (uint32_t)-10; sim->cpu.regs[2] = 5;
    /* SLT x3,x1,x2: -10<5→1, funct3=2 → 0x0050A1B3 */
    exec(sim, 0x0050A1B3, &np);
    CHECK(sim->cpu.regs[3] == 1, "SLT x3,x1,x2     → x3=%d (expect 1)", sim->cpu.regs[3]);
    sim_destroy(sim); free(sim);
}

void test_sltu(void)
{
    printf("\n─── SLTU ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t np;
    sim->cpu.regs[1] = (uint32_t)-10; sim->cpu.regs[2] = 5;  /* unsigned: huge vs 5 */
    /* SLTU x3,x1,x2: 4294967286<5→0, funct3=3 → 0x0050B1B3 */
    exec(sim, 0x0050B1B3, &np);
    CHECK(sim->cpu.regs[3] == 0, "SLTU x3,x1,x2    → x3=%d (expect 0)", sim->cpu.regs[3]);
    sim_destroy(sim); free(sim);
}

void test_xor(void)
{
    printf("\n─── XOR ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t np;
    sim->cpu.regs[1] = 0x0FF0; sim->cpu.regs[2] = 0xF0F;
    /* XOR x3,x1,x2: funct3=4 → (0<<25)|(2<<20)|(1<<15)|(4<<12)|(3<<7)|0x33 = 0x0020C1B3 */
    exec(sim, 0x0020C1B3, &np);
    CHECK(sim->cpu.regs[3] == 0x00FF, "XOR x3,x1,x2     → x3=0x%08X (expect 0xFF)", sim->cpu.regs[3]);
    sim_destroy(sim); free(sim);
}

void test_srl(void)
{
    printf("\n─── SRL ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t np;
    sim->cpu.regs[1] = 0x80000000; sim->cpu.regs[2] = 4;
    /* SRL x3,x1,x2: funct3=5,funct7=0 → (0<<25)|(2<<20)|(1<<15)|(5<<12)|(3<<7)|0x33 = 0x0020D1B3 */
    exec(sim, 0x0020D1B3, &np);
    CHECK(sim->cpu.regs[3] == 0x08000000, "SRL x3,x1,x2     → x3=0x%08X (expect 0x08000000)", sim->cpu.regs[3]);
    sim_destroy(sim); free(sim);
}

void test_sra(void)
{
    printf("\n─── SRA ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t np;
    sim->cpu.regs[1] = 0x80000000; sim->cpu.regs[2] = 4;
    /* SRA x3,x1,x2: funct3=5,funct7=0x20 → (0x20<<25)|(2<<20)|(1<<15)|(5<<12)|(3<<7)|0x33 = 0x4020D1B3 */
    exec(sim, 0x4020D1B3, &np);
    CHECK(sim->cpu.regs[3] == 0xF8000000, "SRA x3,x1,x2     → x3=0x%08X (expect 0xF8000000)", sim->cpu.regs[3]);
    sim_destroy(sim); free(sim);
}

void test_or(void)
{
    printf("\n─── OR ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t np;
    sim->cpu.regs[1] = 0x0F00; sim->cpu.regs[2] = 0x00F0;
    /* OR x3,x1,x2: funct3=6 → (0<<25)|(2<<20)|(1<<15)|(6<<12)|(3<<7)|0x33 = 0x0020E1B3 */
    exec(sim, 0x0020E1B3, &np);
    CHECK(sim->cpu.regs[3] == 0x0FF0, "OR x3,x1,x2      → x3=0x%08X (expect 0x0FF0)", sim->cpu.regs[3]);
    sim_destroy(sim); free(sim);
}

void test_and(void)
{
    printf("\n─── AND ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t np;
    sim->cpu.regs[1] = 0xFF0F; sim->cpu.regs[2] = 0x0FF0;
    /* AND x3,x1,x2: funct3=7 → (0<<25)|(2<<20)|(1<<15)|(7<<12)|(3<<7)|0x33 = 0x0020F1B3 */
    exec(sim, 0x0020F1B3, &np);
    CHECK(sim->cpu.regs[3] == 0x0F00, "AND x3,x1,x2     → x3=0x%08X (expect 0x0F00)", sim->cpu.regs[3]);
    sim_destroy(sim); free(sim);
}

/* ================================================================
 * 第七组：FENCE + SYSTEM (ecall/ebreak)
 * ================================================================ */

void test_fence(void)
{
    printf("\n─── FENCE (NOP) ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t np = 0xDEAD;

    /* FENCE: opcode=0x0F, 单核顺序执行当 NOP，只验证 next_pc 和 no trap
     * 机器码 = 0x0FF0000F */
    exec(sim, 0x0FF0000F, &np);
    CHECK(np == 0x80000004, "FENCE            → next_pc=0x%08X (expect 0x80000004)", np);

    sim_destroy(sim); free(sim);
}

void test_ecall(void)
{
    printf("\n─── ECALL ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000000; uint32_t np;
    /* 设置 mtvec 防止 trap 时 CPU 停机 */
    sim->cpu.mtvec = 0x80001000;

    /* ECALL: opcode=0x73, funct3=0, imm=0 → 0x00000073 */
    exec(sim, 0x00000073, &np);

    CHECK(sim->cpu.mcause == EXC_ECALL_M,
          "ECALL            → mcause=%u (expect %u=EXC_ECALL_M)", sim->cpu.mcause, EXC_ECALL_M);
    CHECK(sim->cpu.mepc == 0x80000000,
          "mepc             → 0x%08X (expect 0x80000000)", sim->cpu.mepc);

    sim_destroy(sim); free(sim);
}

void test_ebreak(void)
{
    printf("\n─── EBREAK ───\n");
    Simulator *sim = new_sim();
    sim->cpu.pc = 0x80000004; uint32_t np;
    sim->cpu.mtvec = 0x80001000;

    /* EBREAK: opcode=0x73, funct3=0, imm=1 → 0x00100073 */
    exec(sim, 0x00100073, &np);

    CHECK(sim->cpu.mcause == EXC_BREAKPOINT,
          "EBREAK           → mcause=%u (expect %u=EXC_BREAKPOINT)", sim->cpu.mcause, EXC_BREAKPOINT);
    CHECK(sim->cpu.mepc == 0x80000004,
          "mepc             → 0x%08X (expect 0x80000004)", sim->cpu.mepc);

    sim_destroy(sim); free(sim);
}

/* ================================================================
 * 主测试入口
 * ================================================================ */
int main(void)
{
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║  execute.c 单元测试 — 全部 RV32I 指令            ║\n");
    printf("╚══════════════════════════════════════════════════╝\n");

    /* 第一组：LUI + AUIPC */
    printf("\n═══ 第一组：LUI + AUIPC ═══");
    test_lui();
    test_lui_zero();
    test_auipc();
    test_auipc_negative();

    /* 第二组：ADDI + ADD + SUB */
    printf("\n═══ 第二组：ADDI + ADD + SUB ═══");
    test_addi_positive();
    test_addi_negative();
    test_addi_from_zero();
    test_add();
    test_sub();
    test_sub_negative();

    /* 第三组：JAL + JALR */
    printf("\n═══ 第三组：JAL + JALR ═══");
    test_jal();
    test_jalr();
    test_jalr_alignment();

    /* 第四组：B-type 分支指令 */
    printf("\n═══ 第四组：B-type 分支 ═══");
    test_beq_taken();
    test_beq_not_taken();
    test_bne_taken();
    test_bne_not_taken();
    test_blt_taken();
    test_blt_not_taken();
    test_bge_taken();
    test_bltu_taken();
    test_bgeu_taken();

    /* 第五组：Load + Store */
    printf("\n═══ 第五组：Load + Store ═══");
    test_lw();
    test_lw_offset();
    test_sw();
    test_lh();
    test_lhu();
    test_lb();
    test_lbu();
    test_sb();
    test_sh();

    /* 第六组：剩余 ALU 指令 */
    printf("\n═══ 第六组：剩余 ALU ═══");
    test_slli();
    test_slti_true();
    test_slti_false();
    test_sltiu();
    test_xori();
    test_srli();
    test_srai();
    test_ori();
    test_andi();
    test_sll();
    test_slt_true();
    test_sltu();
    test_xor();
    test_srl();
    test_sra();
    test_or();
    test_and();

    /* 第七组：SYSTEM + FENCE */
    printf("\n═══ 第七组：SYSTEM + FENCE ═══");
    test_fence();
    test_ecall();
    test_ebreak();

    printf("\n══════════════════════════════════════════════════\n");
    printf("结果: %d passed, %d failed\n", passed, failed);
    printf("══════════════════════════════════════════════════\n");

    return failed > 0 ? 1 : 0;
}