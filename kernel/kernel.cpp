#include "drivers/screen.hpp"
#include "drivers/framebuffer.hpp"
#include "drivers/keyboard.hpp"
#include "arch/gdt.hpp"
#include "arch/idt.hpp"
#include "arch/pic.hpp"
#include "arch/pit.hpp"
#include "utils/logger.hpp"
#include "filesystem/fat32.hpp"
#include "filesystem/ext3.hpp"
#include "filesystem/hanafs.hpp"
#include "filesystem/vfs.hpp"
#include "filesystem/procfs.hpp"
#include "filesystem/devfs.hpp"
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

#include <stdint.h>
#include <stddef.h>

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
    hanacore::fs::procfs_init();
    hanacore::fs::devfs_init();
    
    // Try FAT32 module/image (rootfs.img) first, then fall back to ISO root.
    log_info("Attempting to mount rootfs.img (FAT32) from Limine modules or ATA");
    fat32_mount_all_letter_modules();
    extern bool fat32_ready;
    hanacore::fs::register_mount("fat32", "/");
    log_ok("Mounted FAT32 rootfs (rootfs.img)");

    // Try to find and execute /bin/hcsh or /bin/HCSH from the mounted rootfs
    size_t hcsh_size = 0;
    void* hcsh_data = ::vfs_get_file_alloc("/bin/hcsh", &hcsh_size);
    if (!(hcsh_data && hcsh_size > 0)) {
        hcsh_data = ::vfs_get_file_alloc("/bin/HCSH", &hcsh_size);
        if (hcsh_data && hcsh_size > 0) {
            log_info("Found shell at /bin/HCSH in rootfs (size=%u bytes)", (unsigned)hcsh_size);
        }
    } else {
        log_info("Found shell at /bin/hcsh in rootfs (size=%u bytes)", (unsigned)hcsh_size);
    }
    if (hcsh_data && hcsh_size > 0) {
        void* entry = elf64_load_from_memory(hcsh_data, hcsh_size);
        if (entry) {
            log_ok("Launching shell from rootfs");
            void (*shell_entry)(void) = (void(*)(void))entry;
            shell_entry();
        } else {
            log_fail("Failed to load ELF from shell binary");
        }
    } else {
        log_fail("No shell found at /bin/hcsh or /bin/HCSH in rootfs");
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
