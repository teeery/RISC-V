#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * csr.c — 控制和状态寄存器 (CSR) 读写操作
 *
 * ─── 你需要实现的内容 ─────────────────────────────────────
 *
 * 1. CSR 地址空间 (12 位，高 2 位表示访问权限)：
 *    0x3xx = 机器模式读写
 *    0x1xx = 管理者模式读写
 *
 * 2. 必须实现的 M 模式 CSR：
 *    mvendorid  (0xF11) = 0 (非商业实现)
 *    marchid    (0xF12) = 0
 *    mimpid     (0xF13) = 0
 *    mhartid    (0xF14) = 0
 *    mstatus    (0x300) — 机器状态 (MIE, MPIE, MPP, FS, XS)
 *    misa       (0x301) — ISA 和扩展 (RV32IM)
 *    mie        (0x304) — 中断使能
 *    mtvec      (0x305) — 陷阱向量基址 (MODE + BASE)
 *    mscratch   (0x340) — 机器暂存
 *    mepc       (0x341) — 异常返回 PC
 *    mcause     (0x342) — 异常原因
 *    mtval      (0x343) — 异常附加信息
 *    mip        (0x344) — 中断挂起
 *
 * 3. CSR 指令实现 (6 条)：
 *    CSRRW  (funct3=001): rd = CSR; CSR = rs1   (原子交换)
 *    CSRRS  (funct3=010): rd = CSR; CSR |= rs1  (置位)
 *    CSRRC  (funct3=011): rd = CSR; CSR &= ~rs1 (清除)
 *    CSRRWI (funct3=101): rd = CSR; CSR = zimm  (立即数写)
 *    CSRRSI (funct3=110): rd = CSR; CSR |= zimm (立即数置位)
 *    CSRRCI (funct3=111): rd = CSR; CSR &= ~zimm(立即数清除)
 *    其中 zimm = rs1 (5位零扩展)，rs1 字段用作立即数而非寄存器索引
 *
 * 4. 读写权限检查：
 *    - 只读 CSR (mvendorid 等) 写入被忽略 (不报异常)
 *    - 特权级检查：低特权级访问高特权级 CSR → IllegalInstruction
 *    - WARL (Write Any, Read Legal) 字段处理
 *
 * 5. csr_read(cpu, csr_addr, *val)  → 读取 CSR
 *    csr_write(cpu, csr_addr, val)  → 写入 CSR
 *    csr_handle(dec, cpu)            → 处理 CSR 指令
 *
 * ─── mstatus 字段布局 ─────────────────────────────────────
 *   Bit 31: SD (状态脏位)
 *   Bit 22: TSR (Trap SRET)
 *   Bit 21: TW  (Timeout Wait)
 *   Bit 13: FS[1] (浮点状态)
 *   Bit 12: MPP[1] (机器先前特权级高位)
 *   Bit 11: MPP[0]
 *   Bit 7:  MPIE (先前中断使能)
 *   Bit 5:  SPP (管理者先前特权级)
 *   Bit 3:  MIE (中断使能)
 *   Bit 1:  SIE (管理者中断使能)
 * ============================================================
 */

#include "types.h"
#include "memory.h"
#include "mmu.h"

/* ── CSR 地址常量 ────────────────────────────────────────── */
enum {
    CSR_MVENDORID  = 0xF11,
    CSR_MARCHID    = 0xF12,
    CSR_MIMPID     = 0xF13,
    CSR_MHARTID    = 0xF14,
    CSR_MSTATUS    = 0x300,
    CSR_MISA       = 0x301,
    CSR_MIE        = 0x304,
    CSR_MTVEC      = 0x305,
    CSR_MSCRATCH   = 0x340,
    CSR_MEPC       = 0x341,
    CSR_MCAUSE     = 0x342,
    CSR_MTVAL      = 0x343,
    CSR_MIP        = 0x344,
};

/* ── MISA 字段 ───────────────────────────────────────────── */
// MXL[31:30]=1(RV32), Extensions[25:0]: I=bit8, M=bit12
#define MISA_VALUE  0x40001100   // RV32IM (需要验证值)

/**
 * csr_read() — 读取 CSR 值
 * 返回 false 表示非法访问
 */
static bool csr_read(CPUState *cpu, uint32_t addr, uint32_t *val)
{
    // TODO: 检查特权级 (addr[9:8] > 当前 priv 则拒绝)
    switch (addr) {
    case CSR_MVENDORID: *val = 0; return true;
    case CSR_MARCHID:   *val = 0; return true;
    case CSR_MIMPID:    *val = 0; return true;
    case CSR_MHARTID:   *val = 0; return true;
    case CSR_MSTATUS:   *val = cpu->mstatus;  return true;
    case CSR_MISA:      *val = MISA_VALUE;    return true;
    case CSR_MIE:       *val = cpu->mie;      return true;
    case CSR_MTVEC:     *val = cpu->mtvec;    return true;
    case CSR_MSCRATCH:  *val = cpu->mscratch; return true;
    case CSR_MEPC:      *val = cpu->mepc;     return true;
    case CSR_MCAUSE:    *val = cpu->mcause;   return true;
    case CSR_MTVAL:     *val = cpu->mtval;    return true;
    case CSR_MIP:       *val = cpu->mip;      return true;
    default:
        // 未知 CSR → 读到 0 (简化处理)
        *val = 0;
        return true;
    }
}

/**
 * csr_write() — 写入 CSR 值
 * 返回 false 表示非法访问
 */
static bool csr_write(CPUState *cpu, uint32_t addr, uint32_t val)
{
    // TODO: 检查特权级和只读字段
    switch (addr) {
    case CSR_MVENDORID: case CSR_MARCHID:
    case CSR_MIMPID:    case CSR_MHARTID:
        return true;  // 只读 CSR，忽略写入

    case CSR_MSTATUS:
        // 只允许修改可写位
        cpu->mstatus = val;
        return true;
    case CSR_MIE:       cpu->mie      = val; return true;
    case CSR_MTVEC:     cpu->mtvec    = val; return true;
    case CSR_MSCRATCH:  cpu->mscratch = val; return true;
    case CSR_MEPC:      cpu->mepc     = val; return true;
    case CSR_MCAUSE:    cpu->mcause   = val; return true;
    case CSR_MTVAL:     cpu->mtval    = val; return true;
    case CSR_MIP:       cpu->mip      = val; return true;
    default:
        return true;  // 未知 CSR → 忽略写入
    }
}

/**
 * csr_handle() — 处理 CSR 指令 (由 execute_system 调用)
 *
 * CSRRW  (001):  tmp = CSR; CSR = rs1_val;     rd = tmp
 * CSRRS  (010):  tmp = CSR; CSR |= rs1_val;    rd = tmp
 * CSRRC  (011):  tmp = CSR; CSR &= ~rs1_val;   rd = tmp
 * CSRRWI (101):  tmp = CSR; CSR = zimm;        rd = tmp
 * CSRRSI (110):  tmp = CSR; CSR |= zimm;       rd = tmp
 * CSRRCI (111):  tmp = CSR; CSR &= ~zimm;      rd = tmp
 * (zimm = rs1 字段作为 5 位零扩展)
 */
bool csr_handle(DecodedInstruction *dec, CPUState *cpu)
{
    uint32_t csr_addr = (uint32_t)dec->imm & 0xFFF;  // 12-bit CSR 地址
    uint32_t old_val, new_val, rd_val;

    if (!csr_read(cpu, csr_addr, &old_val))
        return false;

    uint32_t rs1_val = cpu->regs[dec->rs1];
    uint32_t zimm    = dec->rs1;  // 5bit 零扩展立即数

    switch (dec->funct3) {
    case 1:  // CSRRW
        rd_val = old_val;
        new_val = rs1_val;
        break;
    case 2:  // CSRRS
        rd_val = old_val;
        new_val = old_val | (dec->rs1 != 0 ? rs1_val : 0);
        break;
    case 3:  // CSRRC
        rd_val = old_val;
        new_val = old_val & ~(dec->rs1 != 0 ? rs1_val : 0);
        break;
    case 5:  // CSRRWI
        rd_val = old_val;
        new_val = zimm;
        break;
    case 6:  // CSRRSI
        rd_val = old_val;
        new_val = old_val | (zimm != 0 ? zimm : 0);
        break;
    case 7:  // CSRRCI
        rd_val = old_val;
        new_val = old_val & ~(zimm != 0 ? zimm : 0);
        break;
    default:
        return false;
    }

    if (!csr_write(cpu, csr_addr, new_val))
        return false;

    if (dec->rd != 0)
        cpu->regs[dec->rd] = rd_val;

    return true;
}
