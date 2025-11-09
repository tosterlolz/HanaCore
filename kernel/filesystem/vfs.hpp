#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*vfs_dir_cb_t)(const char* name);

#define VFS_TYPE_FILE  0x01
#define VFS_TYPE_DIR   0x02
#define VFS_TYPE_OTHER 0x04

void vfs_init(void);
void vfs_register_mount(const char* fsname, const char* mountpoint);
int vfs_list_mounts(void (*cb)(const char* line));
int vfs_list_dir(const char* path, vfs_dir_cb_t cb);
int vfs_make_dir(const char* path);
int vfs_remove_dir(const char* path);
int vfs_create_file(const char* path);
int vfs_unlink(const char* path);
int vfs_write_file(const char* path, const void* buf, size_t len);
void* vfs_get_file_alloc(const char* path, size_t* out_len);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
namespace hanacore {
namespace fs {
inline void init() { ::vfs_init(); }

inline int list_dir(const char* path, void (*cb)(const char* name)) {
    return ::vfs_list_dir(path, cb);
}

inline int make_dir(const char* path) { return ::vfs_make_dir(path); }
inline int remove_dir(const char* path) { return ::vfs_remove_dir(path); }
inline int create_file(const char* path) { return ::vfs_create_file(path); }
inline int unlink(const char* path) { return ::vfs_unlink(path); }
inline int write_file(const char* path, const void* buf, size_t len) { return ::vfs_write_file(path, buf, len); }
inline void* get_file_alloc(const char* path, size_t* out_len) { return ::vfs_get_file_alloc(path, out_len); }
inline void register_mount(const char* fsname, const char* mountpoint) { ::vfs_register_mount(fsname, mountpoint); }
inline int list_mounts(void (*cb)(const char* line)) { return ::vfs_list_mounts(cb); }

} 
} 
#endif
