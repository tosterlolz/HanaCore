#include "../../filesystem/fat32.hpp"
#include <stddef.h>

extern "C" void print(const char*);
extern "C" void fat32_progress_update(int percent); // weak symbol provided by fat32

// Callback for fat32_list_dir
static void ls_cb(const char* name) {
    if (!name) return;

    // Copy safely into a small local buffer
    char tmp[64];
    size_t i = 0;
    while (i + 1 < sizeof(tmp) && name[i]) {
        tmp[i] = name[i];
        ++i;
    }
    tmp[i] = '\0';

    print(tmp);
    print("\n");
}

extern "C" void builtin_ls_cmd(const char* path) {
    if (!path) {
        print("\nls: null path\n");
        return;
    }

    print("\nListing directory: ");
    print(path);
    print("\n");

    int rc = hanacore::fs::fat32_list_dir(path, ls_cb);
    if (rc != 0) {
        print("\nls: failed to list directory (check mount or cluster)\n");
    }
}
