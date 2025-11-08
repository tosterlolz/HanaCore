#pragma once

#include <stdint.h>
#include <stddef.h>

#include "../api/hanaapi.h"

// HanaFS: a tiny in-memory filesystem for kernel use. Designed to be simple
// and freestanding. This header exposes both C and C++ friendly APIs.

#ifdef __cplusplus
// Legacy C++ wrappers (existing code uses hanacore::fs::hanafs_* names).
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
    int hanafs_persist_to_ata(void);
    int hanafs_load_from_ata(void);
    int hanafs_format_ata_master(int drive_number);
    void hanafs_set_persist_enabled(int enabled);
    int hanafs_stat(const char* path, struct hana_stat* st);
    hana_dir_t* hanafs_opendir(const char* path);
    hana_dirent* hanafs_readdir(hana_dir_t* dir);
    int hanafs_closedir(hana_dir_t* dir);
} }

// New public filesystem namespace requested: `hanafs::fs` with short names
// (no filesystem-name prefix). These are thin wrappers that currently
// delegate to the existing C functions; later we can implement a proper
// ext3-backed backend and point these names there.
namespace hanafs { namespace fs {
    // initialize filesystem subsystem
    int init(void);
    // mount a device (drive number) at the given mount location
    int mount(int drive, const char* mount_point);
    // basic file APIs
    int write_file(const char* path, const void* buf, size_t len);
    void* get_file_alloc(const char* path, size_t* out_len);
    int list_dir(const char* path, void (*cb)(const char* name));
    int create_file(const char* path);
    int unlink(const char* path);
    int make_dir(const char* path);
    int remove_dir(const char* path);
    int persist_to_ata(void);
    int load_from_ata(void);
    int format_ata_master(int drive_number);
    void set_persist_enabled(int enabled);
    int stat(const char* path, struct hana_stat* st);
    hana_dir_t* opendir(const char* path);
    hana_dirent* readdir(hana_dir_t* dir);
    int closedir(hana_dir_t* dir);
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
    int hanafs_mount_iso_drive(int drive, const char* mount_point);
    void hanafs_set_persist_enabled(int enabled);
    int hanafs_mount_iso_drive(int drive, const char* mount_point);
    int hanafs_mount_iso_drive(int drive, const char* mount_point);
    int hanafs_stat(const char* path, struct hana_stat* st);
    hana_dir_t* hanafs_opendir(const char* path);
    hana_dirent* hanafs_readdir(hana_dir_t* dir);
    int hanafs_closedir(hana_dir_t* dir);
}
