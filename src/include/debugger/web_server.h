#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "types.h"

struct Simulator;  /* 前置声明 */

/* ================================================================
 * web_server.h — Web 调试器 HTTP 服务器
 *
 * 用法：
 *   Simulator sim;
 *   sim_init(&sim);
 *   sim_load_elf(&sim, "test.elf");
 *   web_server_start(&sim, 8080);   // 阻塞，直到收到 shutdown 请求
 *   sim_destroy(&sim);
 *
 * 线程模型：
 *   - 主线程：调用 web_server_start()，内部创建监听线程
 *   - Web 线程：accept 连接、解析 HTTP、调用调试器 API
 *   - 所有 sim 访问通过 sim_lock/sim_unlock 同步
 *
 * 平台：MinGW-w64 / Windows，依赖 WinSock2 + CRITICAL_SECTION
 * ================================================================ */

/* 启动 Web 调试器 HTTP 服务器（阻塞当前线程直到服务器退出）
 *
 * sim   — 已初始化并加载 ELF 的模拟器指针
 * port  — 监听端口号（推荐 8080 或 9090）
 *
 * 返回值：EXIT_SUCCESS 正常退出，EXIT_FAILURE 启动失败
 */
int web_server_start(struct Simulator *sim, int port);

#endif /* WEB_SERVER_H */
