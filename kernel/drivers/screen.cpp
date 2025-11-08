// Minimal C++ screen wrapper that initialises Flanterm and provides print()
#include "screen.hpp"
#include "../boot/limine.h"
#include "../utils/logger.hpp"
#include "../mem/heap.hpp"

extern "C" {
#include "../../third_party/flanterm/src/flanterm.h"
#include "../../third_party/flanterm/src/flanterm_backends/fb.h"
}

#include <stdarg.h>

// Declare vsnprintf with C linkage so calls from this C++ TU link to the
// freestanding implementation in kernel/libs/libc.c
extern "C" int vsnprintf(char* s, size_t n, const char* fmt, va_list ap);

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
// When GUI mode is active, route print() into gui_term and avoid writing to
// the main console. Set via screen_set_gui_mode().
// malloc/free wrappers for flanterm when creating an additional context
static void *ft_malloc(size_t s) { return hanacore::mem::kmalloc(s); }
static void ft_free(void *p, size_t s) { (void)s; hanacore::mem::kfree(p); }
// Framebuffer raw pointer and geometry cached for simple drawing operations
static uint32_t* fb_ptr = nullptr;
static size_t fb_width = 0;
static size_t fb_height = 0;
static size_t fb_pitch = 0; // bytes per scanline

// Simple cursor backup (8x8) to restore pixels when moving cursor

namespace hanacore::drivers::screen {

static size_t string_len(const char* s) {
    size_t n = 0; while (s && s[n]) ++n; return n;
}

void clear_screen() {
    // If Flanterm is not initialised yet, initialise it and return.
    if (!term) {
        debug_puts("=== Flanterm Initialization ===\n");

        volatile struct limine_framebuffer_response* resp = framebuffer_request.response;
        if (!resp || resp->framebuffer_count == 0) {
            debug_puts("⚠ No framebuffer response from Limine - Flanterm unavailable.\n");
            debug_puts("Using debug port only.\n");
            return;
        }

        struct limine_framebuffer* fb = resp->framebuffers[0];
        debug_puts("Framebuffer found - initializing Flanterm...\n");

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
            log_ok("Flanterm Terminal Ready!");
            flanterm_flush(term);
            // cache framebuffer info for simple 32-bit RGB drawing
            fb_ptr = fb_virt;
            fb_width = fb->width;
            fb_height = fb->height;
            fb_pitch = fb->pitch;
        } else {
            debug_puts("✗ Flanterm initialization failed. Using debug port only.\n");
        }

        return;
    }

    // If Flanterm is already initialised, send ANSI clear and home sequence
    // so the terminal's internal grid is reset cleanly.
    const char ansi_clear[] = "\x1b[2J\x1b[H";
    flanterm_write(term, ansi_clear, sizeof(ansi_clear) - 1);
    flanterm_full_refresh(term);
    flanterm_flush(term);
}

} // namespace hanacore::drivers::screen

// Extern C wrappers that forward to the namespaced implementations so the
// existing C ABI remains stable.
extern "C" void clear_screen() {
    hanacore::drivers::screen::clear_screen();
}

extern "C" void print(const char* str) {
    hanacore::drivers::screen::print(str);
}

// Provide the namespaced print implementation that the extern wrapper calls.
namespace hanacore::drivers::screen {
    void print(const char* str) {
        if (!str) return;
        if (!term) {
            debug_puts("TERM_NOT_INIT:");
            debug_puts(str);
            return;
        }
        flanterm_write(term, str, string_len(str));
    }
}

// Simple pixel write (assumes 32-bit native framebuffer)
static inline void put_pixel(int x, int y, uint32_t color) {
    if (!fb_ptr) return;
    if (x < 0 || y < 0 || (size_t)x >= fb_width || (size_t)y >= fb_height) return;
    uint32_t* line = (uint32_t*)((uint8_t*)fb_ptr + y * fb_pitch);
    line[x] = color;
}

void screen_fill_rect(int x, int y, int w, int h, uint32_t color) {
    for (int yy = 0; yy < h; ++yy) {
        for (int xx = 0; xx < w; ++xx) put_pixel(x + xx, y + yy, color);
    }
}

void screen_draw_rect(int x, int y, int w, int h, uint32_t color) {
    for (int xx = 0; xx < w; ++xx) { put_pixel(x + xx, y, color); put_pixel(x + xx, y + h - 1, color); }
    for (int yy = 0; yy < h; ++yy) { put_pixel(x, y + yy, color); put_pixel(x + w - 1, y + yy, color); }
}

static void sprintf_putc(char* &buf, char c) {
    *buf++ = c;
}

// Minimal reverse helper
static void reverse_str(char* str, size_t len) {
    for (size_t i = 0; i < len / 2; ++i) {
        char t = str[i];
        str[i] = str[len - 1 - i];
        str[len - 1 - i] = t;
    }
}

// Unsigned integer to string
static size_t uint_to_str(uint64_t value, char* buf, int base, bool uppercase) {
    const char* digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    size_t i = 0;
    if (value == 0) buf[i++] = '0';
    while (value > 0) {
        buf[i++] = digits[value % base];
        value /= base;
    }
    reverse_str(buf, i);
    buf[i] = '\0';
    return i;
}

// Print formatted string directly to Flanterm (bounded)
void print_fmt(const char* fmt, ...) {
    char buf[512]; // bounded buffer
    va_list args;
    va_start(args, fmt);
    // use the freestanding vsnprintf (C linkage) implemented in libc.c
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    print(buf); // uses Flanterm print()
}