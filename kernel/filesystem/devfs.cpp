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
        if (strcmp(path, "/") == 0) path = "/dev";
        if (strcmp(path, "/dev") != 0 && strcmp(path, "/dev/") != 0) return -1;
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
        if (strcmp(path, "/dev/null") == 0 || strcmp(path, "null") == 0) {
            // /dev/null reads as empty
            *out_len = 0;
            return hanacore::mem::kmalloc(1); // empty buffer
        }
        if (strcmp(path, "/dev/console") == 0 || strcmp(path, "console") == 0) {
            const char* s = "console\n";
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
