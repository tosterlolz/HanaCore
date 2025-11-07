#pragma once
#include <stddef.h>

// Physical memory allocator (PMA) - lightweight shim.
// This is a minimal pma implementation that currently delegates to the
// bump allocator during early boot. Later, this can be replaced with a
// proper physical page allocator using the system memory map.

extern "C" {
void pma_init();
// Allocate `count` contiguous pages (4 KiB each). Returns virtual pointer
// to the mapped page range or nullptr on failure. For now this delegates to
// the bump allocator and returns a virtual pointer.
void* pma_alloc_pages(size_t count);
// Free pages (no-op for bump-backed implementation)
void pma_free_pages(void* addr, size_t count);
}
