#include "../../filesystem/fat32.hpp"
#include <stddef.h>

extern "C" void print(const char*);

// callback used by fat32_list_mounts
static void lsblk_cb(const char* line) {
    if (!line) return;
    // color: cyan for device
    print("\x1b[36m");
    print(line);
    print("\x1b[0m\n");
}

extern "C" void builtin_lsblk_cmd(const char* unused) {
    // ignore unused argument; just list mounts
    hanacore::fs::fat32_list_mounts(lsblk_cb);
}
