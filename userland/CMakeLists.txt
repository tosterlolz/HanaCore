# Userland tiny tools
cmake_minimum_required(VERSION 3.16)
project(userland LANGUAGES C)

file(GLOB USERLAND_COREUTILS ${CMAKE_SOURCE_DIR}/userland/coreutils/*.c)

set(ROOTFS_SRC_DIR ${CMAKE_BINARY_DIR}/rootfs_src)
set(ROOTFS_BIN_DIR ${ROOTFS_SRC_DIR}/bin)

add_custom_target(prepare_rootfs_dirs
  COMMAND ${CMAKE_COMMAND} -E make_directory ${ROOTFS_BIN_DIR}
)

foreach(_src IN LISTS USERLAND_COREUTILS)
  get_filename_component(_name ${_src} NAME_WE)
  add_executable(${_name}.elf ${_src})
  set_target_properties(${_name}.elf PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tmp
    OUTPUT_NAME ${_name}
  )
  # Compile and link as freestanding (avoid CRT and libc)
  target_compile_options(${_name}.elf PRIVATE -ffreestanding -nostdlib -fno-asynchronous-unwind-tables -fno-exceptions -O2)
  target_link_options(${_name}.elf PRIVATE -nostdlib -Wl,-N -Wl,-e,_start -Wl,-Ttext=0x0)

  # Copy compiled ELF into rootfs_src/bin so tools like mke2fs -d can include them
  add_custom_command(TARGET ${_name}.elf POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ROOTFS_BIN_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${_name}.elf> ${ROOTFS_BIN_DIR}/${_name}
    COMMENT "Install ${_name} into rootfs_src/bin"
  )
  add_dependencies(prepare_rootfs_dirs ${_name}.elf)
endforeach()

# Build hello_module
if(EXISTS ${CMAKE_SOURCE_DIR}/userland/hello_module.c)
  add_executable(hello_module.elf ${CMAKE_SOURCE_DIR}/userland/hello_module.c)
  set_target_properties(hello_module.elf PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tmp OUTPUT_NAME hello_module)
  target_compile_options(hello_module.elf PRIVATE -ffreestanding -nostdlib -fno-asynchronous-unwind-tables -fno-exceptions -O2)
  target_link_options(hello_module.elf PRIVATE -nostdlib -Wl,-N -Wl,-e,_start -Wl,-Ttext=0x0)
  add_custom_command(TARGET hello_module.elf POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/iso_root/boot
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:hello_module.elf> ${CMAKE_BINARY_DIR}/iso_root/boot/hello_module.elf
    COMMENT "Copy hello_module into iso_root/boot"
  )
endif()

# mkrootfs target: attempt to run mke2fs -d to create build/rootfs.img
add_custom_target(mkrootfs ALL
  DEPENDS prepare_rootfs_dirs
  COMMENT "Create ext2 rootfs image (if mke2fs is available)")

add_custom_command(TARGET mkrootfs POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} ${CMAKE_COMMAND} -E echo "-> Creating rootfs image (build/rootfs.img)"
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} /bin/sh -c "if command -v mke2fs >/dev/null 2>&1; then mke2fs -q -F -t ext2 -d ${ROOTFS_SRC_DIR} build/rootfs.img || true; else echo 'mke2fs not found; skipping image creation'; fi"
)
