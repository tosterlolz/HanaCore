#include "procfs.hpp"
#include "vfs.hpp"
#include <string.h>
#include "../libs/libc.h"
#include "../utils/logger.hpp"
#include "../mem/heap.hpp"

// Minimal procfs initialiser. Registers /proc with the VFS so users/tools
// can see it listed. Full read handlers are not implemented in this stub.

namespace hanacore { namespace fs {

    void procfs_init(void) {
        // Register procfs mount point
        vfs_register_mount("procfs", "/proc");
        hanacore::utils::log_info_cpp("[procfs] initialized and mounted at /proc");
    }

    // Simple listing implementation: when asked for /proc return a few
    // diagnostic entries. For subpaths return -1 (not implemented).
    int procfs_list_dir(const char* path, void (*cb)(const char* name)) {
        if (!path || !cb) return -1;
        // normalize accepted path roots
        if (strcmp(path, "/") == 0) path = "/proc"; // unlikely, but safe
        if (strcmp(path, "/proc") != 0 && strcmp(path, "/proc/") != 0) return -1;
        cb("cpuinfo");
        cb("meminfo");
        cb("self");
        return 0;
    }

    // Minimal file read support for a couple of /proc pseudo-files
    void* procfs_get_file_alloc(const char* path, size_t* out_len) {
        if (!path || !out_len) return NULL;
        if (strcmp(path, "/proc/cpuinfo") == 0 || strcmp(path, "cpuinfo") == 0) {
            const char* s = "HanaCore CPU: 1 core\n";
            size_t n = strlen(s);
            char* b = (char*)hanacore::mem::kmalloc(n+1);
            if (!b) return NULL;
            memcpy(b, s, n+1);
            *out_len = n;
            return b;
        }
        if (strcmp(path, "/proc/meminfo") == 0 || strcmp(path, "meminfo") == 0) {
            const char* s = "MemTotal: minimal\nMemFree: unknown\n";
            size_t n = strlen(s);
            char* b = (char*)hanacore::mem::kmalloc(n+1);
            if (!b) return NULL;
            memcpy(b, s, n+1);
            *out_len = n;
            return b;
        }
        if (strcmp(path, "/proc/self") == 0 || strcmp(path, "self") == 0) {
            const char* s = "1\n";
            size_t n = strlen(s);
            char* b = (char*)hanacore::mem::kmalloc(n+1);
            if (!b) return NULL;
            memcpy(b, s, n+1);
            *out_len = n;
            return b;
        }
        return NULL;
    }

} }
