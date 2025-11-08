#pragma once

#include <stddef.h>

namespace hanacore {
    namespace fs {
        // Initialize floppy filesystem from memory image (FAT12/FAT16)
        int floppy_init_from_memory(const void* data, size_t size);
        int floppy_list_dir(const char* path, void (*cb)(const char* name));
    }
}

extern "C" {
    int floppy_init_from_memory(const void* data, size_t size);
}
