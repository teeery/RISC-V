/* ============================================================================
 * rsp.h / rsp.c — GDB Remote Serial Protocol 实现（选做加分项 C）
 * ============================================================================
 *
 * 这个模块让 GDB 客户端可以直接连接你们的模拟器进行调试。
 *
 * GDB RSP 是一个简单的 ASCII 协议，通过 TCP socket 通信。
 *
 * ---- 协议格式 ----
 *
 *   包格式: $<data>#<checksum>
 *     $        — 包开始标记
 *     data     — 十六进制编码的负载数据
 *     #        — 校验和分隔符
 *     checksum — data 所有字节之和的低 8 位（两个十六进制字符）
 *
 *   应答:
 *     + (ACK)  — 确认收到
 *     - (NAK)  — 请求重发
 *
 * ---- 核心命令（模拟器需要响应的）----
 *
 *   命令      含义                      响应格式
 *   ────────────────────────────────────────────────────
 *   ?         报告停止原因                S05 (SIGTRAP)
 *   g         读所有寄存器                32 个 reg + PC 的 hex 串
 *   G <data>  写所有寄存器                解析后写入 → OK
 *   p <n>     读单个寄存器 n              <hex value>
 *   P <n>=<v> 写单个寄存器 n 为 v         OK
 *   m <addr>,<len>    读内存             <hex data>
 *   M <addr>,<len>:<data> 写内存          OK
 *   s         单步执行                    执行一条 → S05
 *   c         继续执行                    运行到断点 → S05
 *   Z0,<addr>,4  设置软件断点             OK
 *   z0,<addr>,4  删除软件断点             OK
 *   Hc <tid>  设置当前线程                OK (单线程返回 OK)
 *   qSupported    查询支持的功能          返回支持列表
 *   qAttached     查询附加状态            1 (表示正在运行新进程)
 *   vCont?        查询支持的继续模式
 *   vCont;c       继续执行（同 c）
 *   vCont;s       单步执行（同 s）
 *
 * ---- 实现框架 ----
 *
 *   1. 启动 TCP 服务器，监听端口（默认 1234，GDB 的标准调试端口）
 *   2. 接受 GDB 连接
 *   3. 循环：接收包 → 解析命令 → 执行操作 → 发送响应
 *
 *   void rsp_server_start(Simulator *sim, int port) {
 *       // 创建 socket，bind，listen
 *       // accept 连接
 *       // while (running) {
 *       //     recv_packet();
 *       //     handle_command();
 *       //     send_response();
 *       // }
 *   }
 *
 * ---- 寄存器编码（GDB 期望的顺序）----
 *
 *   GDB 期望的 RISC-V 寄存器顺序：
 *   x0~x31 (32个) + PC (1个)，共 33 个寄存器
 *
 *   每个寄存器 4 字节 = 8 hex 字符（小端序）
 *   33 * 8 = 264 hex 字符
 *
 * ---- TCP 端口 ----
 *
 *   默认端口: 1234
 *
 * ---- GDB 连接方式 ----
 *
 *   $ riscv64-linux-gnu-gdb hello
 *   (gdb) target remote :1234
 *   (gdb) b main
 *   (gdb) c
 *   ...
 *
 * ---- 参考 ----
 *
 *   GDB RSP 协议文档:
 *   https://sourceware.org/gdb/current/onlinedocs/gdb.html/Remote-Protocol.html
 */

/* 这是选做模块，基础要求不需要实现 */
/* 代码写到 rsp.c 中，此处仅为说明注释 */
