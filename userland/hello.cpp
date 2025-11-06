// Tiny userland program for testing syscalls.
// This file is written as a simple C++ TU that calls the kernel-provided
// syscall_dispatch symbol. Depending on how you build/run userland modules
// you may compile this into a flat binary or link it into the kernel for
// testing.

#include <stdint.h>
#include <stddef.h>

extern "C" {
    // Dispatcher exposed by the kernel (see kernel/userland/syscalls.*)
    uint64_t syscall_dispatch(uint64_t num, uint64_t a, uint64_t b, uint64_t c);

    // Syscall numbers mirrored here for convenience
    enum {
        SYSCALL_WRITE = 1,
        SYSCALL_EXIT  = 2,
    };

    // Simple entry point the kernel or test harness can call.
    void hello_main(void) {
        const char *msg = "Hello from userland/hello\n";
        syscall_dispatch(SYSCALL_WRITE, (uint64_t)(uintptr_t)msg, 0, 0);
        syscall_dispatch(SYSCALL_EXIT, 0, 0, 0);
        // Should not return, but in case, loop.
        for (;;) __asm__ volatile ("hlt");
    }
}
