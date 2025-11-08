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
#include <stdint.h>

// C wrapper for auto-mounting letter-encoded modules (defined in fat32.cpp)
extern "C" void fat32_mount_all_letter_modules();
#include "scheduler/scheduler.hpp"
#include "shell/shell.hpp"
#include "mem/heap.hpp"
#include "userland/elf_loader.hpp"
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
    log_info("\n=== HanaCore Kernel Starting ===\n");
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
    // Do NOT auto-initialize FAT32 from Limine modules at boot. Mounting
    // filesystems (FAT32, HanaFS persistence, ISO images) should be an
    // explicit user action via the `mount` builtin. We still log available
    // modules for diagnostics but avoid auto-mount side-effects here.
    if (module_request.response) {
        volatile struct limine_module_response* mresp = module_request.response;
        if (mresp->module_count == 0) {
            log_info("No Limine modules found");
        } else {
            log_info("Limine modules: %u (not auto-mounted)", (unsigned)mresp->module_count);
        }
    }

    // Initialize basic VFS and pseudo-filesystems
    hanacore::fs::vfs_init();
    log_info("[kernel] VFS initialized.");
    // Initialize HanaFS in-memory root so /, /dev, /proc exist for tools.
    if (hanacore::fs::hanafs_init() == 0) {
        log_info("[kernel] HanaFS initialized (in-memory)");
    }
    // register procfs and devfs
    hanacore::fs::procfs_init();
    hanacore::fs::devfs_init();

    // Networking disabled in this build (net subsystem removed).

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
            print("attempting to run /bin/hcsh\n");

            // Try to run /bin/hcsh (userland shell) from HanaFS. If present, load
            // ELF image into memory and spawn it as a task. Otherwise fall back to
            // built-in shell.
            hanacore::scheduler::init_scheduler();
            log_info("kernel: scheduler initialized");

            size_t fsize = 0;

            int shell_pid = hanacore::scheduler::create_task((void(*)(void))hanacore::shell::shell_main);
            log_info("kernel: created built-in shell task");
            log_hex64("kernel: shell pid", (uint64_t)shell_pid);
            // Switch to the newly created task and let scheduler drive execution.
            hanacore::scheduler::schedule_next();
            for (;;) __asm__ volatile("hlt");
}
