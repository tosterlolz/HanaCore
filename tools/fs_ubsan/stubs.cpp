#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

// Provide minimal logger and memory implementations so filesystem code can
// be linked into a host executable for UBSAN testing.

extern "C" void print(const char* s) {
    if (!s) return;
    fputs(s, stdout);
}

extern "C" void log_ok_v(const char* fmt, va_list ap) {
    vprintf(fmt, ap); printf("\n");
}
extern "C" void log_fail_v(const char* fmt, va_list ap) {
    vprintf(fmt, ap); printf("\n");
}
extern "C" void log_info_v(const char* fmt, va_list ap) {
    vprintf(fmt, ap); printf("\n");
}
extern "C" void log_debug_v(const char* fmt, va_list ap) {
    vprintf(fmt, ap); printf("\n");
}

extern "C" void log_ok(const char* fmt, ...) { va_list ap; va_start(ap, fmt); log_ok_v(fmt, ap); va_end(ap); }
extern "C" void log_fail(const char* fmt, ...) { va_list ap; va_start(ap, fmt); log_fail_v(fmt, ap); va_end(ap); }
extern "C" void log_info(const char* fmt, ...) { va_list ap; va_start(ap, fmt); log_info_v(fmt, ap); va_end(ap); }
extern "C" void log_debug(const char* fmt, ...) { va_list ap; va_start(ap, fmt); log_debug_v(fmt, ap); va_end(ap); }

extern "C" void log_hex64(const char* label, uint64_t value) {
    printf("%s0x%016llX\n", label ? label : "", (unsigned long long)value);
}

// Minimal memory allocation wrappers matching kernel API (namespaces)
namespace hanacore { namespace mem {
    void heap_init(size_t size) { (void)size; /* noop for host */ }
    void* kmalloc(size_t size) {
        if (size == 0) return NULL;
        void* p = malloc(size);
        return p;
    }
    void kfree(void* ptr) {
        free(ptr);
    }
}}

// Stubs for ATA helpers used by HanaFS/FAT helpers. Return failure so code
// paths that rely on ATA won't unexpectedly succeed in host test.
extern "C" int ata_read_sector(uint32_t lba, void* buf) { (void)lba; (void)buf; return -1; }
extern "C" int ata_write_sector(uint32_t lba, const void* buf) { (void)lba; (void)buf; return -1; }
extern "C" int ata_read_sector_drive(uint32_t drive, uint32_t lba, void* buf) { (void)drive; (void)lba; (void)buf; return -1; }
extern "C" int32_t ata_get_sector_count_drive(int drive) { (void)drive; return 0; }

// Provide minimal fat32 stubs that vfs may reference. These return failure
// or empty results so the host test remains deterministic.
namespace hanacore { namespace fs {
    int fat32_init_from_module(const char*) { return -1; }
    int fat32_init_from_memory(const void*, size_t) { return -1; }
    int64_t fat32_read_file(const char*, void*, size_t) { return -1; }
    void* fat32_get_file_alloc(const char*, size_t*) { return NULL; }
    int fat32_list_dir(const char*, void (*)(const char*)) { return -1; }
    void fat32_mount_all_letter_modules() {}
    void fat32_list_mounts(void (*cb)(const char*)) { (void)cb; }
    int fat32_create_file(const char*) { return -1; }
    int fat32_unlink(const char*) { return -1; }
    int fat32_make_dir(const char*) { return -1; }
    int fat32_remove_dir(const char*) { return -1; }
    int fat32_write_file(const char*, const void*, size_t) { return -1; }
}}

// Minimal procfs/devfs callbacks are implemented in their CPP files; nothing else needed.
