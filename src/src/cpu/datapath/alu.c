/* ============================================================
 * alu.c — RISC-V ALU 组合逻辑实现
 *
 * 这是 CPU 数据通路中 ALU 的软件模型——一块纯组合逻辑电路。
 *
 * 三个函数：
 *   alu_compute()      — 算术/逻辑运算（ADD/SUB/SLL/SLT/...）
 *   alu_branch_cond()  — 分支条件判断（BEQ/BNE/BLT/...）
 *   alu_select_op()    — funct3/funct7 → AluOp 映射
 *
 * 零依赖：只 include "cpu/datapath/alu.h"。
 * 无副作用、无全局状态、无内存访问、无模拟器依赖。
 *
 * 测试：可编写独立单元测试，只需 #include "alu.h"，
 * 对每个 AluOp 跑真值表验证即可。
 * ============================================================
 */

#include "cpu/datapath/alu.h"

/* ════════════════════════════════════════════════════════════
 * alu_compute — 纯组合逻辑 ALU
 *
 * 每个 case 对应一块组合逻辑电路：
 *   ALU_ADD  → 32 位加法器
 *   ALU_SUB  → 32 位减法器（有符号）
 *   ALU_SLL  → 桶形左移器（移位量 = b[4:0]）
 *   ALU_SLT  → 有符号比较器
 *   ALU_SLTU → 无符号比较器
 *   ALU_XOR  → 按位异或门阵列
 *   ALU_SRL  → 桶形逻辑右移器
 *   ALU_SRA  → 桶形算术右移器（符号扩展）
 *   ALU_OR   → 按位或门阵列
 *   ALU_AND  → 按位与门阵列
 *
 * 移位量约定（RISC-V 规范）：
 *   对 32 位操作，移位量只使用低 5 位（b & 0x1F）。
 *   该掩码在此函数内部完成，调用方无需预处理。
 * ════════════════════════════════════════════════════════════
 */
uint32_t alu_compute(AluOp op, uint32_t a, uint32_t b)
{
    switch (op) {

    case ALU_ADD:
        return a + b;

    case ALU_SUB:
        /* 有符号减法：转为 int32_t 计算后转回 uint32_t
         * 位级结果与无符号减法相同，但语义清晰 */
        return (uint32_t)((int32_t)a - (int32_t)b);

    case ALU_SLL:
        /* 逻辑左移：移位量 = b[4:0] */
        return a << (b & 0x1Fu);

    case ALU_SLT:
        /* 有符号比较：a < b (int32_t) → 1，否则 → 0 */
        return ((int32_t)a < (int32_t)b) ? 1u : 0u;

    case ALU_SLTU:
        /* 无符号比较：a < b (uint32_t) → 1，否则 → 0 */
        return (a < b) ? 1u : 0u;

    case ALU_XOR:
        return a ^ b;

    case ALU_SRL:
        /* 逻辑右移：高位填 0，移位量 = b[4:0] */
        return a >> (b & 0x1Fu);

    case ALU_SRA:
        /* 算术右移：高位填符号位，移位量 = b[4:0]
         * 先转 int32_t 让 C 编译器做算术右移 */
        return (uint32_t)((int32_t)a >> (int32_t)(b & 0x1Fu));

    case ALU_OR:
        return a | b;

    case ALU_AND:
        return a & b;

    default:
        /* 非法 ALU 操作码 → 返回 0（硬件中对应输出全零） */
        return 0u;
    }
}

/* ════════════════════════════════════════════════════════════
 * alu_branch_cond — 分支条件判断（B-type 比较器）
 *
 * 6 种条件（funct3 = 0/1/4/5/6/7）：
 *   0 = BEQ  — a == b
 *   1 = BNE  — a != b
 *   4 = BLT  — (int32_t)a < (int32_t)b
 *   5 = BGE  — (int32_t)a >= (int32_t)b
 *   6 = BLTU — a < b（无符号）
 *   7 = BGEU — a >= b（无符号）
 *
 * 注：funct3=2 (FLT) 和 funct3=3 (FLE) 是浮点比较，
 * 不经过 ALU，由 exec_f.c 直接处理。
 * ════════════════════════════════════════════════════════════
 */
bool alu_branch_cond(uint8_t funct3, uint32_t a, uint32_t b)
{
    switch (funct3) {
    case 0:  /* BEQ  — 相等 */
        return a == b;
    case 1:  /* BNE  — 不等 */
        return a != b;
    case 4:  /* BLT  — 有符号小于 */
        return (int32_t)a < (int32_t)b;
    case 5:  /* BGE  — 有符号大于等于 */
        return (int32_t)a >= (int32_t)b;
    case 6:  /* BLTU — 无符号小于 */
        return a < b;
    case 7:  /* BGEU — 无符号大于等于 */
        return a >= b;
    default:
        /* 非法 funct3 → 不跳转（硬件中等价于比较器输出 0） */
        return false;
    }
}

/* ════════════════════════════════════════════════════════════
 * alu_select_op — funct3 + funct7 → AluOp 映射
 *
 * RV32I OP (0x33) 和 OP-IMM (0x13) 共用此映射。
 *
 * 关键区分点（需要 funct7 参与判断）：
 *   - funct3=0, R-type + funct7=0x20 → ALU_SUB（否则 ALU_ADD）
 *   - funct3=5, funct7=0x20         → ALU_SRA（否则 ALU_SRL）
 *
 * 对于 I-type (OP-IMM)：
 *   - funct3=0 始终是 ALU_ADD（ADDI），不存在 SUBI
 *   - funct3=5 仍需 funct7 区分 SRLI(funct7=0x00) 和 SRAI(funct7=0x20)
 *
 * 此函数供三处使用：
 *   1. exec_op_imm() — 获取 I-type 立即数 ALU 操作
 *   2. exec_op()     — 获取 R-type 寄存器 ALU 操作
 *   3. pipeline.c    — ID 阶段预知 ALU 操作（forwarding 冒险检测用）
 * ════════════════════════════════════════════════════════════
 */
AluOp alu_select_op(uint8_t funct3, uint8_t funct7, bool is_r_type)
{
    switch (funct3) {
    case 0:
        /* ADD 或 SUB：只有 R-type + funct7=0x20 才是 SUB */
        if (is_r_type && funct7 == 0x20) {
            return ALU_SUB;
        }
        return ALU_ADD;

    case 1:
        return ALU_SLL;

    case 2:
        return ALU_SLT;

    case 3:
        return ALU_SLTU;

    case 4:
        return ALU_XOR;

    case 5:
        /* SRL 或 SRA：funct7[5] (bit 30) 区分 */
        if (funct7 == 0x20) {
            return ALU_SRA;
        }
        return ALU_SRL;

    case 6:
        return ALU_OR;

    case 7:
        return ALU_AND;

    default:
        /* 非法 funct3 → 默认 ALU_ADD（调用方应检查合法性） */
        return ALU_ADD;
    }
}
