#pragma once

#include <stdint.h>
#include <stddef.h>

namespace hanacore {
    namespace fs {
        // Minimal C++-callable interface that mirrors the C wrappers implemented in
        // `fat32.cpp`. Functions operate on C strings and callbacks to keep the API
        // freestanding-friendly.

        int fat32_init_from_module(const char* module_name);
        // Initialize FAT32 state from an in-memory disk image (e.g. Limine module)
        int fat32_init_from_memory(const void* data, size_t size);
        int64_t fat32_read_file(const char* path, void* buf, size_t len);
        void* fat32_get_file_alloc(const char* path, size_t* out_len);
        int fat32_list_dir(const char* path, void (*cb)(const char* name));
        // Optional progress callback (C linkage). Called periodically during
        // long operations. Implement a weak symbol in C if you want updates.
        // The callback receives current progress (0..100) or -1 for indeterminate.
        extern "C" void fat32_progress_update(int percent);
        // Auto-mount helper (C wrapper provided in fat32.cpp)
        void fat32_mount_all_letter_modules();
        // List mounted filesystems: callback receives a printable line per mount
        void fat32_list_mounts(void (*cb)(const char* line));

    } // namespace fs
} // namespace hanacore

// C wrappers (also declared here for callers in C files)
extern "C" {
// Mount helpers exposed to C callers. Previously these accepted a drive
// letter (e.g. 'C'), but the kernel now uses numeric drive IDs instead.
int fat32_mount_ata_master(int drive_number);
int fat32_mount_ata_slave(int drive_number);
// Format an ATA device as FAT32 (destructive). Drive_number is currently
// unused; driver selects primary master. Returns 0 on success, -1 on failure.
int fat32_format_ata_master(int drive_number);
}
