#include "../../drivers/ide.hpp"
#include "../../filesystem/ext3.hpp"
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

extern "C" void builtin_lsblk_cmd(const char* unused) {
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

    // Report ATA slave (possible CD-ROM) availability. Avoid probing sectors
    // directly here — raw sector reads during device enumeration have caused
    // VM instability. If the slave reports a sector count, show it; otherwise
    // report absent and ask the user to mount explicitly if needed.
    int32_t slave_secs = ata_get_sector_count_drive(1);
    if (slave_secs > 0) {
        char buf[128];
        int32_t mib = slave_secs / 2048;
        snprintf(buf, sizeof(buf), "ATA slave: %u sectors (~%u MiB)", (unsigned)slave_secs, (unsigned)mib);
        print(buf); print("\n");
    } else {
        print("ATA slave: no device detected (probe skipped). Use 'mount' to mount devices manually.\n");
    }
}
