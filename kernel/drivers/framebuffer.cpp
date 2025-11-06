#include "framebuffer.hpp"
#include "../utils/logger.hpp"
#include "screen.hpp"
#include <stdint.h>
#include <stddef.h>

// Global framebuffer state
static struct limine_framebuffer* fb_global = NULL;
static uint64_t fb_width = 0;
static uint64_t fb_height = 0;
static uint64_t fb_pitch = 0;
// Kernel virtual pointer to the framebuffer (computed using Limine HHDM)
static uint8_t* fb_virt = NULL;

// Framebuffer request (defined in screen.c) and HHDM request (provided by Limine)
extern "C" {
    extern volatile struct limine_framebuffer_request framebuffer_request;
    extern volatile struct limine_hhdm_request limine_hhdm_request;
}

// Screen logging function provided by drivers/screen.cpp
extern "C" void print(const char*);

bool framebuffer_init() {
    volatile struct limine_framebuffer_response* resp = framebuffer_request.response;
    
    if (!resp) {
        // No framebuffer response
        print("[FAIL] No Limine framebuffer response\n");
        return false;
    }
    
    if (resp->framebuffer_count == 0) {
        print("[FAIL] Limine returned zero framebuffers\n");
        return false;
    }
    fb_global = resp->framebuffers[0];
    fb_width = fb_global->width;
    fb_height = fb_global->height;
    fb_pitch = fb_global->pitch;

    // Require HHDM offset from Limine to safely derive a kernel virtual
    // pointer for the physical framebuffer address. If HHDM is not present
    // we refuse to initialize to avoid dereferencing physical addresses
    // on a higher-half kernel (which causes crashes/bootloops).
    if (limine_hhdm_request.response == NULL) {
        fb_virt = NULL;
        print("[FAIL] No HHDM provided by Limine; refusing to map framebuffer\n");
        return false;
    }

    uint64_t hhdm_off = limine_hhdm_request.response->offset;

    /* The framebuffer address returned by Limine may already be a virtual
       address in the kernel's HHDM (some setups return HHDM-mapped addresses).
       If the address is already >= hhdm_off, treat it as a virtual pointer
       and use it directly. Otherwise, treat it as a physical address and
       add the HHDM offset. This avoids double-adding the offset which
       would produce an invalid pointer. */
    uintptr_t fb_addr = (uintptr_t)fb_global->address;
    if ((uint64_t)fb_addr >= hhdm_off) {
        fb_virt = (uint8_t*)fb_addr;
    } else {
        fb_virt = (uint8_t*)( (uintptr_t)hhdm_off + fb_addr );
    }

    log_ok("Framebuffer initialized successfully");
    return true;
}

bool framebuffer_available() {
    return fb_global != NULL;
}

void framebuffer_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!fb_global || x >= (uint32_t)fb_width || y >= (uint32_t)fb_height) {
        return;
    }
    if (fb_virt == NULL) return;

    uint64_t bytes_per_pixel = (fb_global->bpp / 8);
    uint64_t pixel_offset = (uint64_t)y * fb_pitch + (uint64_t)x * bytes_per_pixel;
    uint8_t* pixel_addr = fb_virt + pixel_offset;
    // Write 32-bit pixel if framebuffer has at least 32bpp, otherwise write up to available bytes
    if (bytes_per_pixel >= 4) {
        *(uint32_t*)pixel_addr = color;
    } else {
        // write least-significant bytes of color
        for (uint32_t i = 0; i < bytes_per_pixel; ++i) {
            pixel_addr[i] = (uint8_t)((color >> (8 * i)) & 0xFF);
        }
    }
}

void framebuffer_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    for (uint32_t py = 0; py < height; py++) {
        for (uint32_t px = 0; px < width; px++) {
            framebuffer_put_pixel(x + px, y + py, color);
        }
    }
}

void framebuffer_draw_filled_circle(uint32_t cx, uint32_t cy, uint32_t radius, uint32_t color) {
    int r = (int)radius;
    
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            int dist_sq = x * x + y * y;
            int r_sq = r * r;
            
            if (dist_sq <= r_sq) {
                int px = (int)cx + x;
                int py = (int)cy + y;
                
                if (px >= 0 && px < (int)fb_width && py >= 0 && py < (int)fb_height) {
                    framebuffer_put_pixel((uint32_t)px, (uint32_t)py, color);
                }
            }
        }
    }
}

void framebuffer_draw_line(int x1, int y1, int x2, int y2, uint32_t color) {
    // Bresenham's line algorithm
    int dx = x2 - x1;
    int dy = y2 - y1;
    
    int steps = (dx > dy) ? dx : dy;
    if (steps < 0) steps = -steps;
    
    if (steps == 0) {
        framebuffer_put_pixel(x1, y1, color);
        return;
    }
    
    float x_increment = (float)dx / steps;
    float y_increment = (float)dy / steps;
    
    float x = (float)x1;
    float y = (float)y1;
    
    for (int i = 0; i <= steps; i++) {
        int px = (int)(x + 0.5f);
        int py = (int)(y + 0.5f);
        
        if (px >= 0 && px < (int)fb_width && py >= 0 && py < (int)fb_height) {
            framebuffer_put_pixel((uint32_t)px, (uint32_t)py, color);
        }
        
        x += x_increment;
        y += y_increment;
    }
}

void framebuffer_clear(uint32_t color) {
    if (!fb_global) return;
    if (fb_virt == NULL) return;

    uint64_t bytes = fb_pitch * fb_height;
    uint64_t bpp_bytes = fb_global->bpp / 8;

    // If framebuffer is 32bpp and pitch is multiple of 4, fill as words for speed
    if (bpp_bytes >= 4 && (fb_pitch % 4) == 0) {
        uint32_t *pixels = (uint32_t*)fb_virt;
        uint64_t pixel_count = (fb_pitch / 4) * fb_height;
        for (uint64_t i = 0; i < pixel_count; i++) pixels[i] = color;
    } else {
        // Fallback: byte-wise fill using the provided color
        for (uint64_t y = 0; y < fb_height; ++y) {
            uint8_t *row = fb_virt + y * fb_pitch;
            for (uint64_t x = 0; x < fb_width; ++x) {
                uint8_t *pixel = row + x * bpp_bytes;
                for (uint32_t b = 0; b < bpp_bytes; ++b) {
                    pixel[b] = (uint8_t)((color >> (8 * b)) & 0xFF);
                }
            }
        }
    }
}

uint32_t framebuffer_get_width() {
    return (uint32_t)fb_width;
}

uint32_t framebuffer_get_height() {
    return (uint32_t)fb_height;
}

uint32_t framebuffer_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

uint32_t framebuffer_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b | ((uint32_t)a << 24);
}
