#include "fat32.hpp"
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include "../libs/nanoprintf.h"

#include "../utils/logger.hpp"
#include "../libs/libc.h"

// Include IDE driver header so filesystem code can call ata_write_sector and
// ata_get_sector_count when formatting or probing devices.
#include "../drivers/ide.hpp"

// If the kernel was started with Limine modules, we can mount a rootfs image
// directly from memory instead of relying on ATA devices (which aren't
// necessarily present when running from an ISO). Include the Limine header
// and declare the request objects.
#include "../../limine/limine.h"

extern volatile struct limine_hhdm_request limine_hhdm_request;
extern volatile struct limine_module_request module_request;

// The IDE header exposes the C wrappers we use (ata_read_sector, ata_write_sector,
// ata_get_sector_count). Provide a weak fallback for ata_read_sector in case the
// driver isn't linked in.
extern "C" int __attribute__((weak)) ata_read_sector(uint32_t lba, void* buf) {
    (void)lba; (void)buf;
    return -1;
}

// Weak progress callback; callers (shell) can implement this symbol to
// receive periodic progress updates during FAT operations. Default is no-op.
extern "C" void __attribute__((weak)) fat32_progress_update(int percent) {
    (void)percent;
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

        // Notify progress using an indeterminate tick (percent=-1) so callers
        // can update spinners. This keeps the UI responsive for long reads.
        fat32_progress_update(-1);

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

// Formatting helper: create a minimal FAT32 layout on the ATA master device.
namespace hanacore {
namespace fs {

static int fat32_format_ata_impl() {
    // Query device capacity
    int32_t total_sectors = ata_get_sector_count();
    if (total_sectors <= 0) {
        hanacore::utils::log_info_cpp("[FAT32] format: unable to determine device sector count");
        return -1;
    }

    const uint32_t BytsPerSec = 512;
    uint32_t SecPerClus = 1; // small cluster size to keep layout simple
    uint16_t RsvdSecCnt = 32;
    uint8_t NumFATs = 2;
    uint32_t TotSec32 = (uint32_t)total_sectors;

    // Iteratively compute FATSz32
    uint32_t FATSz = 1;
    for (int iter = 0; iter < 32; ++iter) {
        uint32_t data_sectors = TotSec32 - RsvdSecCnt - NumFATs * FATSz;
        if ((int32_t)data_sectors <= 0) return -1;
        uint32_t clusters = data_sectors / SecPerClus;
        uint32_t needed = (clusters * 4 + BytsPerSec - 1) / BytsPerSec;
        if (needed == FATSz) break;
        FATSz = needed;
    }

    uint32_t fat_begin = RsvdSecCnt;
    uint32_t cluster_begin = RsvdSecCnt + NumFATs * FATSz;

    // Prepare boot sector (BPB)
    uint8_t boot[512];
    for (int i = 0; i < 512; ++i) boot[i] = 0;
    boot[0] = 0xEB; boot[1] = 0x58; boot[2] = 0x90; // JMP
    const char* oem = "HanaCore";
    for (int i = 0; i < 7 && oem[i]; ++i) boot[3 + i] = oem[i];
    // bytes per sector
    boot[11] = (uint8_t)(BytsPerSec & 0xFF);
    boot[12] = (uint8_t)((BytsPerSec >> 8) & 0xFF);
    boot[13] = (uint8_t)SecPerClus;
    boot[14] = (uint8_t)(RsvdSecCnt & 0xFF);
    boot[15] = (uint8_t)((RsvdSecCnt >> 8) & 0xFF);
    boot[16] = NumFATs;
    // RootEntCnt (0 for FAT32)
    boot[17] = 0; boot[18] = 0;
    // TotSec16 = 0
    boot[19] = 0; boot[20] = 0;
    boot[21] = 0xF8; // Media
    boot[22] = 0; boot[23] = 0; // FATSz16
    boot[24] = 0; boot[25] = 0; // SecPerTrk
    boot[26] = 0; boot[27] = 0; // NumHeads
    // HiddSec
    boot[28] = 0; boot[29] = 0; boot[30] = 0; boot[31] = 0;
    // TotSec32
    boot[32] = (uint8_t)(TotSec32 & 0xFF);
    boot[33] = (uint8_t)((TotSec32 >> 8) & 0xFF);
    boot[34] = (uint8_t)((TotSec32 >> 16) & 0xFF);
    boot[35] = (uint8_t)((TotSec32 >> 24) & 0xFF);
    // FATSz32
    boot[36] = (uint8_t)(FATSz & 0xFF);
    boot[37] = (uint8_t)((FATSz >> 8) & 0xFF);
    boot[38] = (uint8_t)((FATSz >> 16) & 0xFF);
    boot[39] = (uint8_t)((FATSz >> 24) & 0xFF);
    // ExtFlags, FSVer
    boot[40] = 0; boot[41] = 0; boot[42] = 0; boot[43] = 0;
    // RootClus = 2
    boot[44] = 2; boot[45] = 0; boot[46] = 0; boot[47] = 0;
    // FSInfo = 1, BkBootSec = 6
    boot[48] = 1; boot[49] = 0; boot[50] = 6; boot[51] = 0;
    // Drive number / BootSig
    boot[64] = 0x80; boot[66] = 0x29;
    // VolID
    boot[67] = 0x12; boot[68] = 0x34; boot[69] = 0x56; boot[70] = 0x78;
    // Volume label
    const char* vlab = "NO NAME    ";
    for (int i = 0; i < 11; ++i) boot[71 + i] = vlab[i];
    // Filesystem type
    const char* ftype = "FAT32   ";
    for (int i = 0; i < 8; ++i) boot[82 + i] = ftype[i];
    // signature
    boot[510] = 0x55; boot[511] = 0xAA;

    // FSInfo sector (sector 1)
    uint8_t fsinfo[512];
    for (int i = 0; i < 512; ++i) fsinfo[i] = 0;
    // Lead signature
    fsinfo[0] = 0x52; fsinfo[1] = 0x52; fsinfo[2] = 0x61; fsinfo[3] = 0x41; // 0x41615252 LE
    // Struct signature at offset 484
    fsinfo[484] = 0x72; fsinfo[485] = 0x72; fsinfo[486] = 0x41; fsinfo[487] = 0x61; // 0x61417272 LE
    // Free cluster and next free set to 0xFFFFFFFF
    fsinfo[488] = 0xFF; fsinfo[489] = 0xFF; fsinfo[490] = 0xFF; fsinfo[491] = 0xFF;
    fsinfo[492] = 0xFF; fsinfo[493] = 0xFF; fsinfo[494] = 0xFF; fsinfo[495] = 0xFF;
    fsinfo[508] = 0x55; fsinfo[509] = 0xAA;

    // Write boot sector
    if (ata_write_sector(0, boot) != 0) {
        hanacore::utils::log_info_cpp("[FAT32] format: failed to write boot sector");
        return -1;
    }
    // Write FSInfo
    if (ata_write_sector(1, fsinfo) != 0) {
        hanacore::utils::log_info_cpp("[FAT32] format: failed to write FSInfo");
        return -1;
    }

    // Zero out remaining reserved sectors up to RsvdSecCnt
    uint8_t zero[512]; for (int i = 0; i < 512; ++i) zero[i] = 0;
    for (uint32_t s = 2; s < (uint32_t)RsvdSecCnt; ++s) {
        if (ata_write_sector(s, zero) != 0) {
            hanacore::utils::log_info_cpp("[FAT32] format: failed to clear reserved sectors");
            return -1;
        }
    }

    // Initialize FATs
    // First FAT: set first three entries
    uint8_t fatsec[512];
    for (int i = 0; i < 512; ++i) fatsec[i] = 0;
    // entry 0
    uint32_t e0 = 0x0FFFFFF8;
    memcpy(&fatsec[0], &e0, 4);
    uint32_t e1 = 0xFFFFFFFF;
    memcpy(&fatsec[4], &e1, 4);
    uint32_t e2 = 0x0FFFFFFF;
    memcpy(&fatsec[8], &e2, 4);

    for (int f = 0; f < NumFATs; ++f) {
        for (uint32_t si = 0; si < FATSz; ++si) {
            uint32_t lba = fat_begin + f * FATSz + si;
            if (si == 0) {
                if (ata_write_sector(lba, fatsec) != 0) {
                    hanacore::utils::log_info_cpp("[FAT32] format: failed to write FAT sector 0");
                    return -1;
                }
            } else {
                if (ata_write_sector(lba, zero) != 0) {
                    hanacore::utils::log_info_cpp("[FAT32] format: failed to clear FAT sector");
                    return -1;
                }
            }
        }
    }

    // Zero first cluster (root directory) at cluster_begin
    for (uint32_t s = 0; s < SecPerClus; ++s) {
        uint32_t lba = cluster_begin + s;
        if (ata_write_sector(lba, zero) != 0) {
            hanacore::utils::log_info_cpp("[FAT32] format: failed to clear root cluster");
            return -1;
        }
    }

    hanacore::utils::log_ok_cpp("[FAT32] format: completed successfully");
    return 0;
}

} // namespace fs
} // namespace hanacore

extern "C" {
int fat32_format_ata_master(int drive_number) {
    (void)drive_number;
    hanacore::utils::log_info_cpp("[FAT32] format: starting (destructive!)");
    return hanacore::fs::fat32_format_ata_impl();
}
}
