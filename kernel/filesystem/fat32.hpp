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
        // Auto-mount helper (C wrapper provided in fat32.cpp)
        void fat32_mount_all_letter_modules();
        // List mounted filesystems: callback receives a printable line per mount
        void fat32_list_mounts(void (*cb)(const char* line));

    } // namespace fs
} // namespace hanacore