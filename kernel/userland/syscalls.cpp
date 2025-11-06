// Minimal syscall dispatcher implementation.
// This file provides a tiny set of syscalls the kernel exposes to
// userland or to in-kernel userland-like code.

#include "syscalls.hpp"
#include "../../kernel/drivers/screen.hpp"
#include "../../kernel/utils/logger.hpp"

#include <stdint.h>
#include <stddef.h>

static size_t kstrlen(const char *s) {
    const char *p = s;
    while (p && *p) ++p;
    return (size_t)(p - s);
}

extern "C" uint64_t syscall_dispatch(uint64_t num, uint64_t a, uint64_t b, uint64_t c) {
    (void)b;
    (void)c;

    switch (num) {
        case SYSCALL_WRITE: {
            // a -> const char * (virtual address)
            const char *s = (const char *)(uintptr_t)a;
            if (!s) return 0;
            // Print via the kernel-provided print() front-end.
            print(s);
            return (uint64_t)kstrlen(s);
        }

        case SYSCALL_EXIT: {
            // a -> exit code (ignored for now)
            (void)a;
            log_info("sys_exit");
            // Halt the CPU in a tight loop.
            for (;;) {
                asm volatile ("cli");
                asm volatile ("hlt");
            }
            // Unreachable
            return 0;
        }

        default:
            // Unknown syscall
            log_info("sys_unknown");
            return (uint64_t)-1;
    }
}
