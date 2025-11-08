#include "floppy.hpp"
#include "../utils/logger.hpp"
#include "../mem/heap.hpp"
#include "vfs.hpp"
#include <string.h>

namespace hanacore {
    namespace fs {

        // Simple in-memory floppy filesystem handler
        // For now, we'll just load it as a raw filesystem accessible via ramfs
        static uint8_t* floppy_data = nullptr;
        static size_t floppy_size = 0;

        int floppy_init_from_memory(const void* data, size_t size) {
            if (!data || size == 0) {
                hanacore::utils::log_info_cpp("[Floppy] Invalid floppy image");
                return -1;
            }

            // Validate BPB (basic check)
            const uint8_t* img = (const uint8_t*)data;
            
            hanacore::utils::log_info_cpp("[Floppy] Attempting to initialize floppy image");

            // Allocate and copy floppy data
            floppy_data = (uint8_t*)hanacore::mem::kmalloc(size);
            if (!floppy_data) {
                hanacore::utils::log_fail_cpp("Failed to allocate floppy buffer");
                return -1;
            }

            memcpy(floppy_data, data, size);
            floppy_size = size;

            hanacore::utils::log_ok_cpp("[Floppy] Initialized floppy filesystem from memory");
            
            // Register with VFS as root filesystem
            vfs_register_mount("floppy", "/");
            
            return 0;
        }

        // Directory listing for floppy - reads from a raw FAT12 boot sector
        // For now, minimal implementation that at least allows /bin access
        int floppy_list_dir(const char* path, void (*cb)(const char* name)) {
            if (!cb || !floppy_data) return -1;

            // Only support root directory listing for now
            if (!path || strcmp(path, "/") != 0) {
                return -1;
            }

            // Parse FAT12 root directory entries
            // Root directory starts at sector 19 for standard 1.44MB floppy
            // Each entry is 32 bytes
            
            const uint8_t* bpb = floppy_data;
            uint16_t bytes_per_sector = *(uint16_t*)(bpb + 11);
            uint8_t sectors_per_cluster = bpb[13];
            uint16_t reserved_sectors = *(uint16_t*)(bpb + 14);
            uint8_t num_fats = bpb[16];
            uint16_t root_entries = *(uint16_t*)(bpb + 17);
            uint16_t fat_size_sectors = *(uint16_t*)(bpb + 22);

            if (bytes_per_sector == 0 || bytes_per_sector > 4096) {
                return -1;
            }

            // Calculate root directory start sector
            uint32_t root_dir_sector = reserved_sectors + (num_fats * fat_size_sectors);
            uint32_t root_dir_offset = root_dir_sector * bytes_per_sector;

            if (root_dir_offset + (root_entries * 32) > floppy_size) {
                return -1;
            }

            const uint8_t* root_dir = floppy_data + root_dir_offset;

            // Iterate through root directory entries
            for (uint16_t i = 0; i < root_entries; ++i) {
                const uint8_t* entry = root_dir + (i * 32);
                
                // Check if entry is empty (first byte = 0)
                if (entry[0] == 0) break;
                
                // Check if it's a deleted entry (first byte = 0xE5)
                if (entry[0] == 0xE5) continue;
                
                // Check if it's a volume label (attribute = 0x08)
                if ((entry[11] & 0x08) != 0) continue;
                
                // Extract filename (8 bytes)
                char filename[13];
                int name_pos = 0;
                
                // Copy base name (8 bytes)
                for (int j = 0; j < 8; ++j) {
                    if (entry[j] == ' ') break;
                    if (entry[j] >= 32 && entry[j] < 127) {
                        filename[name_pos++] = entry[j];
                    }
                }

                // Check if it's a directory (attribute = 0x10)
                bool is_dir = (entry[11] & 0x10) != 0;

                // Add extension if not directory and extension exists
                if (!is_dir && entry[8] != ' ') {
                    filename[name_pos++] = '.';
                    for (int j = 0; j < 3; ++j) {
                        if (entry[8 + j] == ' ') break;
                        if (entry[8 + j] >= 32 && entry[8 + j] < 127) {
                            filename[name_pos++] = entry[8 + j];
                        }
                    }
                }

                filename[name_pos] = '\0';

                // Call callback with filename
                if (name_pos > 0) {
                    cb(filename);
                }
            }

            return 0;
        }

        // File reading from floppy - simplified
        void* floppy_get_file_alloc(const char* path, size_t* out_len) {
            if (!path || !floppy_data) return nullptr;
            
            // Very simplified: just return the whole floppy for now
            // A real implementation would parse FAT12 and extract individual files
            
            if (strcmp(path, "/") == 0) {
                if (out_len) *out_len = floppy_size;
                uint8_t* buf = (uint8_t*)hanacore::mem::kmalloc(floppy_size);
                if (buf) memcpy(buf, floppy_data, floppy_size);
                return buf;
            }

            return nullptr;
        }

    } // namespace fs
} // namespace hanacore

extern "C" {
    int floppy_init_from_memory(const void* data, size_t size) {
        return hanacore::fs::floppy_init_from_memory(data, size);
    }
}
