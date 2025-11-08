#include "../../filesystem/vfs.hpp"
#include "../../filesystem/devfs.hpp"
#include "../../filesystem/vfs.hpp"
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

    // Collect device node names from the pseudo `devfs` (no raw ATA probing)
    hanacore::fs::devfs_list_dir("/dev", print_dev_entry);

    // Now print mounted filesystems collected from the VFS registry only
    // (avoid directly calling backend-specific mount enumerators here to
    // reduce risk of invoking complex backend code while listing).
    print("\nMOUNTPOINTS:\n");
    hanacore::fs::vfs_list_mounts(print_mount_line);
}
