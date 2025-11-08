cmake_minimum_required(VERSION 3.16)
project(HanaCoreKernel LANGUAGES C CXX ASM)

# Optional: enable Undefined Behavior Sanitizer (UBSAN) instrumentation.
# UBSAN inserts runtime checks for undefined behavior; this is useful for
# debugging but may increase binary size and change runtime behaviour. It
# is off by default. Enable with -DENABLE_UBSAN=ON when configuring.
option(ENABLE_UBSAN "Enable Undefined Behavior Sanitizer (UBSAN)" OFF)

# ----------------------------------------
# C++20 freestanding kernel with coroutines
# ----------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ----------------------------------------
# Collect sources
# ----------------------------------------
file(GLOB_RECURSE KERNEL_CPP_SRCS
        "${CMAKE_SOURCE_DIR}/kernel/*.cpp"
        "${CMAKE_SOURCE_DIR}/kernel/**/*.cpp"
)
file(GLOB_RECURSE KERNEL_C_SRCS
        "${CMAKE_SOURCE_DIR}/kernel/*.c"
        "${CMAKE_SOURCE_DIR}/kernel/**/*.c"
)
file(GLOB_RECURSE KERNEL_ASM_SRCS
        "${CMAKE_SOURCE_DIR}/kernel/*.S"
        "${CMAKE_SOURCE_DIR}/kernel/**/*.S"
)

set(EXTRA_C_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/flanterm/src/flanterm.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/flanterm/src/flanterm_backends/fb.c
        ${CMAKE_CURRENT_SOURCE_DIR}/boot/limine_entry.c
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/libc.c
        ${CMAKE_CURRENT_SOURCE_DIR}/scheduler/scheduler.cpp
)


set(KERNEL_SRCS ${KERNEL_CPP_SRCS} ${KERNEL_C_SRCS} ${KERNEL_ASM_SRCS} ${EXTRA_C_SRCS})

# Exclude any generated build artifacts that may live inside the source tree
# (for example an in-source `kernel/build` from a previous run). These
# can contain CMake-generated CompilerId sources which must not be
# compiled into the kernel executable.
list(FILTER KERNEL_SRCS EXCLUDE REGEX ".*/kernel/build/.*")
list(FILTER KERNEL_SRCS EXCLUDE REGEX ".*/build/cmake/.*")

# Exclude redundant assembly files
list(REMOVE_ITEM KERNEL_SRCS
        "${CMAKE_SOURCE_DIR}/kernel/arch/gdt_reload.S"
        "${CMAKE_SOURCE_DIR}/kernel/arch/isr_common.S"
)

# ----------------------------------------
# Define executable
# ----------------------------------------
add_executable(kernel ${KERNEL_SRCS})

set_target_properties(kernel PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
        OUTPUT_NAME "kernel.elf"
        LINKER_LANGUAGE CXX
        POSITION_INDEPENDENT_CODE OFF
)

# ----------------------------------------
# Common compile options
# ----------------------------------------
target_compile_options(kernel PRIVATE
        -ffreestanding
        -mcmodel=kernel
        -mno-red-zone
        -fno-stack-protector
        -fno-pie
        -fno-pic
        -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
)

if(ENABLE_UBSAN AND ENABLE_UBSAN_KERNEL)
        message(STATUS "ENABLING UBSAN instrumentation for kernel target (experimental)")
        # Instrument for undefined behavior and make sanitizer abort on error
        target_compile_options(kernel PRIVATE -fsanitize=undefined -fno-sanitize-recover=undefined -g)
        # Link with UBSAN runtime. Note: linking sanitizer runtime into a
        # freestanding kernel may not be fully supported and frequently fails
        # due to missing runtime support; enable only if you know what you're doing.
        target_link_options(kernel PRIVATE -fsanitize=undefined)
else()
endif()

# ----------------------------------------
# C++ specific options (for freestanding + coroutines)
# ----------------------------------------
target_compile_options(kernel PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:
        -fno-exceptions
        -fno-rtti
        -fno-threadsafe-statics
        -fno-use-cxa-atexit
        -fcoroutines
        >
)

# ----------------------------------------
# Linker options
# ----------------------------------------
target_link_options(kernel PRIVATE
        -nostdlib
        -Wl,-m,elf_x86_64
        -Wl,-T,${CMAKE_SOURCE_DIR}/linker.ld
        -no-pie
)

# ----------------------------------------
# Post-build: copy kernel into ISO tree
# ----------------------------------------
add_custom_command(TARGET kernel POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/iso_root/boot
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:kernel> ${CMAKE_BINARY_DIR}/iso_root/boot/kernel
        COMMENT "📦 Copied kernel.elf into iso_root/boot"
)

# If a musl syscall shim is present, build it so userland build rules can link
# against libmusl_shim.a. This is optional and won't break the kernel build
# if the directory is not present.
if(EXISTS "${CMAKE_SOURCE_DIR}/kernel/userland/musl_shim/CMakeLists.txt")
        add_subdirectory(${CMAKE_SOURCE_DIR}/kernel/userland/musl_shim)
endif()