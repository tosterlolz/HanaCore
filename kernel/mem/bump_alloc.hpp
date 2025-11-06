#pragma once
#include <stddef.h>

// Very small page-aligned bump allocator used for transient allocations
// (loading ELF segments). Not thread-safe. Only supports allocating
// memory from the kernel's free region.

void *bump_alloc_alloc(size_t size, size_t align);

// For debugging
size_t bump_alloc_used();
