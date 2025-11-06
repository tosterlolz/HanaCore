#include "bump_alloc.hpp"
#include <stdint.h>
#include <stddef.h>

extern "C" {
    // Provided by linker script
    extern char __kernel_end;
}

static uintptr_t bump_ptr = 0;

static inline uintptr_t align_up(uintptr_t v, size_t a) {
    uintptr_t mask = (uintptr_t)(a - 1);
    return (v + mask) & ~mask;
}

void *bump_alloc_alloc(size_t size, size_t align) {
    if (bump_ptr == 0) {
        bump_ptr = align_up((uintptr_t)&__kernel_end, 0x1000);
    }
    uintptr_t addr = align_up(bump_ptr, align ? align : 1);
    bump_ptr = align_up(addr + size, 0x1000);
    return (void *)addr;
}

size_t bump_alloc_used() {
    if (bump_ptr == 0) return 0;
    return (size_t)(bump_ptr - (uintptr_t)&__kernel_end);
}
