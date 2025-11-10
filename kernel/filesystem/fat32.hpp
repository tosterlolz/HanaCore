#pragma once

#include <stdint.h>
#include <stddef.h>

namespace hanacore {
namespace fs {
    extern bool fat32_ready;
    int fat32_init_from_iso_root();
} // namespace fs
} // namespace hanacore
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
    // Safe, small summary that doesn't enumerate Limine modules. Use this
    // from user-visible tools (like lsblk) to avoid dereferencing
    // module pointers that may be inaccessible in some environments.
    void fat32_get_summary(void (*cb)(const char* line));

    // Write helpers (simplified): create/remove files and directories.
    // These are minimal, short-name only implementations intended for a
    // simple rootfs and tools. Return 0 on success, -1 on failure.
    int fat32_create_file(const char* path);
    int fat32_unlink(const char* path);
    int fat32_make_dir(const char* path);
    int fat32_remove_dir(const char* path);
    // Write a file (create or overwrite) at `path` with the provided buffer.
    // Requires the target filesystem to be mounted (call fat32_mount_ata_master
    // to select ATA before writing). Returns 0 on success.
    int fat32_write_file(const char* path, const void* buf, size_t len);

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
