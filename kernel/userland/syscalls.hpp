// Minimal syscall definitions and dispatcher used by kernel/userland code.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Basic syscall numbers for the minimal kernel implementation.
enum {
    SYSCALL_WRITE = 1,
    SYSCALL_EXIT  = 2,
};

// Kernel syscall dispatcher. Implemented in syscalls.cpp.
// num: syscall number
// a,b,c: opaque arguments (up to three 64-bit args)
uint64_t syscall_dispatch(uint64_t num, uint64_t a, uint64_t b, uint64_t c);

#ifdef __cplusplus
}
#endif
