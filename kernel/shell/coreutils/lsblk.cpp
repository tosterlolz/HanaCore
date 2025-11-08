#include "../../filesystem/vfs.hpp"
#include "../../filesystem/devfs.hpp"
#include "../../filesystem/hanafs.hpp"
#include "../../filesystem/fat32.hpp"
#include "../../libs/libc.h"
#include <stddef.h>
#include <string.h>

extern "C" void print(const char*);

static void print_dev_entry(const char* name) {
    if (!name) return;
    print(name);
    print("\n");
}

static void print_mount_line(const char* line) {
    if (!line) return;
    print(" ");
    print(line);
    print("\n");
}

extern "C" void builtin_lsblk_cmd(const char* unused) {
    (void)unused;
    print("NAME        MAJ:MIN RM   SIZE RO TYPE MOUNTPOINTS\n");

    int dcrc = hanacore::fs::list_dir("/dev", print_dev_entry);
    if (dcrc != 0) {
        print("lsblk: failed to list /dev\n");
    }

    print("\nMOUNTPOINTS:\n");
    hanacore::fs::list_mounts(print_mount_line);
    hanacore::fs::hanafs_list_mounts(print_mount_line);
    hanacore::fs::fat32_list_mounts(print_mount_line);
}
