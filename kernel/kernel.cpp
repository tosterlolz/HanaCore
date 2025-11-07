#include "drivers/screen.hpp"
#include "drivers/framebuffer.hpp"
#include "drivers/keyboard.hpp"
#include "./libs/nanoprintf.h"
#include "arch/gdt.hpp"
#include "arch/idt.hpp"
#include "arch/pic.hpp"
#include "arch/pit.hpp"
#include "utils/logger.hpp"
#include "filesystem/fat32.hpp"
#include "net/e1000.hpp"
#include "net/netif.hpp"
#include <stdint.h>

extern "C" void net_loopback_init();
// C wrapper for auto-mounting letter-encoded modules (defined in fat32.cpp)
extern "C" void fat32_mount_all_letter_modules();
extern "C" void virtio_net_init();
#include "scheduler/scheduler.hpp"
#include "shell/shell.hpp"
#include "mem/heap.hpp"
#include "utils/utils.hpp"
// Linker script symbols for init/fini arrays
extern "C" {
    extern void (*__init_array_start)();
    extern void (*__init_array_end)();
    extern void (*__fini_array_start)();
    extern void (*__fini_array_end)();
}

// Request Limine modules so users can supply an external `shell.elf` as a module.
__attribute__((used, section(".limine_requests")))
volatile struct limine_module_request module_request = {
    .id = { 0xc7b1dd30df4c8b88ULL, 0x0a82e883a194f07bULL, 0x3e7e279702be32afULL, 0xca1c4f3bd1280ceeULL },
    .revision = 0,
    .response = nullptr
};

// Extern HHDM request (provided by Limine) — declared in limine_entry.c
extern volatile struct limine_hhdm_request limine_hhdm_request;
// Initialize syscall MSRs (defined in arch/syscall_init.cpp)
extern "C" void init_syscall();

// Helper: check whether `s` ends with `suffix` (NUL-terminated strings)
static bool ends_with(const char* s, const char* suffix) {
    if (!s || !suffix) return false;
    const char* ps = s; size_t sl = 0; while (ps[sl]) ++sl;
    const char* pf = suffix; size_t fl = 0; while (pf[fl]) ++fl;
    if (fl > sl) return false;
    const char* start = s + (sl - fl);
    for (size_t i = 0; i < fl; ++i) if (start[i] != suffix[i]) return false;
    return true;
}

static inline void serial_putchar(char c) {
    __asm__ volatile("outb %0, $0x3f8" : : "a"(c));
}

static void serial_puts(const char* str) {
    if (!str) return;
    while (*str) {
        if (*str == '\n') {
            serial_putchar('\r');
        }
        serial_putchar(*str++);
    }
}

// Call global constructors
static void call_constructors() {
    // Safely iterate through init array
    if (&__init_array_start == 0 || &__init_array_end == 0) {
        return;
    }
    
    for (void (**func)() = &__init_array_start; func != &__init_array_end; func++) {
        if (*func != nullptr) {
            (*func)();
        }
    }
}

// Call global destructors
static void call_destructors() {
    // Safely iterate through fini array
    if (&__fini_array_start == 0 || &__fini_array_end == 0) {
        return;
    }
    
    for (void (**func)() = &__fini_array_start; func != &__fini_array_end; func++) {
        if (*func != nullptr) {
            (*func)();
        }
    }
}


extern "C" void kernel_main() {
    if (framebuffer_init()) {
        log_ok("Framebuffer initialized!");
        // framebuffer_init() only maps the framebuffer for pixel ops;
        // `clear_screen()` will initialise Flanterm and enable `print()`.
        clear_screen();
    } else {
        log_fail("Framebuffer not available");
    }
    nano_log("\n=== HanaCore Kernel Starting ===\n");
    call_constructors();
    log_ok("Global constructors called.\n");
    gdt_install();
    log_ok("GDT installed.\n");
    idt_install();
    log_ok("IDT installed.\n");
    // Enable syscall/sysret path
    init_syscall();
    log_ok("Syscalls initialized.\n");
    heap_init(1024 * 1024); // 1 MiB heap
    log_ok("Heap initialized.\n");
    keyboard_init();
    log_ok("Keyboard initialized.");
    log_info("HanaCore Kernel Initialized!");
    log_info("Bootloader: Limine (x86_64)");
    log_info("Welcome to HanaCore - minimalist C++ OS kernel.");
    log_info("System ready.");
    log_info("Kernel build: %s", hanacore::utils::build_date);
    log_info("Kernel version: %s", hanacore::utils::version);
    // Try to initialize fat32 rootfs from a module named "rootfs.img"
    // If an embedded rootfs was linked into the kernel, initialize from it first.
    if (module_request.response) {
        volatile struct limine_module_response* mresp = module_request.response;
        // Log module count for diagnostics
        if (mresp->module_count == 0) {
            log_info("No Limine modules found");
        } else {
            char tmp[64];
            // simple print of module count
            log_info("Limine modules: %u", (unsigned) mresp->module_count);
        }
        for (uint64_t i = 0; i < mresp->module_count; ++i) {
            volatile struct limine_file* mod = mresp->modules[i];
            const char* path = (const char*)(uintptr_t)mod->path;
            if (path && limine_hhdm_request.response) {
                uint64_t hoff = limine_hhdm_request.response->offset;
                if ((uint64_t)path < hoff) path = (const char*)((uintptr_t)path + hoff);
            }
            if (path && (ends_with(path, "rootfs.img") || ends_with(path, "rootfs.bin"))) {
                // Prepare module virtual address (respect HHDM offset)
                uintptr_t mod_addr = (uintptr_t)mod->address;
                const void* mod_virt = (const void*)mod_addr;
                if (limine_hhdm_request.response) {
                    uint64_t off = limine_hhdm_request.response->offset;
                    if ((uint64_t)mod_addr < off) mod_virt = (const void*)(off + mod_addr);
                }
                // Initialize FAT32 from the in-memory module image. Only stop
                // searching if initialization succeeded; otherwise continue and
                // try other modules (avoid aborting on a single failure).
                if (hanacore::fs::fat32_init_from_memory(mod_virt, (size_t)mod->size) == 0) {
                    hanacore::utils::log_info_cpp("[kernel] rootfs module initialized from memory");
                    break;
                } else {
                    hanacore::utils::log_info_cpp("[kernel] failed to init rootfs module, trying next module if any");
                }
            }
        }
    }

    // Auto-mount any module whose filename encodes a drive letter
    // (e.g. c.img -> mounted as C:). This allows users to provide
    // additional disk images as Limine modules and access them by
    // drive-letter paths like "C:/path/to/file".
    hanacore::fs::fat32_mount_all_letter_modules();

    // Initialize networking: loopback first, then attempt hardware probe.
    net_loopback_init();
    e1000_init();
    // Probe for virtio-net devices (if running under virtio-capable hypervisor)
    void virtio_net_init();
    virtio_net_init();

    // Try to find a Limine module named "shell.elf" and execute it (ring-0)
    if (module_request.response) {
        volatile struct limine_module_response* mresp = module_request.response;
        for (uint64_t i = 0; i < mresp->module_count; ++i) {
            volatile struct limine_file* mod = mresp->modules[i];
            const char* path = (const char*)(uintptr_t)mod->path;
            // Convert Limine-provided physical pointers to HHDM virtual addresses
            // when necessary so string operations are safe.
            if (path && limine_hhdm_request.response) {
                uint64_t hoff = limine_hhdm_request.response->offset;
                if ((uint64_t)path < hoff) path = (const char*)((uintptr_t)path + hoff);
            }
            if (ends_with(path, "shell.elf") || ends_with(path, "shell.bin")) {
                log_info("Found external shell module; executing in ring-0");

                // Derive a usable virtual address (respect HHDM offset if provided)
                uintptr_t mod_addr = (uintptr_t)mod->address;
                uint8_t* mod_virt = (uint8_t*)mod_addr;
                if (limine_hhdm_request.response) {
                    uint64_t off = limine_hhdm_request.response->offset;
                    if ((uint64_t)mod_addr < off) mod_virt = (uint8_t*)(off + mod_addr);
                }

                // Jump to module as a function. The module is expected to be a
                // flat binary with an entry point at its start (not ELF).
                void (*entry)(void) = (void(*)(void))mod_virt;
                entry();
                // If the module returns, continue boot.
                break;
            }
        }
    }

        // If no external shell was found, fall back to built-in shell as a task
        keyboard_init();
        print("No external shell found — starting built-in shell as task.\n");

        // Initialize scheduler (cooperative for now) and create a task for the built-in shell.
        log_info("kernel: initializing scheduler");
        hanacore::scheduler::init_scheduler();
        log_info("kernel: scheduler initialized");

        // Create shell task
        int shell_pid = hanacore::scheduler::create_task((void(*)(void))hanacore::shell::shell_main);
        log_info("kernel: created shell task");
        log_hex64("kernel: shell pid", (uint64_t)shell_pid);
        hanacore::shell::shell_main();
        // Switch to the newly created task. Cooperative scheduling: tasks must call
        // `sched_yield()` to let other tasks run. We deliberately avoid enabling
        // PIT/PIC here to prevent interrupts during early testing.
        log_info("kernel: about to schedule_next()");
        hanacore::scheduler::schedule_next();
        // If scheduler returns, just idle.
        for (;;) __asm__ volatile("hlt");
}
