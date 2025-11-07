#pragma once
#include <stddef.h>

// Very small VMM shim: initialization and basic map/unmap helpers.
// This is a placeholder so higher-level code can call vmm_map_range while
// the real implementation is filled in later.

extern "C" {
void vmm_init();
// Map `size` bytes from physical `phys` to virtual `virt` with flags.
// Returns 0 on success, non-zero on failure. Current implementation is a stub.
int vmm_map_range(void* phys, void* virt, size_t size, unsigned long flags);
// Unmap `size` bytes at virtual `virt`.
int vmm_unmap_range(void* virt, size_t size);
}
