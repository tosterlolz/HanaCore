#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the kernel heap region with the given size (bytes).
// This will allocate the heap region from the bump allocator.
void heap_init(size_t size);

// Allocate `size` bytes from kernel heap. Returns nullptr on failure.
void *kmalloc(size_t size);

// Free a pointer previously returned by kmalloc.
void kfree(void *ptr);

#ifdef __cplusplus
}
#endif
