#include "ramfs.hpp"
#include "hanafs.hpp"

namespace hanacore { namespace fs {

    int ramfs_init(void) { return ::hanafs_init(); }
    int ramfs_write_file(const char* path, const void* buf, size_t len) { return ::hanafs_write_file(path, buf, len); }
    void* ramfs_get_file_alloc(const char* path, size_t* out_len) { return ::hanafs_get_file_alloc(path, out_len); }
    int ramfs_list_dir(const char* path, void (*cb)(const char* name)) { return ::hanafs_list_dir(path, cb); }
    int ramfs_create_file(const char* path) { return ::hanafs_create_file(path); }
    int ramfs_unlink(const char* path) { return ::hanafs_unlink(path); }
    int ramfs_make_dir(const char* path) { return ::hanafs_make_dir(path); }
    int ramfs_remove_dir(const char* path) { return ::hanafs_remove_dir(path); }
    int ramfs_persist_to_ata(void) { return ::hanafs_persist_to_ata(); }
    int ramfs_load_from_ata(void) { return ::hanafs_load_from_ata(); }
    int ramfs_format_ata_master(int drive_number) { return ::hanafs_format_ata_master(drive_number); }
    void ramfs_set_persist_enabled(int enabled) { ::hanafs_set_persist_enabled(enabled); }
    int ramfs_mount_iso_drive(int drive, const char* mount_point) { return ::hanafs_mount_iso_drive(drive, mount_point); }
    int ramfs_stat(const char* path, struct hana_stat* st) { return ::hanafs_stat(path, st); }
    hana_dir_t* ramfs_opendir(const char* path) { return ::hanafs_opendir(path); }
    hana_dirent* ramfs_readdir(hana_dir_t* dir) { return ::hanafs_readdir(dir); }
    int ramfs_closedir(hana_dir_t* dir) { return ::hanafs_closedir(dir); }

} }

extern "C" {
    int ramfs_init(void) { return hanacore::fs::ramfs_init(); }
    int ramfs_write_file(const char* path, const void* buf, size_t len) { return hanacore::fs::ramfs_write_file(path, buf, len); }
    void* ramfs_get_file_alloc(const char* path, size_t* out_len) { return hanacore::fs::ramfs_get_file_alloc(path, out_len); }
    int ramfs_list_dir(const char* path, void (*cb)(const char* name)) { return hanacore::fs::ramfs_list_dir(path, cb); }
    int ramfs_create_file(const char* path) { return hanacore::fs::ramfs_create_file(path); }
    int ramfs_unlink(const char* path) { return hanacore::fs::ramfs_unlink(path); }
    int ramfs_make_dir(const char* path) { return hanacore::fs::ramfs_make_dir(path); }
    int ramfs_remove_dir(const char* path) { return hanacore::fs::ramfs_remove_dir(path); }
    int ramfs_persist_to_ata(void) { return hanacore::fs::ramfs_persist_to_ata(); }
    int ramfs_load_from_ata(void) { return hanacore::fs::ramfs_load_from_ata(); }
    int ramfs_format_ata_master(int drive_number) { return hanacore::fs::ramfs_format_ata_master(drive_number); }
    void ramfs_set_persist_enabled(int enabled) { hanacore::fs::ramfs_set_persist_enabled(enabled); }
    int ramfs_mount_iso_drive(int drive, const char* mount_point) { return hanacore::fs::ramfs_mount_iso_drive(drive, mount_point); }
    int ramfs_stat(const char* path, struct hana_stat* st) { return hanacore::fs::ramfs_stat(path, st); }
    hana_dir_t* ramfs_opendir(const char* path) { return hanacore::fs::ramfs_opendir(path); }
    hana_dirent* ramfs_readdir(hana_dir_t* dir) { return hanacore::fs::ramfs_readdir(dir); }
    int ramfs_closedir(hana_dir_t* dir) { return hanacore::fs::ramfs_closedir(dir); }
}
