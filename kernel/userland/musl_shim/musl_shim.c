// Minimal musl syscall shim implementation
#include "musl_shim.h"
#include <stdarg.h>
#include <stddef.h>
// Hana syscall numbers (keep in sync with kernel/userland/syscalls.hpp)
#define HANA_SYSCALL_READ 10
#define HANA_SYSCALL_WRITE 11
#define HANA_SYSCALL_OPEN 12
#define HANA_SYSCALL_CLOSE 13
#define HANA_SYSCALL_LSEEK 14

// Basic direct syscalls also present
#define SYSCALL_WRITE 1
#define SYSCALL_EXIT 2

// Linux/x86_64 syscall numbers we expect musl to use (subset)
#define __NR_read 0
#define __NR_write 1
#define __NR_open 2
#define __NR_close 3
#define __NR_stat 4
#define __NR_fstat 5
#define __NR_lseek 8
#define __NR_openat 257
#define __NR_exit 60

// Directly invoke the syscall instruction with the original (Linux) syscall
// number. The kernel provides a Linux-compatible dispatcher for common
// syscalls, so pass-through makes userland programs compiled for Linux
// work without additional mapping.
long syscall(long num, ...){
    va_list ap; va_start(ap, num);
    unsigned long a1 = va_arg(ap, unsigned long);
    unsigned long a2 = va_arg(ap, unsigned long);
    unsigned long a3 = va_arg(ap, unsigned long);
    unsigned long a4 = va_arg(ap, unsigned long);
    unsigned long a5 = va_arg(ap, unsigned long);
    unsigned long a6 = va_arg(ap, unsigned long);
    va_end(ap);

    long ret;
    register unsigned long r10 __asm__("r10") = a4;
    register unsigned long r8  __asm__("r8")  = a5;
    register unsigned long r9  __asm__("r9")  = a6;
    __asm__ volatile (
        "syscall"
        : "=a" (ret)
        : "a" (num), "D" (a1), "S" (a2), "d" (a3), "r" (r10), "r" (r8), "r" (r9)
        : "rcx", "r11", "memory"
    );
    return ret;
}
