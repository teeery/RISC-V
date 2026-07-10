# ================================================================
# RISC-V Simulator — Makefile
#
# 用法:
#   make         编译模拟器
#   make clean   清理
#   make run     编译并运行 (需要指定 ELF: make run ELF=test.elf)
#   make debug   编译并以调试模式运行
# ================================================================

CC       = gcc
CFLAGS   = -std=c11 -Wall -Wextra -g -O0
INCLUDES = -Isrc/include
LDFLAGS  = -lm -lws2_32

TARGET   = riscv-sim

# ── 源文件 ──────────────────────────────────────────────────────
SRCS = \
	src/src/main.c \
	src/src/simulator.c \
	src/src/cpu/cpu.c \
	src/src/cpu/decode.c \
	src/src/cpu/datapath/alu.c \
	src/src/cpu/controller/single_cycle.c \
	src/src/cpu/controller/multi_cycle.c \
	src/src/cpu/controller/pipeline.c \
	src/src/cpu/execute/execute.c \
	src/src/cpu/execute/exec_rv32i.c \
	src/src/cpu/execute/exec_m.c \
	src/src/cpu/execute/exec_f.c \
	src/src/memory/memory.c \
	src/src/memory/mmu.c \
	src/src/debugger/debugger.c \
	src/src/debugger/breakpoint.c \
	src/src/debugger/web_server.c \
	src/src/loader/elf_load.c \
	src/src/loader/elf_segment.c \
	src/src/loader/elf_stack.c \
	src/src/loader/elf_validate.c \
	src/src/loader/elf_section.c \
	src/src/linux/syscall.c

# ── 自动生成 .o 文件列表 ────────────────────────────────────────
OBJS = $(SRCS:.c=.o)

# ── 默认目标 ────────────────────────────────────────────────────
.PHONY: all test-debugger
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

# ── 编译规则 ────────────────────────────────────────────────────
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# ── 清理 ────────────────────────────────────────────────────────
.PHONY: clean
clean:
	rm -f $(OBJS) $(TARGET) build/debugger_test.exe
	@echo "Clean complete."

# ── 运行 ────────────────────────────────────────────────────────
.PHONY: run
run: $(TARGET)
	./$(TARGET) $(ELF)

.PHONY: debug
debug: $(TARGET)
	./$(TARGET) -s $(ELF)

# ── Debugger 单元测试 ──────────────────────────────────────────────
DEBUGGER_TEST_SRCS = \
	src/test/debugger/debugger_test.c \
	src/src/simulator.c \
	src/src/cpu/cpu.c \
	src/src/cpu/decode.c \
	src/src/cpu/datapath/alu.c \
	src/src/cpu/controller/single_cycle.c \
	src/src/cpu/controller/multi_cycle.c \
	src/src/cpu/controller/pipeline.c \
	src/src/cpu/execute/execute.c \
	src/src/cpu/execute/exec_rv32i.c \
	src/src/cpu/execute/exec_m.c \
	src/src/cpu/execute/exec_f.c \
	src/src/memory/memory.c \
	src/src/memory/mmu.c \
	src/src/debugger/debugger.c \
	src/src/debugger/breakpoint.c \
	src/src/loader/elf_validate.c \
	src/src/loader/elf_load.c \
	src/src/loader/elf_segment.c \
	src/src/loader/elf_stack.c \
	src/src/loader/elf_section.c

build/debugger_test.exe: $(DEBUGGER_TEST_SRCS)
	$(CC) $(CFLAGS) -DDEBUGGER_TEST -Isrc/include -Isrc/src/debugger \
	    -o $@ $^ $(LDFLAGS)

.PHONY: test-debugger
test-debugger: build/debugger_test.exe
	./build/debugger_test.exe

# ══════════════════════════════════════════════════════════════════
# 统一测试目标
#
# 用法:
#   make test             运行全部测试
#   make test-decode       仅译码测试
#   make test-execute      仅执行测试
#   make test-m            仅 M 扩展测试
#   make test-f            仅 F 扩展测试
#   make test-multi        仅多周期测试
#   make test-pipeline     仅流水线测试
#   make test-cross        仅交叉模型一致性测试
#   make test-hazard       仅流水线冒险测试
#   make test-e2e          仅端到端测试
#   make test-validate     仅 ELF 校验测试
#   make test-load         仅 ELF 加载测试
# ══════════════════════════════════════════════════════════════════

# ── 公共依赖（所有测试共享的 .o 文件）──────────────────────────
TEST_COMMON_SRCS = \
	src/src/simulator.c \
	src/src/cpu/cpu.c \
	src/src/cpu/decode.c \
	src/src/cpu/datapath/alu.c \
	src/src/cpu/controller/single_cycle.c \
	src/src/cpu/controller/multi_cycle.c \
	src/src/cpu/controller/pipeline.c \
	src/src/cpu/execute/execute.c \
	src/src/cpu/execute/exec_rv32i.c \
	src/src/cpu/execute/exec_m.c \
	src/src/cpu/execute/exec_f.c \
	src/src/memory/memory.c \
	src/src/memory/mmu.c \
	src/src/debugger/debugger.c \
	src/src/debugger/breakpoint.c \
	src/src/loader/elf_validate.c \
	src/src/loader/elf_load.c \
	src/src/loader/elf_segment.c \
	src/src/loader/elf_stack.c \
	src/src/loader/elf_section.c \
	src/src/linux/syscall.c

# ── 各测试编译规则 ──────────────────────────────────────────────
build/decode_test.exe: src/test/cpu/decode_test.c src/src/cpu/decode.c
	$(CC) $(CFLAGS) -Isrc/include -Isrc/include/cpu -o $@ $^

build/execute_test.exe: src/test/cpu/execute_test.c $(TEST_COMMON_SRCS)
	$(CC) $(CFLAGS) -Isrc/include -Isrc/include/cpu -Isrc/include/memory -o $@ $^ $(LDFLAGS)

build/m_test.exe: src/test/cpu/m_test.c $(TEST_COMMON_SRCS)
	$(CC) $(CFLAGS) -Isrc/include -o $@ $^ $(LDFLAGS)

build/f_test.exe: src/test/cpu/f_test.c $(TEST_COMMON_SRCS)
	$(CC) $(CFLAGS) -Isrc/include -o $@ $^ $(LDFLAGS)

build/multi_cycle_test.exe: src/test/cpu/multi_cycle_test.c $(TEST_COMMON_SRCS)
	$(CC) $(CFLAGS) -Isrc/include -o $@ $^ $(LDFLAGS)

build/pipeline_test.exe: src/test/cpu/pipeline_test.c $(TEST_COMMON_SRCS)
	$(CC) $(CFLAGS) -Isrc/include -o $@ $^ $(LDFLAGS)

build/cross_model_test.exe: src/test/cpu/cross_model_test.c $(TEST_COMMON_SRCS)
	$(CC) $(CFLAGS) -Isrc/include -o $@ $^ $(LDFLAGS)

build/pipeline_hazard_test.exe: src/test/cpu/pipeline_hazard_test.c $(TEST_COMMON_SRCS)
	$(CC) $(CFLAGS) -Isrc/include -o $@ $^ $(LDFLAGS)

build/e2e_test.exe: src/test/e2e/e2e_test.c $(TEST_COMMON_SRCS)
	$(CC) $(CFLAGS) -Isrc/include -Isrc/src/loader -o $@ $^ $(LDFLAGS)

build/test_validate.exe: src/test/loader/test_validate.c src/src/loader/elf_validate.c
	$(CC) $(CFLAGS) -Isrc/include -o $@ $^

build/test_load.exe: src/test/loader/test_load.c \
		src/src/loader/elf_validate.c src/src/loader/elf_load.c \
		src/src/loader/elf_segment.c src/src/loader/elf_stack.c \
		src/src/memory/memory.c src/src/memory/mmu.c
	$(CC) $(CFLAGS) -Isrc/include -Isrc/src/loader -o $@ $^

build/debugger_test.exe: src/test/debugger/debugger_test.c $(TEST_COMMON_SRCS)
	$(CC) $(CFLAGS) -DDEBUGGER_TEST -Isrc/include -Isrc/src/debugger -o $@ $^ $(LDFLAGS)

# ── 各测试运行规则 ──────────────────────────────────────────────
.PHONY: test-decode
test-decode: build/decode_test.exe
	@echo "=== decode_test ==="
	./build/decode_test.exe

.PHONY: test-execute
test-execute: build/execute_test.exe
	@echo "=== execute_test ==="
	./build/execute_test.exe

.PHONY: test-m
test-m: build/m_test.exe
	@echo "=== m_test ==="
	./build/m_test.exe

.PHONY: test-f
test-f: build/f_test.exe
	@echo "=== f_test ==="
	./build/f_test.exe

.PHONY: test-multi
test-multi: build/multi_cycle_test.exe
	@echo "=== multi_cycle_test ==="
	./build/multi_cycle_test.exe

.PHONY: test-pipeline
test-pipeline: build/pipeline_test.exe
	@echo "=== pipeline_test ==="
	./build/pipeline_test.exe

.PHONY: test-cross
test-cross: build/cross_model_test.exe
	@echo "=== cross_model_test ==="
	./build/cross_model_test.exe

.PHONY: test-hazard
test-hazard: build/pipeline_hazard_test.exe
	@echo "=== pipeline_hazard_test ==="
	./build/pipeline_hazard_test.exe

.PHONY: test-e2e
test-e2e: build/e2e_test.exe
	@echo "=== e2e_test ==="
	./build/e2e_test.exe

.PHONY: test-validate
test-validate: build/test_validate.exe
	@echo "=== test_validate ==="
	./build/test_validate.exe

.PHONY: test-load
test-load: build/test_load.exe
	@echo "=== test_load ==="
	./build/test_load.exe

# ── 统一测试入口 ────────────────────────────────────────────────
.PHONY: test
test: test-decode test-execute test-m test-f \
      test-multi test-pipeline test-cross test-hazard \
      test-e2e test-validate test-load test-debugger
	@echo ""
	@echo "════════════════════════════════════════════════════"
	@echo "  All test suites completed."
	@echo "  Check output above for FAIL / ❌ lines."
	@echo "════════════════════════════════════════════════════"

# ── 依赖关系 ────────────────────────────────────────────────────
src/src/main.o:            src/src/main.c            src/include/simulator.h src/include/debugger/debugger.h src/include/debugger/web_server.h
src/src/simulator.o:       src/src/simulator.c       src/include/simulator.h src/include/cpu/execute.h src/include/cpu/controller/controller_internal.h src/include/loader/elf_loader.h
src/src/cpu/cpu.o:         src/src/cpu/cpu.c         src/include/cpu/cpu.h src/include/types.h
src/src/cpu/decode.o:      src/src/cpu/decode.c      src/include/cpu/decode.h
src/src/cpu/datapath/alu.o:              src/src/cpu/datapath/alu.c              src/include/cpu/datapath/alu.h
src/src/cpu/controller/single_cycle.o:   src/src/cpu/controller/single_cycle.c   src/include/cpu/controller/controller_internal.h src/include/simulator.h src/include/cpu/execute.h src/include/cpu/datapath/alu.h
src/src/cpu/controller/multi_cycle.o:    src/src/cpu/controller/multi_cycle.c    src/include/cpu/controller/controller_internal.h src/include/simulator.h src/include/cpu/execute.h src/include/cpu/datapath/alu.h
src/src/cpu/controller/pipeline.o:       src/src/cpu/controller/pipeline.c       src/include/cpu/controller/controller_internal.h src/include/simulator.h src/include/cpu/execute.h src/include/cpu/datapath/alu.h src/include/cpu/exec_internal.h src/include/memory/mmu.h
src/src/cpu/execute/execute.o:     src/src/cpu/execute/execute.c     src/include/cpu/execute.h src/include/simulator.h src/include/cpu/exec_internal.h
src/src/cpu/execute/exec_rv32i.o:   src/src/cpu/execute/exec_rv32i.c   src/include/cpu/execute.h src/include/cpu/exec_internal.h src/include/simulator.h src/include/cpu/datapath/alu.h
src/src/cpu/execute/exec_m.o:       src/src/cpu/execute/exec_m.c       src/include/cpu/execute.h src/include/cpu/exec_internal.h src/include/simulator.h
src/src/cpu/execute/exec_f.o:       src/src/cpu/execute/exec_f.c       src/include/cpu/execute.h src/include/cpu/exec_internal.h src/include/simulator.h
src/src/memory/memory.o:   src/src/memory/memory.c   src/include/memory/memory.h src/include/types.h
src/src/memory/mmu.o:      src/src/memory/mmu.c      src/include/memory/mmu.h src/include/memory/memory.h
src/src/debugger/debugger.o:   src/src/debugger/debugger.c   src/include/debugger/debugger.h src/include/simulator.h
src/src/debugger/breakpoint.o: src/src/debugger/breakpoint.c src/include/debugger/debugger.h src/include/simulator.h
src/src/debugger/web_server.o: src/src/debugger/web_server.c src/include/debugger/web_server.h src/include/debugger/debugger.h src/include/simulator.h
src/src/loader/elf_load.o:     src/src/loader/elf_load.c     src/include/loader/elf_loader.h src/include/memory/memory.h src/include/memory/mmu.h
src/src/loader/elf_segment.o:  src/src/loader/elf_segment.c  src/include/loader/elf_loader.h src/include/memory/memory.h
src/src/loader/elf_stack.o:    src/src/loader/elf_stack.c    src/include/loader/elf_loader.h src/include/memory/memory.h
src/src/loader/elf_validate.o: src/src/loader/elf_validate.c src/include/loader/elf_loader.h
src/src/linux/syscall.o:    src/src/linux/syscall.c    src/include/linux/syscall.h src/include/simulator.h
