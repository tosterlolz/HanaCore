#include "vfs.hpp"
#include "../libs/libc.h"
#include "hanafs.hpp"
#include "ramfs.hpp"
#include "fat32.hpp"
#include "../mem/heap.hpp"
#include "procfs.hpp"
#include "devfs.hpp"
#include <string.h>

namespace hanacore { namespace fs {

    struct MountEntry {
        const char* fsname;
        const char* mountpoint;
    };

    static MountEntry mounts[16];
    static int mount_count = 0;

    void vfs_init(void) {
        mount_count = 0;
        for (int i = 0; i < 16; ++i) { mounts[i].fsname = NULL; mounts[i].mountpoint = NULL; }
    }

    void vfs_register_mount(const char* fsname, const char* mountpoint) {
        if (!fsname || !mountpoint) return;
        if (mount_count >= 16) return;
        mounts[mount_count].fsname = fsname;
        mounts[mount_count].mountpoint = mountpoint;
        mount_count++;
    }

    int vfs_list_mounts(void (*cb)(const char* line)) {
        if (!cb) return -1;
        char buf[128];
        for (int i = 0; i < mount_count; ++i) {
            const char* f = mounts[i].fsname ? mounts[i].fsname : "<unknown>";
            const char* m = mounts[i].mountpoint ? mounts[i].mountpoint : "<unknown>";
            snprintf(buf, sizeof(buf), "VFS mount: [%s -> %s]", f, m);
            cb(buf);
        }
        return 0;
    }

    int vfs_list_dir(const char* path, void (*cb)(const char* name)) {
        if (!path || !cb) return -1;
        // Prefer mounted filesystems when the requested path is inside a
        // registered mountpoint. Iterate mounts and dispatch to the
        // appropriate backend when a match is found.
        for (int i = 0; i < mount_count; ++i) {
            const char* mpoint = mounts[i].mountpoint;
            const char* fsname = mounts[i].fsname;
            if (!mpoint || !fsname) continue;
            size_t mlen = strlen(mpoint);
            // Exact match or path is inside mountpoint (e.g., /mounted or /mounted/foo)
            if (strcmp(path, mpoint) == 0 || (strncmp(path, mpoint, mlen) == 0 && path[mlen] == '/')) {
                // compute relative path to pass to backend
                const char* rel = path;
                if (!(strcmp(mpoint, "/") == 0)) {
                    // strip mountpoint prefix
                    if (path[mlen] == '\0') rel = "/"; else rel = path + mlen;
                }
                // Dispatch based on fsname string. Known backends: fat32, hanafs.
                if (strcmp(fsname, "fat32") == 0) {
                    return hanacore::fs::fat32_list_dir(rel, cb);
                }
                if (strcmp(fsname, "hanafs") == 0 || strcmp(fsname, "ramfs") == 0) {
                    return hanacore::fs::ramfs_list_dir(rel, cb);
                }
                // For simple pseudo-filesystems (procfs/devfs) call their
                // specific listing handlers so they can provide dynamic
                // entries instead of falling back to HanaFS.
                if (strcmp(fsname, "procfs") == 0) {
                    return hanacore::fs::procfs_list_dir(path, cb);
                }
                if (strcmp(fsname, "devfs") == 0) {
                    return hanacore::fs::devfs_list_dir(path, cb);
                }
            }
        }

        // No specific mount matched — fall back to HanaFS then FAT32 like before
    int rc = hanacore::fs::ramfs_list_dir(path, cb);
        if (rc == 0) return 0;
    rc = hanacore::fs::fat32_list_dir(path, cb);
        if (rc == 0) return 0;
        return -1;
    }

    int vfs_remove_dir(const char* path) {
        if (!path) return -1;
        // If inside a mountpoint, dispatch to that backend when possible
        for (int i = 0; i < mount_count; ++i) {
            const char* mpoint = mounts[i].mountpoint;
            const char* fsname = mounts[i].fsname;
            if (!mpoint || !fsname) continue;
            size_t mlen = strlen(mpoint);
            if (strcmp(path, mpoint) == 0 || (strncmp(path, mpoint, mlen) == 0 && path[mlen] == '/')) {
                if (strcmp(fsname, "hanafs") == 0 || strcmp(fsname, "ramfs") == 0) {
                    return hanacore::fs::ramfs_remove_dir(path);
                }
                // for other backends, not implemented here
                return -1;
            }
        }
        // fallback to ramfs/hanafs
        return hanacore::fs::ramfs_remove_dir(path);
    }

    int vfs_create_file(const char* path) {
        if (!path) return -1;
        for (int i = 0; i < mount_count; ++i) {
            const char* mpoint = mounts[i].mountpoint;
            const char* fsname = mounts[i].fsname;
            if (!mpoint || !fsname) continue;
            size_t mlen = strlen(mpoint);
            if (strcmp(path, mpoint) == 0 || (strncmp(path, mpoint, mlen) == 0 && path[mlen] == '/')) {
                if (strcmp(fsname, "hanafs") == 0 || strcmp(fsname, "ramfs") == 0) {
                    return hanacore::fs::ramfs_create_file(path);
                }
                if (strcmp(fsname, "fat32") == 0) {
                    return hanacore::fs::fat32_create_file(path);
                }
                return -1;
            }
        }
        return hanacore::fs::ramfs_create_file(path);
    }

    int vfs_unlink(const char* path) {
        if (!path) return -1;
        for (int i = 0; i < mount_count; ++i) {
            const char* mpoint = mounts[i].mountpoint;
            const char* fsname = mounts[i].fsname;
            if (!mpoint || !fsname) continue;
            size_t mlen = strlen(mpoint);
            if (strcmp(path, mpoint) == 0 || (strncmp(path, mpoint, mlen) == 0 && path[mlen] == '/')) {
                if (strcmp(fsname, "hanafs") == 0 || strcmp(fsname, "ramfs") == 0) {
                    return hanacore::fs::ramfs_unlink(path);
                }
                if (strcmp(fsname, "fat32") == 0) {
                    return hanacore::fs::fat32_unlink(path);
                }
                return -1;
            }
        }
        return hanacore::fs::ramfs_unlink(path);
    }

    int vfs_make_dir(const char* path) {
        if (!path) return -1;
        for (int i = 0; i < mount_count; ++i) {
            const char* mpoint = mounts[i].mountpoint;
            const char* fsname = mounts[i].fsname;
            if (!mpoint || !fsname) continue;
            size_t mlen = strlen(mpoint);
            if (strcmp(path, mpoint) == 0 || (strncmp(path, mpoint, mlen) == 0 && path[mlen] == '/')) {
                if (strcmp(fsname, "hanafs") == 0 || strcmp(fsname, "ramfs") == 0) {
                    return hanacore::fs::ramfs_make_dir(path);
                }
                if (strcmp(fsname, "fat32") == 0) {
                    return hanacore::fs::fat32_make_dir(path);
                }
                return -1;
            }
        }
        return hanacore::fs::ramfs_make_dir(path);
    }

    int vfs_write_file(const char* path, const void* buf, size_t len) {
        if (!path) return -1;
        for (int i = 0; i < mount_count; ++i) {
            const char* mpoint = mounts[i].mountpoint;
            const char* fsname = mounts[i].fsname;
            if (!mpoint || !fsname) continue;
            size_t mlen = strlen(mpoint);
            if (strcmp(path, mpoint) == 0 || (strncmp(path, mpoint, mlen) == 0 && path[mlen] == '/')) {
                if (strcmp(fsname, "hanafs") == 0 || strcmp(fsname, "ramfs") == 0) {
                    return hanacore::fs::ramfs_write_file(path, buf, len);
                }
                if (strcmp(fsname, "fat32") == 0) {
                    return hanacore::fs::fat32_write_file(path, buf, len);
                }
                return -1;
            }
        }
        return hanacore::fs::ramfs_write_file(path, buf, len);
    }

    void* vfs_get_file_alloc(const char* path, size_t* out_len) {
        if (!path || !out_len) return NULL;
        void* data = NULL; size_t len = 0;
        // If the path lies inside a registered mountpoint, dispatch to that
        // backend so mounts show up under the requested mount location.
        for (int i = 0; i < mount_count; ++i) {
            const char* mpoint = mounts[i].mountpoint;
            const char* fsname = mounts[i].fsname;
            if (!mpoint || !fsname) continue;
            size_t mlen = strlen(mpoint);
            if (strcmp(path, mpoint) == 0 || (strncmp(path, mpoint, mlen) == 0 && path[mlen] == '/')) {
                // compute relative path for backend
                const char* rel = path;
                if (!(strcmp(mpoint, "/") == 0)) {
                    if (path[mlen] == '\0') rel = "/"; else rel = path + mlen;
                }
                if (strcmp(fsname, "fat32") == 0) {
                    data = hanacore::fs::fat32_get_file_alloc(rel, &len);
                    if (data && len > 0) { *out_len = len; return data; }
                    if (data) { hanacore::mem::kfree(data); data = NULL; }
                    return NULL;
                }
                if (strcmp(fsname, "hanafs") == 0 || strcmp(fsname, "ramfs") == 0) {
                    data = hanacore::fs::ramfs_get_file_alloc(rel, &len);
                    if (data && len > 0) { *out_len = len; return data; }
                    if (data) { hanacore::mem::kfree(data); data = NULL; }
                    return NULL;
                }
                if (strcmp(fsname, "procfs") == 0) {
                    data = hanacore::fs::procfs_get_file_alloc(path, out_len);
                    return data;
                }
                if (strcmp(fsname, "devfs") == 0) {
                    data = hanacore::fs::devfs_get_file_alloc(path, out_len);
                    return data;
                }
            }
        }

        // No mount matched — try common backends in order: ramfs/hanafs, procfs/devfs, FAT32
        data = hanacore::fs::ramfs_get_file_alloc(path, &len);
        if (data && len > 0) { *out_len = len; return data; }
        if (data) { hanacore::mem::kfree(data); data = NULL; }
        data = hanacore::fs::procfs_get_file_alloc(path, &len);
        if (data && len > 0) { *out_len = len; return data; }
        if (data) { hanacore::mem::kfree(data); data = NULL; }
        data = hanacore::fs::devfs_get_file_alloc(path, &len);
        if (data && len > 0) { *out_len = len; return data; }
        if (data) { hanacore::mem::kfree(data); data = NULL; }
        data = hanacore::fs::fat32_get_file_alloc(path, &len);
        if (data && len > 0) { *out_len = len; return data; }
        if (data) { hanacore::mem::kfree(data); }
        *out_len = 0; return NULL;
    }

} }

extern "C" int vfs_list_mounts(void (*cb)(const char* line)) { return hanacore::fs::vfs_list_mounts(cb); }
