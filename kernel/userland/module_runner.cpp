#include "module_runner.hpp"
#include "elf_loader.hpp"
#include "../../boot/limine.h"
#include <stdint.h>
#include <stddef.h>
#include "../utils/logger.hpp"

// We reuse the module_request defined elsewhere in the kernel. Declare it
extern volatile struct limine_module_request module_request;
extern volatile struct limine_hhdm_request limine_hhdm_request;

extern "C" int memcmp(const void* s1, const void* s2, size_t n);

static int run_elf_from_module(volatile struct limine_file* mod) {
    if (!mod) return -1;
    uintptr_t addr = (uintptr_t)mod->address;
    if (limine_hhdm_request.response) {
        uint64_t off = limine_hhdm_request.response->offset;
        if ((uint64_t)addr < off) addr = (uintptr_t)(off + addr);
    }
    void* data = (void*)addr;
    size_t size = (size_t)mod->size;
    void* entry = elf64_load_from_memory(data, size);
    if (!entry) {
        log_info("module: ELF load failed");
        return -1;
    }
    log_info("module: jumping to ELF entry");
    void (*entry_fn)(void) = (void(*)(void))entry;
    entry_fn();
    return 0;
}

int exec_module_by_name(const char* filename) {
    if (!module_request.response || !filename) return -1;
    volatile struct limine_module_response* resp = module_request.response;
    for (uint64_t i = 0; i < resp->module_count; ++i) {
        volatile struct limine_file* mod = resp->modules[i];
        const char* path = (const char*)(uintptr_t)mod->path;
        if (path && limine_hhdm_request.response) {
            uint64_t hoff = limine_hhdm_request.response->offset;
            if ((uint64_t)path < hoff) path = (const char*)((uintptr_t)path + hoff);
        }
        if (!path) continue;
        // match filename suffix
        size_t pl = 0; while (path[pl]) ++pl;
        size_t fl = 0; while (filename[fl]) ++fl;
        if (pl >= fl && !memcmp(path + pl - fl, filename, fl)) {
            // Try to run it as ELF
            if (run_elf_from_module(mod) == 0) return 0;
            // else try flat binary entry
            uintptr_t addr = (uintptr_t)mod->address;
            if (limine_hhdm_request.response) {
                uint64_t off = limine_hhdm_request.response->offset;
                if ((uint64_t)addr < off) addr = (uintptr_t)(off + addr);
            }
            void (*entry)(void) = (void(*)(void))addr;
            entry();
            return 0;
        }
    }
    return -1;
}
