#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
namespace hanacore { namespace initrd {
    // Initialize initrd from a Limine module by name (e.g. "initrd.tar").
    // Returns 0 on success, -1 on failure.
    int init_from_module(const char* module_name);
    // Initialize initrd from an in-memory tar archive pointer/size.
    int init_from_memory(const void* data, size_t size);
} }
#endif

extern "C" {
    int initrd_init_from_module(const char* module_name);
    int initrd_init_from_memory(const void* data, size_t size);
}
