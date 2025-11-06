#include "screen.h"
#include <stdint.h>
#include <stddef.h>
#include "../flanterm/src/flanterm.c"
#include "../flanterm/src/flanterm_backends/fb.c"

void *memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char *)s;
    for (size_t i = 0; i < n; i++) {
        p[i] = (unsigned char)c;
    }
    return s;
}

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}


static size_t string_len(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

static char allocator_buffer[65536];
static size_t allocator_pos = 0;

static void* flanterm_malloc(size_t size) {
    if (allocator_pos + size > sizeof(allocator_buffer)) {
        return NULL;
    }
    void* ptr = allocator_buffer + allocator_pos;
    allocator_pos += size;
    return ptr;
}

static void flanterm_free(void* ptr, size_t size) {
    (void)ptr;
    (void)size;
}

__attribute__((used, section(".limine_requests")))
struct limine_framebuffer_request framebuffer_request = {
    .id = {0x9d5827dcd881dd7b, 0xa3148604f6fab11b, 0, 0},
    .revision = 0,
    .response = NULL
};

static inline void debug_putchar(char c) {
    __asm__ volatile("outb %0, $0xe9" : : "a"(c));
}

static void debug_puts(const char* str) {
    while (*str) {
        debug_putchar(*str++);
    }
}

static struct flanterm_context* term = NULL;

void clear_screen() {
    debug_puts("=== Flanterm Initialization ===\n");
    
    if (term) {
        debug_puts("Already initialized.\n");
        return;
    }
    
    debug_puts("Checking for framebuffer response...\n");
    if (!framebuffer_request.response) {
        debug_puts("⚠ WARNING: No framebuffer response from Limine\n");
        debug_puts("Flanterm unavailable - using debug port only\n");
        return;
    }
    
    if (framebuffer_request.response->framebuffer_count == 0) {
        debug_puts("⚠ WARNING: Framebuffer count is zero\n");
        return;
    }
    
    struct limine_framebuffer* fb = framebuffer_request.response->framebuffers[0];
    debug_puts("✓ Framebuffer found - initializing Flanterm...\n");
    
    term = flanterm_fb_init(
        flanterm_malloc,
        flanterm_free,
        (uint32_t*)(uint64_t)fb->address,
        fb->width,
        fb->height,
        fb->pitch,
        fb->red_mask_size,
        fb->red_mask_shift,
        fb->green_mask_size,
        fb->green_mask_shift,
        fb->blue_mask_size,
        fb->blue_mask_shift,
        NULL,  // canvas
        NULL,  // ansi_colours
        NULL,  // ansi_bright_colours
        NULL,  // default_bg
        NULL,  // default_fg
        NULL,  // default_bg_bright
        NULL,  // default_fg_bright
        NULL,  // font
        0,     // font_width
        0,     // font_height
        0,     // font_spacing
        0,     // font_scale_x (auto)
        0,     // font_scale_y (auto)
        0      // margin
    );
    
    if (term) {
        debug_puts("✓ Flanterm initialization successful!\n");
        flanterm_write(term, "🎨 Flanterm Terminal Ready!\n", 30);
        flanterm_flush(term);
    } else {
        debug_puts("✗ Flanterm initialization failed\n");
    }
}

void print(const char* str) {
    if (!term) {
        debug_puts("TERM_NOT_INIT:");
        debug_puts(str);
        return;
    }
    
    flanterm_write(term, str, string_len(str));
    flanterm_flush(term);
}
