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

// Extended syscall numbers for POSIX-like operations (local kernel numbers)
enum {
    HANA_SYSCALL_READ = 10,
    HANA_SYSCALL_WRITE = 11,
    HANA_SYSCALL_OPEN = 12,
    HANA_SYSCALL_CLOSE = 13,
    HANA_SYSCALL_LSEEK = 14,
    HANA_SYSCALL_UNLINK = 15,
    HANA_SYSCALL_MKDIR = 16,
    HANA_SYSCALL_RMDIR = 17,
    HANA_SYSCALL_STAT = 18,
    HANA_SYSCALL_SPAWN = 19,
    HANA_SYSCALL_WAITPID = 20,
    HANA_SYSCALL_FORK = 21,
    HANA_SYSCALL_DUP2 = 22,
    HANA_SYSCALL_PIPE = 23,
    HANA_SYSCALL_FSTAT = 24,
    HANA_SYSCALL_OPENDIR = 25,
    HANA_SYSCALL_READDIR = 26,
    HANA_SYSCALL_CLOSEDIR = 27,
};

// Kernel syscall dispatcher. Implemented in syscalls.cpp.
// num: syscall number
// a,b,c: opaque arguments (up to three 64-bit args)
// Now accepts up to 6 user arguments (num + a1..a6).
uint64_t syscall_dispatch(uint64_t num, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f);

#ifdef __cplusplus
}
#endif
