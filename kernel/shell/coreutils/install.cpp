// Copy an embedded rootfs image (Limine module) onto an ATA device (raw copy).
// Usage: install <disk>
// Example: `install 0:` will write the bundled rootfs image to ATA device 0.

#include <stddef.h>
#include <stdint.h>
#include "../../filesystem/fat32.hpp"
#include "../../drivers/ide.hpp"
#include "../../libs/libc.h"
#include "../../../limine/limine.h"

extern "C" void print(const char*);

// Limine module requests (same as used in fat32.cpp)
extern volatile struct limine_hhdm_request limine_hhdm_request;
extern volatile struct limine_module_request module_request;

extern "C" void builtin_install_cmd(const char* arg) {
    if (!arg || arg[0] == '\0') {
        print("usage: install <disk>\nExample: install 0:\\n");
        return;
    }

    // parse disk (support '0' or 'ata' as master)
    int drive_number = 0;
    bool is_master = false;
    if (arg[0] >= '0' && arg[0] <= '9') {
        if (arg[0] == '0') is_master = true;
    } else if (arg[0] == 'a' || arg[0] == 'A' || arg[0] == 'm' || arg[0] == 'M') {
        is_master = true;
    }

    if (!is_master) {
        print("install: only ATA master (0:) supported in this build\n");
        return;
    }

    // Helper: ends_with (small local copy)
    auto local_ends_with = [](const char* s, const char* suffix) {
        if (!s || !suffix) return false;
        const char* ps = s; size_t sl = 0; while (ps[sl]) ++sl;
        const char* pf = suffix; size_t fl = 0; while (pf[fl]) ++fl;
        if (fl > sl) return false;
        const char* start = s + (sl - fl);
        for (size_t i = 0; i < fl; ++i) if (start[i] != suffix[i]) return false;
        return true;
    };

    // Find a Limine module that looks like a rootfs image
    if (!module_request.response) {
        print("install: no Limine modules available (rootfs image not found)\n");
        return;
    }

    volatile struct limine_module_response* resp = module_request.response;
    const void* img_ptr = NULL;
    size_t img_size = 0;
    for (uint64_t i = 0; i < resp->module_count; ++i) {
        volatile struct limine_file* mod = resp->modules[i];
        const char* path = (const char*)(uintptr_t)mod->path;
        if (path && limine_hhdm_request.response) {
            uint64_t hoff = limine_hhdm_request.response->offset;
            if ((uint64_t)path < hoff) path = (const char*)((uintptr_t)path + hoff);
        }
    if (!path) continue;
    // match likely names
    if (local_ends_with(path, "rootfs.img") || local_ends_with(path, "rootfs") || local_ends_with(path, "rootfs.iso")) {
            uintptr_t mod_addr = (uintptr_t)mod->address;
            const void* mod_virt = (const void*)mod_addr;
            if (limine_hhdm_request.response) {
                uint64_t off = limine_hhdm_request.response->offset;
                if ((uint64_t)mod_addr < off) mod_virt = (const void*)(off + mod_addr);
            }
            img_ptr = mod_virt;
            img_size = (size_t)mod->size;
            break;
        }
    }

    if (!img_ptr || img_size == 0) {
        print("install: rootfs image module not found\n");
        return;
    }

    // Optionally format the disk first
    print("Formatting target disk (ATA master)...\n");
    int rc = fat32_format_ata_master(drive_number);
    if (rc != 0) {
        print("install: format failed\n");
        return;
    }

    print("install: writing image to disk...\n");
    const uint8_t* data = (const uint8_t*)img_ptr;
    const size_t sector = 512;
    uint32_t sectors = (uint32_t)((img_size + sector - 1) / sector);
    uint8_t buf[512];
    for (uint32_t s = 0; s < sectors; ++s) {
        size_t off = (size_t)s * sector;
        size_t to_copy = ((off + sector) <= img_size) ? sector : (img_size - off);
        // zero buffer then copy remainder
        for (size_t i = 0; i < sector; ++i) buf[i] = 0;
        for (size_t i = 0; i < to_copy; ++i) buf[i] = data[off + i];

        int w = ata_write_sector(s, buf);
        if (w != 0) {
            print("install: write failed (see logs)\n");
            return;
        }

        // simple progress indicator every 100 sectors
        if ((s & 0x3F) == 0) {
            print(".");
        }
    }

    print("\ninstall: completed successfully\n");
}

// bring in helper used above
static bool ends_with(const char* s, const char* suffix) {
    if (!s || !suffix) return false;
    const char* ps = s; size_t sl = 0; while (ps[sl]) ++sl;
    const char* pf = suffix; size_t fl = 0; while (pf[fl]) ++fl;
    if (fl > sl) return false;
    const char* start = s + (sl - fl);
    for (size_t i = 0; i < fl; ++i) if (start[i] != suffix[i]) return false;
    return true;
}
