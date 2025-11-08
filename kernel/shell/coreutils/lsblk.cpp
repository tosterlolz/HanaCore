#include "../../filesystem/fat32.hpp"
#include "../../filesystem/hanafs.hpp"
#include "../../drivers/ide.hpp"
#include "../../libs/libc.h"
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
    // ignore unused argument; list both FAT32 and HanaFS mounts and show ATA info
    hanacore::fs::fat32_list_mounts(lsblk_cb);
    hanacore::fs::hanafs_list_mounts(lsblk_cb);

    // Report ATA master availability and size
    int32_t secs = ata_get_sector_count();
    if (secs > 0) {
        char buf[128];
        // report size in sectors and approximate MiB
        int32_t mib = secs / 2048; // 2048 sectors * 512 = 1 MiB
        snprintf(buf, sizeof(buf), "ATA master: %u sectors (~%u MiB)", (unsigned)secs, (unsigned)mib);
        print(buf); print("\n");
    } else {
        print("ATA master: no device detected\n");
    }

    // Report ATA slave (possible CD-ROM) availability and try to detect ISO9660
    int32_t slave_secs = ata_get_sector_count_drive(1);
    if (slave_secs > 0) {
        char buf[128];
        int32_t mib = slave_secs / 2048;
        snprintf(buf, sizeof(buf), "ATA slave: %u sectors (~%u MiB)", (unsigned)slave_secs, (unsigned)mib);
        print(buf); print("\n");
    } else {
        // Try a light probe for ISO9660: read sector 16 and look for "CD001" at offset 1
        unsigned char sec[512];
        int r = ata_read_sector_drive(1, 16, sec);
        if (r == 0 && memcmp(&sec[1], "CD001", 5) == 0) {
            print("ATA slave: ISO9660 media detected (CD-ROM)\n");
        } else {
            print("ATA slave: no device detected\n");
        }
    }
}
