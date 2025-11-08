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

# Install step: populate build/iso_root similar to previous justfile layout
set(ISO_ROOT_DIR ${CMAKE_BINARY_DIR}/iso_root)
add_custom_target(install_iso ALL
  COMMENT "Populate ${ISO_ROOT_DIR} with boot and rootfs files (kernel and modules copied by their targets)"
)

# Previously the project generated a rootfs.img and copied it into the ISO
# as a Limine module. That behaviour has been removed: the ISO will no longer
# include a bundled rootfs image and the mkrootfs target is not created.

file(GLOB LIMINE_FILES
  "${CMAKE_SOURCE_DIR}/third_party/limine/limine-bios.sys"
  "${CMAKE_SOURCE_DIR}/third_party/limine/limine-bios-cd.bin"
  "${CMAKE_SOURCE_DIR}/cfg/limine.conf"
)
foreach(_f IN LISTS LIMINE_FILES)
  add_custom_command(TARGET install_iso POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ISO_ROOT_DIR}/boot
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_f} ${ISO_ROOT_DIR}/boot/
  )
endforeach()

# Optionally build a FAT32 rootfs image (rootfs.img) from `rootfs_src/` and
# include it as a Limine module in the ISO. This recreates the previous
# behaviour where the ISO bundled a rootfs image that the kernel can mount.
if(BUILD_USERLAND)
  # Run the mkrootfs helper script located in tools/ to produce ${CMAKE_BINARY_DIR}/rootfs.img
  add_custom_command(TARGET install_iso POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Generating rootfs.img from ${CMAKE_SOURCE_DIR}/rootfs_src"
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_SOURCE_DIR}/tools ${CMAKE_SOURCE_DIR}/tools/mkrootfs.sh ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ISO_ROOT_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_BINARY_DIR}/rootfs.img ${ISO_ROOT_DIR}/rootfs.img
    COMMENT "Create rootfs.img and copy into ISO root"
  )
  # Informational note at configure time
  message(STATUS "BUILD_USERLAND=ON: rootfs.img will be created from rootfs_src/ and included in the ISO")
else()
  message(STATUS "BUILD_USERLAND=OFF: skipping rootfs.img creation")
endif()

message(STATUS "Configured HanaCore with CMake. Build directory: ${CMAKE_BINARY_DIR}")
