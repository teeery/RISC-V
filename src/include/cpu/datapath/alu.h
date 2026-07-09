#ifndef ALU_H
#define ALU_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * alu.h — RISC-V ALU 组合逻辑接口
 *
 * 这是 CPU 数据通路的核心组件，三种控制器（单周期/多周期/流水线）
 * 共享同一份 ALU 实现。
 *
 * 设计原则：
 *   - 纯组合逻辑：无副作用、无全局状态、无内存访问
 *   - 输入 = 操作码 + 两个 32 位操作数 → 输出 = 32 位结果
 *   - 可脱离模拟器独立编译和单元测试
 *
 * 参考：Patterson & Hennessy, Chapter 4 (The Processor)
 *       RISC-V Unprivileged ISA Specification, Chapter 2 (RV32I)
 * ============================================================
 */

/* ── ALU 操作码枚举 ──────────────────────────────────────────
 *
 * 覆盖 RV32I 基础整数指令集所需的全部 ALU 操作。
 * M 扩展（乘除法）不在 ALU 中——真实硬件中乘法器/除法器
 * 是独立的执行单元，不在 ALU 的数据通路内。
 */
typedef enum {
    ALU_ADD,    /* 加法：a + b               — ADDI, ADD, AUIPC, 地址计算 */
    ALU_SUB,    /* 减法：a - b               — SUB            */
    ALU_SLL,    /* 逻辑左移：a << b[4:0]      — SLLI, SLL      */
    ALU_SLT,    /* 有符号比较：a < b ? 1 : 0  — SLTI, SLT      */
    ALU_SLTU,   /* 无符号比较：a < b ? 1 : 0  — SLTIU, SLTU    */
    ALU_XOR,    /* 按位异或：a ^ b            — XORI, XOR      */
    ALU_SRL,    /* 逻辑右移：a >> b[4:0]      — SRLI, SRL      */
    ALU_SRA,    /* 算术右移：a >> b[4:0](符号) — SRAI, SRA      */
    ALU_OR,     /* 按位或：a | b             — ORI, OR        */
    ALU_AND,    /* 按位与：a & b             — ANDI, AND      */
} AluOp;

/* ============================================================
 * alu_compute — 纯组合逻辑 ALU
 *
 * 参数：
 *   op — ALU 操作码（AluOp 枚举）
 *   a  — 操作数 A（32 位无符号，语义取决于 op）
 *   b  — 操作数 B（32 位无符号，移位操作自动取 b[4:0]）
 *
 * 返回：32 位运算结果
 *
 * 注意：
 *   - ALU_SUB 按有符号语义计算（a 和 b 转为 int32_t 后相减）
 *   - ALU_SLT 按有符号比较，ALU_SLTU 按无符号比较
 *   - 移位量 = b & 0x1F（RISC-V 规范：只使用低 5 位）
 *   - 此函数与任何 CPU 状态无关，是纯粹的硬件组合逻辑
 * ============================================================
 */
uint32_t alu_compute(AluOp op, uint32_t a, uint32_t b);

/* ============================================================
 * alu_branch_cond — 分支条件判断
 *
 * 参数：
 *   funct3 — 解码后的 funct3 字段（B-type 指令的 [14:12]）
 *   a      — rs1 的值
 *   b      — rs2 的值
 *
 * 返回：true = 分支应跳转，false = 不跳转
 *
 * funct3 → 条件映射：
 *   0 = BEQ  (相等)
 *   1 = BNE  (不等)
 *   4 = BLT  (有符号小于)
 *   5 = BGE  (有符号大于等于)
 *   6 = BLTU (无符号小于)
 *   7 = BGEU (无符号大于等于)
 *
 * 非法 funct3 值默认返回 false。
 * ============================================================
 */
bool alu_branch_cond(uint8_t funct3, uint32_t a, uint32_t b);

/* ============================================================
 * alu_select_op — 从译码字段选择 ALU 操作
 *
 * 把指令译码的 funct3 + funct7 映射到具体的 ALU 操作码。
 * 这是 RV32I OP 和 OP-IMM 指令共用的映射逻辑。
 *
 * 参数：
 *   funct3     — 指令的 funct3 字段 [14:12]
 *   funct7     — 指令的 funct7 字段 [31:25]
 *   is_r_type  — true = R-type (OP, 0x33)，funct3=0 时需 funct7 区分 ADD/SUB
 *                false = I-type (OP-IMM, 0x13)，funct3=0 始终为 ADD
 *
 * 返回：对应的 AluOp 操作码
 *
 * 使用场景：
 *   - execute/ 下各 exec 函数调用它来获知当前指令的 ALU 操作
 *   - 流水线控制器在 ID 阶段调用它，提前知道 EX 阶段将使用哪个 ALU 操作
 *     （用于 forwarding 冒险检测——判断相邻指令是否存在 RAW 依赖）
 * ============================================================
 */
AluOp alu_select_op(uint8_t funct3, uint8_t funct7, bool is_r_type);

#endif /* ALU_H */
