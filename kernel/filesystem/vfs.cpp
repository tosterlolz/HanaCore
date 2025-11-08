// kernel/filesystem/vfs.cpp
#include "vfs.hpp"
#include "../libs/libc.h"
#include "hanafs.hpp"
#include "ramfs.hpp"
#include "fat32.hpp"
#include "isofs.hpp"
#include "floppy.hpp"
#include "../mem/heap.hpp"
#include "procfs.hpp"
#include "devfs.hpp"

namespace hanacore {
namespace fs {

struct MountEntry {
    const char* fsname;
    const char* mountpoint;
};

static MountEntry mounts[16];
static int mount_count = 0;

void vfs_init(void) {
    mount_count = 0;
    for (int i = 0; i < 16; ++i) {
        if (mounts[i].mountpoint) {
            hanacore::mem::kfree((void*)mounts[i].mountpoint);
        }
        mounts[i].fsname = nullptr;
        mounts[i].mountpoint = nullptr;
    }
}

void vfs_register_mount(const char* fsname, const char* mountpoint) {
    if (!fsname || !mountpoint) return;
    if (mount_count >= 16) return;
    size_t mlen = strlen(mountpoint) + 1;
    char* mcopy = (char*)hanacore::mem::kmalloc(mlen);
    if (!mcopy) return;
    memcpy(mcopy, mountpoint, mlen);
    mounts[mount_count].fsname = fsname;
    mounts[mount_count].mountpoint = mcopy;
    mount_count++;
}

int vfs_list_mounts(void (*cb)(const char* line)) {
    if (!cb) return -1;
    char buf[128];
    for (int i = 0; i < mount_count; ++i) {
        const char* f = mounts[i].fsname ? mounts[i].fsname : "";
        const char* m = mounts[i].mountpoint ? mounts[i].mountpoint : "";
        snprintf(buf, sizeof(buf), "VFS mount: [%s -> %s]", f, m);
        cb(buf);
    }
    return 0;
}

int vfs_list_dir(const char* path, void (*cb)(const char* name)) {
    if (!path || !cb) return -1;

    // Find matching mount entry
    for (int i = 0; i < mount_count; ++i) {
        const char* mpoint = mounts[i].mountpoint;
        const char* fsname = mounts[i].fsname;
        if (!mpoint || !fsname) continue;

        size_t mlen = strlen(mpoint);
        // Either exact mountpoint, or path starts with mountpoint + '/'
        if ( (strcmp(path, mpoint) == 0) ||
             (strncmp(path, mpoint, mlen) == 0 && path[mlen] == '/') ) {

            // compute relative path for backend
            const char* rel = path;
            if (!(strcmp(mpoint, "/") == 0)) {
                if (strlen(path) == mlen) {
                    rel = "/";
                } else {
                    rel = path + mlen;
                }
            }

            // Dispatch to correct backend
            if (strcmp(fsname, "fat32") == 0) {
                return fat32_list_dir(rel, cb);
            }
            if ((strcmp(fsname, "hanafs") == 0) || (strcmp(fsname, "ramfs") == 0)) {
                return ramfs_list_dir(rel, cb);
            }
            if (strcmp(fsname, "procfs") == 0) {
                return procfs_list_dir(rel, cb);
            }
            if (strcmp(fsname, "devfs") == 0) {
                return devfs_list_dir(rel, cb);
            }
            if (strcmp(fsname, "isofs") == 0) {
                return isofs_list_dir(rel, cb);
            }
            if (strcmp(fsname, "floppy") == 0) {
                return hanacore::fs::floppy_list_dir(rel, cb);
            }
            // Unknown fsname, break out and fallback
            break;
        }
    }

    // No mounted filesystem matched — fallback to built-ins
    int rc = ramfs_list_dir(path, cb);
    if (rc == 0) return 0;
    rc = fat32_list_dir(path, cb);
    if (rc == 0) return 0;
    return -1;
}

int vfs_remove_dir(const char* path) {
    if (!path) return -1;

    for (int i = 0; i < mount_count; ++i) {
        const char* mpoint = mounts[i].mountpoint;
        const char* fsname = mounts[i].fsname;
        if (!mpoint || !fsname) continue;

        size_t mlen = strlen(mpoint);
        if ((strcmp(path, mpoint) == 0) ||
            (strncmp(path, mpoint, mlen) == 0 && path[mlen] == '/')) {

            if ((strcmp(fsname, "hanafs") == 0) || (strcmp(fsname, "ramfs") == 0)) {
                return ramfs_remove_dir(path);
            }
            // Other FS remove_dir may not be implemented
            return -1;
        }
    }

    return ramfs_remove_dir(path);
}

int vfs_create_file(const char* path) {
    if (!path) return -1;

    for (int i = 0; i < mount_count; ++i) {
        const char* mpoint = mounts[i].mountpoint;
        const char* fsname = mounts[i].fsname;
        if (!mpoint || !fsname) continue;

        size_t mlen = strlen(mpoint);
        if ((strcmp(path, mpoint) == 0) ||
            (strncmp(path, mpoint, mlen) == 0 && path[mlen] == '/')) {

            if ((strcmp(fsname, "hanafs") == 0) || (strcmp(fsname, "ramfs") == 0)) {
                return ramfs_create_file(path);
            }
            if (strcmp(fsname, "fat32") == 0) {
                return fat32_create_file(path);
            }
            return -1;
        }
    }

    return ramfs_create_file(path);
}

int vfs_unlink(const char* path) {
    if (!path) return -1;

    for (int i = 0; i < mount_count; ++i) {
        const char* mpoint = mounts[i].mountpoint;
        const char* fsname = mounts[i].fsname;
        if (!mpoint || !fsname) continue;

        size_t mlen = strlen(mpoint);
        if ((strcmp(path, mpoint) == 0) ||
            (strncmp(path, mpoint, mlen) == 0 && path[mlen] == '/')) {

            if ((strcmp(fsname, "hanafs") == 0) || (strcmp(fsname, "ramfs") == 0)) {
                return ramfs_unlink(path);
            }
            if (strcmp(fsname, "fat32") == 0) {
                return fat32_unlink(path);
            }
            return -1;
        }
    }

    return ramfs_unlink(path);
}

int vfs_make_dir(const char* path) {
    if (!path) return -1;

    for (int i = 0; i < mount_count; ++i) {
        const char* mpoint = mounts[i].mountpoint;
        const char* fsname = mounts[i].fsname;
        if (!mpoint || !fsname) continue;

        size_t mlen = strlen(mpoint);
        if ((strcmp(path, mpoint) == 0) ||
            (strncmp(path, mpoint, mlen) == 0 && path[mlen] == '/')) {

            if ((strcmp(fsname, "hanafs") == 0) || (strcmp(fsname, "ramfs") == 0)) {
                return ramfs_make_dir(path);
            }
            if (strcmp(fsname, "fat32") == 0) {
                return fat32_make_dir(path);
            }
            return -1;
        }
    }

    return ramfs_make_dir(path);
}

int vfs_write_file(const char* path, const void* buf, size_t len) {
    if (!path) return -1;

    for (int i = 0; i < mount_count; ++i) {
        const char* mpoint = mounts[i].mountpoint;
        const char* fsname = mounts[i].fsname;
        if (!mpoint || !fsname) continue;

        size_t mlen = strlen(mpoint);
        if ((strcmp(path, mpoint) == 0) ||
            (strncmp(path, mpoint, mlen) == 0 && path[mlen] == '/')) {

            if ((strcmp(fsname, "hanafs") == 0) || (strcmp(fsname, "ramfs") == 0)) {
                return ramfs_write_file(path, buf, len);
            }
            if (strcmp(fsname, "fat32") == 0) {
                return fat32_write_file(path, buf, len);
            }
            return -1;
        }
    }

    return ramfs_write_file(path, buf, len);
}

void* vfs_get_file_alloc(const char* path, size_t* out_len) {
    if (!path || !out_len) return nullptr;

    for (int i = 0; i < mount_count; ++i) {
        const char* mpoint = mounts[i].mountpoint;
        const char* fsname = mounts[i].fsname;
        if (!mpoint || !fsname) continue;

        size_t mlen = strlen(mpoint);
        if ((strcmp(path, mpoint) == 0) ||
            (strncmp(path, mpoint, mlen) == 0 && path[mlen] == '/')) {

            const char* rel = path;
            if (!(strcmp(mpoint, "/") == 0)) {
                if (strlen(path) == mlen) {
                    rel = "/";
                } else {
                    rel = path + mlen;
                }
            }

            if (strcmp(fsname, "fat32") == 0) {
                size_t len = 0;
                void* data = fat32_get_file_alloc(rel, &len);
                if (data && len > 0) {
                    *out_len = len;
                    return data;
                }
                if (data) {
                    hanacore::mem::kfree(data);
                }
                return nullptr;
            }
            if ((strcmp(fsname, "hanafs") == 0) || (strcmp(fsname, "ramfs") == 0)) {
                size_t len = 0;
                void* data = ramfs_get_file_alloc(rel, &len);
                if (data && len > 0) {
                    *out_len = len;
                    return data;
                }
                if (data) {
                    hanacore::mem::kfree(data);
                }
                return nullptr;
            }
            if (strcmp(fsname, "procfs") == 0) {
                return procfs_get_file_alloc(rel, out_len);
            }
            if (strcmp(fsname, "devfs") == 0) {
                return devfs_get_file_alloc(rel, out_len);
            }
            // Unknown FS
            return nullptr;
        }
    }

    // fallback
    size_t len = 0;
    void* data = ramfs_get_file_alloc(path, &len);
    if (data && len > 0) {
        *out_len = len;
        return data;
    }
    if (data) {
        hanacore::mem::kfree(data);
    }

    data = procfs_get_file_alloc(path, &len);
    if (data && len > 0) {
        *out_len = len;
        return data;
    }
    if (data) {
        hanacore::mem::kfree(data);
    }

    data = devfs_get_file_alloc(path, &len);
    if (data && len > 0) {
        *out_len = len;
        return data;
    }
    if (data) {
        hanacore::mem::kfree(data);
    }

    data = fat32_get_file_alloc(path, &len);
    if (data && len > 0) {
        *out_len = len;
        return data;
    }
    if (data) {
        hanacore::mem::kfree(data);
    }

    *out_len = 0;
    return nullptr;
}

} // namespace fs
} // namespace hanacore

// Provide C-linkage wrappers for all VFS functions
extern "C" {
    void vfs_init(void) {
        hanacore::fs::vfs_init();
    }

    void vfs_register_mount(const char* fsname, const char* mountpoint) {
        hanacore::fs::vfs_register_mount(fsname, mountpoint);
    }

    int vfs_list_mounts(void (*cb)(const char* line)) {
        return hanacore::fs::vfs_list_mounts(cb);
    }

    int vfs_list_dir(const char* path, void (*cb)(const char* name)) {
        return hanacore::fs::vfs_list_dir(path, cb);
    }

    int vfs_make_dir(const char* path) {
        return hanacore::fs::vfs_make_dir(path);
    }

    int vfs_remove_dir(const char* path) {
        return hanacore::fs::vfs_remove_dir(path);
    }

    int vfs_create_file(const char* path) {
        return hanacore::fs::vfs_create_file(path);
    }

    int vfs_unlink(const char* path) {
        return hanacore::fs::vfs_unlink(path);
    }

    int vfs_write_file(const char* path, const void* buf, size_t len) {
        return hanacore::fs::vfs_write_file(path, buf, len);
    }

    void* vfs_get_file_alloc(const char* path, size_t* out_len) {
        return hanacore::fs::vfs_get_file_alloc(path, out_len);
    }
}
