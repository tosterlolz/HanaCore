#include "../../drivers/ide.hpp"
#include "../../filesystem/ext3.hpp"
#include "../../filesystem/hanafs.hpp"
#include "../../filesystem/fat32.hpp"
#include "../../filesystem/vfs.hpp"
#include "../../libs/libc.h"
#include <stddef.h>

extern "C" void print(const char*);

// callback used by ext3_list_mounts
static void lsblk_cb(const char* line) {
    if (!line) return;
    // color: cyan for device
    print("\x1b[36m");
    print(line);
    print("\x1b[0m\n");
}

// internal flag used by callbacks to indicate we printed at least one mount
static int lsblk_found_mounts = 0;

static void lsblk_mount_cb(const char* line) {
    if (!line) return;
    lsblk_found_mounts = 1;
    // reuse the pretty-print callback
    lsblk_cb(line);
}

extern "C" void builtin_lsblk_cmd(const char* unused) {
    // Probe of raw ATA sectors can destabilize some virtual machines
    // (causes VM crashes in VirtualBox). Skip direct probing here and
    // instead instruct the user to mount devices explicitly using the
    // `mount` builtin. The VFS mount list below will show any mounted
    // devices.
    print("ATA master: probe skipped (use 'mount' to attach devices)\n");
    print("ATA slave: probe skipped (use 'mount' to attach devices)\n");

    // Print VFS-registered mounts only. Avoid probing ATA or device hardware
    // here — direct ATA probing destabilizes some VMs. The `mount` builtin
    // should be used to attach devices explicitly; those mounts will be
    // visible via the VFS registry.
    lsblk_found_mounts = 0;
    hanacore::fs::vfs_list_mounts(lsblk_mount_cb);

    if (!lsblk_found_mounts) {
        print("[NO FS]\n");
    }
}
