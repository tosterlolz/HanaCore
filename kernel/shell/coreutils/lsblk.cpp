#include "../../drivers/ide.hpp"
#include "../../filesystem/ext3.hpp"
#include "../../filesystem/vfs.hpp"
#include "../../filesystem/fat32.hpp"
#include "../../filesystem/hanafs.hpp"
#include "../../libs/libc.h"
#include <stddef.h>
#include <string.h>

extern "C" void print(const char*);

// Simple lsblk implementation that avoids raw ATA probing (unsafe in some
// VMs) and instead lists device nodes present under /dev via the VFS,
// then prints known mounts from VFS/FAT32/HanaFS. This shows unmounted
// devices and a readable mounts section.

struct StringList { const char* items[64]; int count; };

static void collect_dev_cb(const char* name, void* ctx) {
    if (!name || !ctx) return;
    StringList* list = (StringList*)ctx;
    if (list->count < 64) list->items[list->count++] = name;
}

static void collect_mount_cb(const char* line, void* ctx) {
    if (!line || !ctx) return;
    StringList* list = (StringList*)ctx;
    if (list->count < 64) list->items[list->count++] = line;
}

extern "C" void builtin_lsblk_cmd(const char* unused) {
    (void)unused;
    // Header
    print("NAME        MAJ:MIN RM   SIZE RO TYPE MOUNTPOINTS\n");

    // Collect device node names from /dev via VFS
    StringList devs; devs.count = 0;
    hanacore::fs::vfs_list_dir("/dev", [](const char* name){
        // vfs_list_dir expects a callback 'void (*cb)(const char*)' so use a
        // small trampoline that appends into our static StringList via a
        // temporary global — to avoid introducing globals we instead call
        // a helper that prints directly. Simpler: print device entries
        // directly here by reusing print().
        char buf[128];
        // NAME, MAJ:MIN, RM, SIZE, RO, TYPE
        snprintf(buf, sizeof(buf), "%-11s %5s %2s %6s %2s %4s\n", name, "0:0", "0", "unknown", "0", "disk");
        print(buf);
    });

    // Now print mounted filesystems collected from VFS/FAT32/HanaFS
    print("\nMOUNTPOINTS:\n");
    // VFS mounts
    hanacore::fs::vfs_list_mounts([](const char* line){ if (line) { print(" "); print(line); print("\n"); } });
    // FAT32 mounts
    hanacore::fs::fat32_list_mounts([](const char* line){ if (line) { print(" "); print(line); print("\n"); } });
    // HanaFS mounts
    hanacore::fs::hanafs_list_mounts([](const char* line){ if (line) { print(" "); print(line); print("\n"); } });

    // If no mounts, say so
    // (vfs_list_mounts prints nothing if none)
}
