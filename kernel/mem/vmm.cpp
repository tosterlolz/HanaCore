#include "vmm.hpp"
#include "../utils/logger.hpp"
#include <stdint.h>

extern "C" void vmm_init() {
    // Stub: architecture-specific page-table setup should be placed here.
    hanacore::utils::log_ok_cpp("VMM: initialized (stub)");
}

extern "C" int vmm_map_range(void* phys, void* virt, size_t size, unsigned long flags) {
    (void)phys; (void)virt; (void)size; (void)flags;
    // No-op for now; pretend success. Replace with real mapping later.
    return 0;
}

extern "C" int vmm_unmap_range(void* virt, size_t size) {
    (void)virt; (void)size;
    return 0;
}
