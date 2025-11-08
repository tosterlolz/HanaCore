#include "../../filesystem/vfs.hpp"
#include <stddef.h>
#include <cstring>
#include "../../tty/tty.hpp"

extern "C" {
    void tty_write(const char*);
}

static const char* ls_base_dir = nullptr;

static void ls_cb(const char* name, int is_dir) {
    if (!name) return;

    char full[256];
    size_t blen = strlen(ls_base_dir);
    if (blen + strlen(name) + 2 < sizeof(full)) {
        strcpy(full, ls_base_dir);
        if (blen > 1 && full[blen-1] != '/') strcat(full, "/");
        strcat(full, name);
    } else {
        strcpy(full, name);
    }

    if (is_dir) {
        tty_write("\x1b[34m"); // blue
        tty_write(name);
        tty_write("/\x1b[0m  ");
    } else {
        tty_write(name);
        tty_write("  ");
    }
}

extern "C" void builtin_ls_cmd(const char* path) {
    if (!path) {
        tty_write("ls: null path\n");
        return;
    }

    tty_write("Listing directory: ");
    tty_write(path);
    tty_write("\n");

    ls_base_dir = path;

    int rc = hanacore::fs::vfs_list_dir(path, [](const char* name, int type) {
        int is_dir = (type & VFS_TYPE_DIR) ? 1 : 0;
        ls_cb(name, is_dir);
    });

    ls_base_dir = nullptr;

    if (rc != 0) {
        tty_write("\nls: failed to list directory or directory is empty\n");
    } else {
        tty_write("\n");
    }
}
