#pragma once

#include <stddef.h>

// Minimal devfs skeleton: registers /dev mount point and some device nodes

namespace hanacore { namespace fs {
    void devfs_init(void);
    int devfs_list_dir(const char* path, void (*cb)(const char* name));
    void* devfs_get_file_alloc(const char* path, size_t* out_len);
} }
