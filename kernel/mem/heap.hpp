#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


namespace hanacore { namespace mem {
    // Forward declaration for C++ namespace
    void heap_init(size_t size);
    void *kmalloc(size_t size);
    void kfree(void *ptr);
}} // namespace hanacore::mem

void heap_init(size_t size);

// Allocate `size` bytes from kernel heap. Returns nullptr on failure.
void *kmalloc(size_t size);

// Free a pointer previously returned by kmalloc.
void kfree(void *ptr);

#ifdef __cplusplus
}
#endif
