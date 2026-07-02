/* ============================================================================
 * decode.c — RISC-V 指令解码器 + 反汇编器
 *
 * 本文件的定位：
 *   CPU 流水线的"译码级"——不执行任何计算，只负责把 32 位二进制指令
 *   翻译成结构化的字段（DecodedInstr）或人类可读的字符串（disasm）。
 *
 * 依赖关系：
 *   零依赖！只 include "cpu/decode.h"。不依赖 CPU 状态、内存、MMU。
 *   这意味着 decode.c 可以完全脱离模拟器单独编译 + 单独测试。
 *
 * 两个核心函数：
 * ┌────────────────┬──────────────────────────────────────┐
 * │ cpu_decode()   │ 把 32bit 二进制 → DecodedInstr 结构体 │
 * │                │ 给 execute.c 用（机器消费）           │
 * ├────────────────┼──────────────────────────────────────┤
 * │ cpu_disasm()   │ 把 32bit 二进制 → "addi x3,x0,42"    │
 * │                │ 给 debugger.c 用（人类消费）          │
 * └────────────────┴──────────────────────────────────────┘
 *
 * cpu_decode 的工作流程：
 *   1. 提取 opcode（低 7 位）→ 判断指令格式（R/I/S/B/U/J）
 *   2. 提取 rd / rs1 / rs2（这 3 个字段在所有格式中位置相同）
 *   3. 提取 funct3 / funct7（R-type 用 funct7，其他格式 funct3 就够了）
 *   4. 根据格式选对应的立即数宏（IMM_I / IMM_S / IMM_B / IMM_U / IMM_J）
 *   5. 填入 DecodedInstr 结构体返回
 *
 * cpu_disasm 的工作流程：
 *   1. 调 cpu_decode 得到字段
 *   2. 根据 opcode + funct3（+ funct7）查表得到指令名（如 "addi"）
 *   3. 根据指令格式拼参数：
 *      - R-type: "rd, rs1, rs2"          例: "add x1, x2, x3"
 *      - I-type 算术: "rd, rs1, imm"      例: "addi x1, x2, 42"
 *      - I-type Load: "rd, imm(rs1)"      例: "lw x1, 8(x2)"
 *      - S-type: "rs2, imm(rs1)"          例: "sw x1, 8(x2)"
 *      - B-type: "rs1, rs2, offset"       例: "beq x1, x2, 0x80000100"
 *      - U-type: "rd, imm"                例: "lui x1, 0x12345000"
 *      - J-type: "rd, offset"             例: "jal x1, 0x80000100"
 *   4. 写入 buf（snprintf，注意不越界）
 *
 * RV32I 指令集对照表（你需要实现的全部 opcode）：
 *
 *   opcode  | 格式 | 指令（按 funct3 细分）
 *   ────────┼──────┼─────────────────────────────────────
 *   0x37    │  U   │ LUI
 *   0x17    │  U   │ AUIPC
 *   0x6F    │  J   │ JAL
 *   0x67    │  I   │ JALR           (funct3=0)
 *   0x63    │  B   │ BEQ/BNE/BLT/BGE/BLTU/BGEU  (funct3=0~7)
 *   0x03    │  I   │ LB/LH/LW/LBU/LHU           (funct3=0~4)
 *   0x23    │  S   │ SB/SH/SW                   (funct3=0~2)
 *   0x13    │  I   │ ADDI/SLTI/SLTIU/XORI/ORI/ANDI/SLLI/SRLI/SRAI
 *          │      │ (funct3=0~7, SLLI/SRLI/SRAI 还需 funct7 区分)
 *   0x33    │  R   │ ADD/SUB/SLL/SLT/SLTU/XOR/SRL/SRA/OR/AND
 *          │      │ (funct3=0~7, ADD/SUB 和 SRL/SRA 需 funct7 区分)
 *   0x0F    │  I   │ FENCE / FENCE.I  (基础模拟器当 NOP)
 *   0x73    │  I   │ ECALL/EBREAK/MRET/CSR  (特权指令，funct3 区分)
 *
 * ============================================================================ */

#include "cpu/decode.h"   // DecodedInstr, 位域宏, 立即数宏
#include <stdio.h>        // snprintf
#include <string.h>       // 暂不需要，预留给后续

/* ============================================================================
 * 第 1 部分：内部查找表（static const，仅本文件可见）
 * ============================================================================
 *
 * 这些表用于 cpu_disasm —— 把 opcode + funct3 映射到指令名字符串。
 *
 * 你需要两张表：
 *
 *   表 A：指令名表
 *   ─────────────
 *   对于大多数指令，"opcode + funct3" 就唯一确定了指令名。
 *   例如 opcode=0x13, funct3=0 → "addi"
 *        opcode=0x33, funct3=0 → 可能是 "add" 或 "sub"（需看 funct7）
 *
 *   你需要定义一个结构体数组，每项包含：
 *     opcode, funct3, funct7, 指令名字符串
 *   funct7=-1 表示 "不关心 funct7"（通配）。
 *
 *   表 B：寄存器 ABI 名称表
 *   ──────────────────────
 *   把寄存器号 (0-31) 映射到 ABI 名称。
 *   索引 0 → "zero", 1 → "ra", 2 → "sp", 3 → "gp", 4 → "tp",
 *   5-7 → "t0"~"t2", 8 → "s0", 9 → "s1", 10-17 → "a0"~"a7",
 *   18-27 → "s2"~"s11", 28-31 → "t3"~"t6"
 *
 *   → 使用方式：reg_names[rd] 就得到目标寄存器的 ABI 名
 */

// TODO: 定义指令名查找表结构体
// 提示：包含 opcode, funct3, funct7（用 -1 表示不关心）, 指令名
// 大概有 40 条左右的条目

//指令表的结构体（定义"列"）
typedef struct {
    uint8_t opcode;   // 操作码（低 7 位）
    uint8_t funct3;   // 功能码 3（3 位，细分指令）
    int8_t  funct7;   // 功能码 7（7 位，进一步细分）——注意：是 int8_t，
                       //   -1 表示"不关心 funct7"，0x00 或 0x20 表示"必须匹配这个值"
    char   *name;     // 指令名字符串（如 "addi"）
} InstrEntry;

//指令表数组（定义"行"）——共 37 条，覆盖全部 RV32I 指令
static const InstrEntry instr_table[] = {
    // ── U-type：高位立即数 ──────────────────────────
    { 0x37, 0, -1,    "lui"   },
    { 0x17, 0, -1,    "auipc" },

    // ── J-type / I-type：跳转 ───────────────────────
    { 0x6F, 0, -1,    "jal"   },
    { 0x67, 0, -1,    "jalr"  },

    // ── B-type：分支（6 条，靠 funct3 区分）─────────
    { 0x63, 0, -1,    "beq"   },
    { 0x63, 1, -1,    "bne"   },
    { 0x63, 4, -1,    "blt"   },
    { 0x63, 5, -1,    "bge"   },
    { 0x63, 6, -1,    "bltu"  },
    { 0x63, 7, -1,    "bgeu"  },

    // ── I-type：Load（5 条）─────────────────────────
    { 0x03, 0, -1,    "lb"    },
    { 0x03, 1, -1,    "lh"    },
    { 0x03, 2, -1,    "lw"    },
    { 0x03, 4, -1,    "lbu"   },
    { 0x03, 5, -1,    "lhu"   },

    // ── S-type：Store（3 条）────────────────────────
    { 0x23, 0, -1,    "sb"    },
    { 0x23, 1, -1,    "sh"    },
    { 0x23, 2, -1,    "sw"    },

    // ── I-type：立即数算术（9 条）───────────────────
    //      注意：slli/srli/srai 的 funct7 不能写 -1，因为它们的
    //      opcode 和 funct3 都一样（0x13, funct3=1 或 5），只能靠 funct7 区分！
    { 0x13, 0, -1,    "addi"  },
    { 0x13, 1, 0x00,  "slli"  },   // funct7=0x00 → 逻辑左移
    { 0x13, 2, -1,    "slti"  },
    { 0x13, 3, -1,    "sltiu" },
    { 0x13, 4, -1,    "xori"  },
    { 0x13, 5, 0x00,  "srli"  },   // funct7=0x00 → 逻辑右移
    { 0x13, 5, 0x20,  "srai"  },   // funct7=0x20 → 算术右移
    { 0x13, 6, -1,    "ori"   },
    { 0x13, 7, -1,    "andi"  },

    // ── R-type：寄存器算术（10 条）──────────────────
    //      注意：add/sub 的 opcode+funct3 相同（0x33, funct3=0），
    //      srl/sra 也相同（0x33, funct3=5），只能靠 funct7 区分！
    { 0x33, 0, 0x00,  "add"   },   // funct7=0x00 → 加法
    { 0x33, 0, 0x20,  "sub"   },   // funct7=0x20 → 减法
    { 0x33, 1, 0x00,  "sll"   },
    { 0x33, 2, 0x00,  "slt"   },
    { 0x33, 3, 0x00,  "sltu"  },
    { 0x33, 4, 0x00,  "xor"   },
    { 0x33, 5, 0x00,  "srl"   },   // funct7=0x00 → 逻辑右移
    { 0x33, 5, 0x20,  "sra"   },   // funct7=0x20 → 算术右移
    { 0x33, 6, 0x00,  "or"    },
    { 0x33, 7, 0x00,  "and"   },
};  // ← 注意分号！

//寄存器 ABI 名称数组：把寄存器号 (0-31) 映射到汇编里用的名字
//索引就是寄存器号，直接 reg_names[10] → "a0"
static const char *reg_names[32] = {
    "zero", "ra", "sp", "gp", "tp",   // x0-x4
    "t0",   "t1", "t2",               // x5-x7
    "s0",   "s1",                      // x8-x9 (s0=fp)
    "a0",   "a1", "a2", "a3",
    "a4",   "a5", "a6", "a7",         // x10-x17
    "s2",   "s3", "s4", "s5",
    "s6",   "s7", "s8", "s9",
    "s10",  "s11",                     // x18-x27
    "t3",   "t4", "t5", "t6",         // x28-x31
};


/* ============================================================================
 * 第 2 部分：cpu_decode —— 二进制 → 结构体
 * ============================================================================
 *
 * 这是整个文件最核心的函数。你要做的事：
 *
 * Step 1: 提取固定字段
 *   d.opcode = OPCODE(instr);   // 宏已在 decode.h 定义好了
 *   d.rd     = RD(instr);
 *   d.rs1    = RS1(instr);
 *   d.rs2    = RS2(instr);
 *   d.funct3 = FUNCT3(instr);
 *   d.funct7 = FUNCT7(instr);
 *
 * Step 2: 根据 opcode 判断格式 + 提取立即数
 *   用一个 switch (d.opcode) 分发：
 *
 *   case 0x37: // LUI       → U-type, imm = IMM_U(instr)
 *   case 0x17: // AUIPC     → U-type, imm = IMM_U(instr)
 *   case 0x6F: // JAL       → J-type, imm = IMM_J(instr)
 *   case 0x67: // JALR      → I-type, imm = IMM_I(instr)
 *   case 0x63: // Branch    → B-type, imm = IMM_B(instr)
 *   case 0x03: // Load      → I-type, imm = IMM_I(instr)
 *   case 0x23: // Store     → S-type, imm = IMM_S(instr)
 *   case 0x13: // OP-IMM    → I-type, imm = IMM_I(instr)
 *   case 0x33: // OP        → R-type, imm = 0（无立即数）
 *   case 0x0F: // FENCE     → I-type, imm = IMM_I(instr)（值无视）
 *   case 0x73: // SYSTEM    → I-type, imm = IMM_I(instr)（ecall/ebreak 用）
 *   default:   // 非法指令 → 可以设 opcode=0 或返回全零结构体
 *
 * Step 3: 符号扩展
 *   IMM_I/IMM_S/IMM_B/IMM_J 宏内部用 (int32_t) 转换，已经做了符号扩展。
 *   IMM_U 返回的是 uint32_t 按位与的结果，高位直接在 bit[31:12]，
 *   转为 int32_t 后就是正确的值（因为 U-type 的立即数不涉及符号扩展）。
 *
 * Step 4: 返回 d
 *
 * 关键点：
 *   - R-type 没有立即数，设 imm = 0
 *   - 不同格式即使 opcode 相同（如 0x13 和 0x67 都是 I-type），
 *     但因为都使用 IMM_I，立即数提取逻辑完全相同
 *   - 非法 opcode 的处理：可以返回全零结构体，execute.c 会检测到 opcode==0
 *     然后产生非法指令异常
 */

DecodedInstr cpu_decode(uint32_t instr)
{
    DecodedInstr d;

    // 初始化 d 的所有字段为 0
    d.opcode=0;
    d.rd=0;
    d.rs1=0;
    d.rs2=0;
    d.funct3=0;
    d.funct7=0;
    d.imm=0;

    // Step 1 — 用宏提取 opcode / rd / rs1 / rs2 / funct3 / funct7
    d.opcode = OPCODE(instr);
    d.rd = RD(instr);
    d.rs1 = RS1(instr);
    d.rs2 = RS2(instr);
    d.funct3 = FUNCT3(instr);
    d.funct7 = FUNCT7(instr);

    // Step 2 — switch(opcode) 设置 d.imm
    //   提示：每个 case 只需要一行，调对应的 IMM_* 宏
    //   例: case 0x37: d.imm = IMM_U(instr); break;  // LUI
    switch (d.opcode) {

        case 0x37: // LUI
        case 0x17: // AUIPC
            d.imm = IMM_U(instr);
            break;

        case 0x6F: // JAL
            d.imm = IMM_J(instr);
            break;

        case 0x67: // JALR
        case 0x03: // Load
        case 0x13: // OP-IMM
        case 0x0F: // FENCE
        case 0x73: //SYSTEM
            d.imm = IMM_I(instr);
            break;

        case 0x63: // Branch
            d.imm = IMM_B(instr);
            break;

        case 0x23: // Store
            d.imm = IMM_S(instr);
            break;
        
        case 0x33: // OP
            d.imm = 0; // R-type 没有立即数
            break;
      
        default:
            d.opcode = 0; // 非法指令，设 opcode=0，execute.c 会处理异常
            break;
    }

    // Step 3 — 返回 d
    return d;
}


/* ============================================================================
 * 第 3 部分：cpu_disasm —— 二进制 → 汇编字符串
 * ============================================================================
 *
 * 这个函数把一个 32 位指令 + 当前 PC 值 → 人类可读字符串。
 *
 * 参数：
 *   instr  — 32 位原始指令
 *   pc     — 当前指令地址（用于计算分支/跳转的目标地址）
 *   buf    — 输出缓冲区（由调用者提供）
 *   bufsz  — 缓冲区大小（避免溢出）
 *
 * 工作流程：
 *
 * Step 1: 调 cpu_decode(instr) 得到 d
 *
 * Step 2: 在指令名查找表中搜索 (d.opcode, d.funct3, d.funct7)
 *   - 优先匹配 funct7 完全相等的
 *   - funct7 = -1 的条目是通配（fallback）
 *   - 找不到 → 输出 "unknown"
 *
 * Step 3: 根据指令格式拼参数
 *
 *   R-type (opcode=0x33):
 *     snprintf(buf, bufsz, "%-7s %s, %s, %s",
 *              name, reg[d.rd], reg[d.rs1], reg[d.rs2]);
 *     例: "add    s0, t0, t1"
 *
 *   I-type 算术 (opcode=0x13 或 0x67):
 *     snprintf(buf, bufsz, "%-7s %s, %s, %d",
 *              name, reg[d.rd], reg[d.rs1], d.imm);
 *     例: "addi   t0, s0, 42"
 *
 *   I-type Load (opcode=0x03):
 *     snprintf(buf, bufsz, "%-7s %s, %d(%s)",
 *              name, reg[d.rd], d.imm, reg[d.rs1]);
 *     例: "lw     t0, 8(sp)"
 *
 *   S-type (opcode=0x23):
 *     snprintf(buf, bufsz, "%-7s %s, %d(%s)",
 *              name, reg[d.rs2], d.imm, reg[d.rs1]);
 *     例: "sw     t0, 8(sp)"
 *
 *   B-type (opcode=0x63):
 *     snprintf(buf, bufsz, "%-7s %s, %s, 0x%x",
 *              name, reg[d.rs1], reg[d.rs2], pc + d.imm);
 *     例: "beq    t0, t1, 0x80000100"
 *     注意：目标地址 = pc + imm（imm 已经是符号扩展的偏移量）
 *
 *   U-type (opcode=0x37, 0x17):
 *     snprintf(buf, bufsz, "%-7s %s, 0x%x",
 *              name, reg[d.rd], d.imm);
 *     例: "lui    x1, 0x12345000"
 *
 *   J-type (opcode=0x6F):
 *     snprintf(buf, bufsz, "%-7s %s, 0x%x",
 *              name, reg[d.rd], pc + d.imm);
 *     例: "jal    ra, 0x80000100"
 *
 *   SYSTEM (opcode=0x73):
 *     这类指令比较特殊——ecall/ebreak/mret 没有寄存器操作数。
 *     ecall: 直接输出 "ecall"
 *     ebreak: 直接输出 "ebreak"
 *     mret: 直接输出 "mret"
 *     (暂时可以不处理 CSR 指令)
 *
 * 格式对齐技巧：
 *   用 "%-7s" 让指令名占 7 个字符宽度左对齐，输出整齐好看。
 *   参考 debugger.c 第 234 行的调用方式：
 *     cpu_disasm(insn, pc, disasm, sizeof(disasm));
 *     printf("0x%08x: %08x  %s\n", pc, insn, disasm);
 *
 * 注意：
 *   - 永远用 snprintf，不要用 sprintf（缓冲区溢出风险）
 *   - 立即数可能是负数（符号扩展后），ADDI/SLTI 等用 %d，
 *     LUI/AUIPC/地址等用 0x%x
 *   - 如果找不到指令名，至少输出 "unknown" 和 opcode 值方便调试
 */

void cpu_disasm(uint32_t instr, uint32_t pc, char *buf, size_t bufsz)
{
    // TODO: Step 1 — 调 cpu_decode(instr)
    DecodedInstr d = cpu_decode(instr);

    // TODO: Step 2 — 在指令名表中查找匹配的条目
    //   提示：写一个循环遍历指令表，优先精确匹配 funct7，
    //   其次匹配 funct7==-1 的通配条目
    const char *name = "unknown"; // 默认指令名
    for(int i=0; i<sizeof(instr_table)/sizeof(instr_table[0]);i++){
        if(instr_table[i].opcode == d.opcode && instr_table[i].funct3 == d.funct3){
            if(instr_table[i].funct7 == d.funct7 || instr_table[i].funct7 == -1){
                name = instr_table[i].name;
                break;
            }
        }
    }

    // Step 3 — 根据 opcode 选格式，snprintf 拼字符串
    //
    //   格式速查：
    //   ┌──────────┬────────────────────────────────────────────┐
    //   │ R-type   │ "add    s0, t0, t1"    ← 3 寄存器          │
    //   │ I-算术    │ "addi   gp, ra, 2"    ← 2 寄存器 + 数字   │
    //   │ I-Load   │ "lw     t0, 8(sp)"    ← rd, imm(rs1)      │
    //   │ S-type   │ "sw     t0, 8(sp)"    ← rs2, imm(rs1)     │
    //   │ B-type   │ "beq    t0, t1, 0x..."← 2 寄存器 + 目标   │
    //   │ U-type   │ "lui    x1, 0x..."    ← rd, 大立即数       │
    //   │ J-type   │ "jal    ra, 0x..."    ← rd, 目标地址       │
    //   │ SYSTEM   │ "ecall" / "ebreak"    ← 特殊，无操作数     │
    //   └──────────┴────────────────────────────────────────────┘

    switch (d.opcode) {

    // ── R-type：rd, rs1, rs2 ────────────────────────────
    case 0x33:
        snprintf(buf, bufsz, "%-7s %s, %s, %s",
                 name,
                 reg_names[d.rd],
                 reg_names[d.rs1],
                 reg_names[d.rs2]);
        break;

    // ── I-type 算术 (ADDI/SLTI/.../JALR)：rd, rs1, imm ─
    case 0x13:
    case 0x67:
    case 0x0F:   // FENCE — 基础模拟器当 NOP
        snprintf(buf, bufsz, "%-7s %s, %s, %d",
                 name,
                 reg_names[d.rd],
                 reg_names[d.rs1],
                 d.imm);
        break;

    // ── I-type Load：rd, imm(rs1) ──────────────────────
    case 0x03:
        snprintf(buf, bufsz, "%-7s %s, %d(%s)",
                 name,
                 reg_names[d.rd],
                 d.imm,
                 reg_names[d.rs1]);
        break;

    // ── S-type：rs2, imm(rs1) ──────────────────────────
    case 0x23:
        snprintf(buf, bufsz, "%-7s %s, %d(%s)",
                 name,
                 reg_names[d.rs2],
                 d.imm,
                 reg_names[d.rs1]);
        break;

    // ── B-type：rs1, rs2, 目标地址 ──────────────────────
    case 0x63:
        snprintf(buf, bufsz, "%-7s %s, %s, 0x%x",
                 name,
                 reg_names[d.rs1],
                 reg_names[d.rs2],
                 pc + d.imm);    // 目标地址 = PC + 偏移
        break;

    // ── U-type (LUI/AUIPC)：rd, 高位立即数 ─────────────
    case 0x37:
    case 0x17:
        snprintf(buf, bufsz, "%-7s %s, 0x%x",
                 name,
                 reg_names[d.rd],
                 d.imm);         // U-type 的 imm 已在 bit[31:12]
        break;

    // ── J-type (JAL)：rd, 目标地址 ──────────────────────
    case 0x6F:
        snprintf(buf, bufsz, "%-7s %s, 0x%x",
                 name,
                 reg_names[d.rd],
                 pc + d.imm);
        break;

    // ── SYSTEM：ecall / ebreak — 没有操作数 ─────────────
    case 0x73:
        if (d.funct3 == 0 && d.imm == 0)
            snprintf(buf, bufsz, "%-7s", "ecall");
        else if (d.funct3 == 0 && d.imm == 1)
            snprintf(buf, bufsz, "%-7s", "ebreak");
        else
            snprintf(buf, bufsz, "%-7s", name);  // 暂不处理的 CSR 指令
        break;

    // ── 非法指令 ──────────────────────────────────────
    default:
        snprintf(buf, bufsz, "%-7s", "unknown");
        break;
    }
}


/* ============================================================================
 * 附录：对你来说最难的地方（以及为什么）
 * ============================================================================
 *
 * 1. cpu_decode 的 switch-case
 *    难度：★★☆☆☆
 *    工作量：11 个 case，每个 1-2 行，合计约 20 行
 *    本质就是"哪个 opcode 用哪个 IMM_* 宏"的映射表
 *
 * 2. cpu_disasm 的指令名查找表
 *    难度：★★★☆☆
 *    工作量：约 40 个条目，每个 1 行，合计约 40 行
 *    关键是设计好结构体 {opcode, funct3, funct7, name}
 *    funct7 的处理是唯一难点（ADD vs SUB, SRL vs SRA, SLLI vs 高位为 0x00 的其他）
 *
 * 3. cpu_disasm 的字符串拼接
 *    难度：★★☆☆☆
 *    你要做的就是把上面注释里那 7 种 snprintf 格式抄对
 *
 * 4. 非法指令处理
 *    难度：★☆☆☆☆
 *    cpu_decode 里 default → 返回全零
 *    cpu_disasm 里查不到 → 输出 "unknown"
 *
 * 总的来看，decode.c 是 CPU 模块里最"体力活"的文件——没有复杂逻辑，
 * 就是要写很多行很相似的代码。正因如此，它非常适合作为你的第一个 .c 文件。
 * ============================================================================ */
