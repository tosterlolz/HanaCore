// Minimal ext2 filesystem scaffold (read-only helper)
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize ext2 from a Limine module named `module_name` (e.g. "rootfs.img").
// Returns true on success.
int ext2_init_from_module(const char* module_name);

// Read at most `len` bytes from file `path` into `buf`. Returns number of
// bytes read, or -1 on error/not found. This is a simple stub now.
int64_t ext2_read_file(const char* path, void* buf, size_t len);

// Allocate and return the contents of `path` from the ext2 image. The
// returned pointer is allocated with the kernel bump allocator and must not
// be freed. On success returns pointer to data and writes size to *out_len;
// returns NULL on error.
void* ext2_get_file_alloc(const char* path, size_t* out_len);

// List directory entries for `path`. For each entry, `cb` is invoked with a
// NUL-terminated name. Returns number of entries reported or -1 on error.
int ext2_list_dir(const char* path, void (*cb)(const char* name));

#ifdef __cplusplus
}
#endif
