#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
namespace hanacore { namespace fs {
    // ISO 9660 filesystem minimal implementation
    int isofs_init(void);
    int isofs_init_from_memory(const void* data, size_t size);
    int isofs_list_dir(const char* path, void (*cb)(const char* name));
    int64_t isofs_read_file(const char* path, void* buf, size_t len);
    void* isofs_get_file_alloc(const char* path, size_t* out_len);
    int isofs_list_mounts(void (*cb)(const char* line));
} }
#endif

