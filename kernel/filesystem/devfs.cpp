#include "devfs.hpp"
#include "vfs.hpp"
#include "../libs/libc.h"
#include "../utils/logger.hpp"
#include "../mem/heap.hpp"

namespace hanacore { namespace fs {

    void devfs_init(void) {
        // Register devfs mount
        vfs_register_mount("devfs", "/dev");
        hanacore::utils::log_info_cpp("[devfs] initialized and mounted at /dev");
    }

    int devfs_list_dir(const char* path, void (*cb)(const char* name)) {
        if (!path || !cb) return -1;
        // Normalize incoming path: callers may pass "/dev", "/", "/tty0" (relative)
        const char* norm = path;
        if (strcmp(path, "/") == 0) {
            norm = "/dev";
        } else if (path[0] == '/' && strncmp(path, "/dev", 4) != 0) {
            // path like "/tty0" -> treat as "/dev/tty0" for normalization
            // but listing a specific device is not supported (not a directory)
            // so return -1 to indicate failure to list.
            return -1;
        } else if (strncmp(path, "dev", 3) == 0) {
            // allow callers to pass "dev" without leading slash
            norm = "/dev";
        }
        if (strcmp(norm, "/dev") != 0 && strcmp(norm, "/dev/") != 0) return -1;
        // Provide a few standard device nodes
        cb("console");
        cb("null");
        cb("tty0");
        cb("hda");
        cb("sda");
        return 0;
    }

    void* devfs_get_file_alloc(const char* path, size_t* out_len) {
        if (!path || !out_len) return NULL;
        // Normalize simple forms: accept "/dev/console", "console", "/console"
        const char* p = path;
        if (p[0] == '/') {
            // /dev/console -> keep, /console -> treat as /dev/console
            if (strncmp(p, "/dev/", 5) == 0) {
                // p is fine
            } else if (p[1] != '\0') {
                // e.g. "/console" -> map to "console"
                p = p + 1;
            }
        }
        // strip leading "dev/" if present
        if (strncmp(p, "dev/", 4) == 0) p = p + 4;

        if (strcmp(p, "null") == 0) {
            // /dev/null reads as empty: indicate zero-length by returning NULL
            *out_len = 0;
            return NULL;
        }

        if (strcmp(p, "console") == 0) {
            const char* s = "console\n";
            size_t n = strlen(s);
            char* b = (char*)hanacore::mem::kmalloc(n+1);
            if (!b) return NULL;
            // ensure buffer is null-terminated and contents are correct
            memcpy(b, s, n+1);
            *out_len = n;
            return b;
        }

        // Unknown device or not a regular file
        *out_len = 0;
        return NULL;
    }

} }
