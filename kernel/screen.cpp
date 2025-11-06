// Minimal C++ screen wrapper that initialises Flanterm and provides print()
#include "screen.hpp"
#include "../boot/limine.h"

extern "C" {
#include "../flanterm/src/flanterm.h"
#include "../flanterm/src/flanterm_backends/fb.h"
}

#include <stdint.h>
#include <stddef.h>

// Place the Limine framebuffer request in this TU so the response pointer
// is available at runtime. This mirrors the previous behaviour that lived
// in the old C implementation.
__attribute__((used, section(".limine_requests")))
volatile struct limine_framebuffer_request framebuffer_request = {
    .id = {
        0xc7b1dd30df4c8b88ULL,
        0x0a82e883a194f07bULL,
        0x9d5827dcd881dd75ULL,
        0xa3148604f6fab11bULL
    },
    .revision = 0,
    .response = nullptr
};

// Extern HHDM request from Limine (may be NULL)
extern volatile struct limine_hhdm_request limine_hhdm_request;

static inline void debug_putchar(char c) {
    __asm__ volatile("outb %0, $0xe9" : : "a"(c));
}

static void debug_puts(const char* s) {
    if (!s) return;
    for (const char* p = s; *p; ++p) debug_putchar(*p);
}

static void debug_puthex64(uint64_t v) {
    const char *hex = "0123456789ABCDEF";
    debug_putchar('0'); debug_putchar('x');
    for (int i = 15; i >= 0; --i) {
        uint8_t nib = (v >> (i * 4)) & 0xF;
        debug_putchar(hex[nib]);
    }
}

static void debug_putln_hex64(const char *label, uint64_t v) {
    debug_puts(label);
    debug_puthex64(v);
    debug_puts("\n");
}

static struct flanterm_context* term = nullptr;

static size_t string_len(const char* s) {
    size_t n = 0; while (s && s[n]) ++n; return n;
}

void clear_screen() {
    debug_puts("=== Flanterm Initialization ===\n");
    if (term) {
        debug_puts("Already initialised.\n");
        return;
    }

    volatile struct limine_framebuffer_response* resp = framebuffer_request.response;
    if (!resp || resp->framebuffer_count == 0) {
        debug_puts("⚠ No framebuffer response from Limine - Flanterm unavailable.\n");
        debug_puts("Using debug port only.\n");
        return;
    }

    struct limine_framebuffer* fb = resp->framebuffers[0];
    debug_puts("✓ Framebuffer found - initializing Flanterm...\n");

    // Print diagnostics helpful for debugging mapping/format issues.
    debug_putln_hex64("limine_response_ptr: ", (uint64_t)(uintptr_t)resp);
    debug_putln_hex64("framebuffer_ptr:    ", (uint64_t)(uintptr_t)fb);
    debug_putln_hex64("fb->address:       ", (uint64_t)(uintptr_t)fb->address);
    debug_putln_hex64("fb->width:         ", (uint64_t)fb->width);
    debug_putln_hex64("fb->height:        ", (uint64_t)fb->height);
    debug_putln_hex64("fb->pitch:         ", (uint64_t)fb->pitch);

    if (!limine_hhdm_request.response) {
        debug_puts("⚠ No HHDM provided by Limine. Avoiding unsafe physical access.\n");
        debug_puts("Flanterm will not be initialised. Using debug port only.\n");
        return;
    }

    uint64_t hhdm_off = limine_hhdm_request.response->offset;
    uint64_t fb_addr = (uint64_t)(uintptr_t)fb->address;

    // If Limine already returned an HHDM-mapped address, use it directly.
    uint32_t* fb_virt = nullptr;
    if (fb_addr >= hhdm_off) {
        fb_virt = (uint32_t*)fb_addr;
    } else {
        fb_virt = (uint32_t*)(hhdm_off + fb_addr);
    }

    debug_putln_hex64("hhdm_offset:       ", hhdm_off);
    debug_putln_hex64("fb_virt:           ", (uint64_t)(uintptr_t)fb_virt);
    debug_putln_hex64("red_mask_size:    ", (uint64_t)fb->red_mask_size);
    debug_putln_hex64("red_mask_shift:   ", (uint64_t)fb->red_mask_shift);
    debug_putln_hex64("green_mask_size:  ", (uint64_t)fb->green_mask_size);
    debug_putln_hex64("green_mask_shift: ", (uint64_t)fb->green_mask_shift);
    debug_putln_hex64("blue_mask_size:   ", (uint64_t)fb->blue_mask_size);
    debug_putln_hex64("blue_mask_shift:  ", (uint64_t)fb->blue_mask_shift);

    // Initialise Flanterm. Pass NULL allocators so Flanterm uses its internal
    // bump allocator (large enough for typical resolutions). Pass NULL font
    // so Flanterm uses its default font.
    term = flanterm_fb_init(
        nullptr, nullptr,
        (uint32_t*)fb_virt,
        fb->width, fb->height, fb->pitch,
        fb->red_mask_size, fb->red_mask_shift,
        fb->green_mask_size, fb->green_mask_shift,
        fb->blue_mask_size, fb->blue_mask_shift,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, 0, 0, 0, 0, 0, 0, 0
    );

    if (term) {
        debug_puts("✓ Flanterm initialization successful!\n");
        flanterm_full_refresh(term);
        flanterm_write(term, "Flanterm ready!\n", 16);
        flanterm_flush(term);
    } else {
        debug_puts("✗ Flanterm initialization failed. Using debug port only.\n");
    }
}


void print(const char* str) {
    if (!str) return;
    if (!term) {
        debug_puts("TERM_NOT_INIT:");
        debug_puts(str);
        return;
    }
    flanterm_write(term, str, string_len(str));
}
