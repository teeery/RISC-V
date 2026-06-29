# ============================================================================
# RISC-V Simulator & Debugger — CMake 构建文件
# ============================================================================
# 使用方法:
#   mkdir build && cd build
#   cmake .. -G "MinGW Makefiles"    (Windows)
#   cmake ..                          (Linux)
#   cmake --build .
# ============================================================================

cmake_minimum_required(VERSION 3.10)
project(rvsim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ============================================================================
# 源文件列表
# ============================================================================
set(SOURCES
    # 入口
    src/main.c
    src/simulator.c

    # CPU 核心
    src/cpu/cpu.c
    src/cpu/decode.c
    src/cpu/execute.c

    # 内存管理
    src/memory/memory.c

    # ELF 加载器
    src/loader/elf.c

    # 系统调用模拟
    src/syscall/syscall.c

    # 调试器
    src/debugger/debugger.c
    src/debugger/breakpoint.c
)

# ============================================================================
# 选做模块（完成后再取消注释）
# ============================================================================
# set(PIPELINE_SOURCES src/cpu/pipeline.c)
# set(FLOAT_SOURCES    src/cpu/fpu.c)
# set(RSP_SOURCES      src/debugger/rsp.c)

# ============================================================================
# 头文件目录
# ============================================================================
set(HEADERS
    include/common.h
    src/simulator.h
    src/cpu/cpu.h
    src/cpu/decode.h
    src/cpu/execute.h
    src/memory/memory.h
    src/loader/elf.h
    src/syscall/syscall.h
    src/debugger/debugger.h
    src/debugger/breakpoint.h
)

# ============================================================================
# 生成可执行文件
# ============================================================================
add_executable(rvsim ${SOURCES} ${HEADERS})

# 头文件搜索路径
target_include_directories(rvsim PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/cpu
    ${CMAKE_SOURCE_DIR}/src/memory
    ${CMAKE_SOURCE_DIR}/src/loader
    ${CMAKE_SOURCE_DIR}/src/syscall
    ${CMAKE_SOURCE_DIR}/src/debugger
)
