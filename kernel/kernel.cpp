#include "drivers/screen.hpp"
#include "drivers/framebuffer.hpp"
#include "drivers/keyboard.hpp"
#include "arch/gdt.hpp"
#include "arch/idt.hpp"
#include "arch/pic.hpp"
#include "arch/pit.hpp"
#include "utils/logger.hpp"
#include "filesystem/fat32.hpp"
#include "filesystem/hanafs.hpp"
#include "filesystem/vfs.hpp"
#include "filesystem/procfs.hpp"
#include "filesystem/devfs.hpp"
#include "filesystem/isofs.hpp"
#include "filesystem/floppy.hpp"
#include "scheduler/scheduler.hpp"
#include "userland/users.hpp"
#include "mem/heap.hpp"
#include "userland/elf_loader.hpp"
#include "utils/utils.hpp"
#include  "libs/libc.h"
#include <stdint.h>

extern "C" void fat32_mount_all_letter_modules();
extern volatile struct limine_hhdm_request limine_hhdm_request;
extern "C" void init_syscall();

extern "C" {
    extern void (*__init_array_start)();
    extern void (*__init_array_end)();
    extern void (*__fini_array_start)();
    extern void (*__fini_array_end)();
}

__attribute__((used, section(".limine_requests")))
volatile struct limine_module_request module_request = {
    .id = { 0xc7b1dd30df4c8b88ULL, 0x0a82e883a194f07bULL, 0x3e7e279702be32afULL, 0xca1c4f3bd1280ceeULL },
    .revision = 0,
    .response = nullptr
};

static bool ends_with(const char* s, const char* suffix) {
    if (!s || !suffix) return false;
    size_t sl = 0, fl = 0;
    while (s[sl]) ++sl;
    while (suffix[fl]) ++fl;
    if (fl > sl) return false;
    return (memcmp(s + sl - fl, suffix, fl) == 0);
}

static void call_constructors() {
    for (void (**f)() = &__init_array_start; f < &__init_array_end; ++f)
        if (*f) (*f)();
}

static void call_destructors() {
    for (void (**f)() = &__fini_array_start; f < &__fini_array_end; ++f)
        if (*f) (*f)();
}

extern "C" void kernel_main() {
    if (framebuffer_init()) {
        clear_screen();
        log_ok("Framebuffer initialized");
    } else {
        log_fail("No framebuffer detected");
    }

    log_info("=== HanaCore Kernel Starting ===");
    call_constructors();
    log_ok("Global constructors called");

    gdt_install();
    idt_install();
    init_syscall();
    heap_init(1024 * 1024);
    keyboard_init();

    log_ok("Core subsystems initialized");
    log_info("Build: %s | Version: %s", hanacore::utils::build_date, hanacore::utils::version);

    // Initialize all filesystems
    ::vfs_init();
    hanacore::fs::hanafs_init();
    hanacore::fs::procfs_init();
    hanacore::fs::devfs_init();
    hanacore::fs::isofs_init();
    
    // Try to mount rootfs from modules (FAT32 preferred, fallback to floppy)
    bool rootfs_mounted = false;
    if (module_request.response) {
        auto resp = module_request.response;
        log_info("Limine modules detected: %u", (unsigned)resp->module_count);
        for (uint64_t i = 0; i < resp->module_count; ++i) {
            auto mod = resp->modules[i];
            const char* path = (const char*)(uintptr_t)mod->path;
            if (path && limine_hhdm_request.response) {
                uint64_t off = limine_hhdm_request.response->offset;
                if ((uint64_t)path < off) path = (const char*)((uintptr_t)path + off);
            }
            log_info("Module %u: %s (size=%u bytes)", (unsigned)i, path ? path : "(null)", (unsigned)mod->size);

            // Try to mount rootfs.img or rootfs
            if (!rootfs_mounted && path && (ends_with(path, ".img") || ends_with(path, "rootfs"))) {
                void* addr = mod->address;
                if (limine_hhdm_request.response) {
                    uint64_t off = limine_hhdm_request.response->offset;
                    if ((uintptr_t)addr < off) addr = (void*)((uintptr_t)addr + off);
                }
                log_info("Attempting FAT32 mount for rootfs.img: %s", path);
                if (hanacore::fs::fat32_init_from_memory(addr, mod->size) == 0) {
                    hanacore::fs::register_mount("fat32", "/");
                    log_ok("Mounted FAT32 rootfs.img at / (%s)", path);
                    rootfs_mounted = true;
                } else {
                    log_fail("FAT32 mount failed for rootfs.img (%s)", path);
                    log_info("Attempting floppy mount for rootfs.img: %s", path);
                    if (hanacore::fs::floppy_init_from_memory(addr, mod->size) == 0) {
                        hanacore::fs::register_mount("floppy", "/");
                        log_ok("Mounted floppy rootfs.img at / (%s)", path);
                        rootfs_mounted = true;
                    } else {
                        log_fail("Floppy mount failed for rootfs.img (%s)", path);
                    }
                }
            }

            // Check for ISO images
            if (path && (ends_with(path, ".iso") || ends_with(path, "HanaCore.iso"))) {
                void* addr = mod->address;
                if (limine_hhdm_request.response) {
                    uint64_t off = limine_hhdm_request.response->offset;
                    if ((uintptr_t)addr < off) addr = (void*)((uintptr_t)addr + off);
                }
                log_info("Attempting ISO mount: %s", path);
                if (hanacore::fs::isofs_init_from_memory(addr, mod->size) == 0) {
                    hanacore::fs::register_mount("isofs", "/iso");
                    log_ok("Mounted ISO image at /iso");
                } else {
                    log_fail("ISO mount failed for (%s)", path);
                }
            }
        }
        // If no rootfs.img was found, try to mount any FAT32 image as root
        if (!rootfs_mounted) {
            for (uint64_t i = 0; i < resp->module_count; ++i) {
                auto mod = resp->modules[i];
                const char* path = (const char*)(uintptr_t)mod->path;
                if (path && limine_hhdm_request.response) {
                    uint64_t off = limine_hhdm_request.response->offset;
                    if ((uint64_t)path < off) path = (const char*)((uintptr_t)path + off);
                }
                void* addr = mod->address;
                if (limine_hhdm_request.response) {
                    uint64_t off = limine_hhdm_request.response->offset;
                    if ((uintptr_t)addr < off) addr = (void*)((uintptr_t)addr + off);
                }
                log_info("Fallback: Attempting FAT32 mount for any module: %s", path);
                if (hanacore::fs::fat32_init_from_memory(addr, mod->size) == 0) {
                    hanacore::fs::register_mount("fat32", "/");
                    log_ok("Mounted fallback FAT32 image at / (%s)", path);
                    rootfs_mounted = true;
                    break;
                }
            }
        }
        if (!rootfs_mounted) {
            log_fail("No rootfs.img or FAT32 image could be mounted as root. Check Limine config and image format.");
        }
    }
    
    // Fallback to ATA only if no rootfs.img was mounted
    if (!rootfs_mounted) {
        log_info("No rootfs.img found, attempting FAT32 from ATA");
        fat32_mount_all_letter_modules();
    }

    // Try to find and execute /bin/hcsh from the mounted rootfs.img
    size_t hcsh_size = 0;
    void* hcsh_data = ::vfs_get_file_alloc("/bin/hcsh", &hcsh_size);
    if (hcsh_data && hcsh_size > 0) {
        log_info("Found shell at /bin/hcsh in rootfs.img (size=%u bytes)", (unsigned)hcsh_size);
        void* entry = elf64_load_from_memory(hcsh_data, hcsh_size);
        if (entry) {
            log_ok("Launching shell from /bin/hcsh");
            void (*shell_entry)(void) = (void(*)(void))entry;
            shell_entry();
        } else {
            log_fail("Failed to load ELF from /bin/hcsh");
        }
    } else {
        log_fail("No shell found at /bin/hcsh in rootfs.img");
    }

    hanacore::scheduler::init_scheduler();
    log_info("Scheduler initialized");

    // Shell support removed. Always start login task.
    log_info("No shell present, starting login");
    
    log_fail("No shell or login task present, halting.");
    for (;;) asm volatile("hlt");

    // Block the main kernel task so it won't be selected by the scheduler
    hanacore::scheduler::current_task->state = hanacore::scheduler::TASK_BLOCKED;
    
    hanacore::scheduler::schedule_next();

    log_fail("No tasks left to run, halting");
    for (;;) asm volatile("hlt");
}
