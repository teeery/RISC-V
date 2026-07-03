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
LDFLAGS  =

TARGET   = riscv-sim

# ── 源文件 ──────────────────────────────────────────────────────
SRCS = \
	src/src/main.c \
	src/src/simulator.c \
	src/src/cpu/cpu.c \
	src/src/cpu/decode.c \
	src/src/cpu/execute.c \
	src/src/memory/memory.c \
	src/src/memory/mmu.c \
	src/src/debugger/debugger.c \
	src/src/debugger/breakpoint.c \
	src/src/loader/elf_load.c \
	src/src/loader/elf_segment.c \
	src/src/loader/elf_stack.c \
	src/src/loader/elf_validate.c

# ── 自动生成 .o 文件列表 ────────────────────────────────────────
OBJS = $(SRCS:.c=.o)

# ── 默认目标 ────────────────────────────────────────────────────
.PHONY: all
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
	rm -f $(OBJS) $(TARGET)
	@echo "Clean complete."

# ── 运行 ────────────────────────────────────────────────────────
.PHONY: run
run: $(TARGET)
	./$(TARGET) $(ELF)

.PHONY: debug
debug: $(TARGET)
	./$(TARGET) -s $(ELF)

# ── 依赖关系 ────────────────────────────────────────────────────
src/src/main.o:            src/src/main.c            src/include/simulator.h src/include/debugger/debugger.h
src/src/simulator.o:       src/src/simulator.c       src/include/simulator.h src/include/cpu/execute.h src/include/loader/elf_loader.h
src/src/cpu/cpu.o:         src/src/cpu/cpu.c         src/include/cpu/cpu.h src/include/types.h
src/src/cpu/decode.o:      src/src/cpu/decode.c      src/include/cpu/decode.h
src/src/cpu/execute.o:     src/src/cpu/execute.c     src/include/cpu/execute.h src/include/simulator.h
src/src/memory/memory.o:   src/src/memory/memory.c   src/include/memory/memory.h src/include/types.h
src/src/memory/mmu.o:      src/src/memory/mmu.c      src/include/memory/mmu.h src/include/memory/memory.h
src/src/debugger/debugger.o:   src/src/debugger/debugger.c   src/include/debugger/debugger.h src/include/simulator.h
src/src/debugger/breakpoint.o: src/src/debugger/breakpoint.c src/include/debugger/debugger.h src/include/simulator.h
src/src/loader/elf_load.o:     src/src/loader/elf_load.c     src/include/loader/elf_loader.h src/include/memory/memory.h src/include/memory/mmu.h
src/src/loader/elf_segment.o:  src/src/loader/elf_segment.c  src/include/loader/elf_loader.h src/include/memory/memory.h
src/src/loader/elf_stack.o:    src/src/loader/elf_stack.c    src/include/loader/elf_loader.h src/include/memory/memory.h
src/src/loader/elf_validate.o: src/src/loader/elf_validate.c src/include/loader/elf_loader.h
