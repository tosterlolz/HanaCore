#include "bump_alloc.hpp"
#include <stdint.h>
#include <stddef.h>
#include "../utils/logger.hpp"

extern "C" {
    // Provided by linker script
    extern char __kernel_end;
}

// Limine HHDM request (optional). If present, bump allocator must use the
// HHDM offset to convert physical addresses to higher-half virtual addresses.
extern "C" {
    struct limine_hhdm_response {
        uint64_t offset;
        uint64_t reserved[7];
    };

    struct limine_hhdm_request {
        uint64_t id[4];
        uint64_t revision;
        volatile struct limine_hhdm_response* response;
    };
    extern volatile struct limine_hhdm_request limine_hhdm_request;
}

static uintptr_t bump_ptr = 0;

static inline uintptr_t align_up(uintptr_t v, size_t a) {
    uintptr_t mask = (uintptr_t)(a - 1);
    return (v + mask) & ~mask;
}

void *bump_alloc_alloc(size_t size, size_t align) {
    if (bump_ptr == 0) {
        uintptr_t ke = (uintptr_t)&__kernel_end;
        hanacore::utils::log_hex64_cpp("bump: __kernel_end", (uint64_t)ke);
        // If Limine provided an HHDM offset, use it to compute the virtual
        // address corresponding to the physical kernel end.
        if (limine_hhdm_request.response) {
            uint64_t off = limine_hhdm_request.response->offset;
            hanacore::utils::log_hex64_cpp("bump: hhdm off", off);
            // Only apply HHDM offset if the linker symbol looks like a
            // physical address (small). If __kernel_end is already a
            // higher-half virtual address, don't add the offset again.
            if (ke < 0x100000000ULL) {
                ke += off;
                hanacore::utils::log_hex64_cpp("bump: ke after hhdm", (uint64_t)ke);
            } else {
                hanacore::utils::log_hex64_cpp("bump: ke appears already virtual", (uint64_t)ke);
            }
        }
        bump_ptr = align_up(ke, 0x1000);
        hanacore::utils::log_hex64_cpp("bump: bump_ptr after align", (uint64_t)bump_ptr);
    }
    uintptr_t addr = align_up(bump_ptr, align ? align : 1);
    hanacore::utils::log_hex64_cpp("bump: alloc addr", (uint64_t)addr);
    bump_ptr = align_up(addr + size, 0x1000);
    hanacore::utils::log_hex64_cpp("bump: bump_ptr updated", (uint64_t)bump_ptr);
    return (void *)addr;
}

size_t bump_alloc_used() {
    if (bump_ptr == 0) return 0;
    return (size_t)(bump_ptr - (uintptr_t)&__kernel_end);
}
