#include "../../filesystem/hanafs.hpp"
#include <stddef.h>

extern "C" void print(const char*);

extern "C" void builtin_format_cmd(const char* arg) {
    if (!arg || arg[0] == '\0') {
        print("usage: format [disk]\n");
        return;
    }

    // Very small parsing: accept "ata", "ata0", "master", "0" as primary
    bool is_master = false;
    if (arg[0] == 'a' || arg[0] == 'A') {
        // starts with "ata"
        is_master = true;
    } else if (arg[0] == 'm' || arg[0] == 'M') {
        // "master"
        is_master = true;
    } else if (arg[0] >= '0' && arg[0] <= '9') {
        // numeric device id 0 -> master
        if (arg[0] == '0') is_master = true;
    }

    if (!is_master) {
        print("format: only primary ATA master supported in this build\n");
        return;
    }

    print("Formatting ATA master as HanaFS (this will erase data)...\n");
    int rc = hanacore::fs::hanafs_format_ata_master(0);
    if (rc == 0) {
        print("format: completed successfully\n");
    } else {
        print("format: failed (see kernel logs)\n");
    }
}
