#pragma once

#include <stdint.h>
#include <stddef.h>

#include "../api/hanaapi.h"

// HanaFS: a tiny in-memory filesystem for kernel use. Designed to be simple
// and freestanding. This header exposes both C and C++ friendly APIs.

#ifdef __cplusplus
namespace hanacore { namespace fs {
    int hanafs_init(void);
    int hanafs_write_file(const char* path, const void* buf, size_t len);
    void* hanafs_get_file_alloc(const char* path, size_t* out_len);
    int hanafs_list_dir(const char* path, void (*cb)(const char* name));
    int hanafs_list_mounts(void (*cb)(const char* line));
    int hanafs_create_file(const char* path);
    int hanafs_unlink(const char* path);
    int hanafs_make_dir(const char* path);
    int hanafs_remove_dir(const char* path);
    // Persistence: attempt to load from ATA at init, and persist changes to ATA.
    // These return 0 on success, -1 on failure. Persistence location is
    // configurable via HANAFS_PERSIST_LBA in the implementation (default 2048).
    int hanafs_persist_to_ata(void);
    int hanafs_load_from_ata(void);
    int hanafs_format_ata_master(int drive_number);
    // Query metadata about a path. Fills a hana_stat structure (defined in ../api/hanaapi.h)
    int hanafs_stat(const char* path, struct hana_stat* st);
    // Directory iteration helpers (C wrappers)
    hana_dir_t* hanafs_opendir(const char* path);
    hana_dirent* hanafs_readdir(hana_dir_t* dir);
    int hanafs_closedir(hana_dir_t* dir);
} }
#endif

// C wrappers
extern "C" {
    int hanafs_init(void);
    int hanafs_write_file(const char* path, const void* buf, size_t len);
    void* hanafs_get_file_alloc(const char* path, size_t* out_len);
    int hanafs_list_dir(const char* path, void (*cb)(const char* name));
    int hanafs_create_file(const char* path);
    int hanafs_unlink(const char* path);
    int hanafs_make_dir(const char* path);
    int hanafs_remove_dir(const char* path);
    int hanafs_persist_to_ata(void);
    int hanafs_load_from_ata(void);
    int hanafs_list_mounts(void (*cb)(const char* line));
    int hanafs_format_ata_master(int drive_number);
    int hanafs_stat(const char* path, struct hana_stat* st);
    hana_dir_t* hanafs_opendir(const char* path);
    hana_dirent* hanafs_readdir(hana_dir_t* dir);
    int hanafs_closedir(hana_dir_t* dir);
}
