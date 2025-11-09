#include "ext3.hpp"
#include "../utils/logger.hpp"
#include <stddef.h>

#include "../libs/libc.h"
#include "../mem/heap.hpp"

extern "C" void __attribute__((weak)) fat32_progress_update(int percent) {}

namespace ext3 {
    // ...existing code...

    // Internal ext3 image state
    static uint8_t* ext3_image = nullptr;
    static size_t ext3_image_size = 0;
    static ext3_superblock superblock;
    static uint32_t block_size = 1024;

    void set_image(void* image, size_t size) {
        ext3_image = (uint8_t*)image;
        ext3_image_size = size;
        // Superblock is always at offset 1024
        if (size < 2048) return;
        memcpy(&superblock, ext3_image + 1024, sizeof(ext3_superblock));
        if (superblock.s_magic == 0xEF53) {
            block_size = 1024 << superblock.s_log_block_size;
        } else {
            block_size = 1024;
        }
    }

    uint32_t ext3_block_size() {
        return block_size;
    }

    void* ext3_read_block(uint32_t block_num) {
        if (!ext3_image || block_num == 0) return nullptr;
        size_t offset = block_num * block_size;
        if (offset + block_size > ext3_image_size) return nullptr;
        return ext3_image + offset;
    }

    int init(void) {
        if (!ext3_image || ext3_image_size < 2048) {
            hanacore::utils::log_fail_cpp("[EXT3] No image set or too small");
            return -1;
        }
        memcpy(&superblock, ext3_image + 1024, sizeof(ext3_superblock));
        if (superblock.s_magic != 0xEF53) {
            hanacore::utils::log_fail_cpp("[EXT3] Invalid superblock magic");
            return -1;
        }
        block_size = 1024 << superblock.s_log_block_size;
        hanacore::utils::log_ok_cpp("[EXT3] Superblock parsed, block size %u", block_size);
        return 0;
    }

    int mount(int drive, const char* mount_point) {
        // For now, just check superblock and return success if valid
        if (init() == 0) {
            hanacore::utils::log_ok_cpp("[EXT3] Mounted ext3 image at %s", mount_point ? mount_point : "/");
            return 0;
        }
        hanacore::utils::log_fail_cpp("[EXT3] mount failed");
        return -1;
    }

    // Helper: read inode by index
    static int read_inode(uint32_t inode_idx, ext3_inode* out_inode) {
        if (!ext3_image) return -1;
        // Inodes start at block group 0's inode table
        uint32_t group_desc_block = (block_size == 1024) ? 2 : 1;
        ext3_group_desc* gd = (ext3_group_desc*)ext3_read_block(group_desc_block);
        if (!gd) return -1;
        uint32_t inode_table_block = gd->bg_inode_table;
        uint32_t inode_size = sizeof(ext3_inode);
        uint32_t inodes_per_block = block_size / inode_size;
        uint32_t block_offset = (inode_idx - 1) / inodes_per_block;
        uint32_t inode_offset = (inode_idx - 1) % inodes_per_block;
        uint8_t* block = (uint8_t*)ext3_read_block(inode_table_block + block_offset);
        if (!block) return -1;
        memcpy(out_inode, block + inode_offset * inode_size, inode_size);
        return 0;
    }

    // Helper: find inode for a given path (only supports / and /bin/NAME for now)
    static int find_inode_by_path(const char* path, ext3_inode* out_inode) {
        if (!path || !out_inode) return -1;
        if (strcmp(path, "/") == 0) {
            // Root inode is always inode #2
            return read_inode(2, out_inode);
        }
        // Only support /bin/NAME
        if (strncmp(path, "/bin/", 5) == 0) {
            ext3_inode root_inode;
            if (read_inode(2, &root_inode) != 0) return -1;
            // Traverse root directory to find 'bin'
            for (int i = 0; i < 12; ++i) {
                if (root_inode.i_block[i] == 0) continue;
                uint8_t* block = (uint8_t*)ext3_read_block(root_inode.i_block[i]);
                if (!block) continue;
                size_t off = 0;
                while (off < block_size) {
                    ext3_dir_entry* entry = (ext3_dir_entry*)(block + off);
                    if (entry->inode && entry->name_len > 0 && entry->name_len < 255) {
                        char name[256] = {0};
                        memcpy(name, entry->name, entry->name_len);
                        name[entry->name_len] = 0;
                        if (strcmp(name, "bin") == 0) {
                            // Found /bin, now traverse /bin
                            ext3_inode bin_inode;
                            if (read_inode(entry->inode, &bin_inode) != 0) return -1;
                            // Traverse /bin for NAME
                            const char* fname = path + 5;
                            for (int j = 0; j < 12; ++j) {
                                if (bin_inode.i_block[j] == 0) continue;
                                uint8_t* bblock = (uint8_t*)ext3_read_block(bin_inode.i_block[j]);
                                if (!bblock) continue;
                                size_t boff = 0;
                                while (boff < block_size) {
                                    ext3_dir_entry* bent = (ext3_dir_entry*)(bblock + boff);
                                    if (bent->inode && bent->name_len > 0 && bent->name_len < 255) {
                                        char bname[256] = {0};
                                        memcpy(bname, bent->name, bent->name_len);
                                        bname[bent->name_len] = 0;
                                        if (strcmp(bname, fname) == 0) {
                                            return read_inode(bent->inode, out_inode);
                                        }
                                    }
                                    if (bent->rec_len == 0) break;
                                    boff += bent->rec_len;
                                }
                            }
                        }
                    }
                    if (entry->rec_len == 0) break;
                    off += entry->rec_len;
                }
            }
        }
        return -1;
    }

    void* get_file_alloc(const char* path, size_t* out_len) {
        ext3_inode inode;
        if (find_inode_by_path(path, &inode) != 0) {
            hanacore::utils::log_fail_cpp("[EXT3] get_file_alloc: not found %s", path ? path : "(null)");
            if (out_len) *out_len = 0;
            return nullptr;
        }
        // Only support direct blocks for now
        if (out_len) *out_len = inode.i_size;
        if (inode.i_size == 0) return nullptr;
        uint8_t* buf = (uint8_t*)hanacore::mem::kmalloc(inode.i_size);
        size_t copied = 0;
        for (int i = 0; i < 12 && copied < inode.i_size; ++i) {
            if (inode.i_block[i] == 0) continue;
            uint8_t* block = (uint8_t*)ext3_read_block(inode.i_block[i]);
            if (!block) break;
            size_t to_copy = block_size;
            if (copied + to_copy > inode.i_size) to_copy = inode.i_size - copied;
            memcpy(buf + copied, block, to_copy);
            copied += to_copy;
        }
        return buf;
    }

    int list_dir(const char* path, void (*cb)(const char* name)) {
        ext3_inode inode;
        if (find_inode_by_path(path, &inode) != 0) return -1;
        // Only support directories with direct blocks
        for (int i = 0; i < 12; ++i) {
            if (inode.i_block[i] == 0) continue;
            uint8_t* block = (uint8_t*)ext3_read_block(inode.i_block[i]);
            if (!block) continue;
            size_t off = 0;
            while (off < block_size) {
                ext3_dir_entry* entry = (ext3_dir_entry*)(block + off);
                if (entry->inode && entry->name_len > 0 && entry->name_len < 255) {
                    char name[256] = {0};
                    memcpy(name, entry->name, entry->name_len);
                    name[entry->name_len] = 0;
                    cb(name);
                }
                if (entry->rec_len == 0) break;
                off += entry->rec_len;
            }
        }
        return 0;
    }

}
