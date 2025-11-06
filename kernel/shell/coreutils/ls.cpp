#include "../../filesystem/ext2.hpp"
#include <stddef.h>

extern "C" void print(const char*);

// callback for ext2_list_dir
static void ls_cb(const char* name) {
    print(name);
    print("\n");
}

extern "C" void builtin_ls_cmd(const char* path) {
    if (!path) {
        print("ls: null path\n");
        return;
    }
    int r = ext2_list_dir(path, ls_cb);
    if (r < 0) {
        print("ls: failed to list ");
        print(path);
        print("\n");
    }
}
