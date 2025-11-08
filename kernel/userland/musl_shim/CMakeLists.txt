cmake_minimum_required(VERSION 3.16)
project(musl_shim C)

add_library(musl_shim STATIC musl_shim.c)
target_include_directories(musl_shim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Install the shim into kernel build output so it can be linked with userland
add_library(musl_shim_alias ALIAS musl_shim)
