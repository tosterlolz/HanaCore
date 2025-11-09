#pragma once

#include <stdint.h>
#include <stddef.h>
#include "hanafs.hpp"

namespace ext3 {
    // Debug: List all files in /bin and log their names
    void debug_list_bin();
    void set_image(void* image, size_t size);
    // Minimal ext3 skeleton API. This file provides a placeholder backend
    // that can be fleshed out later. Functions return -1 when unimplemented.

    int init(void);
    int mount(int drive, const char* mount_point);
    void* get_file_alloc(const char* path, size_t* out_len);
    int list_dir(const char* path, void (*cb)(const char* name));
    // Internal helpers for inode and directory reading
    // (not exposed outside ext3.cpp)

    // Ext3 superblock structure
    struct ext3_superblock {
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
    };

    // Ext3 group descriptor
    struct ext3_group_desc {
        uint32_t bg_block_bitmap;
        uint32_t bg_inode_bitmap;
        uint32_t bg_inode_table;
        uint16_t bg_free_blocks_count;
        uint16_t bg_free_inodes_count;
        uint16_t bg_used_dirs_count;
    };

    // Ext3 inode structure
    struct ext3_inode {
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
        uint32_t i_block[15];
        // ... more fields as needed
    };

    // Ext3 directory entry
    struct ext3_dir_entry {
        uint32_t inode;
        uint16_t rec_len;
        uint8_t name_len;
        uint8_t file_type;
        char name[255]; // max name length
    };

    // API for block reading
    void ext3_set_image(void* image, size_t size);
    uint32_t ext3_block_size();
    void* ext3_read_block(uint32_t block_num);
}
