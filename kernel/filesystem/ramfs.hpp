#pragma once

#include <stdint.h>
#include <stddef.h>
// Pull in the Hana API types (hana_stat, hana_dir_t, hana_dirent, etc.)
#include "../api/hanaapi.h"

#ifdef __cplusplus
namespace hanacore { namespace fs {
    // Minimal RAM-backed filesystem thin shim that currently delegates to
    // the legacy HanaFS C implementation. This lets us migrate callers to
    // a new backend name without immediately deleting HanaFS.
    int ramfs_init(void);
    int ramfs_write_file(const char* path, const void* buf, size_t len);
    void* ramfs_get_file_alloc(const char* path, size_t* out_len);
    int ramfs_list_dir(const char* path, void (*cb)(const char* name));
    int ramfs_create_file(const char* path);
    int ramfs_unlink(const char* path);
    int ramfs_make_dir(const char* path);
    int ramfs_remove_dir(const char* path);
    int ramfs_persist_to_ata(void);
    int ramfs_load_from_ata(void);
    int ramfs_format_ata_master(int drive_number);
    void ramfs_set_persist_enabled(int enabled);
    int ramfs_mount_iso_drive(int drive, const char* mount_point);
    int ramfs_stat(const char* path, struct hana_stat* st);
    hana_dir_t* ramfs_opendir(const char* path);
    hana_dirent* ramfs_readdir(hana_dir_t* dir);
    int ramfs_closedir(hana_dir_t* dir);
} }
#endif

extern "C" {
    int ramfs_init(void);
    int ramfs_write_file(const char* path, const void* buf, size_t len);
    void* ramfs_get_file_alloc(const char* path, size_t* out_len);
    int ramfs_list_dir(const char* path, void (*cb)(const char* name));
    int ramfs_create_file(const char* path);
    int ramfs_unlink(const char* path);
    int ramfs_make_dir(const char* path);
    int ramfs_remove_dir(const char* path);
    int ramfs_persist_to_ata(void);
    int ramfs_load_from_ata(void);
    int ramfs_format_ata_master(int drive_number);
    void ramfs_set_persist_enabled(int enabled);
    int ramfs_mount_iso_drive(int drive, const char* mount_point);
    int ramfs_stat(const char* path, struct hana_stat* st);
    hana_dir_t* ramfs_opendir(const char* path);
    hana_dirent* ramfs_readdir(hana_dir_t* dir);
    int ramfs_closedir(hana_dir_t* dir);
}
