#pragma once

#include <stdint.h>
#include <stddef.h>

// Minimal Virtual Filesystem layer (skeleton)
// Provides a tiny mount registry so special filesystems (procfs/devfs)
// can be registered and enumerated by tools.

namespace hanacore { namespace fs {

    // Register a mount (fsname must be a persistent string or literal)
    void vfs_register_mount(const char* fsname, const char* mountpoint);

    // Initialize the vfs subsystem
    void vfs_init(void);

    // Read a file via VFS: tries registered filesystems (hanafs, fat32...) and
    // returns a newly allocated buffer with the file contents (caller frees).
    void* vfs_get_file_alloc(const char* path, size_t* out_len);

    // List directory via VFS. Callback receives each entry name (not full path).
    int vfs_list_dir(const char* path, void (*cb)(const char* name));

    // Remove an empty directory at the given path. Prefer mounted FS handlers
    // when appropriate; fall back to the legacy ramfs/hanafs implementation.
    int vfs_remove_dir(const char* path);

    // Create a file (empty) at the given path. Returns 0 on success.
    int vfs_create_file(const char* path);

    // Unlink (remove) a file at the given path. Returns 0 on success.
    int vfs_unlink(const char* path);

    // Make directory at the given path. Returns 0 on success.
    int vfs_make_dir(const char* path);

    // Write a full file (create or overwrite) with the provided buffer.
    int vfs_write_file(const char* path, const void* buf, size_t len);

    // List mounts: callback receives a printable line for each mount
    int vfs_list_mounts(void (*cb)(const char* line));

} }

extern "C" {
    int vfs_list_mounts(void (*cb)(const char* line));
}
