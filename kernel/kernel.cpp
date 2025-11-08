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
#include "shell/shell.hpp"
#include "shell/login.hpp"
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
    
    // Try to mount rootfs from modules (floppy or FAT32)
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
            
            // Check for rootfs image (.img) - try floppy first
            if (!rootfs_mounted && path && (ends_with(path, ".img") || ends_with(path, "rootfs"))) {
                void* addr = mod->address;
                if (limine_hhdm_request.response) {
                    uint64_t off = limine_hhdm_request.response->offset;
                    if ((uintptr_t)addr < off) addr = (void*)((uintptr_t)addr + off);
                }
                
                // Try floppy first (FAT12/FAT16)
                if (hanacore::fs::floppy_init_from_memory(addr, mod->size) == 0) {
                    log_ok("Mounted floppy image at / (%s)", path);
                    rootfs_mounted = true;
                } 
                // If floppy fails, try FAT32
                else if (hanacore::fs::fat32_init_from_memory(addr, mod->size) == 0) {
                    hanacore::fs::register_mount("fat32", "/");
                    log_ok("Mounted FAT32 image at / (%s)", path);
                    rootfs_mounted = true;
                } else {
                    log_fail("Failed to mount rootfs image (%s)", path);
                }
            }
            
            // Check for ISO images
            if (path && (ends_with(path, ".iso") || ends_with(path, "HanaCore.iso"))) {
                void* addr = mod->address;
                if (limine_hhdm_request.response) {
                    uint64_t off = limine_hhdm_request.response->offset;
                    if ((uintptr_t)addr < off) addr = (void*)((uintptr_t)addr + off);
                }
                
                if (hanacore::fs::isofs_init_from_memory(addr, mod->size) == 0) {
                    hanacore::fs::register_mount("isofs", "/iso");
                    log_ok("Mounted ISO image at /iso");
                }
            }
        }
    }
    
    // Fallback to ATA if no module rootfs was found
    if (!rootfs_mounted) {
        log_info("No rootfs module found, attempting FAT32 from ATA");
        fat32_mount_all_letter_modules();
    }

    if (module_request.response) {
        auto resp = module_request.response;
        for (uint64_t i = 0; i < resp->module_count; ++i) {
            auto mod = resp->modules[i];
            const char* path = (const char*)(uintptr_t)mod->path;
            if (path && limine_hhdm_request.response) {
                uint64_t off = limine_hhdm_request.response->offset;
                if ((uint64_t)path < off) path = (const char*)((uintptr_t)path + off);
            }
            if (ends_with(path, "shell.elf") || ends_with(path, "shell.bin")) {
                log_info("Found external shell module: %s", path);
                uintptr_t addr = (uintptr_t)mod->address;
                if (limine_hhdm_request.response) {
                    uint64_t off = limine_hhdm_request.response->offset;
                    if (addr < off) addr += off;
                }
                ((void(*)())addr)();
                break;
            }
        }
    }

    hanacore::scheduler::init_scheduler();
    log_info("Scheduler initialized");

    const char* candidates[] = { "/userland/shell/hcsh", "/bin/hcsh", "/bin/sh" };
    void* filedata = nullptr;
    size_t fsize = 0;

    for (auto path : candidates) {
        filedata = ::vfs_get_file_alloc(path, &fsize);
        if (filedata) {
            log_info("Found userland shell: %s", path);
            break;
        }
    }

    if (filedata) {
        void* entry = elf64_load_from_memory(filedata, fsize);
        if (entry) {
            int pid = hanacore::scheduler::create_user_task(entry, 128 * 1024);
            if (pid > 0) {
                log_info("Spawned user shell (pid=%d)", pid);
                hanacore::mem::kfree(filedata);
                hanacore::scheduler::schedule_next();
                for (;;) asm volatile("hlt");
            }
        }
        hanacore::mem::kfree(filedata);
    }

    log_info("No user shell found, starting login");
    int login_pid = hanacore::scheduler::create_task((void(*)(void))hanacore::shell::login_main);
    log_info("Created login task (pid=%d)", login_pid);

    // Block the main kernel task so it won't be selected by the scheduler
    hanacore::scheduler::current_task->state = hanacore::scheduler::TASK_BLOCKED;
    
    hanacore::scheduler::schedule_next();

    log_fail("No tasks left to run, halting");
    for (;;) asm volatile("hlt");
}
