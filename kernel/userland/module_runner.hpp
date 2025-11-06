#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Execute a module by name (filename). Searches Limine modules and if found
// tries to run it as ELF via elf64_load_from_memory(). Returns 0 on success,
// -1 on failure/not found.
int exec_module_by_name(const char* filename);

#ifdef __cplusplus
}
#endif
