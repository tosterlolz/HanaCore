#include "ext3.hpp"
#include "../utils/logger.hpp"
#include <stddef.h>

extern "C" void __attribute__((weak)) fat32_progress_update(int percent) {}

namespace ext3 {

    int init(void) {
        hanacore::utils::log_info_cpp("[EXT3] init: ext3 backend stub (not implemented)");
        return -1;
    }

    int mount(int drive, const char* mount_point) {
        hanacore::utils::log_info_cpp("[EXT3] mount: called drive=%d mount=%s (stub)", drive, mount_point ? mount_point : "(null)");
        return -1;
    }

    void* get_file_alloc(const char* path, size_t* out_len) {
        hanacore::utils::log_info_cpp("[EXT3] get_file_alloc: %s (stub)", path ? path : "(null)");
        if (out_len) *out_len = 0;
        return nullptr;
    }

    int list_dir(const char* path, void (*cb)(const char* name)) {
        hanacore::utils::log_info_cpp("[EXT3] list_dir: %s (stub)", path ? path : "(null)");
        return -1;
    }

}
