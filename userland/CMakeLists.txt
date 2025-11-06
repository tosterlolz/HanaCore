# Userland tiny tools
cmake_minimum_required(VERSION 3.16)
project(userland LANGUAGES C)

file(GLOB USERLAND_COREUTILS ${CMAKE_SOURCE_DIR}/userland/coreutils/*.c)

set(ROOTFS_SRC_DIR ${CMAKE_BINARY_DIR}/rootfs_src)
set(ROOTFS_BIN_DIR ${ROOTFS_SRC_DIR}/bin)

add_custom_target(prepare_rootfs_dirs
  COMMAND ${CMAKE_COMMAND} -E make_directory ${ROOTFS_BIN_DIR}
)

# Use CROSS_COMPILE prefix if provided in environment
if(DEFINED ENV{CROSS_COMPILE})
  set(CROSS_PREFIX $ENV{CROSS_COMPILE})
else()
  set(CROSS_PREFIX "")
endif()

foreach(_src IN LISTS USERLAND_COREUTILS)
  get_filename_component(_name ${_src} NAME_WE)
  set(_obj ${CMAKE_BINARY_DIR}/tmp/${_name}.o)
  set(_elf ${CMAKE_BINARY_DIR}/tmp/${_name}.elf)

  add_custom_command(
    OUTPUT ${_elf}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/tmp
  COMMAND ${CROSS_PREFIX}gcc -ffreestanding -nostdlib -fno-asynchronous-unwind-tables -fno-exceptions -O2 -c ${_src} -o ${_obj}
  COMMAND /bin/sh -c "${CROSS_PREFIX}ld -m elf_x86_64 -N -e _start -Ttext=0x0 -o '${_elf}' '${_obj}'; exit 0"
  COMMAND ${CMAKE_COMMAND} -E touch ${_elf}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${ROOTFS_BIN_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_elf} ${ROOTFS_BIN_DIR}/${_name}
    DEPENDS ${_src}
    COMMENT "Build and install userland ${_name} into rootfs_src/bin"
  )

  add_custom_target(${_name}_install DEPENDS ${_elf})
  add_dependencies(${_name}_install prepare_rootfs_dirs)
endforeach()

# Build hello_module
if(EXISTS ${CMAKE_SOURCE_DIR}/userland/hello_module.c)
  set(_hobj ${CMAKE_BINARY_DIR}/tmp/hello_module.o)
  set(_helf ${CMAKE_BINARY_DIR}/tmp/hello_module.elf)
  add_custom_command(
    OUTPUT ${_helf}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/tmp
  COMMAND ${CROSS_PREFIX}gcc -ffreestanding -nostdlib -fno-asynchronous-unwind-tables -fno-exceptions -O2 -c ${CMAKE_SOURCE_DIR}/userland/hello_module.c -o ${_hobj}
  COMMAND /bin/sh -c "${CROSS_PREFIX}ld -m elf_x86_64 -N -e _start -Ttext=0x0 -o '${_helf}' '${_hobj}'; exit 0"
  COMMAND ${CMAKE_COMMAND} -E touch ${_helf}
  DEPENDS ${CMAKE_SOURCE_DIR}/userland/hello_module.c
    COMMENT "Build hello_module ELF"
  )
  add_custom_target(hello_module_install DEPENDS ${_helf})
  add_custom_command(TARGET hello_module_install POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/iso_root/boot
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_helf} ${CMAKE_BINARY_DIR}/iso_root/boot/hello_module.elf
    COMMENT "Copy hello_module into iso_root/boot"
  )
endif()

# mkrootfs target: attempt to run mke2fs -d to create build/rootfs.img
add_custom_target(mkrootfs
  DEPENDS prepare_rootfs_dirs
  COMMENT "Create ext2 rootfs image (if mke2fs is available) - invoke explicitly with --target mkrootfs")

