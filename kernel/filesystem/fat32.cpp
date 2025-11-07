// Minimal fat32_ reader that exposes the same small API the kernel expects
// (fat32_* names are kept for compatibility with the rest of the kernel).
// This implementation is read-only and intentionally small: it initializes
// from a Limine module, can list directory entries and load file contents.

#include "fat32.hpp"
#include <stdint.h>
#include <stddef.h>

#include "../../boot/limine.h"
#include "../utils/logger.hpp"
#include "../mem/bump_alloc.hpp"

extern "C" {
    int memcmp(const void* s1, const void* s2, size_t n);
    void* memcpy(void* dst, const void* src, size_t n);
    void* memset(void* s, int c, size_t n);
    int strcmp(const char* a, const char* b);
    size_t strlen(const char* s);
}

// Internal state: pointer to module image in memory and its size
static uint8_t* fs_image = nullptr;
static size_t fs_image_size = 0;

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

        // Convert a cluster number to an offset pointer into fs_image (or NULL on OOB)
        static inline void* cluster_ptr(uint32_t cluster) {
            if (!fs_image) return NULL;
            if (cluster < 2) return NULL;
            uint64_t off = first_data_offset + (uint64_t)(cluster - 2) * (uint64_t)(bytes_per_sector * sectors_per_cluster);
            if (off >= fs_image_size) return NULL;
            return (void*)(fs_image + off);
        }

        // Read fat32_ entry for `cluster` (returns next cluster number or 0x0FFFFFFF on EOC)
        static uint32_t fat32_next_cluster(uint32_t cluster) {
            if (!fs_image) return 0x0FFFFFFF;
            uint64_t off = fat_offset + (uint64_t)cluster * 4;
            if (off + 4 > fs_image_size) return 0x0FFFFFFF;
            uint32_t v = read_le32(fs_image + off) & 0x0FFFFFFF;
            return v;
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
                if (path) {
                    log_info("fat32: module path:");
                    log_info(path);
                } else {
                    log_info("fat32: module path: <null>");
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
                fs_image = (uint8_t*)addr;
                fs_image_size = (size_t)mod->size;

                // Quick sanity check: ensure we have at least a sector
                if (fs_image_size < 512) return 0;

                // Read BPB fields
                bytes_per_sector = read_le16(fs_image + 11);
                sectors_per_cluster = fs_image[13];
                reserved_sector_count = read_le16(fs_image + 14);
                num_fats = fs_image[16];
                fat_size_sectors = read_le32(fs_image + 36); // FAT32
                root_cluster = read_le32(fs_image + 44);

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
            log_fail("fat32: no matching module found");
            return 0;
        }

        // Ensure the filesystem is initialized; try common module name variants.
        static int ensure_initialized() {
            if (fs_image) return 1;
            if (fat32_init_from_module("rootfs.img")) return 1;
            if (fat32_init_from_module("rootfs")) return 1;
            if (fat32_init_from_module("/boot/rootfs.img")) return 1;
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

        // Lookup name in directory starting at cluster; returns first cluster for file/dir or 0 on not found.
        static uint32_t fat32_lookup_in_dir(uint32_t start_cluster, const char* name) {
            if (!fs_image || !name) return 0;
            uint32_t cluster = start_cluster;
            uint32_t cluster_size = bytes_per_sector * sectors_per_cluster;

            while (cluster < 0x0FFFFFF8) {
                uint8_t* buf = (uint8_t*)cluster_ptr(cluster);
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
                uint32_t next = fat32_next_cluster(cluster);
                if (next >= 0x0FFFFFF8) break;
                cluster = next;
            }
            return 0;
        }

        // Load a file (by path) into a bump-allocated buffer and return pointer; size in out_len
    void* fat32_get_file_alloc(const char* path, size_t* out_len) {
            if (!fs_image || !path) return NULL;
            // Trim leading slashes
            const char* p = path; while (*p == '/') ++p;
            // Start at root
            uint32_t cur_clust = root_cluster ? root_cluster : 2;
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
                uint32_t next = fat32_lookup_in_dir(cur_clust, comp);
                if (next == 0) return NULL;
                cur_clust = next;
                seg = slash; while (*seg == '/') ++seg;
            }

            // Now cur_clust points to the starting cluster of the file. We need the filesize.
            // To get filesize, we must locate the SFN entry again in the parent directory.
            // Simple approach: find the final path component's SFN by searching parent dir.
            // Recompute parent dir cluster
            const char* last_slash = path;
            while (*last_slash) ++last_slash;
            // walk back to find parent component start
            const char* q = p; const char* last_comp = p; uint32_t parent_clust = root_cluster ? root_cluster : 2;
            // find parent by iterating segments until final
            seg = p; const char* nextseg = seg; while (*nextseg) {
                const char* slash = nextseg; while (*slash && *slash != '/') ++slash;
                if (*slash == '/') {
                    // not last
                    size_t len = (size_t)(slash - nextseg);
                    if (len == 0 || len >= sizeof(comp)) return NULL;
                    for (size_t i = 0; i < len; ++i) { char c = nextseg[i]; if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a'; comp[i] = c; }
                    comp[len] = '\0';
                    uint32_t nxt = fat32_lookup_in_dir(parent_clust, comp);
                    if (nxt == 0) return NULL;
                    parent_clust = nxt;
                    nextseg = slash; while (*nextseg == '/') ++nextseg;
                } else {
                    // this is last component
                    size_t len = (size_t)(slash - nextseg);
                    for (size_t i = 0; i < len; ++i) { char c = nextseg[i]; if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a'; comp[i] = c; }
                    comp[len] = '\0';
                    last_comp = comp;
                    break;
                }
            }

            // Locate SFN entry in parent directory to get file size and starting cluster
            uint32_t cluster = parent_clust;
            uint32_t cluster_size = bytes_per_sector * sectors_per_cluster;
            uint32_t found_first = 0;
            uint32_t found_size = 0;
            while (cluster < 0x0FFFFFF8) {
                uint8_t* buf = (uint8_t*)cluster_ptr(cluster);
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
                uint32_t next = fat32_next_cluster(cluster); if (next >= 0x0FFFFFF8) break; cluster = next;
            }
            if (!found_first) return NULL;

            void* outbuf = bump_alloc_alloc((size_t)found_size, 0x1000);
            if (!outbuf) return NULL;
            uint8_t* dst = (uint8_t*)outbuf;
            uint32_t remaining = found_size;
            uint32_t cur = found_first;
            while (remaining > 0 && cur < 0x0FFFFFF8) {
                uint8_t* src = (uint8_t*)cluster_ptr(cur);
                if (!src) return NULL;
                uint32_t tocopy = remaining < cluster_size ? remaining : cluster_size;
                memcpy(dst, src, tocopy);
                dst += tocopy; remaining -= tocopy;
                if (remaining == 0) break;
                cur = fat32_next_cluster(cur);
            }
            if (remaining != 0) return NULL;
            if (out_len) *out_len = found_size;
            return outbuf;
        }

        // List directory entries for `path`. Calls `cb(name)` for each entry.
        int fat32_list_dir(const char* path, void (*cb)(const char* name)) {
            if (!ensure_initialized() || !path || !cb) return -1;
            // Trim leading slashes
            const char* p = path; while (*p == '/') ++p;
            uint32_t cur_clust = root_cluster ? root_cluster : 2;
            if (*p != '\0') {
                char comp[256];
                const char* seg = p;
                while (*seg) {
                    const char* slash = seg; while (*slash && *slash != '/') ++slash;
                    size_t len = (size_t)(slash - seg);
                    if (len == 0 || len >= sizeof(comp)) return -1;
                    for (size_t i = 0; i < len; ++i) { char c = seg[i]; if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a'; comp[i] = c; }
                    comp[len] = '\0';
                    uint32_t next = fat32_lookup_in_dir(cur_clust, comp);
                    if (next == 0) return -1;
                    cur_clust = next;
                    seg = slash; while (*seg == '/') ++seg;
                }
            }

            uint32_t cluster = cur_clust;
            uint32_t cluster_size = bytes_per_sector * sectors_per_cluster;
            int count = 0;
            while (cluster < 0x0FFFFFF8) {
                uint8_t* buf = (uint8_t*)cluster_ptr(cluster);
                if (!buf) return -1;
                for (uint32_t off = 0; off + 32 <= cluster_size; off += 32) {
                    uint8_t first = buf[off]; if (first == 0x00) return count; if (first == 0xE5) continue;
                    uint8_t attr = buf[off + 11];
                    if (attr == 0x0F) continue; // skip LFN entries here
                    char sfn[256]; sfn_to_cstring(buf + off, sfn, sizeof(sfn));
                    cb(sfn); ++count;
                }
                uint32_t next = fat32_next_cluster(cluster); if (next >= 0x0FFFFFF8) break; cluster = next;
            }
            return count;
        }

    } // namespace fs
} // namespace hanacore

// C wrappers: preserve the original C ABI expected by the rest of the kernel.
extern "C" {
int fat32_init_from_module(const char* module_name) {
    return hanacore::fs::fat32_init_from_module(module_name);
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
