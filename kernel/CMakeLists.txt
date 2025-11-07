# Kernel build
cmake_minimum_required(VERSION 3.16)
project(kernel LANGUAGES C CXX ASM)

# Collect sources
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

# Additional non-kernel sources the justfile compiled into the kernel
set(EXTRA_C_SRCS
  ${CMAKE_SOURCE_DIR}/boot/limine_entry.c
  ${CMAKE_SOURCE_DIR}/flanterm/src/flanterm.c
  ${CMAKE_SOURCE_DIR}/flanterm/src/flanterm_backends/fb.c
  ${CMAKE_SOURCE_DIR}/kernel/libs/nanoprintf.c
  ${CMAKE_SOURCE_DIR}/kernel/libs/libc.c
  ${CMAKE_SOURCE_DIR}/kernel/shell/shell.cpp
  ${CMAKE_SOURCE_DIR}/kernel/scheduler/scheduler.cpp
)

set(KERNEL_SRCS ${KERNEL_CPP_SRCS} ${KERNEL_C_SRCS} ${KERNEL_ASM_SRCS} ${EXTRA_C_SRCS})

# Exclude certain assembly stubs that have C implementations to avoid duplicate symbols
list(REMOVE_ITEM KERNEL_SRCS "${CMAKE_SOURCE_DIR}/kernel/arch/gdt_reload.S")
list(REMOVE_ITEM KERNEL_SRCS "${CMAKE_SOURCE_DIR}/kernel/arch/isr_common.S")

add_executable(kernel ${KERNEL_SRCS})

# Place output at top-level build directory and name it kernel.elf
set_target_properties(kernel PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
  OUTPUT_NAME "kernel.elf"
  LINKER_LANGUAGE CXX
  POSITION_INDEPENDENT_CODE OFF
)

# Common compile options
target_compile_options(kernel PRIVATE
  -ffreestanding
  -mcmodel=kernel
  -mno-red-zone
  -fno-stack-protector
  -fno-pie
  -fno-pic
)

# C++ specific options
target_compile_options(kernel PRIVATE $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fno-exceptions> $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fno-rtti>)

# Link options: pass linker script and ensure ELF format
target_link_options(kernel PRIVATE
  -nostdlib
  -Wl,-m,elf_x86_64
  -Wl,-T,${CMAKE_SOURCE_DIR}/linker.ld
  -no-pie
)

add_custom_command(TARGET kernel POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/iso_root/boot
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:kernel> ${CMAKE_BINARY_DIR}/iso_root/boot/kernel
  COMMENT "Copy kernel.elf into iso_root/boot"
)
