// Minimal fat32_ reader that exposes the same small API the kernel expects
// (fat32_* names are kept for compatibility with the rest of the kernel).
// This implementation is read-only and intentionally small: it initializes
// from a Limine module, can list directory entries and load file contents.

#include "fat32.hpp"
#include <stdint.h>
#include <stddef.h>

#include "../../boot/limine.h"
#include "../utils/logger.hpp"
#include "../mem/heap.hpp"

extern "C" {
    int memcmp(const void* s1, const void* s2, size_t n);
    void* memcpy(void* dst, const void* src, size_t n);
    void* memset(void* s, int c, size_t n);
    int strcmp(const char* a, const char* b);
    size_t strlen(const char* s);
}

// Internal state: pointer to module image in memory and its size
// Single default fs (kept for compatibility) and a mount table for drive letters
static uint8_t* fs_image = nullptr;
static size_t fs_image_size = 0;

struct MountedFS {
    bool in_use;
    char letter; // uppercase ASCII letter 'A'..'Z'
    uint8_t* image;
    size_t image_size;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t reserved_sector_count;
    uint32_t num_fats;
    uint32_t fat_size_sectors;
    uint32_t root_cluster;
    uint64_t fat_offset;
    uint64_t first_data_offset;
};

static MountedFS mounts[16]; // up to 16 mounted volumes

// fat32_ layout parameters
static uint32_t bytes_per_sector = 0;
static uint32_t sectors_per_cluster = 0;
static uint32_t reserved_sector_count = 0;
static uint32_t num_fats = 0;
static uint32_t fat_size_sectors = 0;
static uint32_t root_cluster = 0;
static uint64_t fat_offset = 0;
static uint64_t first_data_offset = 0;

// Use the global module_request defined in kernel/kernel.cpp so we don't
// register a second Limine request with the same ID (that causes a panic).
extern volatile struct limine_module_request module_request;

// For HHDM handling (provided by limine_entry.c)
extern volatile struct limine_hhdm_request limine_hhdm_request;

// Helper: read little-endian values from the image safely
namespace hanacore {
    namespace fs {

        static inline uint16_t read_le16(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
        static inline uint32_t read_le32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

        // Convert a cluster number to an offset pointer into a given image (or NULL on OOB)
        static inline void* cluster_ptr_for(MountedFS* m, uint32_t cluster) {
            if (!m || !m->image) return NULL;
            if (cluster < 2) return NULL;
            uint64_t off = m->first_data_offset + (uint64_t)(cluster - 2) * (uint64_t)(m->bytes_per_sector * m->sectors_per_cluster);
            if (off >= m->image_size) return NULL;
            return (void*)(m->image + off);
        }

        // Read fat32_ entry for `cluster` (returns next cluster number or 0x0FFFFFFF on EOC)
        static uint32_t fat32_next_cluster_for(MountedFS* m, uint32_t cluster) {
            if (!m || !m->image) return 0x0FFFFFFF;
            uint64_t off = m->fat_offset + (uint64_t)cluster * 4;
            if (off + 4 > m->image_size) return 0x0FFFFFFF;
            uint32_t v = read_le32(m->image + off) & 0x0FFFFFFF;
            return v;
        }

        // Backwards-compatible wrappers that operate on the single default
        // fs_image (so existing functions that call cluster_ptr/fat32_next_cluster
        // continue to work).
        static MountedFS default_mount_storage;
        static bool default_mount_inited = false;

        static MountedFS* get_default_mount() {
            if (!fs_image) return NULL;
            if (!default_mount_inited) {
                default_mount_storage.in_use = true;
                default_mount_storage.letter = 'C';
                default_mount_storage.image = fs_image;
                default_mount_storage.image_size = fs_image_size;
                default_mount_storage.bytes_per_sector = bytes_per_sector;
                default_mount_storage.sectors_per_cluster = sectors_per_cluster;
                default_mount_storage.reserved_sector_count = reserved_sector_count;
                default_mount_storage.num_fats = num_fats;
                default_mount_storage.fat_size_sectors = fat_size_sectors;
                default_mount_storage.root_cluster = root_cluster;
                default_mount_storage.fat_offset = fat_offset;
                default_mount_storage.first_data_offset = first_data_offset;
                default_mount_inited = true;
            }
            return &default_mount_storage;
        }

        static inline void* cluster_ptr(uint32_t cluster) {
            return cluster_ptr_for(get_default_mount(), cluster);
        }

        static uint32_t fat32_next_cluster(uint32_t cluster) {
            return fat32_next_cluster_for(get_default_mount(), cluster);
        }

        // Initialize fat32_ parameters from the boot sector
        int fat32_init_from_module(const char* module_name) {
            if (!module_request.response) {
                log_fail("fat32: no module response");
                return 0;
            }
            volatile struct limine_module_response* resp = module_request.response;
            // Diagnostic: report how many modules were provided
            log_info("fat32: checking modules");
            for (uint64_t i = 0; i < resp->module_count; ++i) {
                volatile struct limine_file* mod = resp->modules[i];
                const char* path = (const char*)(uintptr_t)mod->path;
                // If Limine provided a physical pointer, convert it to the
                // higher-half direct map (HHDM) virtual address so string ops
                // and logging are valid. This mirrors how we handle mod->address
                // below.
                if (path && limine_hhdm_request.response) {
                    uint64_t hoff = limine_hhdm_request.response->offset;
                    if ((uint64_t)path < hoff) path = (const char*)((uintptr_t)path + hoff);
                }
                if (path) {
                    log_info("fat32: module path:");
                    log_info(path);
                } else {
                    log_info("fat32: module path: <null>");
                }
                // Additional diagnostics: report module size and first bytes
                log_hex64("fat32: module size", (uint64_t)mod->size);
                uintptr_t addr_dbg = (uintptr_t)mod->address;
                if (limine_hhdm_request.response) {
                    uint64_t off = limine_hhdm_request.response->offset;
                    if ((uint64_t)addr_dbg < off) addr_dbg = (uintptr_t)(off + addr_dbg);
                }
                if (addr_dbg) {
                    uint8_t* dbg = (uint8_t*)addr_dbg;
                    char bbuf[64]; size_t bp = 0;
                    for (size_t x = 0; x < 16 && x < mod->size && bp + 3 < sizeof(bbuf); ++x) {
                        unsigned v = dbg[x];
                        const char hex[] = "0123456789ABCDEF";
                        bbuf[bp++] = hex[(v >> 4) & 0xF];
                        bbuf[bp++] = hex[v & 0xF];
                        bbuf[bp++] = ' ';
                    }
                    if (bp == 0) { bbuf[bp++] = '<'; bbuf[bp++] = 'e'; bbuf[bp++] = 'm'; bbuf[bp++] = 'p'; bbuf[bp++] = 't'; bbuf[bp++] = 'y'; }
                    bbuf[bp] = '\0';
                    log_info(bbuf);
                }
                if (!path) continue;
                // Accept either suffix match or substring match so paths like
                // "/boot/rootfs.img" or "rootfs.img" are both accepted.
                size_t pl = 0; while (path[pl]) ++pl;
                size_t ml = 0; if (module_name) while (module_name[ml]) ++ml;
                bool match = false;
                if (module_name && pl >= ml && strcmp(path + pl - ml, module_name) == 0) match = true;
                if (!match && module_name) {
                    // naive substring search (freestanding-friendly)
                    const char* pp = path;
                    while (*pp) {
                        size_t k = 0;
                        while (k < ml && pp[k] && pp[k] == module_name[k]) ++k;
                        if (k == ml) { match = true; break; }
                        ++pp;
                    }
                }
                if (!match) {
                    // Log that this module didn't match the requested name
                    log_info("fat32: module did not match");
                    continue;
                }

                uintptr_t addr = (uintptr_t)mod->address;
                if (limine_hhdm_request.response) {
                    uint64_t off = limine_hhdm_request.response->offset;
                    if ((uint64_t)addr < off) addr = (uintptr_t)(off + addr);
                }

                // Map candidate image and try to detect FAT32 BPB. We accept the
                // module if it either matches the requested name or looks like
                // a valid FAT32 image by inspecting its BPB fields.
                uint8_t* cand = (uint8_t*)addr;
                size_t csize = (size_t)mod->size;
                if (csize < 512) {
                    log_info("fat32: module too small to be FS image");
                    continue;
                }

                // Read BPB from candidate
                uint32_t cb_bytes_per_sector = read_le16(cand + 11);
                uint32_t cb_sectors_per_cluster = (uint32_t)cand[13];
                uint32_t cb_reserved_sectors = read_le16(cand + 14);
                uint32_t cb_num_fats = cand[16];
                uint32_t cb_fat_size = read_le32(cand + 36);
                uint32_t cb_root_cluster = read_le32(cand + 44);

                // Basic sanity checks: BPB non-zero fields and boot sector signature 0x55AA
                bool sig_ok = (cand[510] == 0x55 && cand[511] == 0xAA);
                bool bpb_ok = sig_ok && (cb_bytes_per_sector != 0 && cb_sectors_per_cluster != 0 && cb_fat_size != 0);

                if (!match && !bpb_ok) {
                    log_info("fat32: module did not match and is not a fat32 image");
                    continue;
                }

                // Accept this module: set fs_image from candidate
                fs_image = cand;
                fs_image_size = csize;

                // Read BPB fields into globals
                bytes_per_sector = cb_bytes_per_sector;
                sectors_per_cluster = cb_sectors_per_cluster;
                reserved_sector_count = cb_reserved_sectors;
                num_fats = cb_num_fats;
                fat_size_sectors = cb_fat_size; // FAT32
                root_cluster = cb_root_cluster;

                if (bytes_per_sector == 0 || sectors_per_cluster == 0 || fat_size_sectors == 0) {
                    log_fail("fat32: invalid BPB in image");
                    return 0;
                }

                fat_offset = (uint64_t)reserved_sector_count * bytes_per_sector;
                uint64_t first_data_sector = reserved_sector_count + (uint64_t)num_fats * fat_size_sectors;
                first_data_offset = first_data_sector * (uint64_t)bytes_per_sector;

                log_ok("fat32: initialized from module");
                return 1;
            }
            // If we didn't find a module by name, try a best-effort fallback:
            // scan all provided Limine modules and pick the first one that
            // looks like a valid FAT32 image (BPB checks). This makes the
            // boot process tolerant to different ISO layouts where the
            // module path may not include the filename we expected.
            for (uint64_t j = 0; j < resp->module_count; ++j) {
                volatile struct limine_file* mmod = resp->modules[j];
                uintptr_t addr2 = (uintptr_t)mmod->address;
                if (limine_hhdm_request.response) {
                    uint64_t off = limine_hhdm_request.response->offset;
                    if ((uint64_t)addr2 < off) addr2 = (uintptr_t)(off + addr2);
                }
                uint8_t* cand2 = (uint8_t*)addr2;
                size_t csize2 = (size_t)mmod->size;
                if (!cand2 || csize2 < 512) continue;
                uint32_t cb_bytes_per_sector2 = read_le16(cand2 + 11);
                uint32_t cb_sectors_per_cluster2 = (uint32_t)cand2[13];
                uint32_t cb_fat_size2 = read_le32(cand2 + 36);
                bool sig_ok2 = (cand2[510] == 0x55 && cand2[511] == 0xAA);
                bool bpb_ok2 = sig_ok2 && (cb_bytes_per_sector2 != 0 && cb_sectors_per_cluster2 != 0 && cb_fat_size2 != 0);
                if (!bpb_ok2) continue;
                // accept this image as fallback rootfs
                fs_image = cand2;
                fs_image_size = csize2;
                bytes_per_sector = cb_bytes_per_sector2;
                sectors_per_cluster = cb_sectors_per_cluster2;
                reserved_sector_count = read_le16(cand2 + 14);
                num_fats = cand2[16];
                fat_size_sectors = cb_fat_size2;
                root_cluster = read_le32(cand2 + 44);
                fat_offset = (uint64_t)reserved_sector_count * bytes_per_sector;
                uint64_t first_data_sector2 = reserved_sector_count + (uint64_t)num_fats * fat_size_sectors;
                first_data_offset = first_data_sector2 * (uint64_t)bytes_per_sector;
                log_info("fat32: fallback: initialized from first valid FAT32 module");
                return 1;
            }

            log_fail("fat32: no matching module found");
            return 0;
        }

        // Initialize a MountedFS structure from an image buffer (BPB check + populate)
        static int init_mounted_from_image(MountedFS* m, uint8_t* cand, size_t csize, char letter) {
            if (!m || !cand || csize < 512) return 0;
            uint32_t cb_bytes_per_sector = read_le16(cand + 11);
            uint32_t cb_sectors_per_cluster = (uint32_t)cand[13];
            uint32_t cb_reserved_sectors = read_le16(cand + 14);
            uint32_t cb_num_fats = cand[16];
            uint32_t cb_fat_size = read_le32(cand + 36);
            uint32_t cb_root_cluster = read_le32(cand + 44);
            bool sig_ok = (cand[510] == 0x55 && cand[511] == 0xAA);
            bool bpb_ok = sig_ok && (cb_bytes_per_sector != 0 && cb_sectors_per_cluster != 0 && cb_fat_size != 0);
            if (!bpb_ok) return 0;
            m->in_use = true;
            m->letter = letter;
            m->image = cand;
            m->image_size = csize;
            m->bytes_per_sector = cb_bytes_per_sector;
            m->sectors_per_cluster = cb_sectors_per_cluster;
            m->reserved_sector_count = cb_reserved_sectors;
            m->num_fats = cb_num_fats;
            m->fat_size_sectors = cb_fat_size;
            m->root_cluster = cb_root_cluster;
            m->fat_offset = (uint64_t)cb_reserved_sectors * cb_bytes_per_sector;
            uint64_t first_data_sector = cb_reserved_sectors + (uint64_t)cb_num_fats * cb_fat_size;
            m->first_data_offset = first_data_sector * (uint64_t)cb_bytes_per_sector;
            return 1;
        }

        // Find an unused mount slot
        static MountedFS* find_free_mount() {
            for (size_t i = 0; i < sizeof(mounts)/sizeof(mounts[0]); ++i) {
                if (!mounts[i].in_use) return &mounts[i];
            }
            return NULL;
        }

        // Try to mount a Limine module as a drive letter (e.g. 'C')
        int mount_module_as_drive(const char* module_name, char letter) {
            if (!module_request.response || !module_name) return 0;
            volatile struct limine_module_response* resp = module_request.response;
            for (uint64_t i = 0; i < resp->module_count; ++i) {
                volatile struct limine_file* mod = resp->modules[i];
                const char* path = (const char*)(uintptr_t)mod->path;
                if (!path) continue;
                // HHDM fix for path pointer
                if (limine_hhdm_request.response) {
                    uint64_t hoff = limine_hhdm_request.response->offset;
                    if ((uint64_t)path < hoff) path = (const char*)((uintptr_t)path + hoff);
                }
                // match exact name or suffix
                size_t pl = 0; while (path[pl]) ++pl;
                size_t ml = 0; while (module_name[ml]) ++ml;
                bool match = false;
                if (pl >= ml && strcmp(path + pl - ml, module_name) == 0) match = true;
                if (!match) {
                    // substring
                    const char* pp = path;
                    while (*pp) {
                        size_t k = 0; while (k < ml && pp[k] && pp[k] == module_name[k]) ++k;
                        if (k == ml) { match = true; break; }
                        ++pp;
                    }
                }
                if (!match) continue;

                uintptr_t addr = (uintptr_t)mod->address;
                if (limine_hhdm_request.response) {
                    uint64_t off = limine_hhdm_request.response->offset;
                    if ((uint64_t)addr < off) addr = (uintptr_t)(off + addr);
                }
                uint8_t* cand = (uint8_t*)addr;
                size_t csize = (size_t)mod->size;
                MountedFS* m = find_free_mount();
                if (!m) return 0;
                if (!init_mounted_from_image(m, cand, csize, (char)letter)) {
                    // not a FAT32 image
                    m->in_use = false;
                    continue;
                }
                char logbuf[32];
                // log which letter was mounted
                log_info("fat32: mounted module as drive");
                return 1;
            }
            return 0;
        }

        // Auto-mount modules whose filename encodes a drive letter, e.g.
        // c.img, disk_c.img, drive_c.img -> mounted as 'C'
        static void mount_all_letter_modules() {
            if (!module_request.response) return;
            volatile struct limine_module_response* resp = module_request.response;
            for (uint64_t i = 0; i < resp->module_count; ++i) {
                volatile struct limine_file* mod = resp->modules[i];
                const char* path = (const char*)(uintptr_t)mod->path;
                if (!path) continue;
                if (limine_hhdm_request.response) {
                    uint64_t hoff = limine_hhdm_request.response->offset;
                    if ((uint64_t)path < hoff) path = (const char*)((uintptr_t)path + hoff);
                }
                // extract filename
                const char* p = path; const char* last = p; while (*p) { if (*p == '/') last = p+1; ++p; }
                const char* name = last;
                // pattern: X.img -> letter name[0]
                size_t ln = 0; while (name[ln]) ++ln;
                if (ln >= 5) {
                    // check single-letter like c.img
                    if ((name[1] == '.' || name[2] == '.') && (name[ln-4] == '.' && name[ln-3] == 'i' && name[ln-2] == 'm' && name[ln-1] == 'g')) {
                        // fallback generic check below
                    }
                }
                // check patterns: "c.img" or "disk_c.img" or "drive_c.img"
                char letter = 0;
                if (ln == 5 && name[1] == '.' && name[2] == 'i' && name[3] == 'm' && name[4] == 'g') {
                    // e.g. c.img
                    if (name[0] >= 'a' && name[0] <= 'z') letter = name[0] - 'a' + 'A';
                    else if (name[0] >= 'A' && name[0] <= 'Z') letter = name[0];
                }
                // disk_x.img or drive_x.img
                if (!letter) {
                    for (size_t k = 0; k + 6 < ln; ++k) {
                        // look for '_x.img' at position k
                        if (name[k] == '_' && ((name[k+2] == '.' && name[k+3]=='i' && name[k+4]=='m' && name[k+5]=='g') || (k+4 < ln && name[k+4]=='.' && name[k+5]=='i' && name[k+6]=='m' && name[k+7]=='g'))) {
                            char c = name[k+1];
                            if (c >= 'a' && c <= 'z') letter = c - 'a' + 'A';
                            else if (c >= 'A' && c <= 'Z') letter = c;
                            break;
                        }
                    }
                }
                if (letter) {
                    // avoid double-mounting same letter
                    bool already = false;
                    for (size_t j = 0; j < sizeof(mounts)/sizeof(mounts[0]); ++j) if (mounts[j].in_use && mounts[j].letter == letter) { already = true; break; }
                    if (!already) {
                        // try mount by direct address
                        uintptr_t addr = (uintptr_t)mod->address;
                        if (limine_hhdm_request.response) {
                            uint64_t off = limine_hhdm_request.response->offset;
                            if ((uint64_t)addr < off) addr = (uintptr_t)(off + addr);
                        }
                        uint8_t* cand = (uint8_t*)addr;
                        size_t csize = (size_t)mod->size;
                        MountedFS* m = find_free_mount();
                        if (m && init_mounted_from_image(m, cand, csize, letter)) {
                            log_info("fat32: auto-mounted module as drive");
                        }
                    }
                }
            }
        }

        // Ensure the filesystem is initialized; try common module name variants.
        static int ensure_initialized() {
            if (fs_image) return 1;
            if (fat32_init_from_module("rootfs.img")) return 1;
            if (fat32_init_from_module("rootfs")) return 1;
            if (fat32_init_from_module("/boot/rootfs.img")) return 1;
            // Accept alternative module names that users sometimes provide
            // (e.g. "ramfs.img" or "ramfs"). This makes the boot process
            // tolerant to different image naming conventions.
            if (fat32_init_from_module("ramfs.img")) return 1;
            if (fat32_init_from_module("ramfs")) return 1;
            return 0;
        }

        // Normalize a short name from SFN entry to a C string (lowercase, dot-separated)
        static void sfn_to_cstring(const uint8_t* name11, char* out, size_t out_len) {
            size_t p = 0;
            // name (8 chars)
            for (int i = 0; i < 8 && p + 1 < out_len; ++i) {
                char c = (char)name11[i];
                if (c == ' ') continue;
                if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
                out[p++] = c;
            }
            // ext (3 chars)
            bool has_ext = false;
            char ext[4] = {0};
            size_t ep = 0;
            for (int i = 8; i < 11 && ep + 1 < sizeof(ext); ++i) {
                char c = (char)name11[i];
                if (c == ' ') continue;
                if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
                ext[ep++] = c; has_ext = true;
            }
            if (has_ext && p + 1 + ep < out_len) {
                out[p++] = '.';
                for (size_t i = 0; i < ep && p + 1 < out_len; ++i) out[p++] = ext[i];
            }
            out[p] = '\0';
        }

        // Case-insensitive compare of two NUL-terminated strings
        static int ci_cmp(const char* a, const char* b) {
            size_t i = 0;
            while (a[i] && b[i]) {
                char ca = a[i], cb = b[i];
                if (ca >= 'A' && ca <= 'Z') ca = ca - 'A' + 'a';
                if (cb >= 'A' && cb <= 'Z') cb = cb - 'A' + 'a';
                if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
                ++i;
            }
            return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        }

        // Lookup name in directory starting at cluster for a given mounted FS; returns first cluster for file/dir or 0 on not found.
        static uint32_t fat32_lookup_in_dir_for(MountedFS* m, uint32_t start_cluster, const char* name) {
            if (!m || !m->image || !name) return 0;
            uint32_t cluster = start_cluster;
            uint32_t cluster_size = m->bytes_per_sector * m->sectors_per_cluster;

            while (cluster < 0x0FFFFFF8) {
                uint8_t* buf = (uint8_t*)cluster_ptr_for(m, cluster);
                if (!buf) return 0;
                for (uint32_t off = 0; off + 32 <= cluster_size; off += 32) {
                    uint8_t first = buf[off];
                    if (first == 0x00) return 0; // no more entries
                    if (first == 0xE5) continue; // deleted
                    uint8_t attr = buf[off + 11];

                    if (attr == 0x0F) {
                        // LFN entry: skip here; rely on SFN matching (simple compatibility)
                        continue;
                    }

                    // Short name entry
                    char sfn[64];
                    sfn_to_cstring(buf + off, sfn, sizeof(sfn));
                    if (ci_cmp(sfn, name) == 0) {
                        uint16_t ch = read_le16(buf + off + 20);
                        uint16_t cl = read_le16(buf + off + 26);
                        uint32_t first = ((uint32_t)ch << 16) | (uint32_t)cl;
                        return first;
                    }
                }
                // advance to next cluster in chain
                uint32_t next = fat32_next_cluster_for(m, cluster);
                if (next >= 0x0FFFFFF8) break;
                cluster = next;
            }
            return 0;
        }

        // Backwards-compatible wrapper: lookup using the default mounted image
        static uint32_t fat32_lookup_in_dir(uint32_t start_cluster, const char* name) {
            return fat32_lookup_in_dir_for(get_default_mount(), start_cluster, name);
        }

        // Load a file (by path) into a bump-allocated buffer and return pointer; size in out_len
    // Internal: get file from a specific mounted FS (path is relative to that FS root)
    static void* fat32_get_file_alloc_for(MountedFS* m, const char* path, size_t* out_len) {
            if (!m || !m->image || !path) return NULL;
            // Trim leading slashes
            const char* p = path; while (*p == '/') ++p;
            // Start at root
            uint32_t cur_clust = m->root_cluster ? m->root_cluster : 2;
            if (*p == '\0') return NULL;

            char comp[256];
            const char* seg = p;
            while (*seg) {
                const char* slash = seg; while (*slash && *slash != '/') ++slash;
                size_t len = (size_t)(slash - seg);
                if (len == 0 || len >= sizeof(comp)) return NULL;
                // copy segment and lowercase it
                for (size_t i = 0; i < len; ++i) {
                    char c = seg[i]; if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a'; comp[i] = c; }
                comp[len] = '\0';
                uint32_t next = fat32_lookup_in_dir_for(m, cur_clust, comp);
                if (next == 0) return NULL;
                cur_clust = next;
                seg = slash; while (*seg == '/') ++seg;
            }

            // Now cur_clust points to the starting cluster of the file. We need the filesize.
            // Locate SFN entry in parent directory to get file size and starting cluster
            const char* q = p; const char* last_comp = p; uint32_t parent_clust = m->root_cluster ? m->root_cluster : 2;
            // find parent by iterating segments until final
            seg = p; const char* nextseg = seg; while (*nextseg) {
                const char* slash = nextseg; while (*slash && *slash != '/') ++slash;
                if (*slash == '/') {
                    size_t len = (size_t)(slash - nextseg);
                    if (len == 0 || len >= sizeof(comp)) return NULL;
                    for (size_t i = 0; i < len; ++i) { char c = nextseg[i]; if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a'; comp[i] = c; }
                    comp[len] = '\0';
                    uint32_t nxt = fat32_lookup_in_dir_for(m, parent_clust, comp);
                    if (nxt == 0) return NULL;
                    parent_clust = nxt;
                    nextseg = slash; while (*nextseg == '/') ++nextseg;
                } else {
                    size_t len = (size_t)(slash - nextseg);
                    for (size_t i = 0; i < len; ++i) { char c = nextseg[i]; if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a'; comp[i] = c; }
                    comp[len] = '\0';
                    last_comp = comp;
                    break;
                }
            }

            uint32_t cluster = parent_clust;
            uint32_t cluster_size = m->bytes_per_sector * m->sectors_per_cluster;
            uint32_t found_first = 0;
            uint32_t found_size = 0;
            while (cluster < 0x0FFFFFF8) {
                uint8_t* buf = (uint8_t*)cluster_ptr_for(m, cluster);
                if (!buf) return NULL;
                for (uint32_t off = 0; off + 32 <= cluster_size; off += 32) {
                    uint8_t first = buf[off]; if (first == 0x00) return NULL; if (first == 0xE5) continue;
                    uint8_t attr = buf[off + 11]; if (attr == 0x0F) continue;
                    char sfn[64]; sfn_to_cstring(buf + off, sfn, sizeof(sfn));
                    if (ci_cmp(sfn, last_comp) == 0) {
                        uint16_t ch = read_le16(buf + off + 20);
                        uint16_t cl = read_le16(buf + off + 26);
                        uint32_t firstcl = ((uint32_t)ch << 16) | (uint32_t)cl;
                        uint32_t fsize = read_le32(buf + off + 28);
                        found_first = firstcl; found_size = fsize; break;
                    }
                }
                if (found_first) break;
                uint32_t next = fat32_next_cluster_for(m, cluster); if (next >= 0x0FFFFFF8) break; cluster = next;
            }
            if (!found_first) return NULL;

            void* outbuf = hanacore::mem::kmalloc((size_t)found_size);
            if (!outbuf) return NULL;
            uint8_t* dst = (uint8_t*)outbuf;
            uint32_t remaining = found_size;
            uint32_t cur = found_first;
            while (remaining > 0 && cur < 0x0FFFFFF8) {
                uint8_t* src = (uint8_t*)cluster_ptr_for(m, cur);
                if (!src) return NULL;
                uint32_t tocopy = remaining < cluster_size ? remaining : cluster_size;
                memcpy(dst, src, tocopy);
                dst += tocopy; remaining -= tocopy;
                if (remaining == 0) break;
                cur = fat32_next_cluster_for(m, cur);
            }
            if (remaining != 0) return NULL;
            if (out_len) *out_len = found_size;
            return outbuf;
        }

    // Public: dispatch based on drive-letter prefix (e.g. "C:/path") or default FS
    void* fat32_get_file_alloc(const char* path, size_t* out_len) {
            if (!path) return NULL;
            // if path begins with 'X:/' or 'x:/' treat as drive reference
            if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':' && path[2] == '/') {
                char letter = path[0]; if (letter >= 'a' && letter <= 'z') letter = letter - 'a' + 'A';
                // find mounted FS
                for (size_t i = 0; i < sizeof(mounts)/sizeof(mounts[0]); ++i) {
                    if (mounts[i].in_use && mounts[i].letter == letter) {
                        return fat32_get_file_alloc_for(&mounts[i], path + 3, out_len);
                    }
                }
                return NULL;
            }
            // fallback to default rootfs image
            MountedFS* def = get_default_mount();
            if (!def) return NULL;
            return fat32_get_file_alloc_for(def, path, out_len);
        }

        // List directory entries for `path`. Calls `cb(name)` for each entry.
        // List directory entries for a specific mounted FS
        static int fat32_list_dir_for(MountedFS* m, const char* path, void (*cb)(const char* name)) {
            if (!m || !m->image || !path || !cb) return -1;
            const char* p = path; while (*p == '/') ++p;
            uint32_t cur_clust = m->root_cluster ? m->root_cluster : 2;
            if (*p != '\0') {
                char comp[256];
                const char* seg = p;
                while (*seg) {
                    const char* slash = seg; while (*slash && *slash != '/') ++slash;
                    size_t len = (size_t)(slash - seg);
                    if (len == 0 || len >= sizeof(comp)) return -1;
                    for (size_t i = 0; i < len; ++i) { char c = seg[i]; if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a'; comp[i] = c; }
                    comp[len] = '\0';
                    uint32_t next = fat32_lookup_in_dir_for(m, cur_clust, comp);
                    if (next == 0) return -1;
                    cur_clust = next;
                    seg = slash; while (*seg == '/') ++seg;
                }
            }

            uint32_t cluster = cur_clust;
            uint32_t cluster_size = m->bytes_per_sector * m->sectors_per_cluster;
            int count = 0;
            while (cluster < 0x0FFFFFF8) {
                uint8_t* buf = (uint8_t*)cluster_ptr_for(m, cluster);
                if (!buf) return -1;
                for (uint32_t off = 0; off + 32 <= cluster_size; off += 32) {
                    uint8_t first = buf[off]; if (first == 0x00) return count; if (first == 0xE5) continue;
                    uint8_t attr = buf[off + 11];
                    if (attr == 0x0F) continue; // skip LFN entries here
                    char sfn[256]; sfn_to_cstring(buf + off, sfn, sizeof(sfn));
                    cb(sfn); ++count;
                }
                uint32_t next = fat32_next_cluster_for(m, cluster); if (next >= 0x0FFFFFF8) break; cluster = next;
            }
            return count;
        }

        // Public list_dir: dispatch by drive-letter prefix or default FS
        int fat32_list_dir(const char* path, void (*cb)(const char* name)) {
            if (!ensure_initialized() || !path || !cb) return -1;
            if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':' && path[2] == '/') {
                char letter = path[0]; if (letter >= 'a' && letter <= 'z') letter = letter - 'a' + 'A';
                for (size_t i = 0; i < sizeof(mounts)/sizeof(mounts[0]); ++i) {
                    if (mounts[i].in_use && mounts[i].letter == letter) return fat32_list_dir_for(&mounts[i], path + 3, cb);
                }
                return -1;
            }
            MountedFS* def = get_default_mount();
            if (!def) return -1;
            return fat32_list_dir_for(def, path, cb);
        }

        // Enumerate mounted filesystems; callback receives a printable line.
        void fat32_list_mounts(void (*cb)(const char* line)) {
            if (!cb) return;
            for (size_t i = 0; i < sizeof(mounts)/sizeof(mounts[0]); ++i) {
                if (!mounts[i].in_use) continue;
                // Build a small printable line
                char buf[64]; size_t p = 0;
                buf[p++] = mounts[i].letter; buf[p++] = ':'; buf[p++] = ' ';
                uint64_t sz = (uint64_t)mounts[i].image_size;
                char rev[32]; size_t rn = 0;
                if (sz == 0) rev[rn++] = '0';
                while (sz > 0 && rn + 1 < sizeof(rev)) { rev[rn++] = (char)('0' + (sz % 10)); sz /= 10; }
                for (size_t j = 0; j < rn; ++j) buf[p + j] = rev[rn - 1 - j];
                p += rn;
                const char* suf = " bytes";
                size_t k = 0; while (suf[k]) { if (p + 1 < sizeof(buf)) buf[p++] = suf[k++]; else break; }
                buf[p] = '\0';
                cb(buf);
            }
        }

    } // namespace fs
} // namespace hanacore

// C wrappers: preserve the original C ABI expected by the rest of the kernel.
extern "C" {
int fat32_init_from_module(const char* module_name) {
    return hanacore::fs::fat32_init_from_module(module_name);
}

// Trigger auto-mounting of modules that encode drive letters in their names
// (e.g. c.img, disk_c.img). Exposed as a C ABI wrapper so callers from
// other translation units (like kernel_main) can invoke it at boot time.
void fat32_mount_all_letter_modules() {
    // mount_all_letter_modules is defined static within the
    // hanacore::fs namespace above; call it to auto-mount letter-encoded modules.
    hanacore::fs::mount_all_letter_modules();
}

// Provide a C-callable way to enumerate mounted volumes. The callback
// receives a printable ASCII line describing the mount (e.g. "C: 32 MiB").
void fat32_list_mounts(void (*cb)(const char* line)) {
    if (!cb) return;
    // iterate mounts in the C++ namespace
    for (size_t i = 0; i < sizeof(mounts) / sizeof(mounts[0]); ++i) {
        MountedFS* m = &mounts[i];
        if (!m->in_use) continue;
        // format basic info: "C: <size> bytes"
        // we avoid using sprintf to keep freestanding; build a small buffer.
        char buf[64];
        size_t p = 0;
        buf[p++] = m->letter;
        buf[p++] = ':';
        buf[p++] = ' ';
        // size in bytes -> decimal
        uint64_t sz = (uint64_t)m->image_size;
        // produce decimal string (reverse)
        char rev[32]; size_t rn = 0;
        if (sz == 0) rev[rn++] = '0';
        while (sz > 0 && rn + 1 < sizeof(rev)) { rev[rn++] = (char)('0' + (sz % 10)); sz /= 10; }
        // copy reversed into buf
        if (p + rn + 6 < sizeof(buf)) {
            for (size_t j = 0; j < rn; ++j) buf[p + j] = rev[rn - 1 - j];
            p += rn;
            buf[p++] = ' ';
            buf[p++] = 'b'; buf[p++] = 'y'; buf[p++] = 't'; buf[p++] = 'e'; buf[p++] = 's';
        }
        buf[p] = '\0';
        cb(buf);
    }
}

int64_t fat32_read_file(const char* path, void* buf, size_t len) {
    if (!hanacore::fs::ensure_initialized() || !path) return -1;
    // up to `len` bytes into `buf`.
    size_t fsz = 0;
    void* data = hanacore::fs::fat32_get_file_alloc(path, &fsz);
    if (!data) return -1;
    size_t tocopy = fsz < len ? fsz : len;
    if (buf && tocopy) memcpy(buf, data, tocopy);
    return (int64_t)tocopy;
}

void* fat32_get_file_alloc(const char* path, size_t* out_len) {
    return hanacore::fs::fat32_get_file_alloc(path, out_len);
}

int fat32_list_dir(const char* path, void (*cb)(const char* name)) {
    return hanacore::fs::fat32_list_dir(path, cb);
}
} // extern "C"
