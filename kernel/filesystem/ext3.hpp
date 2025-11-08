#pragma once

#include <stdint.h>
#include <stddef.h>
#include "hanafs.hpp"

namespace ext3 {
    // Minimal ext3 skeleton API. This file provides a placeholder backend
    // that can be fleshed out later. Functions return -1 when unimplemented.

    int init(void);
    int mount(int drive, const char* mount_point);
    void* get_file_alloc(const char* path, size_t* out_len);
    int list_dir(const char* path, void (*cb)(const char* name));
}
