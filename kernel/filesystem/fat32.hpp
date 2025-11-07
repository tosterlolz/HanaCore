#pragma once

#include <stdint.h>
#include <stddef.h>

namespace hanacore {
    namespace fs {
        // Minimal C++-callable interface that mirrors the C wrappers implemented in
        // `fat32.cpp`. Functions operate on C strings and callbacks to keep the API
        // freestanding-friendly.

        int fat32_init_from_module(const char* module_name);
        int64_t fat32_read_file(const char* path, void* buf, size_t len);
        void* fat32_get_file_alloc(const char* path, size_t* out_len);
        int fat32_list_dir(const char* path, void (*cb)(const char* name));

    } // namespace fs
} // namespace hanacore