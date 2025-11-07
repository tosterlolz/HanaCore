#include "fat32.hpp"
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include "../libs/nanoprintf.h"

#include "../utils/logger.hpp"
#include "../libs/libc.h"

// If the kernel was started with Limine modules, we can mount a rootfs image
// directly from memory instead of relying on ATA devices (which aren't
// necessarily present when running from an ISO). Include the Limine header
// and declare the request objects.
#include "../../limine/limine.h"

extern volatile struct limine_hhdm_request limine_hhdm_request;
extern volatile struct limine_module_request module_request;

extern "C" int ata_read_sector(uint32_t lba, void* buf); // implemented by ATA driver (may be provided elsewhere)

/* Fallback weak implementation: if a real ATA driver is linked in, it will override
   this weak symbol. If not present, the fallback returns error. */
extern "C" int __attribute__((weak)) ata_read_sector(uint32_t lba, void* buf) {
    (void)lba; (void)buf;
    return -1;
}

static uint32_t parse_uint32(const char* s) {
    uint32_t v = 0;
    if (!s) return 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (uint32_t)(*s - '0');
        ++s;
    }
    return v;
}

namespace hanacore {
namespace fs {

// =================== FAT32 Structures ===================

#pragma pack(push, 1)
struct BPB_FAT32 {
    uint8_t  jmpBoot[3];
    uint8_t  OEMName[8];
    uint16_t BytsPerSec;
    uint8_t  SecPerClus;
    uint16_t RsvdSecCnt;
    uint8_t  NumFATs;
    uint16_t RootEntCnt;
    uint16_t TotSec16;
    uint8_t  Media;
    uint16_t FATSz16;
    uint16_t SecPerTrk;
    uint16_t NumHeads;
    uint32_t HiddSec;
    uint32_t TotSec32;
    uint32_t FATSz32;
    uint16_t ExtFlags;
    uint16_t FSVer;
    uint32_t RootClus;
    uint16_t FSInfo;
    uint16_t BkBootSec;
    uint8_t  Reserved[12];
    uint8_t  DrvNum;
    uint8_t  Reserved1;
    uint8_t  BootSig;
    uint32_t VolID;
    uint8_t  VolLab[11];
    uint8_t  FilSysType[8];
};
#pragma pack(pop)

// =================== State ===================

static BPB_FAT32 bpb;
static bool fat32_ready = false;
static uint32_t fat_begin_lba;
static uint32_t cluster_begin_lba;
static uint32_t sectors_per_cluster;
static uint32_t bytes_per_sector;
static uint32_t root_dir_cluster;

// =================== Helpers ===================

// Simple helper: check whether `s` ends with `suffix` (NUL-terminated strings)
static bool ends_with(const char* s, const char* suffix) {
    if (!s || !suffix) return false;
    const char* ps = s; size_t sl = 0; while (ps[sl]) ++sl;
    const char* pf = suffix; size_t fl = 0; while (pf[fl]) ++fl;
    if (fl > sl) return false;
    const char* start = s + (sl - fl);
    for (size_t i = 0; i < fl; ++i) if (start[i] != suffix[i]) return false;
    return true;
}

static inline uint32_t cluster_to_lba(uint32_t cluster) {
    return cluster_begin_lba + (cluster - 2) * sectors_per_cluster;
}

static uint32_t read_fat_entry(uint32_t cluster) {
    uint8_t sector[512];
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_begin_lba + (fat_offset / bytes_per_sector);
    uint32_t ent_offset = fat_offset % bytes_per_sector;
    if (bytes_per_sector > sizeof(sector)) {
        hanacore::utils::log_ok_cpp("[FAT32] unsupported bytes_per_sector > 512");
        return 0x0FFFFFF7;
    }
    if (ata_read_sector(fat_sector, sector) != 0)
        return 0x0FFFFFF7; // mark as bad cluster on read error
    uint32_t val = *(uint32_t*)&sector[ent_offset];
    val &= 0x0FFFFFFF;
    return val;
}


// =================== Initialization ===================

int fat32_init_from_module(const char* module_name) {
    // If Limine provided modules, try to find one matching module_name
    if (module_request.response && module_name) {
        volatile struct limine_module_response* resp = module_request.response;
        for (uint64_t i = 0; i < resp->module_count; ++i) {
            volatile struct limine_file* mod = resp->modules[i];
            const char* path = (const char*)(uintptr_t)mod->path;
            if (path && limine_hhdm_request.response) {
                uint64_t hoff = limine_hhdm_request.response->offset;
                if ((uint64_t)path < hoff) path = (const char*)((uintptr_t)path + hoff);
            }
            if (!path) continue;
            // match exact module name or path ending
            if (ends_with(path, module_name) || strcmp(path, module_name) == 0) {
                uintptr_t mod_addr = (uintptr_t)mod->address;
                const void* mod_virt = (const void*)mod_addr;
                if (limine_hhdm_request.response) {
                    uint64_t off = limine_hhdm_request.response->offset;
                    if ((uint64_t)mod_addr < off) mod_virt = (const void*)(off + mod_addr);
                }
                if (fat32_init_from_memory(mod_virt, (size_t)mod->size) == 0) {
                    return 0;
                }
                // otherwise continue to try ATA fallback
                break;
            }
        }
    }
    // If we reach here, either there are no Limine modules or none matched
    // the requested module_name. Do not attempt to read ATA here — the
    // caller should decide whether to fall back to ATA. Return -1 to
    // indicate module-based init did not occur.
    return -1;
}

// Initialize FS by reading sector 0 from ATA (legacy path).
int fat32_init_from_ata() {
    uint8_t sector[512];
    if (ata_read_sector(0, sector) != 0) {
        hanacore::utils::log_info_cpp("[FAT32] Failed to read boot sector from ATA device");
        return -1;
    }

    memcpy(&bpb, sector, sizeof(BPB_FAT32));

    bytes_per_sector     = bpb.BytsPerSec;
    sectors_per_cluster  = bpb.SecPerClus;
    fat_begin_lba        = bpb.RsvdSecCnt;
    cluster_begin_lba    = bpb.RsvdSecCnt + bpb.NumFATs * bpb.FATSz32;
    root_dir_cluster     = bpb.RootClus;

    fat32_ready = true;

    hanacore::utils::log_ok_cpp("[FAT32] Initialized from ATA device");
    {
        char tmp[128];
        npf_snprintf(tmp, sizeof(tmp), "[FAT32] %u bytes/sector, %u sectors/cluster, root cluster=%u",
                     bytes_per_sector, sectors_per_cluster, root_dir_cluster);
        hanacore::utils::log_ok_cpp(tmp);
    }
    return 0;
}

int fat32_init_from_memory(const void* data, size_t size) {
    if (!data || size < 512) return -1;
    const uint8_t* d = (const uint8_t*)data;
    uint8_t sector[512];
    // copy first sector (boot sector / BPB)
    for (size_t i = 0; i < 512; ++i) sector[i] = (i < size) ? d[i] : 0;

    memcpy(&bpb, sector, sizeof(BPB_FAT32));

    bytes_per_sector     = bpb.BytsPerSec;
    sectors_per_cluster  = bpb.SecPerClus;
    fat_begin_lba        = bpb.RsvdSecCnt;
    cluster_begin_lba    = bpb.RsvdSecCnt + bpb.NumFATs * bpb.FATSz32;
    root_dir_cluster     = bpb.RootClus;

    // Basic validation of BPB values
    if (bytes_per_sector == 0 || bytes_per_sector > 4096) {
        hanacore::utils::log_info_cpp("[FAT32] init_from_memory: unsupported bytes_per_sector");
        return -1;
    }
    if (sectors_per_cluster == 0 || sectors_per_cluster > 128) {
        hanacore::utils::log_info_cpp("[FAT32] init_from_memory: invalid sectors_per_cluster");
        return -1;
    }

    fat32_ready = true;
    hanacore::utils::log_ok_cpp("[FAT32] Initialized from memory module");
    return 0;
}

// =================== Directory Listing ===================

int fat32_list_dir(const char* path, void (*cb)(const char* name)) {
    if (!cb) {
        hanacore::utils::log_info_cpp("[FAT32] list_dir: null callback");
        return -1;
    }
    if (!fat32_ready) {
        char tmp[128];
        const char* p = path ? path : "(null)";
        npf_snprintf(tmp, sizeof(tmp), "[FAT32] list_dir: filesystem not ready (path=%s)", p);
        hanacore::utils::log_info_cpp(tmp);
        return -1;
    }

    (void)path;

    {
        char tmp[128];
        npf_snprintf(tmp, sizeof(tmp),
                     "[FAT32] list_dir: fat_ready=%d bytes/sector=%u sectors/cluster=%u fat_begin=%u cluster_begin=%u root=%u",
                     fat32_ready ? 1 : 0, bytes_per_sector, sectors_per_cluster, fat_begin_lba, cluster_begin_lba, root_dir_cluster);
        hanacore::utils::log_info_cpp(tmp); // print formatted buffer as plain string
    }

    uint32_t cluster = root_dir_cluster;
    if (cluster < 2) {
        char tmp[128];
        npf_snprintf(tmp, sizeof(tmp), "[FAT32] list_dir: invalid root cluster=%u", cluster);
        hanacore::utils::log_info_cpp(tmp);
        return -1;
    }

    // safe large buffer for sector contents; ensure bytes_per_sector <= 4096
    uint8_t sector[4096];
    if (bytes_per_sector == 0 || bytes_per_sector > sizeof(sector)) {
        hanacore::utils::log_info_cpp("[FAT32] unsupported bytes_per_sector (0 or >4096)");
        return -1;
    }

    while (true) {
        if (cluster == 0 || cluster == 0x0FFFFFF7 || cluster >= 0x0FFFFFF8) break;

        for (uint32_t s = 0; s < sectors_per_cluster; ++s) {
            uint32_t lba = cluster_to_lba(cluster) + s;

            int r = ata_read_sector(lba, sector);
            if (r != 0) {
                char tmp[128];
                npf_snprintf(tmp, sizeof(tmp), "[FAT32] ata_read_sector failed for lba=%u rc=%d (cluster=%u, sec=%u)", lba, r, cluster, s);
                hanacore::utils::log_info_cpp(tmp);
                return -1;
            }

            for (int i = 0; i < (int)bytes_per_sector; i += 32) {
                uint8_t first = sector[i];
                if (first == 0x00) return 0;
                if (first == 0xE5) continue;
                if ((sector[i + 11] & 0x08) != 0) continue;

                char name[13];
                // copy short name (8 bytes) and null-terminate
                memcpy(name, &sector[i], 8);
                name[8] = '\0';
                // trim trailing spaces
                for (int j = 7; j >= 0 && name[j] == ' '; --j) name[j] = '\0';

                // append extension if present
                if (sector[i + 8] != ' ') {
                    char ext[4];
                    ext[0] = sector[i + 8];
                    ext[1] = sector[i + 9];
                    ext[2] = sector[i + 10];
                    ext[3] = '\0';
                    size_t len = strlen(name);
                    if (len + 1 + 3 + 1 <= sizeof(name)) {
                        name[len++] = '.';
                        name[len++] = ext[0];
                        name[len++] = ext[1];
                        name[len++] = ext[2];
                        name[len] = '\0';
                    }
                }
                cb(name);
            }
        }

        uint32_t next = read_fat_entry(cluster);
        if (next == 0x0FFFFFF7) {
            char tmp[128];
            npf_snprintf(tmp, sizeof(tmp), "[FAT32] list_dir: bad cluster entry read for cluster=%u", cluster);
            hanacore::utils::log_info_cpp(tmp);
            break;
        }
        if (next == 0 || next >= 0x0FFFFFF8) break;
        if (next == cluster) {
            hanacore::utils::log_info_cpp("[FAT32] FAT chain loop detected — aborting");
            break;
        }
        cluster = next;
    }

    return 0;
}


// =================== File Reading ===================

int64_t fat32_read_file(const char* path, void* buf, size_t len) {
    if (!fat32_ready || !path || !buf || len == 0) {
        char tmp[128];
        npf_snprintf(tmp, sizeof(tmp), "[FAT32] read_file: invalid args or FS not ready (path=%s, len=%u)", path ? path : "(null)", (unsigned)len);
        hanacore::utils::log_info_cpp(tmp);
        return -1;
    }

    // interpret path as cluster number string (e.g. "5")
    uint32_t cluster = atoi(path);
    if (cluster < 2) {
        char tmp[128]; npf_snprintf(tmp, sizeof(tmp), "[FAT32] read_file: invalid cluster number=%u (path=%s)", cluster, path ? path : "(null)");
        hanacore::utils::log_info_cpp(tmp);
        return -1;
    }

    // defensive checks
    if (bytes_per_sector == 0 || sectors_per_cluster == 0) {
        hanacore::utils::log_info_cpp("[FAT32] read_file: filesystem not initialized properly");
        return -1;
    }

    uint8_t sector[4096];
    if (bytes_per_sector > sizeof(sector)) {
        hanacore::utils::log_info_cpp("[FAT32] read_file: sector size too large (>4096)");
        return -1;
    }

    size_t total = 0;

    while (true) {
        // stop if cluster chain ends or invalid cluster
        if (cluster == 0 || cluster == 0x0FFFFFF7 || cluster >= 0x0FFFFFF8)
            break;

        for (uint32_t s = 0; s < sectors_per_cluster; ++s) {
            uint32_t lba = cluster_to_lba(cluster) + s;

            int r = ata_read_sector(lba, sector);
            if (r != 0) {
                char tmp[128];
                npf_snprintf(tmp, sizeof(tmp),
                             "[FAT32] read_file: ata_read_sector failed (lba=%u, rc=%d)", lba, r);
                hanacore::utils::log_info_cpp(tmp);
                return -1;
            }

            size_t remaining = len - total;
            size_t copy_len = (remaining < bytes_per_sector) ? remaining : bytes_per_sector;
            memcpy((uint8_t*)buf + total, sector, copy_len);
            total += copy_len;

            if (total >= len)
                return (int64_t)total;
        }

        uint32_t next = read_fat_entry(cluster);
        if (next == 0 || next == 0x0FFFFFF7 || next >= 0x0FFFFFF8)
            break;
        if (next == cluster) {
            hanacore::utils::log_info_cpp("[FAT32] read_file: FAT chain loop detected");
            break;
        }
        cluster = next;
    }

    return (int64_t)total;
}


void* fat32_get_file_alloc(const char* path, size_t* out_len) {
    (void)path;
    (void)out_len;
    return nullptr;
}

// =================== Mount Info ===================

void fat32_mount_all_letter_modules() {
    hanacore::utils::log_ok_cpp("[FAT32] Auto-mounting drives (QEMU/VirtualBox)");

    // First try to mount from any Limine-provided module image (e.g. rootfs.img)
    if (module_request.response) {
        volatile struct limine_module_response* resp = module_request.response;
        // Diagnostic: list available modules so we can match correctly
        {
            char tmp[128];
            npf_snprintf(tmp, sizeof(tmp), "[FAT32] Limine module_count=%u", (unsigned)resp->module_count);
            hanacore::utils::log_info_cpp(tmp);
        }
        for (uint64_t i = 0; i < resp->module_count; ++i) {
            volatile struct limine_file* mod = resp->modules[i];
            const char* path = (const char*)(uintptr_t)mod->path;
            // Print each module path for debugging (respect HHDM offset)
            if (path && limine_hhdm_request.response) {
                uint64_t hoff = limine_hhdm_request.response->offset;
                if ((uint64_t)path < hoff) path = (const char*)((uintptr_t)path + hoff);
            }
            if (path) {
                char tmp[160];
                npf_snprintf(tmp, sizeof(tmp), "[FAT32] module[%u] path=%s size=%u", (unsigned)i, path, (unsigned)mod->size);
                hanacore::utils::log_info_cpp(tmp);
            }
            // Also print physical/virtual address and a small hex preview of the module
            {
                uintptr_t mod_addr = (uintptr_t)mod->address;
                const void* mod_virt = (const void*)mod_addr;
                if (limine_hhdm_request.response) {
                    uint64_t off = limine_hhdm_request.response->offset;
                    if ((uint64_t)mod_addr < off) mod_virt = (const void*)(off + mod_addr);
                }
                char tmp2[192];
                // show address and size
                npf_snprintf(tmp2, sizeof(tmp2), "[FAT32] module[%u] addr=0x%016x size=%u", (unsigned)i, (unsigned)mod_addr, (unsigned)mod->size);
                hanacore::utils::log_info_cpp(tmp2);
                // preview first 16 bytes if accessible
                if (mod_virt && mod->size > 0) {
                    const uint8_t* b = (const uint8_t*)mod_virt;
                    char hex[64];
                    int off = 0;
                    for (int j = 0; j < 16 && (size_t)j < mod->size; ++j) {
                        off += npf_snprintf(hex + off, sizeof(hex) - off, "%02X ", (unsigned)b[j]);
                    }
                    hanacore::utils::log_info_cpp(hex);
                }
            }
            if (!path) continue;

            // Accept a wider range of module names: any .img, or names containing
            // "rootfs" or the literal "ata_master". This handles variations in
            // how the ISO/boot layout exposes the module path (e.g. "/boot/rootfs.img").
            bool want = false;
            if (ends_with(path, ".img") || ends_with(path, ".bin")) want = true;
            if (!want && strstr(path, "rootfs")) want = true;
            if (!want && strstr(path, "ata_master")) want = true;
            if (want) {
                uintptr_t mod_addr = (uintptr_t)mod->address;
                const void* mod_virt = (const void*)mod_addr;
                if (limine_hhdm_request.response) {
                    uint64_t off = limine_hhdm_request.response->offset;
                    if ((uint64_t)mod_addr < off) mod_virt = (const void*)(off + mod_addr);
                }
                if (fat32_init_from_memory(mod_virt, (size_t)mod->size) == 0) {
                    hanacore::utils::log_ok_cpp("[FAT32] Mounted from module:");
                    hanacore::utils::log_ok_cpp(path);
                    fat32_list_mounts([](const char* line) { hanacore::utils::log_info_cpp(line); });
                    return;
                } else {
                    // Try next module
                    char tmp[128];
                    npf_snprintf(tmp, sizeof(tmp), "[FAT32] Failed to init FS from module %s", path);
                    hanacore::utils::log_info_cpp(tmp);
                }
            }
        }
    }

    // Fallback to attempting to mount from ATA master device if module mount
    // did not succeed.
    if (fat32_init_from_ata() == 0) {
        hanacore::utils::log_ok_cpp("[FAT32] Mounted from ATA fallback");
    } else {
        hanacore::utils::log_info_cpp("[FAT32] No usable module or ATA device found");
    }
    fat32_list_mounts([](const char* line) { hanacore::utils::log_info_cpp(line); });
}

void fat32_list_mounts(void (*cb)(const char* line)) {
    if (cb)
        cb("FAT32 mount: [0: ATA master]");
}

} // namespace fs
} // namespace hanacore

// =================== C Wrappers ===================

extern "C" {

// C-visible wrapper so callers that expect C linkage (e.g. kernel entry code)
// can call into the C++ implementation in namespace hanacore::fs.
extern "C" void fat32_mount_all_letter_modules() {
    hanacore::fs::fat32_mount_all_letter_modules();
}

int fat32_mount_ata_master(int drive_number) {
    hanacore::utils::log_ok_cpp("[FAT32] Mounting ATA master as %d:", drive_number);
    (void)drive_number; // currently unused beyond logging
    return hanacore::fs::fat32_init_from_ata();
}

int fat32_mount_ata_slave(int drive_number) {
    hanacore::utils::log_ok_cpp("[FAT32] Mounting ATA slave as %d:", drive_number);
    (void)drive_number;
    return hanacore::fs::fat32_init_from_ata();
}

}
