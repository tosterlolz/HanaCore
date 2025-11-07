#include "fat32.hpp"
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include "../libs/nanoprintf.h"

#include "../utils/logger.hpp"
#include "../libs/libc.h"
#include "../mem/heap.hpp"

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
// Which virtual drive number is mounted for the current FAT32 instance.
// -1 = none, 0 = ATA master, 1 = module/rootfs. Updated when init functions succeed.
static int mounted_drive = -1;

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

// Write a FAT entry for `cluster` (both FAT copies). `val` is masked to 28 bits.
static int write_fat_entry(uint32_t cluster, uint32_t val) {
    if (bytes_per_sector == 0) return -1;
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector_index = fat_offset / bytes_per_sector;
    uint32_t ent_offset = fat_offset % bytes_per_sector;
    uint32_t fatsz = bpb.FATSz32;
    if (fatsz == 0) return -1;

    // buffer large enough for sector
    uint8_t sector[4096];
    if (bytes_per_sector > sizeof(sector)) return -1;

    for (uint32_t fat_i = 0; fat_i < (uint32_t)bpb.NumFATs; ++fat_i) {
        uint32_t fat_sector = fat_begin_lba + fat_i * fatsz + fat_sector_index;
        if (ata_read_sector(fat_sector, sector) != 0) return -1;
        uint32_t w = val & 0x0FFFFFFF;
        memcpy(&sector[ent_offset], &w, 4);
        if (ata_write_sector(fat_sector, sector) != 0) return -1;
    }
    return 0;
}

// Find a free cluster (entry == 0). Returns cluster number or 0 on failure.
static uint32_t find_free_cluster() {
    uint32_t fatsz = bpb.FATSz32;
    if (fatsz == 0 || bytes_per_sector == 0) return 0;
    uint64_t fat_bytes = (uint64_t)fatsz * (uint64_t)bytes_per_sector;
    uint32_t entries = (uint32_t)(fat_bytes / 4);
    for (uint32_t c = 2; c < entries; ++c) {
        uint32_t v = read_fat_entry(c);
        if (v == 0) return c;
    }
    return 0;
}

// Allocate a single cluster and mark as end-of-chain.
static uint32_t alloc_cluster() {
    uint32_t c = find_free_cluster();
    if (c == 0) return 0;
    if (write_fat_entry(c, 0x0FFFFFFF) != 0) return 0;
    return c;
}

// Free cluster chain starting at `start` (set FAT entries to 0).
static void free_cluster_chain(uint32_t start) {
    if (start < 2) return;
    uint32_t cur = start;
    while (cur >= 2 && cur < 0x0FFFFFF8) {
        uint32_t next = read_fat_entry(cur);
        write_fat_entry(cur, 0);
        if (next == 0 || next >= 0x0FFFFFF8) break;
        if (next == cur) break;
        cur = next;
    }
}

// Helper: format a short (8.3) name into 11-byte buffer. Returns false if name invalid.
static bool format_short_name(const char* in, char out[11]) {
    // clear
    for (int i = 0; i < 11; ++i) out[i] = ' ';
    if (!in || !*in) return false;
    // split on last '.'
    const char* dot = NULL;
    for (const char* p = in; *p; ++p) if (*p == '.') dot = p;
    size_t bi = 0;
    if (dot) {
        for (const char* p = in; p < dot && bi < 8; ++p) {
            char c = *p; if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
            if (c == ' ') return false;
            out[bi++] = c;
        }
        // ext
        size_t ei = 0;
        for (const char* p = dot + 1; *p && ei < 3; ++p) {
            char c = *p; if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
            if (c == ' ') return false;
            out[8 + (ei++)] = c;
        }
    } else {
        for (const char* p = in; *p && bi < 8; ++p) {
            char c = *p; if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
            if (c == ' ') return false;
            out[bi++] = c;
        }
    }
    return true;
}

// Helper: write a 32-byte directory entry into parent directory cluster chain.
// parent_cluster is starting cluster of dir. name11 must be 11 bytes.
static int write_dir_entry(uint32_t parent_cluster, const char name11[11], uint8_t attr, uint32_t first_cluster, uint32_t filesize) {
    uint8_t sector[4096];
    uint32_t cluster = parent_cluster;
    if (cluster < 2) cluster = root_dir_cluster;

    while (cluster != 0 && cluster != 0x0FFFFFF7 && cluster < 0x0FFFFFF8) {
        for (uint32_t s = 0; s < sectors_per_cluster; ++s) {
            uint32_t lba = cluster_to_lba(cluster) + s;
            if (ata_read_sector(lba, sector) != 0) return -1;
            for (uint32_t off = 0; off < bytes_per_sector; off += 32) {
                uint8_t first = sector[off];
                if (first == 0x00 || first == 0xE5) {
                    // write entry
                    for (int i = 0; i < 11; ++i) sector[off + i] = name11[i];
                    sector[off + 11] = attr;
                    // clear reserved/times
                    for (int i = 12; i < 20; ++i) sector[off + i] = 0;
                    // first cluster high (offset 20..21)
                    sector[off + 20] = (uint8_t)((first_cluster >> 16) & 0xFF);
                    sector[off + 21] = (uint8_t)((first_cluster >> 24) & 0xFF);
                    // first cluster low (offset 26..27)
                    sector[off + 26] = (uint8_t)(first_cluster & 0xFF);
                    sector[off + 27] = (uint8_t)((first_cluster >> 8) & 0xFF);
                    // file size
                    sector[off + 28] = (uint8_t)(filesize & 0xFF);
                    sector[off + 29] = (uint8_t)((filesize >> 8) & 0xFF);
                    sector[off + 30] = (uint8_t)((filesize >> 16) & 0xFF);
                    sector[off + 31] = (uint8_t)((filesize >> 24) & 0xFF);
                    if (ata_write_sector(lba, sector) != 0) return -1;
                    return 0;
                }
            }
        }
        uint32_t next = read_fat_entry(cluster);
        if (next == 0 || next == 0x0FFFFFF7 || next >= 0x0FFFFFF8) break;
        cluster = next;
    }
    return -1;
}

// Find and remove directory entry by path. If remove_clusters true, free cluster chain.
static int remove_dir_entry_by_path(const char* path, bool remove_clusters) {
    if (!path || *path == '\0') return -1;
    const char* p = path;
    if (*p == '/') ++p;

    uint32_t current_cluster = root_dir_cluster;
    char comp[64];
    char last_comp[64]; last_comp[0] = '\0';

    // traverse to parent directory
    while (*p) {
        size_t ci = 0;
        while (*p && *p != '/' && ci + 1 < sizeof(comp)) comp[ci++] = *p++;
        comp[ci] = '\0';
        if (*p == '/') ++p;
        if (*p == '\0') {
            // last component is comp
            break;
        }
        // find comp in current_cluster and set current_cluster to its cluster
        bool found = false;
        uint32_t cluster = current_cluster;
        uint8_t sector[4096];
        while (cluster != 0 && cluster != 0x0FFFFFF7 && cluster < 0x0FFFFFF8) {
            for (uint32_t s = 0; s < sectors_per_cluster; ++s) {
                uint32_t lba = cluster_to_lba(cluster) + s;
                if (ata_read_sector(lba, sector) != 0) return -1;
                for (uint32_t off = 0; off < bytes_per_sector; off += 32) {
                    uint8_t first = sector[off];
                    if (first == 0x00) { cluster = 0; break; }
                    if (first == 0xE5) continue;
                    uint8_t attr = sector[off + 11];
                    if ((attr & 0x0F) == 0x0F) continue;
                    char name[13];
                    for (int j = 0; j < 8; ++j) name[j] = sector[off + j]; name[8] = '\0';
                    for (int j = 7; j >= 0 && name[j] == ' '; --j) name[j] = '\0';
                    if (sector[off + 8] != ' ') {
                        char ext[4]; ext[0] = sector[off+8]; ext[1] = sector[off+9]; ext[2] = sector[off+10]; ext[3] = '\0';
                        size_t len = strlen(name);
                        if (len + 1 + 3 + 1 <= sizeof(name)) {
                            name[len++] = '.'; name[len++] = ext[0]; name[len++] = ext[1]; name[len++] = ext[2]; name[len] = '\0';
                        }
                    }
                    // uppercase compare
                    size_t k = 0; bool match = true;
                    for (; k < ci; ++k) { char c = comp[k]; if (c >= 'a' && c <= 'z') c = c - 'a' + 'A'; if (c != name[k]) { match = false; break; } }
                    if (!match) continue;
                    // matched directory
                    uint32_t high = (uint32_t)sector[off + 20] | ((uint32_t)sector[off + 21] << 8);
                    uint32_t low = (uint32_t)sector[off + 26] | ((uint32_t)sector[off + 27] << 8);
                    current_cluster = (high << 16) | low;
                    found = true; break;
                }
                if (found || cluster == 0) break;
            }
            if (found) break;
            if (cluster == 0) break;
            uint32_t next = read_fat_entry(cluster);
            if (next == 0 || next == 0x0FFFFFF7 || next >= 0x0FFFFFF8) break;
            cluster = next;
        }
        if (!found) return -1;
    }

    // Now current_cluster is parent dir; comp holds final component
    // Search parent for entry and mark deleted
    uint32_t cluster = current_cluster;
    uint8_t sector[4096];
    while (cluster != 0 && cluster != 0x0FFFFFF7 && cluster < 0x0FFFFFF8) {
        for (uint32_t s = 0; s < sectors_per_cluster; ++s) {
            uint32_t lba = cluster_to_lba(cluster) + s;
            if (ata_read_sector(lba, sector) != 0) return -1;
            for (uint32_t off = 0; off < bytes_per_sector; off += 32) {
                uint8_t first = sector[off];
                if (first == 0x00) return -1;
                if (first == 0xE5) continue;
                uint8_t attr = sector[off + 11];
                if ((attr & 0x0F) == 0x0F) continue;
                char name[13];
                for (int j = 0; j < 8; ++j) name[j] = sector[off + j]; name[8] = '\0';
                for (int j = 7; j >= 0 && name[j] == ' '; --j) name[j] = '\0';
                if (sector[off + 8] != ' ') {
                    char ext[4]; ext[0] = sector[off+8]; ext[1] = sector[off+9]; ext[2] = sector[off+10]; ext[3] = '\0';
                    size_t len = strlen(name);
                    if (len + 1 + 3 + 1 <= sizeof(name)) {
                        name[len++] = '.'; name[len++] = ext[0]; name[len++] = ext[1]; name[len++] = ext[2]; name[len] = '\0';
                    }
                }
                // compare
                bool match = true;
                size_t ci = 0; while (comp[ci]) ++ci;
                for (size_t k = 0; k < ci; ++k) { char c = comp[k]; if (c >= 'a' && c <= 'z') c = c - 'a' + 'A'; if (c != name[k]) { match = false; break; } }
                if (!match) continue;

                // got it — retrieve starting cluster and size
                uint32_t high = (uint32_t)sector[off + 20] | ((uint32_t)sector[off + 21] << 8);
                uint32_t low = (uint32_t)sector[off + 26] | ((uint32_t)sector[off + 27] << 8);
                uint32_t start = (high << 16) | low;
                uint32_t size = (uint32_t)sector[off + 28] | ((uint32_t)sector[off + 29] << 8) | ((uint32_t)sector[off + 30] << 16) | ((uint32_t)sector[off + 31] << 24);

                if (remove_clusters && start >= 2) {
                    free_cluster_chain(start);
                }

                // mark deleted (0xE5)
                sector[off] = 0xE5;
                if (ata_write_sector(lba, sector) != 0) return -1;
                return 0;
            }
        }
        uint32_t next = read_fat_entry(cluster);
        if (next == 0 || next == 0x0FFFFFF7 || next >= 0x0FFFFFF8) break;
        cluster = next;
    }
    return -1;
}

// Helper: given a path like "/a/b/c", return parent directory cluster and
// set `name_out` to the final component. Returns 0 on success, -1 on error.
static int get_parent_cluster_and_name(const char* path, uint32_t* out_parent_cluster, char* name_out, size_t name_out_sz) {
    if (!path || !out_parent_cluster || !name_out) return -1;
    const char* p = path;
    if (*p == '/') ++p;
    // empty path
    if (*p == '\0') return -1;

    uint32_t current_cluster = root_dir_cluster;
    char comp[64];
    char prev[64]; prev[0] = '\0';

    // iterate components; stop when next is last
    while (*p) {
        size_t ci = 0;
        while (*p && *p != '/' && ci + 1 < sizeof(comp)) comp[ci++] = *p++;
        comp[ci] = '\0';
        if (*p == '/') ++p;
        if (*p == '\0') {
            // comp is final component; current_cluster is parent
            if (ci + 1 > name_out_sz) return -1;
            for (size_t i = 0; i <= ci; ++i) name_out[i] = comp[i];
            *out_parent_cluster = current_cluster;
            return 0;
        }

        // find component in current_cluster
        bool found = false;
        uint32_t cluster = current_cluster;
        uint8_t sector[4096];
        while (cluster != 0 && cluster != 0x0FFFFFF7 && cluster < 0x0FFFFFF8) {
            for (uint32_t s = 0; s < sectors_per_cluster; ++s) {
                uint32_t lba = cluster_to_lba(cluster) + s;
                if (ata_read_sector(lba, sector) != 0) return -1;
                for (uint32_t off = 0; off < bytes_per_sector; off += 32) {
                    uint8_t first = sector[off];
                    if (first == 0x00) { cluster = 0; break; }
                    if (first == 0xE5) continue;
                    uint8_t attr = sector[off + 11];
                    if ((attr & 0x0F) == 0x0F) continue;
                    // build short name
                    char name[13];
                    for (int j = 0; j < 8; ++j) name[j] = sector[off + j]; name[8] = '\0';
                    for (int j = 7; j >= 0 && name[j] == ' '; --j) name[j] = '\0';
                    if (sector[off + 8] != ' ') {
                        char ext[4]; ext[0] = sector[off+8]; ext[1] = sector[off+9]; ext[2] = sector[off+10]; ext[3] = '\0';
                        size_t len = strlen(name);
                        if (len + 1 + 3 + 1 <= sizeof(name)) {
                            name[len++] = '.'; name[len++] = ext[0]; name[len++] = ext[1]; name[len++] = ext[2]; name[len] = '\0';
                        }
                    }
                    // compare
                    bool match = true;
                    for (size_t k = 0; k < ci; ++k) { char c = comp[k]; if (c >= 'a' && c <= 'z') c = c - 'a' + 'A'; if (c != name[k]) { match = false; break; } }
                    if (!match) continue;
                    // matched — ensure it's a directory
                    if ((attr & 0x10) == 0) return -1;
                    uint32_t high = (uint32_t)sector[off + 20] | ((uint32_t)sector[off + 21] << 8);
                    uint32_t low = (uint32_t)sector[off + 26] | ((uint32_t)sector[off + 27] << 8);
                    current_cluster = (high << 16) | low;
                    found = true; break;
                }
                if (found || cluster == 0) break;
            }
            if (found) break;
            if (cluster == 0) break;
            uint32_t next = read_fat_entry(cluster);
            if (next == 0 || next == 0x0FFFFFF7 || next >= 0x0FFFFFF8) break;
            cluster = next;
        }
        if (!found) return -1;
    }
    return -1;
}

// Public write helpers
int fat32_create_file(const char* path) {
    if (!fat32_ready || !path) return -1;
    uint32_t parent = 0;
    char name[64];
    if (get_parent_cluster_and_name(path, &parent, name, sizeof(name)) != 0) return -1;
    char name11[11]; if (!format_short_name(name, name11)) return -1;
    // create zero-length file — starting cluster 0
    if (write_dir_entry(parent, name11, 0x20, 0, 0) != 0) return -1;
    return 0;
}

int fat32_unlink(const char* path) {
    if (!fat32_ready || !path) return -1;
    return remove_dir_entry_by_path(path, true);
}

int fat32_make_dir(const char* path) {
    if (!fat32_ready || !path) return -1;
    uint32_t parent = 0; char name[64];
    if (get_parent_cluster_and_name(path, &parent, name, sizeof(name)) != 0) return -1;
    char name11[11]; if (!format_short_name(name, name11)) return -1;
    // allocate cluster for new directory
    uint32_t newc = alloc_cluster(); if (newc == 0) return -1;
    // write directory entry pointing to new cluster
    if (write_dir_entry(parent, name11, 0x10, newc, 0) != 0) { free_cluster_chain(newc); return -1; }
    // initialize new directory cluster: zero all sectors and write '.' and '..'
    uint8_t zero[4096]; for (uint32_t i = 0; i < bytes_per_sector; ++i) zero[i] = 0;
    for (uint32_t s = 0; s < sectors_per_cluster; ++s) {
        uint32_t lba = cluster_to_lba(newc) + s;
        if (ata_write_sector(lba, zero) != 0) return -1;
    }
    // write '.' and '..' entries in first sector
    uint8_t sector[4096]; if (ata_read_sector(cluster_to_lba(newc), sector) != 0) return -1;
    // '.' entry
    char dotname[11] = {'.',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '};
    for (int i = 0; i < 11; ++i) sector[i] = dotname[i];
    sector[11] = 0x10; // directory
    sector[20] = (uint8_t)((newc >> 16) & 0xFF); sector[21] = (uint8_t)((newc >> 24) & 0xFF);
    sector[26] = (uint8_t)(newc & 0xFF); sector[27] = (uint8_t)((newc >> 8) & 0xFF);
    // '..' entry at offset 32
    char dotdot[11] = {'.','.',' ',' ',' ',' ',' ',' ',' ',' ',' '};
    for (int i = 0; i < 11; ++i) sector[32 + i] = dotdot[i];
    sector[32 + 11] = 0x10;
    // parent cluster may be 0 for root
    uint32_t pcl = parent < 2 ? 0 : parent;
    sector[32 + 20] = (uint8_t)((pcl >> 16) & 0xFF); sector[32 + 21] = (uint8_t)((pcl >> 24) & 0xFF);
    sector[32 + 26] = (uint8_t)(pcl & 0xFF); sector[32 + 27] = (uint8_t)((pcl >> 8) & 0xFF);
    if (ata_write_sector(cluster_to_lba(newc), sector) != 0) return -1;
    return 0;
}

int fat32_remove_dir(const char* path) {
    if (!fat32_ready || !path) return -1;
    // For now, reuse unlink semantics but ensure entry is directory
    // remove_dir_entry_by_path frees clusters when requested
    return remove_dir_entry_by_path(path, true);
}

// Write a file to the currently-mounted filesystem (overwrite if exists).
int fat32_write_file(const char* path, const void* buf, size_t len) {
    if (!fat32_ready || !path) return -1;
    // remove existing entry if present
    remove_dir_entry_by_path(path, true);

    // prepare parent and name
    uint32_t parent = 0; char name[64];
    if (get_parent_cluster_and_name(path, &parent, name, sizeof(name)) != 0) return -1;
    char name11[11]; if (!format_short_name(name, name11)) return -1;

    // if len==0 create empty file (no clusters)
    if (len == 0) {
        if (write_dir_entry(parent, name11, 0x20, 0, 0) != 0) return -1;
        return 0;
    }

    size_t remaining = len;
    const uint8_t* p = (const uint8_t*)buf;
    uint32_t first_cluster = 0;
    uint32_t prev_cluster = 0;
    uint32_t cluster_bytes = sectors_per_cluster * bytes_per_sector;

    while (remaining > 0) {
        uint32_t c = alloc_cluster();
        if (c == 0) { // cleanup allocated chain
            if (first_cluster) free_cluster_chain(first_cluster);
            return -1;
        }
        if (!first_cluster) first_cluster = c;
        if (prev_cluster) {
            // link previous -> c
            if (write_fat_entry(prev_cluster, c) != 0) { free_cluster_chain(first_cluster); return -1; }
        }
        // mark c as end-of-chain for now
        if (write_fat_entry(c, 0x0FFFFFFF) != 0) { free_cluster_chain(first_cluster); return -1; }

        // write cluster sectors
        for (uint32_t s = 0; s < sectors_per_cluster; ++s) {
            uint32_t lba = cluster_to_lba(c) + s;
            uint8_t sector_buf[4096];
            size_t to_copy = (remaining < bytes_per_sector) ? remaining : bytes_per_sector;
            // zero then copy
            for (uint32_t i = 0; i < bytes_per_sector; ++i) sector_buf[i] = 0;
            for (size_t i = 0; i < to_copy; ++i) sector_buf[i] = p[i];
            if (ata_write_sector(lba, sector_buf) != 0) { free_cluster_chain(first_cluster); return -1; }
            p += to_copy;
            remaining -= to_copy;
            if (remaining == 0) {
                // mark end-of-chain already set
                break;
            }
        }

        prev_cluster = c;
    }

    // Now write directory entry pointing to first_cluster and filesize len
    if (write_dir_entry(parent, name11, 0x20, first_cluster, (uint32_t)len) != 0) {
        // On failure, free clusters
        free_cluster_chain(first_cluster);
        return -1;
    }

    return 0;
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

    // Mark as mounted from ATA device (drive 0)
    mounted_drive = 0;
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
        char tmp[160];
        npf_snprintf(tmp, sizeof(tmp), "[FAT32] init_from_memory: unsupported bytes_per_sector=%u (module size=%u)", bytes_per_sector, (unsigned)size);
        hanacore::utils::log_info_cpp(tmp);
        // dump first 32 bytes for debugging
        char hex[128]; int off = 0;
        for (int i = 0; i < 32 && (size_t)i < size; ++i) off += npf_snprintf(hex + off, sizeof(hex) - off, "%02X ", (unsigned)sector[i]);
        hanacore::utils::log_info_cpp(hex);
        return -1;
    }
    if (sectors_per_cluster == 0 || sectors_per_cluster > 128) {
        char tmp[160];
        npf_snprintf(tmp, sizeof(tmp), "[FAT32] init_from_memory: invalid sectors_per_cluster=%u (module size=%u)", sectors_per_cluster, (unsigned)size);
        hanacore::utils::log_info_cpp(tmp);
        char hex[128]; int off = 0;
        for (int i = 0; i < 32 && (size_t)i < size; ++i) off += npf_snprintf(hex + off, sizeof(hex) - off, "%02X ", (unsigned)sector[i]);
        hanacore::utils::log_info_cpp(hex);
        return -1;
    }

    fat32_ready = true;
    hanacore::utils::log_ok_cpp("[FAT32] Initialized from memory module");
    // Mark that this filesystem came from a module and should be exposed as drive 1:/
    mounted_drive = 1;
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

    // Resolve `path` to a directory cluster. Accept forms like "", "/", "0:/foo", "/foo".
    const char* p = path ? path : "";
    // If caller specified a drive prefix like '1:/', honor it. If it differs
    // from the currently mounted drive, attempt to mount the requested
    // drive (best-effort) before proceeding.
    int requested_drive = mounted_drive;
    if (p[0] && p[1] == ':') {
        if (p[0] >= '0' && p[0] <= '9') requested_drive = (int)(p[0] - '0');
    }
    if (requested_drive != mounted_drive) {
        if (requested_drive == 0) {
            // try ATA (will set mounted_drive on success)
            fat32_init_from_ata();
        } else if (requested_drive == 1) {
            // Attempt to initialize from any Limine module that looks like
            // a disk image (prefer names with .img/.bin or containing "rootfs").
            if (module_request.response) {
                volatile struct limine_module_response* resp = module_request.response;
                for (uint64_t mi = 0; mi < resp->module_count; ++mi) {
                    volatile struct limine_file* mod = resp->modules[mi];
                    const char* mpath = (const char*)(uintptr_t)mod->path;
                    if (mpath && limine_hhdm_request.response) {
                        uint64_t hoff = limine_hhdm_request.response->offset;
                        if ((uint64_t)mpath < hoff) mpath = (const char*)((uintptr_t)mpath + hoff);
                    }
                    bool candidate = false;
                    if (mpath) {
                        if (ends_with(mpath, ".img") || ends_with(mpath, ".bin")) candidate = true;
                        if (!candidate && strstr(mpath, "rootfs")) candidate = true;
                    }
                    // also accept any sufficiently large module as a last resort
                    if (!candidate && mod->size >= 512) candidate = true;
                    if (!candidate) continue;

                    uintptr_t mod_addr = (uintptr_t)mod->address;
                    const void* mod_virt = (const void*)mod_addr;
                    if (limine_hhdm_request.response) {
                        uint64_t off = limine_hhdm_request.response->offset;
                        if ((uint64_t)mod_addr < off) mod_virt = (const void*)(off + mod_addr);
                    }
                    if (fat32_init_from_memory(mod_virt, (size_t)mod->size) == 0) {
                        mounted_drive = 1;
                        hanacore::utils::log_ok_cpp("[FAT32] Mounted module image for drive 1:");
                        if (mpath) hanacore::utils::log_ok_cpp(mpath);
                        break;
                    }
                }
            }
        }
    }
    // If caller provided a drive prefix, skip it for path traversal
    if (p[0] && p[1] == ':') p += 2;
    // strip leading '/'
    if (*p == '/') ++p;

    uint32_t cluster = root_dir_cluster;
    if (!p || *p == '\0') {
        // root directory, nothing to do
    } else {
        // traverse components in `p`
        char comp[64];
        uint8_t tmpsec[4096];
        while (*p) {
            size_t ci = 0;
            while (*p && *p != '/' && ci + 1 < sizeof(comp)) comp[ci++] = *p++;
            comp[ci] = '\0';
            if (*p == '/') ++p;

            // uppercase comp for comparison with short names
            for (size_t i = 0; i < ci; ++i) { char c = comp[i]; if (c >= 'a' && c <= 'z') comp[i] = c - 'a' + 'A'; }

            bool found = false;
            uint32_t found_cluster = 0;
            uint32_t cur = cluster;
            while (cur != 0 && cur != 0x0FFFFFF7 && cur < 0x0FFFFFF8) {
                for (uint32_t s = 0; s < sectors_per_cluster; ++s) {
                    uint32_t lba = cluster_to_lba(cur) + s;
                    if (ata_read_sector(lba, tmpsec) != 0) return -1;
                    for (uint32_t off = 0; off < bytes_per_sector; off += 32) {
                        uint8_t first = tmpsec[off];
                        if (first == 0x00) { cur = 0; break; }
                        if (first == 0xE5) continue;
                        if ((tmpsec[off + 11] & 0x08) != 0) continue; // volume label
                        if ((tmpsec[off + 11] & 0x0F) == 0x0F) continue; // long name

                        // build short name (base only)
                        char name[13];
                        for (int j = 0; j < 8; ++j) name[j] = tmpsec[off + j]; name[8] = '\0';
                        for (int j = 7; j >= 0 && name[j] == ' '; --j) name[j] = '\0';

                        // compare only base name (before dot)
                        size_t ni = 0; while (name[ni] && name[ni] != '.') ++ni;
                        if (ni != ci) continue;
                        bool match = true;
                        for (size_t k = 0; k < ci; ++k) if (comp[k] != name[k]) { match = false; break; }
                        if (!match) continue;

                        // ensure directory
                        if ((tmpsec[off + 11] & 0x10) == 0) return -1;
                        uint32_t high = (uint32_t)tmpsec[off + 20] | ((uint32_t)tmpsec[off + 21] << 8);
                        uint32_t low = (uint32_t)tmpsec[off + 26] | ((uint32_t)tmpsec[off + 27] << 8);
                        found_cluster = (high << 16) | low;
                        found = true; break;
                    }
                    if (found || cur == 0) break;
                }
                if (found) break;
                if (cur == 0) break;
                uint32_t next = read_fat_entry(cur);
                if (next == 0 || next == 0x0FFFFFF7 || next >= 0x0FFFFFF8) break;
                cur = next;
            }
            if (!found) return -1;
            cluster = found_cluster;
        }
    }

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
    if (!fat32_ready || !path || !out_len) return nullptr;

    // Expect absolute path like "/bin/foo" (no drive letter)
    const char* p = path;
    if (*p == '/') ++p; // skip leading slash
    if (*p == '\0') return nullptr;

    uint32_t current_cluster = root_dir_cluster;

    // temp buffers
    uint8_t sector[4096];

    // tokenize path by '/'
    char comp[64];
    while (*p) {
        // read next component
        size_t ci = 0;
        while (*p && *p != '/' && ci + 1 < sizeof(comp)) comp[ci++] = *p++;
        comp[ci] = '\0';
        if (*p == '/') ++p; // skip separator

        // uppercase component for comparison (FAT short names are uppercase)
        for (size_t i = 0; i < ci; ++i) {
            char c = comp[i];
            if (c >= 'a' && c <= 'z') comp[i] = c - 'a' + 'A';
        }

        bool found = false;
        bool is_dir = false;
        uint32_t found_cluster = 0;
        uint32_t found_size = 0;

        // iterate directory clusters
        uint32_t cluster = current_cluster;
        while (cluster != 0 && cluster != 0x0FFFFFF7 && cluster < 0x0FFFFFF8) {
            for (uint32_t s = 0; s < sectors_per_cluster; ++s) {
                uint32_t lba = cluster_to_lba(cluster) + s;
                if (ata_read_sector(lba, sector) != 0) return nullptr;

                for (int off = 0; off < (int)bytes_per_sector; off += 32) {
                    uint8_t first = sector[off];
                    if (first == 0x00) { cluster = 0; break; }
                    if (first == 0xE5) continue;
                    uint8_t attr = sector[off + 11];
                    if ((attr & 0x08) != 0) continue; // volume label
                    if ((attr & 0x0F) == 0x0F) continue; // long name entry

                    // build short name (uppercase) without trailing spaces
                    char name[13];
                    for (int j = 0; j < 8; ++j) name[j] = sector[off + j];
                    name[8] = '\0';
                    for (int j = 7; j >= 0; --j) if (name[j] == ' ') name[j] = '\0';
                    // extension
                    if (sector[off + 8] != ' ') {
                        char ext[4]; ext[0] = sector[off + 8]; ext[1] = sector[off + 9]; ext[2] = sector[off + 10]; ext[3] = '\0';
                        size_t len = 0; while (name[len]) ++len;
                        if (len + 1 + 3 + 1 <= sizeof(name)) {
                            name[len++] = '.';
                            name[len++] = ext[0]; name[len++] = ext[1]; name[len++] = ext[2];
                            name[len] = '\0';
                        }
                    }

                    // Compare component: if component contains a dot, compare full name
                    bool match = false;
                    bool comp_has_dot = false;
                    for (size_t k = 0; k < ci; ++k) if (comp[k] == '.') comp_has_dot = true;
                    if (comp_has_dot) {
                        // compare whole name case-insensitive (both uppercase)
                        size_t ni = 0; while (name[ni]) ++ni;
                        if (ni == ci) {
                            match = true;
                            for (size_t k = 0; k < ci; ++k) if (comp[k] != name[k]) { match = false; break; }
                        }
                    } else {
                        // compare only base name (before dot)
                        size_t ni = 0; while (name[ni] && name[ni] != '.') ++ni;
                        if (ni == ci) {
                            match = true;
                            for (size_t k = 0; k < ci; ++k) if (comp[k] != name[k]) { match = false; break; }
                        }
                    }

                    if (!match) continue;

                    // matched entry
                    found = true;
                    is_dir = (attr & 0x10) != 0;
                    uint32_t high = (uint32_t)sector[off + 20] | ((uint32_t)sector[off + 21] << 8);
                    uint32_t low = (uint32_t)sector[off + 26] | ((uint32_t)sector[off + 27] << 8);
                    found_cluster = (high << 16) | low;
                    found_size = (uint32_t)sector[off + 28] | ((uint32_t)sector[off + 29] << 8) | ((uint32_t)sector[off + 30] << 16) | ((uint32_t)sector[off + 31] << 24);
                    break;
                }
                if (found || cluster == 0) break;
            }
            if (found) break;
            if (cluster == 0) break;
            uint32_t next = read_fat_entry(cluster);
            if (next == 0 || next == 0x0FFFFFF7 || next >= 0x0FFFFFF8) break;
            cluster = next;
        }

        if (!found) return nullptr;

        // If this is the last component, and it's a file, read it
        if (*p == '\0') {
            if (is_dir) return nullptr;
            // allocate buffer and read file
            void* buf = kmalloc(found_size ? found_size : 1);
            if (!buf) return nullptr;
            // use fat32_read_file which accepts cluster number as decimal string
            char clstr[32];
            // convert found_cluster to decimal string
            uint32_t tmpc = found_cluster; int idx = 0;
            if (tmpc == 0) { clstr[idx++] = '0'; }
            else {
                char rev[32]; int ri = 0;
                while (tmpc > 0) { rev[ri++] = '0' + (tmpc % 10); tmpc /= 10; }
                while (ri--) clstr[idx++] = rev[ri];
            }
            clstr[idx] = '\0';
            int64_t r = fat32_read_file(clstr, buf, found_size ? found_size : 1);
            if (r < 0) { kfree(buf); return nullptr; }
            *out_len = (size_t)r;
            return buf;
        }

        // Otherwise, traverse into directory
        if (!is_dir) return nullptr;
        current_cluster = found_cluster;
    }

    return nullptr;
}

// =================== Mount Info ===================

void fat32_mount_all_letter_modules() {
    hanacore::utils::log_ok_cpp("[FAT32] Auto-mounting drives (QEMU/VirtualBox)");

    // If a filesystem is already mounted (e.g. kernel_main mounted a module
    // earlier), don't override it here — just report mounts.
    if (fat32_ready) {
        hanacore::utils::log_info_cpp("[FAT32] filesystem already mounted; skipping auto-mount");
        fat32_list_mounts([](const char* line) { hanacore::utils::log_info_cpp(line); });
        return;
    }

    // First try to mount from any Limine-provided module image (e.g. rootfs.img)
    if (module_request.response) {
        // Quick-path: attempt to initialise from common rootfs module names
        // first. This handles cases where the module list/order differs and
        // ensures rootfs.img is preferred when present.
        if (!fat32_ready) {
            if (fat32_init_from_module("rootfs.img") == 0) {
                hanacore::utils::log_ok_cpp("[FAT32] Mounted module rootfs.img (quick-path)");
                fat32_list_mounts([](const char* line) { hanacore::utils::log_info_cpp(line); });
                return;
            }
            if (fat32_init_from_module("rootfs.bin") == 0) {
                hanacore::utils::log_ok_cpp("[FAT32] Mounted module rootfs.bin (quick-path)");
                fat32_list_mounts([](const char* line) { hanacore::utils::log_info_cpp(line); });
                return;
            }
        }
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
    if (!cb) return;
    char buf[64];
    if (mounted_drive == 1) {
        npf_snprintf(buf, sizeof(buf), "FAT32 mount: [1: rootfs]");
    } else if (mounted_drive == 0) {
        npf_snprintf(buf, sizeof(buf), "FAT32 mount: [0: ATA master]");
    } else {
        npf_snprintf(buf, sizeof(buf), "FAT32 mount: [no mount]");
    }
    cb(buf);
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
    char msg[64];
    npf_snprintf(msg, sizeof(msg), "[FAT32] Mounting ATA master as %d:", drive_number);
    hanacore::utils::log_ok_cpp(msg);
    (void)drive_number; // currently unused beyond logging
    return hanacore::fs::fat32_init_from_ata();
}

int fat32_mount_ata_slave(int drive_number) {
    char msg[64];
    npf_snprintf(msg, sizeof(msg), "[FAT32] Mounting ATA slave as %d:", drive_number);
    hanacore::utils::log_ok_cpp(msg);
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
