#include "pma.hpp"
#include "bump_alloc.hpp"
#include <stdint.h>
#include "../utils/logger.hpp"

extern "C" void pma_init() {
    // No global state yet. Bump allocator will be lazily initialized on first use.
    hanacore::utils::log_ok_cpp("PMA: initialized (bump-backed)");
}

extern "C" void* pma_alloc_pages(size_t count) {
    if (count == 0) return nullptr;
    size_t size = count * 0x1000;
    // Request page-aligned allocation from bump allocator
    void* ptr = bump_alloc_alloc(size, 0x1000);
    hanacore::utils::log_hex64_cpp("PMA: alloc pages", (uint64_t)(uintptr_t)ptr);
    return ptr;
}

extern "C" void pma_free_pages(void* addr, size_t count) {
    // no-op for bump-backed implementation
    (void)addr; (void)count;
}
