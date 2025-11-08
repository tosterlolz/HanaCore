#pragma once

#include <stddef.h>

// Minimal procfs skeleton: registers /proc mount point and produces a few
// dynamic entries. This is intentionally tiny — it provides listing support
// and a couple of read-only entries for diagnostics.

namespace hanacore { namespace fs { 
    void procfs_init(void);
    int procfs_list_dir(const char* path, void (*cb)(const char* name));
    void* procfs_get_file_alloc(const char* path, size_t* out_len);
} }
