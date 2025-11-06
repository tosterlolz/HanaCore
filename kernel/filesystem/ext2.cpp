// Minimal ext2 helper (scaffold)
// This file provides a tiny read-only interface to load a filesystem image
// from a Limine module and read simple files. It's intentionally small and
// incomplete — a starting point for a full ext2 implementation.

#include "ext2.hpp"
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

// Use the global module_request defined in kernel/kernel.cpp so we don't
// register a second Limine request with the same ID (that causes a panic).
extern volatile struct limine_module_request module_request;

// For HHDM handling (provided by limine_entry.c)
extern volatile struct limine_hhdm_request limine_hhdm_request;

int ext2_init_from_module(const char* module_name) {
    if (!module_request.response) return 0;
    volatile struct limine_module_response* resp = module_request.response;
    for (uint64_t i = 0; i < resp->module_count; ++i) {
        volatile struct limine_file* mod = resp->modules[i];
        const char* path = (const char*)(uintptr_t)mod->path;
        if (!path) continue;
        // simple suffix match
        size_t pl = 0; while (path[pl]) ++pl;
        size_t ml = 0; while (module_name[ml]) ++ml;
        if (pl >= ml && strcmp(path + pl - ml, module_name) == 0) {
            uintptr_t addr = (uintptr_t)mod->address;
            if (limine_hhdm_request.response) {
                uint64_t off = limine_hhdm_request.response->offset;
                if ((uint64_t)addr < off) addr = (uintptr_t)(off + addr);
            }
            fs_image = (uint8_t*)addr;
            fs_image_size = (size_t)mod->size;
            log_ok("ext2: initialized from module");
            return 1;
        }
    }
    return 0;
}

// Helpers for ext2 parsing (minimal, only what's needed for simple /bin files)
struct ext2_super_block {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
};

struct ext2_group_desc {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint32_t bg_reserved[3];
};

struct ext2_inode {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];
};

struct ext2_dir_entry {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
};

// Internal helper: read block pointer (returns pointer into fs_image)
static inline void* ext2_block_ptr(uint32_t block, uint32_t block_size) {
    if (!fs_image) return NULL;
    uint64_t off = (uint64_t)block * (uint64_t)block_size;
    if (off >= fs_image_size) return NULL;
    return (void*)(fs_image + off);
}

// Read inode number `ino` into out_inode. Returns 0 on success.
static int ext2_read_inode(uint32_t ino, struct ext2_inode* out, const struct ext2_super_block* sb, uint32_t block_size) {
    if (!fs_image || ino == 0) return -1;
    uint64_t gdt_off = (block_size == 1024) ? 2048 : block_size;
    if (gdt_off + sizeof(struct ext2_group_desc) > fs_image_size) return -1;
    const struct ext2_group_desc* gd = (const struct ext2_group_desc*)(fs_image + gdt_off);
    uint32_t inode_table_block = gd->bg_inode_table;
    uint64_t inode_table_off = (uint64_t)inode_table_block * block_size;
    uint32_t inode_size = sb->s_inode_size ? sb->s_inode_size : 128;
    uint64_t idx = (uint64_t)(ino - 1) * inode_size;
    if (inode_table_off + idx + inode_size > fs_image_size) return -1;
    const uint8_t* src = fs_image + inode_table_off + idx;
    memset(out, 0, sizeof(*out));
    out->i_mode = *(const uint16_t*)(src + 0);
    out->i_uid = *(const uint16_t*)(src + 2);
    out->i_size = *(const uint32_t*)(src + 4);
    out->i_atime = *(const uint32_t*)(src + 8);
    out->i_ctime = *(const uint32_t*)(src + 12);
    out->i_mtime = *(const uint32_t*)(src + 16);
    out->i_dtime = *(const uint32_t*)(src + 20);
    out->i_gid = *(const uint16_t*)(src + 24);
    out->i_links_count = *(const uint16_t*)(src + 26);
    out->i_blocks = *(const uint32_t*)(src + 28);
    out->i_flags = *(const uint32_t*)(src + 32);
    const uint32_t* blocks = (const uint32_t*)(src + 40);
    for (int i = 0; i < 15; ++i) out->i_block[i] = blocks[i];
    return 0;
}

// Walk directory `inode_no` for a single path component `name`. On success
// returns the inode number of the entry, or 0 if not found or error.
static uint32_t ext2_lookup_in_dir(uint32_t inode_no, const char* name, const struct ext2_super_block* sb, uint32_t block_size) {
    struct ext2_inode inode;
    if (ext2_read_inode(inode_no, &inode, sb, block_size) != 0) return 0;
    uint32_t sz = inode.i_size;
    uint32_t per_block = block_size;
    uint32_t read = 0;
    for (int bi = 0; bi < 12 && read < sz; ++bi) {
        uint32_t b = inode.i_block[bi];
        if (b == 0) { read += per_block; continue; }
        uint8_t* blk = (uint8_t*)ext2_block_ptr(b, block_size);
        if (!blk) return 0;
        uint32_t off = 0;
        while (off < block_size) {
            struct ext2_dir_entry* de = (struct ext2_dir_entry*)(blk + off);
            if (de->inode == 0) break;
            char namebuf[256];
            uint32_t nl = de->name_len;
            if (nl >= sizeof(namebuf)) nl = sizeof(namebuf)-1;
            memcpy(namebuf, blk + off + 8, nl);
            namebuf[nl] = '\0';
            if (nl == strlen(name) && memcmp(namebuf, name, nl) == 0) {
                return de->inode;
            }
            if (de->rec_len == 0) break;
            off += de->rec_len;
        }
        read += per_block;
    }
    return 0;
}

void* ext2_get_file_alloc(const char* path, size_t* out_len) {
    if (!fs_image || !path) return NULL;
    if (fs_image_size < 2048) return NULL;
    struct ext2_super_block sb;
    memcpy(&sb, fs_image + 1024, sizeof(sb));
    if (sb.s_magic != 0xEF53) return NULL;
    uint32_t block_size = 1024u << sb.s_log_block_size;
    const char* p = path;
    while (*p == '/') ++p;
    if (*p == '\0') return NULL;
    char comp[256];
    uint32_t cur_ino = 2; // root
    const char* seg = p;
    while (*seg) {
        const char* slash = seg;
        while (*slash && *slash != '/') ++slash;
        size_t len = (size_t)(slash - seg);
        if (len == 0 || len >= sizeof(comp)) return NULL;
        memcpy(comp, seg, len); comp[len] = '\0';
        uint32_t next = ext2_lookup_in_dir(cur_ino, comp, &sb, block_size);
        if (next == 0) return NULL;
        cur_ino = next;
        seg = slash;
        while (*seg == '/') ++seg;
    }
    struct ext2_inode inode;
    if (ext2_read_inode(cur_ino, &inode, &sb, block_size) != 0) return NULL;
    uint32_t fsize = inode.i_size;
    void* buf = bump_alloc_alloc((size_t)fsize, 0x1000);
    if (!buf) return NULL;
    uint8_t* dst = (uint8_t*)buf;
    uint32_t remaining = fsize;
    for (int bi = 0; bi < 12 && remaining > 0; ++bi) {
        uint32_t b = inode.i_block[bi];
        if (b == 0) break;
        uint8_t* src = (uint8_t*)ext2_block_ptr(b, block_size);
        if (!src) return NULL;
        uint32_t tocopy = remaining < block_size ? remaining : block_size;
        memcpy(dst, src, tocopy);
        dst += tocopy;
        remaining -= tocopy;
    }
    if (remaining != 0) {
        return NULL;
    }
    if (out_len) *out_len = fsize;
    return buf;
}
