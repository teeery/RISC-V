/* ============================================================================
 * pipeline.h / pipeline.c — 五级流水线模拟（选做加分项 A）
 * ============================================================================
 *
 * 流水线是 CPU 内部的"工厂流水线"，让多条指令在不同阶段同时处理。
 *
 * 五级流水线阶段：
 *   IF (Instruction Fetch)  — 从内存取指令，PC+4
 *   ID (Instruction Decode) — 译码，读寄存器文件
 *   EX (Execute)            — ALU 运算，计算分支目标和条件
 *   MEM (Memory Access)     — 读/写数据内存
 *   WB (Write Back)         — 结果写回寄存器文件
 *
 * ---- 流水线寄存器结构体 ----
 *
 *   typedef struct {
 *       uint32_t pc;        // 当前指令的 PC
 *       uint32_t insn;      // 当前指令的 32 位编码
 *       bool     valid;     // 是否有效（stall/flush 时为 false）
 *
 *       // 解码后的信息
 *       uint8_t  opcode, rd, rs1, rs2, funct3, funct7;
 *       int32_t  imm;
 *
 *       // 读出的寄存器值
 *       uint32_t rs1_val;
 *       uint32_t rs2_val;
 *
 *       // 控制信号
 *       bool reg_write;     // 是否需要写回寄存器
 *       bool mem_read;      // 是否需要读内存（LOAD）
 *       bool mem_write;     // 是否需要写内存（STORE）
 *       bool is_branch;     // 是否是分支指令
 *       bool is_jalr;       // 是否是 JALR
 *       uint8_t alu_op;     // ALU 操作码
 *
 *       // EX 阶段结果
 *       uint32_t alu_result;
 *       uint32_t branch_target;
 *       bool     branch_taken;
 *
 *       // MEM 阶段结果
 *       uint32_t mem_data;  // LOAD 回来的数据
 *   } PipelineReg;
 *
 * ---- 流水线寄存器组 ----
 *
 *   每个阶段之间有一组"锁存器"：
 *
 *      IF/ID  ← IF 阶段的输出，ID 阶段的输入
 *      ID/EX  ← ID 阶段的输出，EX 阶段的输入
 *      EX/MEM ← EX 阶段的输出，MEM 阶段的输入
 *      MEM/WB ← MEM 阶段的输出，WB 阶段的输入
 *
 *     typedef struct {
 *         PipelineReg if_id;    // IF → ID
 *         PipelineReg id_ex;    // ID → EX
 *         PipelineReg ex_mem;   // EX → MEM
 *         PipelineReg mem_wb;   // MEM → WB
 *     } PipelineState;
 *
 * ---- 每个时钟周期做这些事 ----
 *
 *   void pipeline_cycle(Simulator *sim, PipelineState *p) {
 *       // 所有阶段并行执行（在一个周期内）
 *       pipeline_wb(sim, p);    // Write Back
 *       pipeline_mem(sim, p);   // Memory Access
 *       pipeline_ex(sim, p);    // Execute
 *       pipeline_id(sim, p);    // Decode
 *       pipeline_if(sim, p);    // Fetch
 *
 *       sim->cycle_count++;
 *   }
 *
 * ---- Hazard 检测与处理 ----
 *
 * ① 数据冒险 (Data Hazard) — Forwarding 解决
 *
 *   当 ID 阶段需要的寄存器值，正好被前面指令在 EX 或 MEM 阶段产生时，
 *   不等到 WB，直接从流水线寄存器"转发"：
 *
 *     uint32_t forward_rs1(PipelineState *p, uint32_t reg_val) {
 *         // EX/MEM 阶段正在写 rd，且 rd == rs1，且 rd != 0
 *         if (p->ex_mem.reg_write && p->ex_mem.rd == rs1 && p->ex_mem.rd != 0)
 *             return p->ex_mem.alu_result;
 *         // MEM/WB 阶段正在写 rd
 *         if (p->mem_wb.reg_write && p->mem_wb.rd == rs1 && p->mem_wb.rd != 0)
 *             return p->mem_wb.alu_result;
 *         // 没有相关，使用寄存器文件中的值
 *         return reg_val;
 *     }
 *
 *   特殊：Load-Use Hazard — Forwarding 救不了，必须 stall 1 个周期
 *     条件：上一条是 LOAD 指令（mem_read=true），且 load 的目标寄存器
 *           正好是当前指令的源寄存器
 *     处理：在 ID 阶段插入一个 bubble（nop），让当前指令在 ID 停一周期
 *
 * ② 控制冒险 (Control Hazard) — Predict Not Taken
 *
 *   分支在 EX 阶段才知道是否跳转，但此时 IF 已经取了下两条指令。
 *   简单方案：总是预测不跳转。
 *     - 不跳转：没损失
 *     - 跳转：flush IF/ID 中的两条指令（valid = false），CPI 损失 2
 *
 *   JAL/JALR 可以在 ID 阶段就判断（无条件跳转），损失减少到 1。
 *
 * ---- CPI 统计 ----
 *
 *   CPI = total_cycles / completed_instructions
 *
 *   统计项目：
 *     uint64_t total_cycles;         // 总周期
 *     uint64_t completed_insts;      // 完成的指令数（从 WB 阶段退出）
 *     uint64_t stall_cycles;         // 因 Hazard 停顿的周期
 *     uint64_t flush_cycles;         // 因分支预测失败浪费的周期
 *     uint64_t load_use_stalls;      // Load-Use Hazard 造成的停顿
 *     uint64_t branch_mispredicts;   // 分支预测失败次数
 *
 *   最终输出：
 *     printf("=== CPI Statistics ===\n");
 *     printf("Total cycles:        %lu\n", total_cycles);
 *     printf("Instructions:         %lu\n", completed_insts);
 *     printf("CPI:                  %.2f\n",
 *            (double)total_cycles / completed_insts);
 *     printf("Stall cycles:         %lu (%.1f%%)\n", stall_cycles, ...);
 *     printf("  Load-use stalls:    %lu\n", load_use_stalls);
 *     printf("  Branch mispredicts: %lu\n", branch_mispredicts);
 *
 * ---- 流水线状态转换图（需要画的图）----
 *
 *   IF ──→ ID ──→ EX ──→ MEM ──→ WB
 *    ↑       ↑       ↑       ↑
 *   stall   stall   stall  (MEM 很少 stall)
 *
 *   Flush 信号：
 *     - 分支预测失败 → flush IF/ID
 *     - ECALL/EBREAK  → flush 全部（进入异常处理）
 *     - JALR 在 ID 阶段解析 → flush IF/ID 中的 1 条
 *
 *   Stall 信号：
 *     - Load-Use Hazard → stall ID + flush IF/ID（插入 bubble）
 *     - 数据相关且无法 forwarding 时 → stall 对应阶段
 */

/* 这是选做模块，基础要求不需要实现 */
/* 代码写到 pipeline.c 中，此处仅为说明注释 */
