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
