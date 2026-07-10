#include "debugger/debugger.h"
#include "debugger/web_server.h"
#include "simulator.h"
#include "demo_programs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
#include <inttypes.h>

/* Windows 网络 + 线程 */
#include <winsock2.h>
#include <windows.h>          /* CreateProcess, CreatePipe 等 */
#include <ws2tcpip.h>

/* ================================================================
 * web_server.c — Web 调试器 HTTP 服务器
 *
 * 单线程事件循环：accept → parse → route → respond
 * 所有 sim 访问通过 sim_lock/sim_unlock 同步。
 *
 * 依赖：WinSock2 (libws2_32)
 * ================================================================ */

/* ── 常量 ─────────────────────────────────────────────────────── */
#define HTTP_BUF_SIZE     8192
#define MAX_HEADERS       32
#define MAX_PATH_LEN      256
#define RECV_TIMEOUT_MS   2000

/* ── 在线编译配置 ────────────────────────────────────────────────
 *
 * RISC_V_GCC: xPack RISC-V Embedded GCC 的路径。
 *   默认从项目根目录 tools/ 子目录查找（下载后自动放在这里）。
 *   也可以通过编译时 -DRISC_V_GCC=\"...\" 覆盖。
 */
#ifndef RISC_V_GCC
#define RISC_V_GCC     "tools/riscv-gcc/xpack-riscv-none-elf-gcc-14.2.0-3/bin/riscv-none-elf-gcc"
#endif
#define COMPILE_INPUT  ".compile_input.c"
#define COMPILE_OUTPUT ".compile_output.elf"

/* GCC 编译选项：
 *   -march=rv32imf  : RV32 + Integer + Multiply + Float
 *   -mabi=ilp32     : 32-bit ABI（int=long=pointer=32bit）
 *   -nostdlib       : 不链接标准库（裸机环境）
 *   -nostartfiles   : 不链接 crt0
 *   -O0             : 不优化（方便调试）
 *   -Wall           : 启用常用警告
 *
 * 注意：不用 -e main，而是自动追加 _start() 包装函数，
 * 由 _start 调用 main() 然后 while(1) 死循环，防止程序跑飞。
 */
#define COMPILE_FLAGS  "-march=rv32imf -mabi=ilp32 " \
                       "-nostdlib -nostartfiles " \
                       "-O0 -Wall"

/* ── HTTP 请求 ─────────────────────────────────────────────────── */
typedef struct {
    char method[8];           /* GET / POST / DELETE */
    char path[MAX_PATH_LEN];  /* /registers 等 */
    char query[256];          /* addr=0x1000&count=16 */
    char body[16384];         /* POST body（大 buffer 支持长源码编译） */
    int  body_len;
} HttpRequest;

/* ── 全局指针（供线程函数访问）────────────────────────────────── */
static Simulator *g_sim;
static int        g_server_port;
static volatile bool g_server_running;
static char       g_elf_path[512];  /* 当前加载的 ELF 路径 */

/* ── 前向声明 ─────────────────────────────────────────────────── */
static void handle_request(SOCKET client);
static void send_response(SOCKET client, int code, const char *content_type,
                          const char *body);
static void send_json(SOCKET client, int code, const char *json);

/* ── JSON 字符串构建辅助 ───────────────────────────────────────── */

/* 写 JSON 到动态缓冲区，自动扩容 */
typedef struct {
    char *buf;
    int   len;
    int   cap;
} JsonBuf;

static void jb_init(JsonBuf *jb)
{
    jb->cap = 512;
    jb->buf = malloc((size_t)jb->cap);
    if (!jb->buf) {
        jb->cap = 0;
        jb->len = 0;
        return;
    }
    jb->buf[0] = '\0';
    jb->len = 0;
}

static void jb_free(JsonBuf *jb)
{
    free(jb->buf);
    jb->buf = NULL;
    jb->len = 0;
    jb->cap = 0;
}

static void jb_append(JsonBuf *jb, const char *s)
{
    int slen = (int)strlen(s);
    if (jb->len + slen + 1 > jb->cap) {
        int new_cap = (jb->len + slen + 1) * 2;
        char *new_buf = realloc(jb->buf, (size_t)new_cap);
        if (!new_buf) {
            /* OOM: 保持 jb->buf 不变（可能为 NULL），调用方应在
             * jb_free 后检查 jb->buf 或直接返回错误 */
            return;
        }
        jb->buf = new_buf;
        jb->cap = new_cap;
    }
    memcpy(jb->buf + jb->len, s, (size_t)slen + 1);
    jb->len += slen;
}

static void jb_append_escaped(JsonBuf *jb, const char *s)
{
    /* 简易 JSON 字符串转义（处理控制字符和引号） */
    jb_append(jb, "\"");
    for (const char *p = s; *p; p++) {
        if (*p == '"')      jb_append(jb, "\\\"");
        else if (*p == '\\') jb_append(jb, "\\\\");
        else if (*p == '\n') jb_append(jb, "\\n");
        else if (*p == '\r') jb_append(jb, "\\r");
        else if (*p == '\t') jb_append(jb, "\\t");
        else if ((unsigned char)*p < 0x20) {
            char hex[8];
            snprintf(hex, sizeof(hex), "\\u%04x", (unsigned char)*p);
            jb_append(jb, hex);
        } else {
            char tmp[2] = { *p, '\0' };
            jb_append(jb, tmp);
        }
    }
    jb_append(jb, "\"");
}

static void jb_printf(JsonBuf *jb, const char *fmt, ...)
{
    char tmp[1024];  /* 增大缓冲区，减少截断风险 */
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    jb_append(jb, tmp);
}

/* ── HTTP 解析 ─────────────────────────────────────────────────── */

/* 解码 URL 编码 (%XX) → 原地修改 */
static void url_decode(char *s)
{
    char *src = s, *dst = s;
    while (*src) {
        if (*src == '%' && isxdigit((unsigned char)src[1])
            && isxdigit((unsigned char)src[2])) {
            char hex[3] = { src[1], src[2], '\0' };
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/* 解析查询字符串 key=value&key2=value2 → 取指定 key 的值 */
static const char *query_get(const char *query, const char *key)
{
    static char val[256];
    int keylen = (int)strlen(key);
    const char *p = query;

    while (p && *p) {
        if (strncmp(p, key, (size_t)keylen) == 0 && p[keylen] == '=') {
            p += keylen + 1;
            int i = 0;
            while (*p && *p != '&' && i < 255) val[i++] = *p++;
            val[i] = '\0';
            url_decode(val);
            return val;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
    return NULL;
}

/* 解析 HTTP 请求行 + 头部 + body */
static bool parse_http(const char *raw, int len, HttpRequest *req)
{
    memset(req, 0, sizeof(*req));

    /* 查找请求行结束 \r\n */
    const char *line_end = strstr(raw, "\r\n");
    if (!line_end) return false;

    /* 解析请求行: METHOD /path?query HTTP/1.x */
    char line[512];
    int line_len = (int)(line_end - raw);
    if (line_len >= (int)sizeof(line)) line_len = (int)sizeof(line) - 1;
    memcpy(line, raw, (size_t)line_len);
    line[line_len] = '\0';

    /* 手动解析 "METHOD SP URI SP HTTP/1.x" */
    char *method = line;
    char *sp1 = strchr(line, ' ');
    if (!sp1) return false;
    *sp1 = '\0';
    char *uri = sp1 + 1;
    char *sp2 = strchr(uri, ' ');
    if (!sp2) return false;
    *sp2 = '\0';
    /* sp2+1 指向 HTTP 版本，这里忽略 */

    strncpy(req->method, method, sizeof(req->method) - 1);
    req->method[sizeof(req->method) - 1] = '\0';

    /* 分离 path 和 query */
    char *qmark = strchr(uri, '?');
    if (qmark) {
        *qmark = '\0';
        strncpy(req->query, qmark + 1, sizeof(req->query) - 1);
    }
    url_decode(uri);
    strncpy(req->path, uri, sizeof(req->path) - 1);

    /* 查找 body（跳过头部，直到空行 \r\n\r\n） */
    const char *body_start = strstr(line_end + 2, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        int body_len = len - (int)(body_start - raw);
        if (body_len > 0 && body_len < (int)sizeof(req->body) - 1) {
            memcpy(req->body, body_start, (size_t)body_len);
            req->body[body_len] = '\0';
            req->body_len = body_len;
        }
    }

    return true;
}

/* ── 路由处理 ─────────────────────────────────────────────────── */

/* GET /status — 模拟器运行状态 */
static void route_get_status(SOCKET client)
{
    JsonBuf jb;
    jb_init(&jb);

    sim_lock(g_sim);
    jb_append(&jb, "{");
    jb_printf(&jb, "\"running\":%s,", g_sim->cpu.running ? "true" : "false");
    jb_printf(&jb, "\"debug_mode\":%s,", g_sim->debug_mode ? "true" : "false");
    jb_printf(&jb, "\"single_step\":%s,", g_sim->single_step ? "true" : "false");
    jb_printf(&jb, "\"pc\":%u,", g_sim->cpu.pc);
    jb_printf(&jb, "\"priv\":%d,", (int)g_sim->cpu.priv);
    jb_printf(&jb, "\"instr_count\":%" PRIu64 ",", g_sim->instr_count);
    jb_printf(&jb, "\"cycle_count\":%" PRIu64 ",", g_sim->cycle_count);
    jb_printf(&jb, "\"bp_count\":%d,", g_sim->bp_count);
    jb_printf(&jb, "\"cpu_model\":%d,", (int)g_sim->cpu_model);
    jb_printf(&jb, "\"cpu_model_name\":\"%s\"",
              (g_sim->cpu_model == MODEL_SINGLE_CYCLE) ? "single" :
              (g_sim->cpu_model == MODEL_MULTI_CYCLE)  ? "multi" : "pipeline");
    jb_append(&jb, "}");
    sim_unlock(g_sim);

    send_json(client, 200, jb.buf);
    jb_free(&jb);
}

/* GET /registers — 全部寄存器 */
static void route_get_registers(SOCKET client)
{
    /* ABI 名称表 */
    static const char *abi[] = {
        "zero","ra","sp","gp","tp","t0","t1","t2",
        "s0","s1","a0","a1","a2","a3","a4","a5","a6","a7",
        "s2","s3","s4","s5","s6","s7","s8","s9","s10","s11",
        "t3","t4","t5","t6"
    };

    JsonBuf jb;
    jb_init(&jb);

    sim_lock(g_sim);
    jb_append(&jb, "{");
    /* 通用寄存器 */
    jb_append(&jb, "\"regs\":[");
    for (int i = 0; i < 32; i++) {
        char tmp[128];
        snprintf(tmp, sizeof(tmp),
                 "{\"idx\":%d,\"name\":\"%s\",\"abi\":\"%s\",\"value\":%u}%s",
                 i, abi[i], abi[i], g_sim->cpu.regs[i],
                 (i < 31) ? "," : "");
        jb_append(&jb, tmp);
    }
    jb_append(&jb, "],");

    /* 浮点寄存器 */
    jb_append(&jb, "\"fregs\":[");
    for (int i = 0; i < 32; i++) {
        char tmp[128];
        /* 将 uint32_t 位模式显示为浮点值 */
        float fval;
        memcpy(&fval, &g_sim->cpu.fregs[i], sizeof(float));
        snprintf(tmp, sizeof(tmp),
                 "{\"idx\":%d,\"bits\":%u,\"float\":%f}%s",
                 i, g_sim->cpu.fregs[i], (double)fval,
                 (i < 31) ? "," : "");
        jb_append(&jb, tmp);
    }
    jb_append(&jb, "],");

    /* PC */
    jb_printf(&jb, "\"pc\":%u,", g_sim->cpu.pc);

    /* 关键 CSR */
    jb_printf(&jb, "\"mstatus\":%u,", g_sim->cpu.mstatus);
    jb_printf(&jb, "\"mtvec\":%u,", g_sim->cpu.mtvec);
    jb_printf(&jb, "\"mepc\":%u,", g_sim->cpu.mepc);
    jb_printf(&jb, "\"mcause\":%u,", g_sim->cpu.mcause);
    jb_printf(&jb, "\"mtval\":%u", g_sim->cpu.mtval);
    jb_append(&jb, "}");
    sim_unlock(g_sim);

    send_json(client, 200, jb.buf);
    jb_free(&jb);
}

/* GET /memory?addr=0x...&count=N&format=x&unit=w */
static void route_get_memory(SOCKET client, const HttpRequest *req)
{
    const char *addr_str = query_get(req->query, "addr") ?
                           query_get(req->query, "addr") : "0";
    const char *count_str = query_get(req->query, "count") ?
                            query_get(req->query, "count") : "16";
    const char *format_str = query_get(req->query, "format") ?
                             query_get(req->query, "format") : "x";
    const char *unit_str = query_get(req->query, "unit") ?
                           query_get(req->query, "unit") : "w";

    uint32_t addr = (uint32_t)strtoul(addr_str, NULL, 0);
    int count = atoi(count_str);
    char format = format_str[0];
    char unit = unit_str[0];

    if (count <= 0) count = 16;
    if (format == 0) format = 'x';
    if (unit == 0) unit = 'w';

    int unit_size;
    switch (unit) {
        case 'b': unit_size = 1; break;
        case 'h': unit_size = 2; break;
        case 'w': unit_size = 4; break;
        default:  unit_size = 4; break;
    }

    JsonBuf jb;
    jb_init(&jb);

    sim_lock(g_sim);
    jb_append(&jb, "{");
    jb_printf(&jb, "\"addr\":%u,", addr);
    jb_printf(&jb, "\"count\":%d,", count);
    jb_printf(&jb, "\"unit\":\"%c\",", unit);
    jb_printf(&jb, "\"unit_size\":%d,", unit_size);
    jb_append(&jb, "\"bytes\":[");

    for (int i = 0; i < count; i++) {
        ExceptionType exc = EXC_NONE;
        uint32_t val = 0;

        switch (unit) {
            case 'b': {
                uint8_t b;
                mmu_read_8(&g_sim->mmu, &g_sim->pmem, addr + i,
                           &b, g_sim->cpu.priv, &exc);
                val = b;
                break;
            }
            case 'h': {
                uint16_t h;
                mmu_read_16(&g_sim->mmu, &g_sim->pmem, addr + i * 2,
                            &h, g_sim->cpu.priv, &exc);
                val = h;
                break;
            }
            default: {
                mmu_read_32(&g_sim->mmu, &g_sim->pmem, addr + i * 4,
                            &val, g_sim->cpu.priv, &exc);
                break;
            }
        }

        char tmp[64];
        snprintf(tmp, sizeof(tmp), "{\"offset\":%d,\"value\":%u,\"fault\":%s}%s",
                 i * unit_size, val, (exc != EXC_NONE) ? "true" : "false",
                 (i < count - 1) ? "," : "");
        jb_append(&jb, tmp);
    }
    jb_append(&jb, "]}");
    sim_unlock(g_sim);

    send_json(client, 200, jb.buf);
    jb_free(&jb);
}

/* GET /backtrace */
static void route_get_backtrace(SOCKET client)
{
    JsonBuf jb;
    jb_init(&jb);

    sim_lock(g_sim);

    uint32_t fp = g_sim->cpu.regs[REG_S0];  /* x8 = s0/fp */
    uint32_t pc = g_sim->cpu.pc;

    jb_append(&jb, "{\"frames\":[");

    /* 帧 #0：当前 PC */
    jb_printf(&jb, "{\"depth\":0,\"pc\":%u,\"fp\":%u,", pc, fp);
    {
        ExceptionType exc = EXC_NONE;
        uint32_t instr;
        char disasm[128] = "???";
        if (mmu_read_32(&g_sim->mmu, &g_sim->pmem, pc, &instr,
                        g_sim->cpu.priv, &exc)) {
            cpu_disasm(instr, pc, disasm, sizeof(disasm));
        }
        jb_append_escaped(&jb, disasm);
        jb_append(&jb, ",\"symbol\":");
        jb_append_escaped(&jb, disasm);
        jb_append(&jb, "}");
    }

    /* 回溯帧指针链 */
    int depth = 0;
    while (fp != 0
           && fp >= STACK_BASE && fp < STACK_TOP
           && depth < 64)
    {
        ExceptionType exc = EXC_NONE;
        uint32_t ra, prev_fp;
        if (!mmu_read_32(&g_sim->mmu, &g_sim->pmem, fp + 4,
                         &ra, g_sim->cpu.priv, &exc)) break;
        if (!mmu_read_32(&g_sim->mmu, &g_sim->pmem, fp,
                         &prev_fp, g_sim->cpu.priv, &exc)) break;

        depth++;
        jb_printf(&jb, ",{\"depth\":%d,\"fp\":%u,\"ra\":%u", depth, fp, ra);

        if (ra >= 4) {
            uint32_t call_instr;
            char disasm[128] = "???";
            if (mmu_read_32(&g_sim->mmu, &g_sim->pmem, ra - 4,
                            &call_instr, g_sim->cpu.priv, &exc)) {
                cpu_disasm(call_instr, ra - 4, disasm, sizeof(disasm));
            }
            jb_append(&jb, ",\"symbol\":");
            jb_append_escaped(&jb, disasm);
        }
        jb_append(&jb, "}");
        fp = prev_fp;
    }

    jb_append(&jb, "]}");
    sim_unlock(g_sim);

    send_json(client, 200, jb.buf);
    jb_free(&jb);
}

/* GET /breakpoints */
static void route_get_breakpoints(SOCKET client)
{
    JsonBuf jb;
    jb_init(&jb);

    sim_lock(g_sim);
    jb_append(&jb, "{\"breakpoints\":[");
    for (int i = 0; i < g_sim->bp_count; i++) {
        Breakpoint *bp = &g_sim->breakpoints[i];
        char tmp[256];
        snprintf(tmp, sizeof(tmp),
                 "{\"index\":%d,\"addr\":%u,\"enabled\":%s}%s",
                 i, bp->addr, bp->enabled ? "true" : "false",
                 (i < g_sim->bp_count - 1) ? "," : "");
        jb_append(&jb, tmp);
    }
    jb_append(&jb, "]}");
    sim_unlock(g_sim);

    send_json(client, 200, jb.buf);
    jb_free(&jb);
}

/* POST /breakpoint — body: {"addr": 0x...} 或 {"addr": "0x..."} */
static void route_post_breakpoint(SOCKET client, const HttpRequest *req)
{
    /* 解析 body 中的 addr */
    uint32_t addr = 0;
    bool found = false;

    /* 尝试 JSON: "addr": N 或 "addr": "0xN" */
    const char *p = strstr(req->body, "\"addr\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p++;
            while (*p && isspace((unsigned char)*p)) p++;
            if (*p == '"') {
                p++;
                addr = (uint32_t)strtoul(p, NULL, 0);
            } else {
                addr = (uint32_t)strtoul(p, NULL, 0);
            }
            found = true;
        }
    }

    if (!found) {
        send_json(client, 400, "{\"status\":\"error\",\"message\":\"Missing addr field\"}");
        return;
    }

    sim_lock(g_sim);
    int idx = debugger_add_breakpoint(g_sim, addr);
    sim_unlock(g_sim);

    if (idx < 0) {
        char err[256];
        snprintf(err, sizeof(err),
                 "{\"status\":\"error\",\"message\":\"Failed to set breakpoint at 0x%08x\"}",
                 addr);
        send_json(client, 400, err);
    } else {
        char ok[256];
        snprintf(ok, sizeof(ok),
                 "{\"status\":\"ok\",\"index\":%d,\"addr\":%u}", idx, addr);
        send_json(client, 200, ok);
    }
}

/* DELETE /breakpoint?id=N */
static void route_delete_breakpoint(SOCKET client, const HttpRequest *req)
{
    const char *id_str = query_get(req->query, "id");
    if (!id_str) {
        send_json(client, 400,
                  "{\"status\":\"error\",\"message\":\"Missing id parameter\"}");
        return;
    }
    int index = atoi(id_str);

    sim_lock(g_sim);
    bool ok = debugger_del_breakpoint(g_sim, index);
    sim_unlock(g_sim);

    if (ok) {
        char resp[128];
        snprintf(resp, sizeof(resp),
                 "{\"status\":\"ok\",\"deleted\":%d}", index);
        send_json(client, 200, resp);
    } else {
        char err[128];
        snprintf(err, sizeof(err),
                 "{\"status\":\"error\",\"message\":\"No breakpoint %d\"}", index);
        send_json(client, 404, err);
    }
}

/* POST /step */
static void route_post_step(SOCKET client)
{
    sim_lock(g_sim);

    /* 记录执行前的 PC */
    uint32_t pc_before = g_sim->cpu.pc;

    debugger_step(g_sim);

    /* 获取执行的指令反汇编 */
    char disasm[128] = "";
    uint32_t instr = 0;
    {
        ExceptionType exc = EXC_NONE;
        if (mmu_read_32(&g_sim->mmu, &g_sim->pmem, pc_before, &instr,
                        g_sim->cpu.priv, &exc)) {
            cpu_disasm(instr, pc_before, disasm, sizeof(disasm));
        }
    }

    /* 使用 JsonBuf 安全构建 JSON（对 disasm 做转义） */
    JsonBuf jb;
    jb_init(&jb);
    jb_append(&jb, "{\"status\":\"ok\",\"pc_before\":");
    jb_printf(&jb, "%u,\"pc_after\":%u,\"instr\":%u,\"disasm\":", pc_before, g_sim->cpu.pc, instr);
    jb_append_escaped(&jb, disasm);
    jb_printf(&jb, ",\"running\":%s,\"fsm_state\":",
              g_sim->cpu.running ? "true" : "false");
    jb_append_escaped(&jb, g_sim->dp.fsm_state[0] ? g_sim->dp.fsm_state : "");
    jb_printf(&jb, ",\"instr_count\":%" PRIu64 ",\"cycle_count\":%" PRIu64 "}",
              g_sim->instr_count, g_sim->cycle_count);
    sim_unlock(g_sim);

    send_json(client, 200, jb.buf);
    jb_free(&jb);
}

/* GET /datapath — 数据通路信号状态（SVG 可视化用） */
static void route_get_datapath(SOCKET client)
{
    sim_lock(g_sim);
    DatapathState *dp = &g_sim->dp;

    char json[3072];
    snprintf(json, sizeof(json),
        "{"
        "\"valid\":%s,"
        "\"pc\":%u,"
        "\"instr\":%u,"
        "\"opcode\":%u,"
        "\"rd\":%u,\"rs1\":%u,\"rs2\":%u,"
        "\"funct3\":%u,\"funct7\":%u,"
        "\"imm\":%d,"
        "\"rs1_val\":%u,\"rs2_val\":%u,\"rd_val\":%u,"
        "\"alu_op\":\"%s\","
        "\"alu_a\":%u,\"alu_b\":%u,\"alu_result\":%u,"
        "\"mem_addr\":%u,\"mem_rdata\":%u,\"mem_wdata\":%u,"
        "\"mem_read\":%s,\"mem_write\":%s,"
        "\"branch_taken\":%s,"
        "\"next_pc\":%u,"
        "\"reg_write\":%s,"
        "\"disasm\":\"%s\","
        "\"fsm_state\":\"%s\","
        "\"stall\":%s,\"flush\":%s,"
        "\"fwd_a\":%s,\"fwd_b\":%s,"
        "\"pipe_valid_mask\":%u"
        "}",
        dp->valid ? "true" : "false",
        dp->pc, dp->instr, dp->opcode,
        dp->rd, dp->rs1, dp->rs2,
        dp->funct3, dp->funct7,
        dp->imm,
        dp->rs1_val, dp->rs2_val, dp->rd_val,
        dp->alu_op,
        dp->alu_a, dp->alu_b, dp->alu_result,
        dp->mem_addr, dp->mem_rdata, dp->mem_wdata,
        dp->mem_read ? "true" : "false",
        dp->mem_write ? "true" : "false",
        dp->branch_taken ? "true" : "false",
        dp->next_pc,
        dp->reg_write ? "true" : "false",
        dp->disasm,
        dp->fsm_state[0] ? dp->fsm_state : "",
        dp->stall ? "true" : "false",
        dp->flush ? "true" : "false",
        dp->fwd_a ? "true" : "false",
        dp->fwd_b ? "true" : "false",
        dp->pipe_valid_mask);
    sim_unlock(g_sim);

    send_json(client, 200, json);
}

/* GET /disassembly — 指令列表（左侧汇编面板用）
 * 返回 PC 周围 N 条指令的反汇编，当前指令标记 is_current=true */
static void route_get_disassembly(SOCKET client, const HttpRequest *req)
{
    const char *count_str = query_get(req->query, "count") ?
                           query_get(req->query, "count") : "64";
    int count = atoi(count_str);
    if (count <= 0) count = 64;
    if (count > 256) count = 256;

    JsonBuf jb;
    jb_init(&jb);

    sim_lock(g_sim);

    uint32_t pc = g_sim->cpu.pc;
    /* 从 PC 往前 20 条指令开始 */
    uint32_t start = (pc >= 0x50) ? (pc - 0x50) : 0;

    jb_printf(&jb, "{\"pc\":%u,\"start\":%u,\"count\":%d,\"instructions\":[",
              pc, start, count);

    for (int i = 0; i < count; i++) {
        uint32_t addr = start + (uint32_t)i * 4;
        ExceptionType exc = EXC_NONE;
        uint32_t instr = 0;
        bool valid = mmu_read_32(&g_sim->mmu, &g_sim->pmem, addr, &instr,
                                 g_sim->cpu.priv, &exc);

        char disasm[128] = "???";
        if (valid) {
            cpu_disasm(instr, addr, disasm, sizeof(disasm));
        }

        jb_printf(&jb,
            "{\"addr\":%u,\"instr\":%u,\"valid\":%s,\"is_current\":%s,\"disasm\":",
            addr, instr,
            valid ? "true" : "false",
            (addr == pc) ? "true" : "false");
        jb_append_escaped(&jb, valid ? disasm : "(invalid)");
        jb_append(&jb, "}");
        if (i < count - 1) jb_append(&jb, ",");
    }

    jb_append(&jb, "]}");
    sim_unlock(g_sim);

    send_json(client, 200, jb.buf);
    jb_free(&jb);
}

/* POST /continue */
static void route_post_continue(SOCKET client)
{
    sim_lock(g_sim);

    debugger_continue(g_sim);

    char resp[256];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"ok\",\"pc\":%u,\"running\":%s}",
             g_sim->cpu.pc,
             g_sim->cpu.running ? "true" : "false");
    sim_unlock(g_sim);

    send_json(client, 200, resp);
}

/* POST /stop — 终止 Web 服务器 */
static void route_post_stop(SOCKET client)
{
    /* 清理临时编译文件 */
    remove(COMPILE_INPUT);
    remove(COMPILE_OUTPUT);
    send_json(client, 200, "{\"status\":\"ok\",\"message\":\"Server stopping\"}");
    g_server_running = false;
}

/* GET /datapath.svg — 根据 CPU 模型返回对应的数据通路图 */
static void route_get_datapath_svg(SOCKET client)
{
    const char *filename = NULL;

    sim_lock(g_sim);
    switch (g_sim->cpu_model) {
        case MODEL_SINGLE_CYCLE:
            filename = "datapath_single_dynamic.svg";
            break;
        case MODEL_MULTI_CYCLE:
            /* 多周期暂用单周期图（数据通路相同，仅控制不同） */
            filename = "datapath_single_dynamic.svg";
            break;
        case MODEL_PIPELINE:
        default:
            filename = "datapath_pipeline_dynamic.svg";
            break;
    }
    sim_unlock(g_sim);

    /* 尝试多个路径 */
    const char *dirs[] = {
        "src/src/debugger/",
        "../src/src/debugger/",
        "docs/",
        "../docs/",
        NULL
    };

    char *content = NULL;
    long fsize = 0;

    for (int i = 0; dirs[i]; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s%s", dirs[i], filename);
        FILE *f = fopen(path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            fsize = ftell(f);
            fseek(f, 0, SEEK_SET);
            content = malloc((size_t)(fsize + 1));
            if (content) {
                size_t n = fread(content, 1, (size_t)fsize, f);
                content[n] = '\0';
            }
            fclose(f);
            if (content) break;
        }
    }

    if (!content) {
        send_json(client, 404, "{\"status\":\"error\",\"message\":\"Datapath SVG not found\"}");
        return;
    }

    send_response(client, 200, "image/svg+xml; charset=utf-8", content);
    free(content);
}

/* GET / — HTML + Pipeline SVG (loaded from /pipeline.svg) */
static void route_get_index(SOCKET client)
{
    static const char *html =
        "<!DOCTYPE html>\r\n"
        "<html lang=\"en\">\r\n"
        "<head>\r\n"
        "<meta charset=\"UTF-8\">\r\n"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\r\n"
        "<title>RISC-V Debugger v2</title>\r\n"
        "<style>\r\n"
        "*{box-sizing:border-box;margin:0;padding:0}\r\n"
        "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#eef0f4;color:#222;height:100vh;display:flex;flex-direction:column;overflow:hidden}\r\n"
        ".toolbar{display:flex;align-items:center;gap:10px;padding:7px 14px;background:#fff;border-bottom:1px solid #d4d8e0;flex-shrink:0;z-index:10}\r\n"
        ".toolbar .title{font-size:15px;font-weight:700;color:#1a1a2e;margin-right:auto;letter-spacing:-0.3px}\r\n"
        ".stat{padding:2px 10px;border-radius:9px;font-size:11px;font-weight:600;line-height:18px}\r\n"
        ".stat.running{background:#e8f5e9;color:#2e7d32}\r\n"
        ".stat.stopped{background:#fce4ec;color:#c62828}\r\n"
        ".toolbar .info{font-size:11px;color:#666;font-family:'Cascadia Code',Consolas,monospace}\r\n"
        ".toolbar a{font-size:11px;color:#1a73e8;text-decoration:none;font-weight:500}\r\n"
        ".toolbar a:hover{text-decoration:underline}\r\n"
        "button{padding:4px 12px;border:1px solid #c0c4cc;border-radius:4px;background:#fff;color:#333;cursor:pointer;font-size:11px;font-weight:500;white-space:nowrap}\r\n"
        "button:hover{background:#f0f1f3;border-color:#999}\r\n"
        "button.primary{background:#1a73e8;color:#fff;border-color:#1a73e8}\r\n"
        "button.primary:hover{background:#1557b0;border-color:#1557b0}\r\n"
        "button.danger{background:#fff;color:#c62828;border-color:#c62828}\r\n"
        "button.danger:hover{background:#fce4ec}\r\n"
        "button.small{padding:1px 7px;font-size:10px;border-radius:3px}\r\n"
        "button.outline{background:#fff;color:#1a73e8;border-color:#1a73e8}\r\n"
        "button.outline:hover{background:#e8f0fe;border-color:#1557b0}\r\n"
        "button.icon-btn{background:transparent;border:none;color:#bbb;font-size:16px;padding:2px 4px;line-height:1;cursor:pointer}\r\n"
        "button.icon-btn:hover{color:#c62828;background:transparent}\r\n"
        "input{padding:3px 7px;border:1px solid #c0c4cc;border-radius:4px;font-size:11px;font-family:'Cascadia Code',Consolas,monospace;outline:none}\r\n"
        "input:focus{border-color:#1a73e8;box-shadow:0 0 0 2px rgba(26,115,232,0.15)}\r\n"
        ".main{flex:1;display:grid;grid-template-columns:270px 1fr 290px;overflow:hidden;gap:0}\r\n"
        "@media(max-width:900px){.main{grid-template-columns:1fr}}\r\n"
        ".pnl{display:flex;flex-direction:column;background:#fff;overflow:hidden}\r\n"
        ".pnl-l{border-right:1px solid #e0e3e8}\r\n"
        ".pnl-r{border-left:1px solid #e0e3e8}\r\n"
        ".pnl-head{flex-shrink:0;padding:6px 10px;font-size:11px;font-weight:700;color:#555;text-transform:uppercase;letter-spacing:0.5px;background:#f8f9fb;border-bottom:1px solid #e8eaef}\r\n"
        ".pnl-body{flex:1;overflow-y:auto;font-family:'Cascadia Code',Consolas,monospace;font-size:11px;line-height:1.35}\r\n"
        ".asm-line{display:flex;align-items:flex-start;padding:1px 8px;cursor:pointer;white-space:nowrap;border-left:3px solid transparent}\r\n"
        ".asm-line:hover{background:#f5f6f9}\r\n"
        ".asm-line .mark{width:14px;flex-shrink:0;text-align:center;font-size:9px;line-height:15px}\r\n"
        ".asm-line .adr{color:#888;margin-right:8px;flex-shrink:0;min-width:72px}\r\n"
        ".asm-line .code{color:#1a1a2e}\r\n"
        ".asm-line.cur{border-left-color:#1a73e8;background:#e8f0fe}\r\n"
        ".asm-line.cur .adr{color:#1a73e8;font-weight:600}\r\n"
        ".asm-line.cur .code{color:#1557b0;font-weight:600}\r\n"
        ".asm-line.bkpt .mark{color:#c62828}\r\n"
        ".mid-area{display:flex;flex-direction:column;overflow:hidden;padding:6px 8px;gap:4px;background:#eef0f4}\r\n"
        ".pipe-wrap{flex:1;background:#fff;border:1px solid #d4d8e0;border-radius:6px;overflow:hidden;display:flex;align-items:center;justify-content:center;min-height:0}\r\n"
        ".pipe-wrap svg{width:100%;height:100%;display:block}\r\n"
        ".instr-bar{flex-shrink:0;padding:4px 12px;background:#fff;border:1px solid #d4d8e0;border-radius:5px;font-family:'Cascadia Code',Consolas,monospace;font-size:11px;color:#333;display:flex;align-items:center;gap:12px}\r\n"
        ".instr-bar .pc{color:#1a73e8;font-weight:600}\r\n"
        ".instr-bar .hex{color:#888}\r\n"
        ".instr-bar .asm{color:#222}\r\n"
        ".sect{margin-bottom:1px}\r\n"
        ".sect-title{font-size:10px;font-weight:700;color:#888;text-transform:uppercase;letter-spacing:0.4px;padding:5px 10px 3px;background:#fafbfc;border-bottom:1px solid #eee}\r\n"
        "table.rg{width:100%;border-collapse:collapse;font-family:'Cascadia Code',Consolas,monospace;font-size:10.5px;line-height:1.4}\r\n"
        "table.rg td{padding:0 2px}\r\n"
        "table.rg td.rn{color:#aaa;text-align:right;width:18px}\r\n"
        "table.rg td.ra{color:#888;width:30px;font-size:9px}\r\n"
        "table.rg td.rv{color:#1a73e8;font-weight:500}\r\n"
        "table.cs{width:100%;font-family:'Cascadia Code',Consolas,monospace;font-size:10.5px;line-height:1.5}\r\n"
        "table.cs td.cl{color:#888;padding-right:10px}\r\n"
        "table.cs td.cv{color:#1a73e8}\r\n"
        ".bp-line{display:flex;align-items:center;gap:6px;padding:1px 10px;font-family:'Cascadia Code',Consolas,monospace;font-size:10.5px}\r\n"
        ".bp-line .bi{color:#888}\r\n"
        ".bp-line .ba{color:#c62828;font-weight:500}\r\n"
        ".log-bar{flex-shrink:0;height:80px;background:#fafbfc;border-top:1px solid #d4d8e0;overflow-y:auto;padding:4px 14px;font-family:'Cascadia Code',Consolas,monospace;font-size:10.5px;color:#666;line-height:1.5}\r\n"
        ".log-bar .e{padding:0 2px}\r\n"
        ".row-sm{display:flex;align-items:center;gap:5px;padding:3px 10px}\r\n"
        "select{padding:3px 6px;border:1px solid #c0c4cc;border-radius:4px;font-size:10px;background:#fff;color:#333;font-weight:500;outline:none;cursor:pointer}\r\n"
        "select:focus{border-color:#1a73e8}\r\n"
        "</style>\r\n"
        "</head>\r\n"
        "<body>\r\n"
        "<div class=\"toolbar\">\r\n"
        "<span class=\"title\">RISC-V Simulator</span>\r\n"
        "<select id=\"model_sel\" onchange=\"switchModel()\" style=\"padding:3px 6px;border:1px solid #c0c4cc;border-radius:4px;font-size:10px;background:#fff;color:#333;font-weight:500\">\r\n"
        "<option value=\"single\">Single-Cycle</option>\r\n"
        "<option value=\"multi\">Multi-Cycle</option>\r\n"
        "<option value=\"pipeline\" selected>Pipeline</option>\r\n"
        "</select>\r\n"
        "<span style=\"flex:1\"></span>\r\n"
        "<select id=\"prog_sel\" onchange=\"loadDemo(this.value)\" style=\"padding:3px 6px;border:1px solid #c0c4cc;border-radius:4px;font-size:10px;background:#fff;color:#333\">\r\n"
        "<option value=\"\">Loading...</option>\r\n"
        "</select>\r\n"
        "<span class=\"stat stopped\" id=\"status\">Stopped</span>\r\n"
        "<button class=\"primary\" onclick=\"step()\">Step</button>\r\n"
        "<button class=\"primary\" onclick=\"cont()\">Run</button>\r\n"
        "<button class=\"icon-btn\" title=\"Stop Server\" onclick=\"stopSrv()\">⏻</button>\r\n"
        "</div>\r\n"
        "<div class=\"main\">\r\n"
        "<!-- Left: Assembly -->\r\n"
        "<div class=\"pnl pnl-l\">\r\n"
        "<div class=\"pnl-head\" style=\"display:flex;justify-content:space-between;align-items:center\">\r\n"
        "<span>Assembly</span>\r\n"
        "<span style=\"font-weight:400;font-size:9px;color:#999;font-family:'Cascadia Code',Consolas,monospace\">\r\n"
        "<span id=\"pc_disp\">PC:--</span>\r\n"
        "<span id=\"ic_disp\" style=\"margin-left:8px\">Instrs:0</span>\r\n"
        "</span>\r\n"
        "</div>\r\n"
        "<div class=\"pnl-body\" id=\"asm_list\"><div style=\"padding:10px;color:#888\">Loading...</div></div>\r\n"
        "</div>\r\n"
        "<!-- Center: Pipeline + instr detail -->\r\n"
        "<div class=\"mid-area\">\r\n"
        "<div id=\"mode_hint\" style=\"flex-shrink:0;padding:3px 10px;background:#fff;border:1px solid #d4d8e0;border-radius:4px;font-size:10px;color:#888;text-align:center;font-weight:500\">\r\n"
        "Pipeline Mode — 5-stage pipeline: IF → ID → EX → MEM → WB (each Step = 1 cycle)\r\n"
        "</div>\r\n"
        "<div class=\"pipe-wrap\" id=\"pipeline\"><div style=\"color:#888;font-size:13px\">Loading pipeline...</div></div>\r\n"
        "<div class=\"instr-bar\" id=\"instr_disp\"><span class=\"pc\">PC:----</span><span class=\"hex\">--------</span><span class=\"asm\">No instruction executed yet</span></div>\r\n"
        "</div>\r\n"
        "<!-- Right: Regs + CSR + BPs + Mem -->\r\n"
        "<div class=\"pnl pnl-r\" style=\"overflow-y:auto\">\r\n"
        "<div class=\"sect\"><div class=\"sect-title\">Registers</div><div id=\"regs\" style=\"padding:3px 8px\">Loading...</div></div>\r\n"
        "<div class=\"sect\"><div class=\"sect-title\">CSR</div><table class=\"cs\" style=\"margin:2px 10px\"><tbody id=\"csr_body\"><tr><td class=\"cl\">Loading...</td></tr></tbody></table></div>\r\n"
        "<div class=\"sect\"><div class=\"sect-title\">Breakpoints</div><div id=\"bp_list\" style=\"padding:2px 0\"><span style=\"color:#999;font-size:10px;padding:0 10px\">No breakpoints</span></div></div>\r\n"
        "<div class=\"sect\" style=\"border-top:1px solid #eee;padding-top:4px\">\r\n"
        "<div class=\"row-sm\"><span style=\"font-size:10px;color:#888;font-weight:600\">Add BP</span><input id=\"bp_addr\" placeholder=\"addr\" style=\"width:90px\" onkeydown=\"if(event.key==='Enter')addBP()\"><button class=\"small\" onclick=\"addBP()\">+</button></div>\r\n"
        "<div class=\"sect-title\" style=\"display:flex;align-items:center;gap:4px\">Memory\r\n"
        "<button class=\"small\" onclick=\"gotoText()\">.text</button>\r\n"
        "<button class=\"small\" onclick=\"gotoStack()\">.stack</button>\r\n"
        "</div>\r\n"
        "<div style=\"display:flex;align-items:center;gap:3px;padding:3px 8px\">\r\n"
        "<button class=\"small\" onclick=\"navMem(-64)\" title=\"-64B\">◀</button>\r\n"
        "<input id=\"mem_addr\" value=\"0x10000\" style=\"width:72px;font-size:9px;padding:1px 4px\" onkeydown=\"if(event.key==='Enter')showMem()\">\r\n"
        "<button class=\"small\" onclick=\"navMem(64)\" title=\"+64B\">▶</button>\r\n"
        "<button class=\"small\" onclick=\"showMem()\">Go</button>\r\n"
        "</div>\r\n"
        "<div id=\"mem_view\" style=\"padding:3px 8px;font-family:'Cascadia Code',Consolas,monospace;font-size:9px;color:#aaa;line-height:1.4;white-space:pre-wrap;min-height:48px\">Loading...</div>\r\n"
        "</div>\r\n"
        "</div>\r\n"
        "</div>\r\n"
        "<div class=\"log-bar\" id=\"log\"><div class=\"e\">Ready. Click Step to execute the first instruction.</div></div>\r\n"
        "<script>\r\n"
        "function L(m){var o=document.getElementById('log');o.innerHTML+='<div class=\"e\">'+m+'</div>';o.scrollTop=o.scrollHeight;}\r\n"
        "async function api(m,p,b){try{var o={method:m,headers:{}};if(b){o.headers['Content-Type']='application/json';o.body=JSON.stringify(b);}var r=await fetch(p,o);var j=await r.json();if(!r.ok){L('[ERR] '+(j.message||r.status));return null;}return j;}catch(e){L('[ERR] '+e.message);return null;}}\r\n"
        "function hx(n){return '0x'+n.toString(16).padStart(8,'0');}\r\n"
        "function setTv(id,txt){var e=document.getElementById(id);if(e)e.textContent=txt;}\r\n"
        "var g_bps=[],g_mem_addr=0x10000,g_pc=0x10000,g_cpu_model=2;\r\n"
        "var MODEL_NAMES=['Single-cycle','Multi-cycle','Pipeline'];\r\n"
        "function ahx(n){return n.toString(16).padStart(8,'0');}\r\n"
        "function navMem(d){g_mem_addr=(g_mem_addr+d)>>>0;var i=document.getElementById('mem_addr');i.value='0x'+g_mem_addr.toString(16);showMem();}\r\n"
        "function gotoText(){g_mem_addr=g_pc;document.getElementById('mem_addr').value='0x'+g_pc.toString(16);showMem();}\r\n"
        "async function gotoStack(){var d=await api('GET','/registers');if(!d)return;var sp=d.regs[2].value;g_mem_addr=sp;document.getElementById('mem_addr').value='0x'+sp.toString(16);showMem();}\r\n"
        "function updateAsm(data){\r\n"
        "var pc=data.pc,items=data.instructions,cur=null;\r\n"
        "var h='';\r\n"
        "for(var i=0;i<items.length;i++){\r\n"
        "var it=items[i];\r\n"
        "var cls='asm-line';\r\n"
        "var mark='';\r\n"
        "if(it.addr===pc){cls+=' cur';cur=it;}\r\n"
        "for(var j=0;j<g_bps.length;j++){if(g_bps[j].addr===it.addr){cls+=' bkpt';mark='\\u25cf';break;}}\r\n"
        "h+='<div class=\"'+cls+'\" data-addr=\"'+it.addr+'\" onclick=\"toggleBpAt('+it.addr+')\">';\r\n"
        "h+='<span class=\"mark\">'+mark+'</span>';\r\n"
        "h+='<span class=\"adr\">'+hx(it.addr)+'</span>';\r\n"
        "h+='<span class=\"code\">'+(it.valid?it.disasm:'?\?\?')+'</span></div>';\r\n"
        "}\r\n"
        "var o=document.getElementById('asm_list');o.innerHTML=h;\r\n"
        "if(cur){var el=o.querySelector('.asm-line.cur');if(el)el.scrollIntoView({block:'center',behavior:'smooth'});}\r\n"
        "}\r\n"
        "async function toggleBpAt(a){for(var i=0;i<g_bps.length;i++){if(g_bps[i].addr===a){await api('DELETE','/breakpoint?id='+g_bps[i].index);await refreshBPs();await refreshAsm();return;}}\r\n"
        "await api('POST','/breakpoint',{addr:a});await refreshBPs();await refreshAsm();}\r\n"
        "async function refreshAsm(){var d=await api('GET','/disassembly');if(d){g_bps=d.bps||g_bps;updateAsm(d);}}\r\n"
        "async function step(){var d=await api('POST','/step');if(d){var fsm=d.fsm_state?' ['+d.fsm_state+']':'';var cyc=' (cycles:'+d.cycle_count+' instrs:'+d.instr_count+')';L('Step: '+d.disasm+fsm+cyc);\r\n"
        "var ib=document.getElementById('instr_disp');ib.innerHTML='<span class=\"pc\">PC:'+hx(d.pc_after)+'</span><span class=\"hex\">'+hx(d.instr)+'</span><span class=\"asm\">'+d.disasm+'</span>';\r\n"
        "await refreshAll();}}\r\n"
        "async function cont(){var d=await api('POST','/continue');if(d){L('Continue: PC='+hx(d.pc)+(d.running?'':' [stopped]'));await refreshAll();}}\r\n"
        "async function addBP(){var a=document.getElementById('bp_addr').value;var n=parseInt(a);if(isNaN(n)){L('Invalid address: '+a);return;}var r=await api('POST','/breakpoint',{addr:n});if(r){L('BP at '+hx(n));await refreshBPs();await refreshAsm();}}\r\n"
        "async function delBP(i){var r=await api('DELETE','/breakpoint?id='+i);if(r){L('BP '+i+' deleted');await refreshBPs();await refreshAsm();}}\r\n"
        "async function showMem(){var a=document.getElementById('mem_addr').value;var n=parseInt(a.replace(/^0x/i,''),16);if(isNaN(n))return;g_mem_addr=n;document.getElementById('mem_addr').value='0x'+n.toString(16);var r=await api('GET','/memory?addr=0x'+n.toString(16)+'&count=16&unit=w');if(!r)return;var o=document.getElementById('mem_view'),l=[];for(var i=0;i<r.bytes.length;i++){var b=r.bytes[i];if(i%4==0){if(i>0)l.push('\\n');l.push(ahx(r.addr+b.offset)+':');}l.push(b.value.toString(16).padStart(8,'0'));}o.textContent=l.join(' ');o.style.color='#1a1a2e';}\r\n"
        "async function stopSrv(){await api('POST','/stop');}\r\n"
        "async function switchModel(){var sel=document.getElementById('model_sel').value;\r\n"
        "var r=await api('POST','/cpu-model',{model:sel});\r\n"
        "if(r){L('Switched to '+r.name+' mode, entry=0x'+r.entry.toString(16));updateModeHint(r.name);\r\n"
        "await loadDatapath();await refreshAll();}\r\n"  /* reload SVG + refresh */
        "}\r\n"
        "function updateModeHint(name){\r\n"
        "var h=document.getElementById('mode_hint');\r\n"
        "if(!h)return;\r\n"
        "if(name==='single')h.textContent='Single-Cycle — datapath diagram is for reference only (1 cycle = 1 instruction)';\r\n"
        "else if(name==='multi')h.textContent='Multi-Cycle — datapath diagram is for reference only (each Step = 1 FSM state: IF→ID→EX→MEM→WB)';\r\n"
        "else h.textContent='Pipeline Mode — 5-stage pipeline: IF → ID → EX → MEM → WB (each Step = 1 cycle)';\r\n"
        "var sel=document.getElementById('model_sel');if(sel)sel.value=name;\r\n"
        "}\r\n"
        "async function refreshAll(){var dp=await api('GET','/datapath');await refresh(dp);await refreshRegs();await refreshAsm();showMem();\r\n"
        "if(g_cpu_model===2){await refreshPipelineState();}}\r\n"
        "async function refreshPipelineState(){var d=await api('GET','/pipeline-state');if(!d)return;\r\n"
        "if(d.stall||d.flush){L('Pipeline: stall='+d.stall+' flush='+d.flush);}}\r\n"
        "async function refresh(dp){var s=await api('GET','/status');if(!s)return;\r\n"
        "var el=document.getElementById('status');el.textContent=s.running?'Running':'Stopped';el.className='stat '+(s.running?'running':'stopped');\r\n"
        "document.getElementById('pc_disp').textContent='PC:'+hx(s.pc);document.getElementById('ic_disp').textContent='Instrs:'+s.instr_count;g_pc=s.pc;\r\n"
        "g_cpu_model=(typeof s.cpu_model==='number')?s.cpu_model:g_cpu_model;\r\n"
        "var mel=document.getElementById('model_disp');if(mel)mel.textContent=MODEL_NAMES[g_cpu_model]||'Unknown';\r\n"
        "if(dp){updateInstrBar(dp);updateDatapath(dp);}}\r\n"
        "function updateInstrBar(dp){\r\n"
        "var ib=document.getElementById('instr_disp');if(!ib)return;\r\n"
        "var pc_hex=hx(dp.pc);\r\n"
        "var instr_hex=(dp.instr||dp.instr===0)?hx(dp.instr):'--------';\r\n"
        "var asm=dp.disasm||((dp.valid||dp.pc!==0)?'':'fetching...');\r\n"
        "ib.innerHTML='<span class=\"pc\">PC:'+pc_hex+'</span><span class=\"hex\">'+instr_hex+'</span><span class=\"asm\">'+asm+'</span>';\r\n"
        "}\r\n"
        "function updateDatapath(dp){\r\n"
        "setTv('dv_pc','PC='+hx(dp.pc));\r\n"
        "setTv('dv_instr','INST='+hx(dp.instr));setTv('dv_disasm',dp.disasm);\r\n"
        "setTv('dv_alu_op','OP='+dp.alu_op);\r\n"
        "setTv('dv_alu_a','ALU_A='+hx(dp.alu_a));setTv('dv_alu_b','ALU_B='+hx(dp.alu_b));\r\n"
        "setTv('dv_alu_out','ALU_OUT='+hx(dp.alu_result));\r\n"
        "setTv('dv_rs1','RS1=x'+dp.rs1+'='+hx(dp.rs1_val));setTv('dv_rs2','RS2=x'+dp.rs2+'='+hx(dp.rs2_val));\r\n"
        "setTv('dv_imm','IMM='+dp.imm);\r\n"
        "setTv('dv_wb','WB=x'+dp.rd+(dp.reg_write?'='+hx(dp.rd_val):'--'));\r\n"
        "setTv('dv_next_pc','NPC='+hx(dp.next_pc)+(dp.branch_taken?' JMP':''));\r\n"
        "setTv('dv_mem_addr',(dp.mem_read||dp.mem_write)?'MEM_ADDR='+hx(dp.mem_addr):'MEM_ADDR=--');\r\n"
        "setTv('dv_mem_rd',dp.mem_read?'MEM_RD='+hx(dp.mem_rdata):'MEM_RD=--');\r\n"
        "setTv('dv_mem_wd',dp.mem_write?'MEM_WD='+hx(dp.mem_wdata):'MEM_WD=--');\r\n"
        "setTv('dv_fwd',dp.fwd_a||dp.fwd_b?'FWD='+(dp.fwd_a?'A':'')+(dp.fwd_a&&dp.fwd_b?'/':'')+(dp.fwd_b?'B':''):'FWD=--');\r\n"
        "setTv('dv_stall',dp.stall?'STALL=1':'STALL=0');\r\n"
        "setTv('dv_flush',dp.flush?'FLUSH=1':'FLUSH=0');\r\n"
        "if(dp.fsm_state){setTv('dv_fsm','FSM='+dp.fsm_state);}else{setTv('dv_fsm','');}\r\n"
        "}\r\n"
        "async function refreshRegs(){var d=await api('GET','/registers');if(!d)return;\r\n"
        "var h='<table class=\"rg\">';for(var r=0;r<16;r++){h+='<tr>';for(var c=0;c<2;c++){var i=r+c*16;var x=d.regs[i];h+='<td class=\"rn\">x'+i+'</td><td class=\"ra\">'+x.abi+'</td><td class=\"rv\">'+hx(x.value)+'</td>';if(c==0)h+='<td style=\"width:14px\"></td>';}h+='</tr>';}h+='</table>';\r\n"
        "document.getElementById('regs').innerHTML=h;\r\n"
        "document.getElementById('csr_body').innerHTML='<tr><td class=\"cl\">pc</td><td class=\"cv\">'+hx(d.pc)+'</td></tr>'\r\n"
        "+'<tr><td class=\"cl\">mstatus</td><td class=\"cv\">'+hx(d.mstatus)+'</td></tr>'\r\n"
        "+'<tr><td class=\"cl\">mtvec</td><td class=\"cv\">'+hx(d.mtvec)+'</td></tr>'\r\n"
        "+'<tr><td class=\"cl\">mepc</td><td class=\"cv\">'+hx(d.mepc)+'</td></tr>'\r\n"
        "+'<tr><td class=\"cl\">mcause</td><td class=\"cv\">'+hx(d.mcause)+'</td></tr>'\r\n"
        "+'<tr><td class=\"cl\">mtval</td><td class=\"cv\">'+hx(d.mtval)+'</td></tr>';\r\n"
        "}\r\n"
        "async function refreshBPs(){var d=await api('GET','/breakpoints');if(!d)return;\r\n"
        "g_bps=d.breakpoints;\r\n"
        "var h='';for(var i=0;i<g_bps.length;i++){var b=g_bps[i];h+='<div class=\"bp-line\"><span class=\"bi\">#'+b.index+'</span><span class=\"ba\">'+hx(b.addr)+'</span><span style=\"font-size:9px;color:#888\">'+(b.enabled?'on':'off')+'</span><button class=\"small\" onclick=\"delBP('+b.index+')\">Del</button></div>';}\r\n"
        "if(!g_bps.length)h='<span style=\"color:#999;font-size:10px;padding:0 10px\">No breakpoints</span>';\r\n"
        "document.getElementById('bp_list').innerHTML=h;}\r\n"
        "async function loadDatapath(){var r=await fetch('/pipeline.svg');\r\n"
        "if(!r.ok){document.getElementById('pipeline').innerHTML='<div style=\"color:#c62828;padding:20px\">SVG not loaded</div>';return;}\r\n"
        "var t=await r.text();document.getElementById('pipeline').innerHTML=t;}\r\n"
        "async function loadDemo(name){\r\n"
        "if(!name)return;\r\n"
        "var r=await api('POST','/program',{program:name});\r\n"
        "if(r&&r.status==='ok'){location.reload();}\r\n"
        "else{alert('Failed to load program: '+(r?r.message:'unknown'));}}\r\n"
        "async function populateProgs(){\r\n"
        "var r=await api('GET','/programs');if(!r||!r.programs)return;\r\n"
        "var s=document.getElementById('prog_sel');\r\n"
        "s.innerHTML='';\r\n"
        "for(var i=0;i<r.programs.length;i++){\r\n"
        "var p=r.programs[i];\r\n"
        "s.innerHTML+='<option value=\"'+p.name+'\">'+p.name+' — '+p.desc+'</option>';}\r\n"
        "s.value=r.current||'';}\r\n"
        "async function init(){await loadDatapath();\r\n"
        "var s=await api('GET','/status');if(s&&s.cpu_model_name){updateModeHint(s.cpu_model_name);}\r\n"
        "var dp=await api('GET','/datapath');await refresh(dp);await refreshRegs();await refreshBPs();await refreshAsm();\r\n"
        "await populateProgs();}\r\n"
        "init();\r\n"
        "</script>\r\n"
        "</body></html>\r\n";

    send_response(client, 200, "text/html; charset=utf-8", html);
}

/* GET /programs — 返回内置 demo 程序列表 */
static void route_get_programs(SOCKET client)
{
    char json[2048];
    int pos = 0;
    pos += snprintf(json + pos, sizeof(json) - pos, "{\"programs\":[");
    for (int i = 0; i < DEMO_PROGRAM_COUNT; i++) {
        if (i > 0) pos += snprintf(json + pos, sizeof(json) - pos, ",");
        pos += snprintf(json + pos, sizeof(json) - pos,
            "{\"name\":\"%s\",\"desc\":\"%s\"}",
            DEMO_PROGRAMS[i].name, DEMO_PROGRAMS[i].desc);
    }
    /* 标记当前加载的是哪个程序 */
    const char *cur = g_elf_path;
    if (cur) {
        for (int i = 0; i < DEMO_PROGRAM_COUNT; i++) {
            if (strstr(cur, DEMO_PROGRAMS[i].name)) {
                pos += snprintf(json + pos, sizeof(json) - pos,
                    "],\"current\":\"%s\"}", DEMO_PROGRAMS[i].name);
                send_json(client, 200, json);
                return;
            }
        }
    }
    pos += snprintf(json + pos, sizeof(json) - pos, "],\"current\":null}");
    send_json(client, 200, json);
}

/* POST /program — 加载指定 demo 程序 */
static void route_post_program(SOCKET client, const HttpRequest *req)
{
    const char *body = req->body;
    if (!body) {
        send_json(client, 400, "{\"status\":\"error\",\"message\":\"Missing body\"}");
        return;
    }
    /* 查找 name 字段 */
    const char *key = "\"program\"";
    const char *p = strstr(body, key);
    if (!p) {
        send_json(client, 400, "{\"status\":\"error\",\"message\":\"Missing program name\"}");
        return;
    }
    p += strlen(key);
    while (*p == ' ' || *p == ':' || *p == '"') p++;
    char name[32];
    int i = 0;
    while (*p && *p != '"' && i < 31) name[i++] = *p++;
    name[i] = '\0';

    /* 在 demo 列表中查找 */
    const DemoProgram *dp = NULL;
    for (int j = 0; j < DEMO_PROGRAM_COUNT; j++) {
        if (strcmp(DEMO_PROGRAMS[j].name, name) == 0) {
            dp = &DEMO_PROGRAMS[j];
            break;
        }
    }
    if (!dp) {
        send_json(client, 404, "{\"status\":\"error\",\"message\":\"Program not found\"}");
        return;
    }

    /* 写入 ELF 文件 */
    char fname[64];
    snprintf(fname, sizeof(fname), "demo_%s.elf", dp->name);
    FILE *fp = fopen(fname, "wb");
    if (!fp) {
        send_json(client, 500, "{\"status\":\"error\",\"message\":\"Cannot write file\"}");
        return;
    }
    fwrite(dp->data, 1, dp->size, fp);
    fclose(fp);

    /* 重载 ELF */
    sim_lock(g_sim);
    sim_reload(g_sim);
    bool ok = sim_load_elf(g_sim, fname);
    if (ok) {
        strncpy(g_elf_path, fname, sizeof(g_elf_path) - 1);
        g_elf_path[sizeof(g_elf_path) - 1] = '\0';
    }
    sim_unlock(g_sim);

    if (ok) {
        char resp[256];
        snprintf(resp, sizeof(resp),
            "{\"status\":\"ok\",\"program\":\"%s\",\"entry\":\"0x%08x\"}",
            name, g_sim->cpu.pc);
        send_json(client, 200, resp);
        printf("[program] Loaded: %s\n", name);
    } else {
        send_json(client, 500, "{\"status\":\"error\",\"message\":\"Load failed\"}");
    }
}

/* ── (unused on release build) POST /compile — C 源码编译 + 加载 ── */
static void route_post_compile(SOCKET client, const HttpRequest *req)
{
    /* ── 1. 从 JSON body 中提取 source 字段 ──────────────────── */
    const char *src = NULL;
    char src_buf[16384];
    src_buf[0] = '\0';

    /* 查找 "source": 后面的 JSON 字符串 */
    const char *key = strstr(req->body, "\"source\"");
    if (key) {
        key = strchr(key, ':');
        if (key) {
            key++; /* 跳过冒号 */
            while (*key && (*key == ' ' || *key == '\t' || *key == '\n'))
                key++;
            if (*key == '"') {
                key++; /* 跳过开引号 */
                int i = 0;
                while (*key && *key != '"' && i < (int)sizeof(src_buf) - 1) {
                    /* 处理 JSON 转义 */
                    if (*key == '\\' && key[1] != '\0') {
                        key++;
                        switch (*key) {
                            case 'n':  src_buf[i++] = '\n'; break;
                            case 'r':  src_buf[i++] = '\r'; break;
                            case 't':  src_buf[i++] = '\t'; break;
                            case '"':  src_buf[i++] = '"';  break;
                            case '\\': src_buf[i++] = '\\'; break;
                            default:   src_buf[i++] = *key;  break;
                        }
                    } else {
                        src_buf[i++] = *key;
                    }
                    key++;
                }
                src_buf[i] = '\0';
                src = src_buf;
            }
        }
    }

    if (!src || strlen(src) == 0) {
        send_json(client, 400,
            "{\"status\":\"error\",\"message\":\"Missing or empty 'source' field in JSON body\"}");
        return;
    }

    /* ── 2. 写入源文件（用户代码 + _start 包装） ──────────────── */
    {
        FILE *f = fopen(COMPILE_INPUT, "w");
        if (!f) {
            send_json(client, 500,
                "{\"status\":\"error\",\"message\":\"Failed to write temp source file\"}");
            return;
        }
        /* 用户代码原样写入 */
        fprintf(f, "%s\n", src);

        /* 自动追加 _start 包装函数：
         *   1. 调用 main()（用户代码的入口）
         *   2. main 返回后，用 ecall 触发 SYS_exit(93) 干净退出
         *      - a0 保留 main 的返回值（在 Debugger 寄存器面板可见）
         *      - a7 = 93 → syscall handler → cpu.running = false
         */
        fprintf(f,
            "\n/* ---- auto-generated by simulator ---- */\n"
            "void _start() {\n"
            "    main();\n"
            "    __asm__ volatile(\"li a7, 93\\n\\tecall\");\n"
            "}\n");
        fclose(f);
        printf("[compile] Wrote source + _start wrapper to %s\n", COMPILE_INPUT);
    }

    /* ── 3. 调用 RISC-V GCC 编译（CreateProcess 直调，不走 cmd.exe） */
    /* 将相对路径解析为绝对路径 */
    char gcc_path[512];
    if (!_fullpath(gcc_path, RISC_V_GCC, sizeof(gcc_path))) {
        strncpy(gcc_path, RISC_V_GCC, sizeof(gcc_path) - 1);
        gcc_path[sizeof(gcc_path) - 1] = '\0';
    }
    /* 构建命令行：gcc flags -o output input */
    char cmdline[1024];
    snprintf(cmdline, sizeof(cmdline),
        "\"%s\" %s -o \"%s\" \"%s\"",
        gcc_path, COMPILE_FLAGS, COMPILE_OUTPUT, COMPILE_INPUT);
    printf("[compile] Running: %s\n", cmdline);

    char compile_output[4096];
    compile_output[0] = '\0';
    int compile_ok = 0; /* 0=未知, 1=成功, -1=失败 */

    /* 创建匿名管道用于捕获 stderr+stdout */
    HANDLE hReadPipe, hWritePipe;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        snprintf(compile_output, sizeof(compile_output),
            "Error: Cannot create pipe for compiler output");
        compile_ok = -1;
    } else {
        /* 确保读端不被子进程继承 */
        SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFO si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.hStdError  = hWritePipe;
        si.hStdOutput = hWritePipe;
        si.dwFlags   |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        ZeroMemory(&pi, sizeof(pi));

        /* 创建进程：不显示窗口，重定向输出到管道 */
        if (CreateProcessA(
                NULL,           /* lpApplicationName — 从命令行解析 */
                cmdline,        /* lpCommandLine */
                NULL, NULL,     /* lpProcessAttributes, lpThreadAttributes */
                TRUE,           /* bInheritHandles — 子进程继承管道写端 */
                CREATE_NO_WINDOW, /* dwCreationFlags */
                NULL, NULL,     /* lpEnvironment, lpCurrentDirectory */
                &si, &pi)) {

            /* 关掉写端（不然 ReadFile 永远等不到 EOF） */
            CloseHandle(hWritePipe);
            hWritePipe = NULL;

            /* 读取编译输出 */
            DWORD total = 0;
            DWORD nread;
            char buf[512];
            while (ReadFile(hReadPipe, buf, sizeof(buf) - 1, &nread, NULL)
                   && nread > 0) {
                if (total + nread >= sizeof(compile_output) - 1) {
                    nread = (DWORD)(sizeof(compile_output) - 1 - total);
                    if (nread == 0) break;
                }
                memcpy(compile_output + total, buf, nread);
                total += nread;
            }
            compile_output[total] = '\0';

            /* 等待编译完成，获取退出码 */
            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD exit_code;
            GetExitCodeProcess(pi.hProcess, &exit_code);
            compile_ok = (exit_code == 0) ? 1 : -1;

            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);

            /* 去掉输出末尾的空白 */
            while (total > 0 &&
                   (compile_output[total - 1] == '\n' ||
                    compile_output[total - 1] == '\r')) {
                compile_output[--total] = '\0';
            }
        } else {
            DWORD err = GetLastError();
            snprintf(compile_output, sizeof(compile_output),
                "Error: Cannot start compiler.\n"
                "Path: %s\n"
                "Windows error code: %lu\n\n"
                "Make sure RISC-V GCC is installed at the expected location.",
                gcc_path, err);
            compile_ok = -1;
        }

        CloseHandle(hReadPipe);
        if (hWritePipe) CloseHandle(hWritePipe);
    }

    printf("[compile] Exit: %s, output: %s\n",
           (compile_ok == 1) ? "OK" : "FAIL",
           compile_output[0] ? compile_output : "(none)");

    /* ── 4. 编译成功 → 加载 ELF ───────────────────────────────── */
    if (compile_ok == 1) {
        sim_lock(g_sim);

        /* 重新初始化模拟器（保留 CPU 模型 + 锁） */
        sim_reload(g_sim);

        /* 加载新编译的 ELF */
        bool loaded = sim_load_elf(g_sim, COMPILE_OUTPUT);

        /* 更新 ELF 路径，使得模型切换时能重新加载 */
        if (loaded) {
            strncpy(g_elf_path, COMPILE_OUTPUT, sizeof(g_elf_path) - 1);
            g_elf_path[sizeof(g_elf_path) - 1] = '\0';
        }

        uint32_t entry = g_sim->cpu.pc;
        uint32_t sp    = g_sim->cpu.regs[REG_SP];

        sim_unlock(g_sim);

        if (loaded) {
            char resp[4800];
            /* 转义 compiler output 中的特殊字符 */
            char escaped_output[4096];
            char *od = escaped_output;
            for (const char *s = compile_output; *s &&
                 od < escaped_output + sizeof(escaped_output) - 2; s++) {
                if (*s == '"')  *od++ = '\\';
                if (*s == '\\') *od++ = '\\';
                if (*s == '\n') { *od++ = '\\'; *od++ = 'n'; }
                else if (*s == '\r') { }
                else if (*s == '\t') { *od++ = '\\'; *od++ = 't'; }
                else *od++ = *s;
            }
            *od = '\0';

            snprintf(resp, sizeof(resp),
                "{\"status\":\"ok\","
                "\"entry\":%u,\"sp\":%u,"
                "\"compile_output\":\"%s\"}",
                entry, sp, escaped_output);
            send_json(client, 200, resp);
        } else {
            send_json(client, 500,
                "{\"status\":\"error\","
                "\"message\":\"Compilation OK but ELF load failed\"}");
        }
    } else {
        /* 编译失败 → 返回错误信息 */
        char resp[4608];
        /* 转义编译错误输出 */
        char escaped_output[4096];
        char *od = escaped_output;
        for (const char *s = compile_output; *s &&
             od < escaped_output + sizeof(escaped_output) - 2; s++) {
            if (*s == '"')  *od++ = '\\';
            if (*s == '\\') *od++ = '\\';
            if (*s == '\n') { *od++ = '\\'; *od++ = 'n'; }
            else if (*s == '\r') { }
            else if (*s == '\t') { *od++ = '\\'; *od++ = 't'; }
            else *od++ = *s;
        }
        *od = '\0';

        snprintf(resp, sizeof(resp),
            "{\"status\":\"error\","
            "\"message\":\"Compilation failed\","
            "\"compile_output\":\"%s\"}",
            escaped_output);
        send_json(client, 400, resp);
    }
}

/* GET /pipeline-state — 流水线寄存器状态 */
static void route_get_pipeline_state(SOCKET client)
{
    JsonBuf jb;
    jb_init(&jb);

    sim_lock(g_sim);
    PipelineState *p = &g_sim->pipe;

    jb_printf(&jb,
        "{\"stall_cycles\":%" PRIu64 ",\"flush_cycles\":%" PRIu64 ",",
        p->stall_cycles, p->flush_cycles);

    /* IF 阶段 */
    jb_append(&jb, "\"if_id\":{");
    jb_printf(&jb, "\"valid\":%s,\"pc\":%u,\"instr\":%u",
              p->if_id.valid ? "true" : "false", p->if_id.pc, p->if_id.instr);
    if (p->if_id.valid) {
        char disasm[128];
        cpu_disasm(p->if_id.instr, p->if_id.pc, disasm, sizeof(disasm));
        jb_append(&jb, ",\"disasm\":");
        jb_append_escaped(&jb, disasm);
    }
    jb_append(&jb, "},");

    /* ID 阶段 */
    jb_append(&jb, "\"id_ex\":{");
    jb_printf(&jb, "\"valid\":%s,\"pc\":%u,\"opcode\":%u,\"rd\":%u,"
              "\"rs1\":%u,\"rs2\":%u,\"funct3\":%u,\"funct7\":%u,"
              "\"imm\":%d,\"rs1_val\":%u,\"rs2_val\":%u",
              p->id_ex.valid ? "true" : "false", p->id_ex.pc,
              p->id_ex.d.opcode, p->id_ex.d.rd, p->id_ex.d.rs1,
              p->id_ex.d.rs2, p->id_ex.d.funct3, p->id_ex.d.funct7,
              p->id_ex.d.imm, p->id_ex.rs1_val, p->id_ex.rs2_val);
    if (p->id_ex.valid) {
        char disasm[128];
        cpu_disasm(p->id_ex.instr, p->id_ex.pc, disasm, sizeof(disasm));
        jb_append(&jb, ",\"disasm\":");
        jb_append_escaped(&jb, disasm);
    }
    jb_append(&jb, "},");

    /* EX 阶段 */
    jb_append(&jb, "\"ex_mem\":{");
    jb_printf(&jb, "\"valid\":%s,\"pc\":%u,\"alu_result\":%u,"
              "\"rs2_val\":%u,\"rd\":%u,\"opcode\":%u,\"funct3\":%u,"
              "\"reg_write\":%s,\"mem_read\":%s,\"mem_write\":%s",
              p->ex_mem.valid ? "true" : "false", p->ex_mem.pc,
              p->ex_mem.alu_result, p->ex_mem.rs2_val,
              p->ex_mem.rd, p->ex_mem.opcode, p->ex_mem.funct3,
              p->ex_mem.reg_write ? "true" : "false",
              p->ex_mem.mem_read ? "true" : "false",
              p->ex_mem.mem_write ? "true" : "false");
    if (p->ex_mem.valid) {
        char disasm[128];
        cpu_disasm(p->ex_mem.instr, p->ex_mem.pc, disasm, sizeof(disasm));
        jb_append(&jb, ",\"disasm\":");
        jb_append_escaped(&jb, disasm);
    }
    jb_append(&jb, "},");

    /* MEM 阶段 */
    jb_append(&jb, "\"mem_wb\":{");
    jb_printf(&jb, "\"valid\":%s,\"pc\":%u,\"alu_result\":%u,"
              "\"mem_data\":%u,\"rd\":%u,\"opcode\":%u,"
              "\"reg_write\":%s,\"is_load\":%s",
              p->mem_wb.valid ? "true" : "false", p->mem_wb.pc,
              p->mem_wb.alu_result, p->mem_wb.mem_data,
              p->mem_wb.rd, p->mem_wb.opcode,
              p->mem_wb.reg_write ? "true" : "false",
              p->mem_wb.is_load ? "true" : "false");
    if (p->mem_wb.valid) {
        char disasm[128];
        cpu_disasm(p->mem_wb.instr, p->mem_wb.pc, disasm, sizeof(disasm));
        jb_append(&jb, ",\"disasm\":");
        jb_append_escaped(&jb, disasm);
    }
    jb_append(&jb, "}}");
    sim_unlock(g_sim);

    send_json(client, 200, jb.buf);
    jb_free(&jb);
}

/* GET /cpu-model — 返回当前 CPU 模型 */
static void route_get_cpu_model(SOCKET client)
{
    sim_lock(g_sim);
    int model = (int)g_sim->cpu_model;
    const char *name = (model == 0) ? "single" :
                       (model == 1) ? "multi" : "pipeline";
    char json[128];
    snprintf(json, sizeof(json),
             "{\"model\":%d,\"name\":\"%s\"}", model, name);
    sim_unlock(g_sim);

    send_json(client, 200, json);
}

/* POST /cpu-model — 切换 CPU 模型 */
static void route_post_cpu_model(SOCKET client, const HttpRequest *req)
{
    /* 解析 model 字段 */
    const char *model_str = NULL;
    const char *key = strstr(req->body, "\"model\"");
    if (key) {
        key = strchr(key, ':');
        if (key) {
            key++;
            while (*key && (*key == ' ' || *key == '\t' || *key == '\n'))
                key++;
            if (*key == '"') {
                key++;
                static char buf[32];
                int i = 0;
                while (*key && *key != '"' && i < 31) buf[i++] = *key++;
                buf[i] = '\0';
                model_str = buf;
            } else if (*key >= '0' && *key <= '9') {
                static char buf2[32];
                int i = 0;
                while (*key && *key != ',' && *key != '}' && i < 31) buf2[i++] = *key++;
                buf2[i] = '\0';
                model_str = buf2;
            }
        }
    }

    if (!model_str) {
        send_json(client, 400,
            "{\"status\":\"error\",\"message\":\"Missing 'model' field\"}");
        return;
    }

    CpuModel new_model;
    if (strcmp(model_str, "single") == 0 || strcmp(model_str, "0") == 0)
        new_model = MODEL_SINGLE_CYCLE;
    else if (strcmp(model_str, "multi") == 0 || strcmp(model_str, "1") == 0)
        new_model = MODEL_MULTI_CYCLE;
    else if (strcmp(model_str, "pipeline") == 0 || strcmp(model_str, "2") == 0)
        new_model = MODEL_PIPELINE;
    else {
        char err[128];
        snprintf(err, sizeof(err),
            "{\"status\":\"error\",\"message\":\"Unknown model '%s'\"}", model_str);
        send_json(client, 400, err);
        return;
    }

    sim_lock(g_sim);
    g_sim->cpu_model = new_model;

    /* 重新加载 ELF */
    bool loaded = false;
    uint32_t entry = 0;
    if (g_elf_path[0]) {
        sim_reload(g_sim);
        loaded = sim_load_elf(g_sim, g_elf_path);
        entry = g_sim->cpu.pc;
    }
    sim_unlock(g_sim);

    const char *name = (new_model == MODEL_SINGLE_CYCLE) ? "single" :
                       (new_model == MODEL_MULTI_CYCLE) ? "multi" : "pipeline";
    if (loaded) {
        char resp[256];
        snprintf(resp, sizeof(resp),
            "{\"status\":\"ok\",\"model\":%d,\"name\":\"%s\",\"entry\":%u}",
            (int)new_model, name, entry);
        send_json(client, 200, resp);
    } else {
        char resp[200];
        snprintf(resp, sizeof(resp),
            "{\"status\":\"ok\",\"model\":%d,\"name\":\"%s\",\"entry\":0,"
            "\"warning\":\"Model changed but no ELF reloaded\"}",
            (int)new_model, name);
        send_json(client, 200, resp);
    }
}

/* GET /compile — 在线 C 代码编辑器页面 */
static void route_get_compile(SOCKET client)
{
    static const char *html =
        "<!DOCTYPE html>\r\n"
        "<html lang=\"en\">\r\n"
        "<head>\r\n"
        "<meta charset=\"UTF-8\">\r\n"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\r\n"
        "<title>RISC-V C Compiler</title>\r\n"
        "<style>\r\n"
        "*{box-sizing:border-box;margin:0;padding:0}\r\n"
        "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#fff;color:#333;padding:12px 16px;max-width:1100px;margin:0 auto}\r\n"
        ".toolbar{display:flex;align-items:center;gap:12px;padding:6px 0;border-bottom:1.5px solid #e0e0e0;margin-bottom:8px;flex-wrap:wrap}\r\n"
        ".toolbar .title{font-size:18px;font-weight:700;color:#222;margin-right:auto}\r\n"
        ".toolbar a{color:#1a73e8;font-size:12px;text-decoration:none}\r\n"
        ".toolbar a:hover{text-decoration:underline}\r\n"
        "button{padding:5px 14px;border:1.5px solid #bbb;border-radius:4px;background:#fafafa;color:#333;cursor:pointer;font-size:12px;font-weight:500}\r\n"
        "button:hover{background:#e8e8e8;border-color:#888}\r\n"
        "button.primary{background:#1a73e8;color:#fff;border-color:#1a73e8}\r\n"
        "button.primary:hover{background:#1557b0}\r\n"
        "button.green{background:#2e7d32;color:#fff;border-color:#2e7d32}\r\n"
        "button.green:hover{background:#1b5e20}\r\n"
        ".main-row{display:flex;gap:8px;margin-bottom:8px}\r\n"
        ".left{flex:1;min-width:0}\r\n"
        ".right{flex:1;min-width:0}\r\n"
        "#editor{width:100%;height:380px;padding:10px;font-family:'Cascadia Code',Consolas,monospace;font-size:13px;border:1.5px solid #ccc;border-radius:6px;resize:vertical;background:#fafafa;tab-size:4}\r\n"
        "#editor:focus{outline:none;border-color:#1a73e8;box-shadow:0 0 0 1px #1a73e8}\r\n"
        "#output{width:100%;height:380px;padding:10px;font-family:'Cascadia Code',Consolas,monospace;font-size:12px;border:1px solid #d4d8e0;border-radius:6px;background:#fafbfc;color:#333;overflow-y:auto;white-space:pre-wrap;word-break:break-all;line-height:1.5}\r\n"
        "#output .err{color:#c62828;font-weight:500}\r\n"
        "#output .ok{color:#2e7d32;font-weight:500}\r\n"
        "#output .info{color:#1a73e8}\r\n"
        ".row{display:flex;align-items:center;gap:8px;flex-wrap:wrap;margin-bottom:6px}\r\n"
        "select{padding:4px 8px;border:1.5px solid #ccc;border-radius:4px;font-size:12px;background:#fff}\r\n"
        ".sample-btn{font-size:11px;padding:2px 8px;border:1px solid #ddd;border-radius:3px;background:#f5f5f5;cursor:pointer;color:#666}\r\n"
        ".sample-btn:hover{background:#e8e8e8;color:#333}\r\n"
        "</style>\r\n"
        "</head>\r\n"
        "<body>\r\n"
        "<div class=\"toolbar\">\r\n"
        "<span class=\"title\">RISC-V C Compiler</span>\r\n"
        "<a href=\"/\">← Back to Debugger</a>\r\n"
        "</div>\r\n"
        "<div class=\"row\" style=\"justify-content:space-between\">\r\n"
        "<div style=\"display:flex;align-items:center;gap:6px\">\r\n"
        "<span style=\"font-size:11px;color:#888;font-weight:600\">Samples:</span>\r\n"
        "<button class=\"sample-btn\" onclick=\"loadSample('fib')\">Fibonacci</button>\r\n"
        "<button class=\"sample-btn\" onclick=\"loadSample('add')\">Add Test</button>\r\n"
        "<button class=\"sample-btn\" onclick=\"loadSample('mem')\">Memory</button>\r\n"
        "<button class=\"sample-btn\" onclick=\"loadSample('float')\">Float</button>\r\n"
        "</div>\r\n"
        "<div style=\"display:flex;align-items:center;gap:8px\">\r\n"
        "<button class=\"primary\" onclick=\"compileCode()\">Compile &amp; Load</button>\r\n"
        "<span style=\"font-size:10px;color:#aaa\">Ctrl+Enter</span>\r\n"
        "</div>\r\n"
        "</div>\r\n"
        "<div class=\"main-row\">\r\n"
        "<div class=\"left\">\r\n"
        "<div style=\"font-size:11px;color:#888;margin-bottom:2px\">C Source Code</div>\r\n"
        "<textarea id=\"editor\" spellcheck=\"false\">int main() {\r\n"
        "    int a = 10;\r\n"
        "    int b = 20;\r\n"
        "    int c = a + b;\r\n"
        "    return c;\r\n"
        "}</textarea>\r\n"
        "</div>\r\n"
        "<div class=\"right\">\r\n"
        "<div style=\"font-size:11px;color:#888;margin-bottom:2px\">Compiler Output</div>\r\n"
        "<div id=\"output\"><span class=\"info\">Ready. Write C code and click 'Compile & Load'.</span></div>\r\n"
        "</div>\r\n"
        "</div>\r\n"
        "<script>\r\n"
        "var samples={\r\n"
        "fib:'int fib(int n) {\\n    if (n <= 1) return n;\\n    return fib(n-1) + fib(n-2);\\n}\\n\\nint main() {\\n    return fib(10);\\n}',\r\n"
        "add:'int add(int x, int y) { return x + y; }\\nint sub(int x, int y) { return x - y; }\\n\\nint main() {\\n    int a = add(100, 200);\\n    int b = sub(a, 50);\\n    return b;\\n}',\r\n"
        "mem:'int arr[10];\\n\\nint main() {\\n    for (int i = 0; i < 10; i++) {\\n        arr[i] = i * i;\\n    }\\n    int sum = 0;\\n    for (int i = 0; i < 10; i++) {\\n        sum += arr[i];\\n    }\\n    return sum;\\n}',\r\n"
        "float:'float mul_add(float a, float b, float c) {\\n    return a * b + c;\\n}\\n\\nint main() {\\n    volatile float x = 3.14f;\\n    volatile float y = 2.0f;\\n    volatile float z = 1.5f;\\n    float r = mul_add(x, y, z);\\n    return (int)(r * 100.0f);\\n}'\r\n"
        "};\r\n"
        "function loadSample(n){document.getElementById('editor').value=samples[n];}\r\n"
        "function $(id){return document.getElementById(id);}\r\n"
        "function setOutput(html){$('output').innerHTML=html;}\r\n"
        "async function compileCode(){\r\n"
        "    var src=$('editor').value;\r\n"
        "    if(!src.trim()){setOutput('<span class=\"err\">Error: Source code is empty</span>');return;}\r\n"
        "    setOutput('<span class=\"info\">Compiling...</span>');\r\n"
        "    try{\r\n"
        "        var r=await fetch('/compile',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({source:src})});\r\n"
        "        var j=await r.json();\r\n"
        "        if(j.status==='ok'){\r\n"
        "            var h='<div style=\"font-size:13px;font-weight:600;color:#2e7d32;margin-bottom:6px\">Compilation SUCCESS</div>';\r\n"
        "            h+='<div style=\"font-size:11px;color:#555;margin-bottom:4px\">Entry: 0x'+j.entry.toString(16).padStart(8,'0')+'  |  SP: 0x'+j.sp.toString(16).padStart(8,'0')+'</div>';\r\n"
        "            if(j.compile_output) h+='<div style=\"font-size:11px;color:#888;margin-bottom:8px\">'+j.compile_output+'</div>';\r\n"
        "            h+='<a href=\"/\" style=\"display:inline-block;padding:6px 18px;background:#1a73e8;color:#fff !important;border-radius:5px;text-decoration:none;font-size:12px;font-weight:600;font-family:-apple-system,BlinkMacSystemFont,sans-serif\">Open Debugger →</a>';\r\n"
        "            setOutput(h);\r\n"
        "        }else{\r\n"
        "            var h='<div style=\"font-size:13px;font-weight:600;color:#c62828;margin-bottom:6px\">Compilation FAILED</div>';\r\n"
        "            if(j.compile_output) h+='<div style=\"font-size:11px;color:#555;white-space:pre-wrap\">'+j.compile_output+'</div>';\r\n"
        "            else h+='<div style=\"font-size:11px;color:#888\">'+(j.message||'Unknown error')+'</div>';\r\n"
        "            setOutput(h);\r\n"
        "        }\r\n"
        "    }catch(e){setOutput('<span class=\"err\">Network error: '+e.message+'</span>');}\r\n"
        "}\r\n"
        "/* Ctrl+Enter to compile */\r\n"
        "document.getElementById('editor').addEventListener('keydown',function(e){\r\n"
        "    if(e.ctrlKey && e.key==='Enter'){e.preventDefault();compileCode();}\r\n"
        "});\r\n"
        "</script>\r\n"
        "</body></html>\r\n";

    send_response(client, 200, "text/html; charset=utf-8", html);
}

/* ── HTTP 响应发送 ─────────────────────────────────────────────── */

static void send_response(SOCKET client, int code, const char *content_type,
                          const char *body)
{
    char header[512];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Cache-Control: no-store, no-cache, must-revalidate\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        code,
        (code == 200) ? "OK" :
        (code == 400) ? "Bad Request" :
        (code == 404) ? "Not Found" :
        (code == 405) ? "Method Not Allowed" : "Error",
        content_type,
        (int)strlen(body));

    /* 发送 header + body（忽略 SIGPIPE-like 错误） */
    send(client, header, header_len, 0);
    send(client, body, (int)strlen(body), 0);
}

static void send_json(SOCKET client, int code, const char *json)
{
    send_response(client, code, "application/json; charset=utf-8", json);
}

/* ── 请求分发 ─────────────────────────────────────────────────── */

static void dispatch(SOCKET client, const HttpRequest *req)
{
    /* 请求日志 */
    printf("[web_server] %s %s%s%s\n", req->method, req->path,
           req->query[0] ? "?" : "", req->query[0] ? req->query : "");

    /* CORS 预检 */
    if (strcmp(req->method, "OPTIONS") == 0) {
        send_response(client, 204, "text/plain", "");
        return;
    }

    /* 路由表 */
    if (strcmp(req->path, "/") == 0 && strcmp(req->method, "GET") == 0) {
        route_get_index(client);
    }
    else if (strcmp(req->path, "/status") == 0 && strcmp(req->method, "GET") == 0) {
        route_get_status(client);
    }
    else if (strcmp(req->path, "/registers") == 0 && strcmp(req->method, "GET") == 0) {
        route_get_registers(client);
    }
    else if (strcmp(req->path, "/memory") == 0 && strcmp(req->method, "GET") == 0) {
        route_get_memory(client, req);
    }
    else if (strcmp(req->path, "/backtrace") == 0 && strcmp(req->method, "GET") == 0) {
        route_get_backtrace(client);
    }
    else if (strcmp(req->path, "/breakpoints") == 0 && strcmp(req->method, "GET") == 0) {
        route_get_breakpoints(client);
    }
    else if (strcmp(req->path, "/breakpoint") == 0 && strcmp(req->method, "POST") == 0) {
        route_post_breakpoint(client, req);
    }
    else if (strcmp(req->path, "/breakpoint") == 0 && strcmp(req->method, "DELETE") == 0) {
        route_delete_breakpoint(client, req);
    }
    else if (strcmp(req->path, "/datapath") == 0 && strcmp(req->method, "GET") == 0) {
        route_get_datapath(client);
    }
    else if (strcmp(req->path, "/disassembly") == 0 && strcmp(req->method, "GET") == 0) {
        route_get_disassembly(client, req);
    }
    else if (strcmp(req->path, "/datapath.svg") == 0 && strcmp(req->method, "GET") == 0) {
        route_get_datapath_svg(client);
    }
    /* 保留旧路径兼容 */
    else if (strcmp(req->path, "/pipeline.svg") == 0 && strcmp(req->method, "GET") == 0) {
        route_get_datapath_svg(client);
    }
    else if (strcmp(req->path, "/step") == 0 && strcmp(req->method, "POST") == 0) {
        route_post_step(client);
    }
    else if (strcmp(req->path, "/continue") == 0 && strcmp(req->method, "POST") == 0) {
        route_post_continue(client);
    }
    else if (strcmp(req->path, "/stop") == 0 && strcmp(req->method, "POST") == 0) {
        route_post_stop(client);
    }
    else if (strcmp(req->path, "/programs") == 0 && strcmp(req->method, "GET") == 0) {
        route_get_programs(client);
    }
    else if (strcmp(req->path, "/program") == 0 && strcmp(req->method, "POST") == 0) {
        route_post_program(client, req);
    }
    else if (strcmp(req->path, "/pipeline-state") == 0 && strcmp(req->method, "GET") == 0) {
        route_get_pipeline_state(client);
    }
    else if (strcmp(req->path, "/cpu-model") == 0 && strcmp(req->method, "GET") == 0) {
        route_get_cpu_model(client);
    }
    else if (strcmp(req->path, "/cpu-model") == 0 && strcmp(req->method, "POST") == 0) {
        route_post_cpu_model(client, req);
    }
    else {
        send_json(client, 404, "{\"status\":\"error\",\"message\":\"Not found\"}");
    }
}

/* ── 请求处理（读取 + 解析 + 分发）───────────────────────────── */

static void handle_request(SOCKET client)
{
    char buf[HTTP_BUF_SIZE];
    int total = 0;

    /* 设置接收超时 */
    int timeout = RECV_TIMEOUT_MS;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
               (const char *)&timeout, sizeof(timeout));

    /* 读取请求 */
    int n = recv(client, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        closesocket(client);
        return;
    }
    total = n;

    /* 偷看 Content-Length 以确定是否还有 body 没读完 */
    buf[total] = '\0';
    const char *cl_hdr = strstr(buf, "Content-Length:");
    if (cl_hdr) {
        cl_hdr += 15;
        while (*cl_hdr == ' ') cl_hdr++;
        int expected = atoi(cl_hdr);
        const char *body_start = strstr(buf, "\r\n\r\n");
        if (body_start) {
            int body_have = total - (int)(body_start + 4 - buf);
            while (body_have < expected && total < (int)sizeof(buf) - 1) {
                n = recv(client, buf + total, sizeof(buf) - 1 - total, 0);
                if (n <= 0) break;
                total += n;
                body_have += n;
            }
        }
    }
    buf[total] = '\0';

    HttpRequest req;
    if (!parse_http(buf, total, &req)) {
        send_response(client, 400, "text/plain", "Bad Request");
        closesocket(client);
        return;
    }

    dispatch(client, &req);
    closesocket(client);
}

/* ================================================================
 * 公共服务端入口
 * ================================================================ */

int web_server_start(struct Simulator *sim, int port, const char *elf_path)
{
    g_sim         = sim;
    g_server_port = port;
    g_server_running = true;
    if (elf_path) {
        strncpy(g_elf_path, elf_path, sizeof(g_elf_path) - 1);
        g_elf_path[sizeof(g_elf_path) - 1] = '\0';
    }

    /* ① 初始化 WinSock */
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "[web_server] WSAStartup failed\n");
        return EXIT_FAILURE;
    }

    /* ② 创建 socket */
    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == INVALID_SOCKET) {
        fprintf(stderr, "[web_server] socket() failed: %d\n", WSAGetLastError());
        WSACleanup();
        return EXIT_FAILURE;
    }

    /* ③ 设置 SO_REUSEADDR（快速重启） */
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&opt, sizeof(opt));

    /* ④ bind */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((unsigned short)port);

    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr, "[web_server] bind() failed: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return EXIT_FAILURE;
    }

    /* ⑤ listen */
    if (listen(listen_sock, 5) == SOCKET_ERROR) {
        fprintf(stderr, "[web_server] listen() failed: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return EXIT_FAILURE;
    }

    printf("[web_server] Listening on http://localhost:%d\n", port);
    printf("[web_server] Open this URL in your browser to use the Web Debugger\n");

    /* ⑥ accept 循环 */
    while (g_server_running) {
        struct sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);
        SOCKET client = accept(listen_sock,
                               (struct sockaddr *)&client_addr, &addr_len);
        if (client == INVALID_SOCKET) {
            if (!g_server_running) break;
            /* accept 失败但非关闭信号，继续 */
            continue;
        }

        handle_request(client);
        /* client 在 handle_request 中已 closesocket */
    }

    /* ⑦ 清理 */
    closesocket(listen_sock);
    WSACleanup();

    printf("[web_server] Server stopped.\n");
    return EXIT_SUCCESS;
}
