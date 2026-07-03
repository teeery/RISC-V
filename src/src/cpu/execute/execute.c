/* ============================================================
 * execute.c — cpu_trap + cpu_execute 调度入口
 *
 * 这是整个执行模块的"前台"：
 *   cpu_trap()     — 异常处理（填写 CSR + 跳转 mtvec 或停机）
 *   cpu_execute()  — 按 opcode 分发到各个 exec_*.c 的处理函数
 *
 * 不包含任何指令的具体实现逻辑 — 全部在 exec_*.c 里。
 * 新增指令扩展时只需在这里加一个 case，不需要改已有代码。
 *
 * 目前支持的 opcode 分布：
 *   0x37 (LUI)       → exec_lui()       — exec_rv32i.c
 *   0x17 (AUIPC)     → exec_auipc()     — exec_rv32i.c
 *   0x13 (OP-IMM)    → exec_op_imm()    — exec_rv32i.c
 *   0x33 (OP)        → exec_op()        — exec_rv32i.c (内部按 funct7 分流 M)
 *   0x03 (LOAD)      → exec_load()      — exec_rv32i.c
 *   0x23 (STORE)     → exec_store()     — exec_rv32i.c
 *   0x63 (BRANCH)    → exec_branch()    — exec_rv32i.c
 *   0x6F (JAL)       → exec_jal()       — exec_rv32i.c
 *   0x67 (JALR)      → exec_jalr()      — exec_rv32i.c
 *   0x73 (SYSTEM)    → exec_system()    — exec_rv32i.c
 *   0x0F (FENCE)     → exec_fence()     — exec_rv32i.c
 *   0x07 (LOAD-FP)   → exec_load_fp()   — exec_f.c  [待实现]
 *   0x27 (STORE-FP)  → exec_store_fp()  — exec_f.c  [待实现]
 *   0x43 (OP-FP)     → exec_fp_op()     — exec_f.c  [待实现]
 * ============================================================
 */

#include "cpu/execute.h"
#include "cpu/decode.h"
#include "simulator.h"
#include "memory/mmu.h"
#include "types.h"
#include "cpu/exec_internal.h"
#include <stdio.h>

/* ============================================================
 * cpu_trap — RISC-V 异常处理
 *
 * ── CSR 缩写速查 ────────────────────────────────────────
 *  mstatus  — Machine STATUS: MPP[12:11], MPIE[7], MIE[3]
 *  mtvec    — Machine Trap VECtor: 异常处理程序入口地址
 *  mepc     — Machine Exception PC: 异常指令地址
 *  mcause   — Machine CAUSE: 异常原因（ExceptionType 枚举）
 *  mtval    — Machine Trap VALue: 附加信息（地址/指令码）
 *
 * ── 6 步处理 ─────────────────────────────────────────────
 *  ① mepc   = pc              保存"出事的指令地址"
 *  ② mcause = exc             保存"出了什么事"
 *  ③ mtval  = tval            保存"附加信息"
 *  ④ mstatus: MPP←priv, MPIE←MIE, MIE←0
 *  ⑤ priv = PRIV_MACHINE      切到机器态
 *  ⑥ 若 mtvec≠0 → 跳转; 若 mtvec=0 → 停机
 *     （ecall 的"停机"是假停机 — simulator.c 中的 syscall handler
 *       会检查 mcause==EXC_ECALL_M 并接管）
 * ============================================================
 */
void cpu_trap(Simulator *sim, uint32_t exc, uint32_t tval)
{
    CPU *cpu = &sim->cpu;

    /* ① mepc — 记录"出事的指令在哪" */
    cpu->mepc = cpu->pc;

    /* ② mcause — 记录"出了什么事" */
    cpu->mcause = exc;

    /* ③ mtval — 记录"附加信息" */
    cpu->mtval = tval;

    /* ④ mstatus — 更新 MPP / MPIE / MIE */
    uint32_t old_priv_bits = (cpu->priv & 0x3) << 11;
    uint32_t old_mie_bit   = (cpu->mstatus >> 3) & 1;

    cpu->mstatus &= ~((0x3u << 11) | (1u << 7) | (1u << 3));
    cpu->mstatus |= old_priv_bits | (old_mie_bit << 7);

    /* ⑤ 切到机器态 */
    cpu->priv = PRIV_MACHINE;

    /* ⑥ 跳转到异常处理程序或停机 */
    if (cpu->mtvec != 0) {
        cpu->pc = cpu->mtvec & ~0x3u;
    } else {
        /* mtvec == 0 → 没有注册异常处理程序 → 停机
         *
         * 特例：ecall 的异常由 sim_step 中的 syscall handler 接管，
         * 这里不打印"halting"避免误导（handler 可能恢复 running=true）*/
        if (exc != EXC_ECALL_M) {
            printf("\n[Trap] exception=%u, tval=0x%08x at mepc=0x%08x\n",
                   exc, tval, cpu->mepc);
            printf("[Trap] mtvec == 0 — no handler registered, halting CPU.\n");
        }
        cpu->running = false;
    }
}

/* ============================================================
 * cpu_execute — 指令执行调度入口
 *
 * 按 opcode 分发到对应的处理函数。
 * 默认设置 *next_pc = pc + 4（顺序执行），分支/跳转指令会覆盖。
 * ============================================================
 */
bool cpu_execute(Simulator *sim, DecodedInstr *d, uint32_t *next_pc)
{
    CPU *cpu = &sim->cpu;

    /* 默认顺序执行 */
    *next_pc = cpu->pc + 4;

    switch (d->opcode) {

    /* ── RV32I ── */
    case 0x37: return exec_lui    (sim, d, next_pc);
    case 0x17: return exec_auipc  (sim, d, next_pc);
    case 0x13: return exec_op_imm (sim, d, next_pc);
    case 0x33: return exec_op     (sim, d, next_pc);
    case 0x03: return exec_load   (sim, d, next_pc);
    case 0x23: return exec_store  (sim, d, next_pc);
    case 0x63: return exec_branch (sim, d, next_pc);
    case 0x6F: return exec_jal    (sim, d, next_pc);
    case 0x67: return exec_jalr   (sim, d, next_pc);
    case 0x73: return exec_system (sim, d, next_pc);
    case 0x0F: return exec_fence  (sim, d, next_pc);

    /* ── F 扩展 ── */
    case 0x07: return exec_load_fp (sim, d, next_pc);
    case 0x27: return exec_store_fp(sim, d, next_pc);
    /* TODO: OP-FP 标准 opcode 为 0x53，当前暂用 0x43 */
    case 0x43: return exec_fp_op   (sim, d, next_pc);

    /* ── FMA 融合乘加 (R4 格式，opcode 0x43/0x47/0x4B/0x4F) ── */
    case 0x47: return exec_fma     (sim, d, next_pc);
    case 0x4B: return exec_fma     (sim, d, next_pc);
    case 0x4F: return exec_fma     (sim, d, next_pc);

    /* ── 未实现的 opcode → 非法指令异常 ── */
    default:
        cpu_trap(sim, EXC_ILLEGAL_INST, d->opcode);
        return false;
    }
}
