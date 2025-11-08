#include "isofs.hpp"
#include "../utils/logger.hpp"
#include "../mem/heap.hpp"
#include <string.h>
#include <stddef.h>

namespace hanacore { namespace fs {

// Minimal ISO 9660 support
static const void* iso_image_data = nullptr;
static size_t iso_image_size = 0;
static int isofs_ready = 0;
static uint32_t logical_block_size = 2048;
static uint32_t root_cluster = 0;

// ISO 9660 structures (simplified)
struct IsoVolumeDescriptor {
    uint8_t type;
    uint8_t id[5];      // "CD001"
    uint8_t version;
    uint8_t unused[2041];
} __attribute__((packed));

struct IsoPrimaryVolumeDescriptor {
    uint8_t type;
    uint8_t id[5];
    uint8_t version;
    uint8_t unused1;
    uint8_t system_id[32];
    uint8_t volume_id[32];
    uint8_t unused2[8];
    uint32_t space_size_le;
    uint32_t space_size_be;
    uint8_t unused3[32];
    uint32_t volume_set_size_le;
    uint32_t volume_set_size_be;
    uint32_t volume_seq_number_le;
    uint32_t volume_seq_number_be;
    uint32_t logical_block_size_le;
    uint32_t logical_block_size_be;
    uint32_t path_table_size_le;
    uint32_t path_table_size_be;
    uint32_t path_table_l_block;
    uint32_t path_table_opt_l_block;
    uint32_t path_table_m_block;
    uint32_t path_table_opt_m_block;
    uint8_t root_dir_record[34];
    uint8_t unused4[1858];
} __attribute__((packed));

struct IsoDirRecord {
    uint8_t length;
    uint8_t ext_attr_length;
    uint32_t extent_le;
    uint32_t extent_be;
    uint32_t data_len_le;
    uint32_t data_len_be;
    uint8_t date_time[7];
    uint8_t flags;
    uint8_t file_unit_size;
    uint8_t interleave_gap_size;
    uint32_t seq_number_le;
    uint32_t seq_number_be;
    uint8_t name_len;
    uint8_t name[];
} __attribute__((packed));

// Helper function to read 32-bit little-endian
static inline uint32_t read_le32(const uint32_t* p) {
    const uint8_t* b = (const uint8_t*)p;
    return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
}

// Helper function to read a sector
static const void* read_iso_sector(uint32_t sector) {
    uint64_t offset = (uint64_t)sector * logical_block_size;
    if (offset >= iso_image_size) return nullptr;
    return (const uint8_t*)iso_image_data + offset;
}

int isofs_init(void) {
    return 0;
}

int isofs_init_from_memory(const void* data, size_t size) {
    if (!data || size < 0x8000) {
        hanacore::utils::log_info_cpp("[ISOFS] Invalid ISO image size");
        return -1;
    }
    
    iso_image_data = data;
    iso_image_size = size;
    logical_block_size = 2048;
    
    // Check for primary volume descriptor at sector 16 (0x8000)
    const IsoPrimaryVolumeDescriptor* pvd = (const IsoPrimaryVolumeDescriptor*)((uintptr_t)data + 0x8000);
    
    if (pvd->type != 1 || strncmp((const char*)pvd->id, "CD001", 5) != 0) {
        hanacore::utils::log_info_cpp("[ISOFS] Invalid primary volume descriptor");
        return -1;
    }
    
    // Extract logical block size
    logical_block_size = read_le32(&pvd->logical_block_size_le);
    if (logical_block_size != 2048 && logical_block_size != 512) {
        hanacore::utils::log_info_cpp("[ISOFS] Unsupported logical block size");
        return -1;
    }
    
    // Extract root directory extent (first sector of root dir)
    const IsoDirRecord* root_dir = (const IsoDirRecord*)pvd->root_dir_record;
    root_cluster = read_le32(&root_dir->extent_le);
    
    if (root_cluster == 0) {
        hanacore::utils::log_info_cpp("[ISOFS] Invalid root directory cluster");
        return -1;
    }
    
    isofs_ready = 1;
    hanacore::utils::log_ok_cpp("[ISOFS] ISO 9660 filesystem initialized");
    return 0;
}

int isofs_list_dir(const char* path, void (*cb)(const char* name)) {
    if (!cb || !isofs_ready || !iso_image_data) return -1;
    
    if (!path || *path == '\0') path = "/";
    
    // For now, only support root directory listing
    if (strcmp(path, "/") != 0) {
        return -1; // Not implemented for subdirectories yet
    }
    
    // Read root directory sector
    const uint8_t* dir_data = (const uint8_t*)read_iso_sector(root_cluster);
    if (!dir_data) return -1;
    
    uint32_t dir_offset = 0;
    uint32_t max_offset = logical_block_size;
    
    // Parse directory records
    while (dir_offset < max_offset) {
        const IsoDirRecord* rec = (const IsoDirRecord*)(dir_data + dir_offset);
        
        if (rec->length == 0) break; // End of directory
        
        // Skip . and .. entries
        if (rec->name_len == 1 && (rec->name[0] == 0 || rec->name[0] == 1)) {
            dir_offset += rec->length;
            continue;
        }
        
        // Extract filename
        char name[256];
        uint8_t name_len = rec->name_len;
        
        // Copy name
        if (name_len > 0 && name_len < sizeof(name)) {
            memcpy(name, rec->name, name_len);
            
            // Remove version suffix (;1)
            if (name_len > 2 && name[name_len - 2] == ';') {
                name_len -= 2;
            }
            
            // Convert to lowercase if uppercase
            for (uint8_t i = 0; i < name_len; ++i) {
                if (name[i] >= 'A' && name[i] <= 'Z') {
                    name[i] = name[i] - 'A' + 'a';
                }
            }
            
            name[name_len] = '\0';
            
            // Skip names with special characters (system files)
            if (name[0] != '\x00' && name[0] != '\x01') {
                cb(name);
            }
        }
        
        dir_offset += rec->length;
        
        // Align to next record boundary
        if (dir_offset % 32 != 0) {
            dir_offset += 32 - (dir_offset % 32);
        }
    }
    
    return 0;
}

int64_t isofs_read_file(const char* path, void* buf, size_t len) {
    if (!path || !buf || !isofs_ready) return -1;
    
    // Minimal implementation
    return -1;
}

void* isofs_get_file_alloc(const char* path, size_t* out_len) {
    if (!path || !out_len || !isofs_ready) return nullptr;
    
    *out_len = 0;
    return nullptr;
}

int isofs_list_mounts(void (*cb)(const char* line)) {
    if (!cb || !isofs_ready) return -1;
    
    if (iso_image_data) {
        cb("ISOFS mount: ISO 9660 image");
    }
    return 0;
}

} } // namespace hanacore::fs

// C-linkage wrappers
extern "C" {
    int isofs_init(void) {
        return hanacore::fs::isofs_init();
    }

    int isofs_init_from_memory(const void* data, size_t size) {
        return hanacore::fs::isofs_init_from_memory(data, size);
    }

    int isofs_list_dir(const char* path, void (*cb)(const char* name)) {
        return hanacore::fs::isofs_list_dir(path, cb);
    }

    int64_t isofs_read_file(const char* path, void* buf, size_t len) {
        return hanacore::fs::isofs_read_file(path, buf, len);
    }

    void* isofs_get_file_alloc(const char* path, size_t* out_len) {
        return hanacore::fs::isofs_get_file_alloc(path, out_len);
    }

    int isofs_list_mounts(void (*cb)(const char* line)) {
        return hanacore::fs::isofs_list_mounts(cb);
    }
}

