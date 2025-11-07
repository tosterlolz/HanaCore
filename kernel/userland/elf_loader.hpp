#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Load an ELF64 image from memory (data,size). This allocates kernel memory
// for the program's segments (using the bump allocator) and returns the
// entry point pointer (callable as void(*)(void)). Returns NULL on error.
void* elf64_load_from_memory(const void* data, size_t size);

#ifdef __cplusplus
}
#endif

// C++ namespace-friendly wrapper. Keeps the C ABI symbol above for linkage
// while allowing C++ code to call the namespaced API.
namespace hanacore {
	namespace userland {
		inline void* elf64_load_from_memory(const void* data, size_t size) {
			return ::elf64_load_from_memory(data, size);
		}
	} // namespace userland
} // namespace hanacore
