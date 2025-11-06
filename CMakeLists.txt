cmake_minimum_required(VERSION 3.16)
project(HanaCore LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Allow user to override compiler (e.g. use x86_64-elf- toolchain)
if(DEFINED ENV{CROSS_COMPILE} AND NOT CMAKE_C_COMPILER)
  set(CROSS_PREFIX $ENV{CROSS_COMPILE})
  if(NOT CROSS_PREFIX STREQUAL "")
    set(CMAKE_C_COMPILER "${CROSS_PREFIX}gcc" CACHE FILEPATH "C compiler")
    set(CMAKE_CXX_COMPILER "${CROSS_PREFIX}g++" CACHE FILEPATH "C++ compiler")
    set(CMAKE_ASM_COMPILER "nasm" CACHE FILEPATH "ASM compiler")
  endif()
endif()

option(BUILD_USERLAND "Build simple userland tools and populate rootfs_src" ON)

# Output folders
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory(kernel)
if (BUILD_USERLAND)
  add_subdirectory(userland)
endif()

# Install step: populate build/iso_root similar to previous justfile layout
set(ISO_ROOT_DIR ${CMAKE_BINARY_DIR}/iso_root)
add_custom_target(install_iso ALL
  COMMENT "Populate ${ISO_ROOT_DIR} with boot and rootfs files (kernel and modules copied by their targets)"
)

# Copy Limine files (if present) during install_iso
file(GLOB LIMINE_FILES
  "${CMAKE_SOURCE_DIR}/limine-bootloader/limine-bios.sys"
  "${CMAKE_SOURCE_DIR}/limine-bootloader/limine-bios-cd.bin"
  "${CMAKE_SOURCE_DIR}/cfg/limine.conf"
)
foreach(_f IN LISTS LIMINE_FILES)
  add_custom_command(TARGET install_iso POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ISO_ROOT_DIR}/boot
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_f} ${ISO_ROOT_DIR}/boot/
  )
endforeach()

message(STATUS "Configured HanaCore with CMake. Build directory: ${CMAKE_BINARY_DIR}")
