# ============================================================
# Makefile — RISC-V Simulator & Debugger
#
# 目标:
#   make             构建模拟器 (riscv-sim)
#   make debug       构建调试版本 (带 -g -O0)
#   make test        构建并运行测试程序
#   make clean       清理构建产物
#
# 需要安装的工具链:
#   - GCC/MinGW (用于编译模拟器本身)
#   - RISC-V GNU Toolchain (用于编译测试程序)
#     riscv64-unknown-elf-as, riscv64-unknown-elf-ld
#   或 clang (支持 --target=riscv32)
# ============================================================

CC       = gcc
CFLAGS   = -Wall -Wextra -std=c11 -Iinclude
LDFLAGS  =
TARGET   = riscv-sim

# Debug 构建
CFLAGS_DEBUG = -g -O0 -DDEBUG

# Release 构建
CFLAGS_RELEASE = -O2

# 源文件
SRCDIR   = src
SRCS     = $(SRCDIR)/main.c \
           $(SRCDIR)/cpu/cpu.c \
           $(SRCDIR)/cpu/decode.c \
           $(SRCDIR)/cpu/execute.c \
           $(SRCDIR)/cpu/csr.c \
           $(SRCDIR)/memory/memory.c \
           $(SRCDIR)/memory/mmu.c \
           $(SRCDIR)/elf_loader.c \
           $(SRCDIR)/debugger.c \
           $(SRCDIR)/syscall.c

OBJS     = $(SRCS:.c=.o)

# RISC-V 交叉编译器 (用于生成测试 ELF)
RV_AS    = riscv64-unknown-elf-as
RV_LD    = riscv64-unknown-elf-ld
RV_CFLAGS = -march=rv32im

.PHONY: all clean debug test help

# ── 默认目标 ───────────────────────────────────────
all: CFLAGS += $(CFLAGS_RELEASE)
all: $(TARGET)

# ── 调试版本 ───────────────────────────────────────
debug: CFLAGS += $(CFLAGS_DEBUG)
debug: $(TARGET)

# ── 链接 ───────────────────────────────────────────
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

# ── 编译规则 ───────────────────────────────────────
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ── 测试程序 ───────────────────────────────────────
test/hello.elf: test/hello.s
	@echo "Assembling test program..."
	$(RV_AS) $(RV_CFLAGS) -o test/hello.o test/hello.s
	$(RV_LD) -o test/hello.elf test/hello.o
	@echo "Test ELF built: test/hello.elf"

# ── 运行测试 ───────────────────────────────────────
test: $(TARGET) test/hello.elf
	@echo "Running test program..."
	./$(TARGET) test/hello.elf

# ── 运行调试 ───────────────────────────────────────
test-debug: $(TARGET) test/hello.elf
	@echo "Running in debug mode..."
	./$(TARGET) -s test/hello.elf

# ── 清理 ───────────────────────────────────────────
clean:
	rm -f $(OBJS)
	rm -f $(TARGET)
	rm -f test/hello.o test/hello.elf
	@echo "Clean complete"

# ── 帮助 ───────────────────────────────────────────
help:
	@echo "RISC-V Simulator Build System"
	@echo ""
	@echo "Targets:"
	@echo "  make              Build simulator (release mode)"
	@echo "  make debug        Build simulator (debug mode)"
	@echo "  make test         Build and run test program"
	@echo "  make test-debug   Run test in debug mode"
	@echo "  make clean        Remove build artifacts"
	@echo ""
	@echo "Prerequisites:"
	@echo "  GCC (for building the simulator)"
	@echo "  RISC-V GNU Toolchain (for building test ELF files)"
	@echo "    Arch: pacman -S riscv64-unknown-elf-gcc"
	@echo "    Ubuntu: apt install gcc-riscv64-unknown-elf"
