#include "../../filesystem/vfs.hpp"
#include "../../filesystem/devfs.hpp"
#include "../../filesystem/hanafs.hpp"
#include "../../filesystem/fat32.hpp"
#include "../../libs/libc.h"
#include <stddef.h>
#include <string.h>

extern "C" void print(const char*);

// Simple lsblk implementation that avoids raw ATA probing (unsafe in some
// VMs) and instead lists device nodes present under /dev via the VFS,
// then prints known mounts from VFS/FAT32/HanaFS. This shows unmounted
// devices and a readable mounts section.

static void print_dev_entry(const char* name) {
    if (!name) return;
    // Print a simple, safe device name line to avoid complex formatting
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
    // Header
    print("NAME        MAJ:MIN RM   SIZE RO TYPE MOUNTPOINTS\n");

    // Collect device node names via VFS so any registered devfs handler is
    // invoked (safer than calling devfs directly and keeps behaviour
    // consistent with other directory listings).
    int dcrc = hanacore::fs::vfs_list_dir("/dev", print_dev_entry);
    if (dcrc != 0) {
        print("lsblk: failed to list /dev\n");
    }

    // Now print mounted filesystems. We include mounts registered via VFS
    // (procfs/devfs/explicit mounts) and also query backend registries
    // (HanaFS and FAT32) so `lsblk` shows both pseudo and block mounts.
    print("\nMOUNTPOINTS:\n");
    hanacore::fs::vfs_list_mounts(print_mount_line);
    hanacore::fs::hanafs_list_mounts(print_mount_line);
    hanacore::fs::fat32_list_mounts(print_mount_line);
}
