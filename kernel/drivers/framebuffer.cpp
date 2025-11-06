#include "framebuffer.h"
#include "../screen.h"
#include <stdint.h>
#include <stddef.h>

// Global framebuffer state
static struct limine_framebuffer* fb_global = NULL;
static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_pitch = 0;

// Framebuffer request (defined in screen.c)
extern "C" {
    extern struct limine_framebuffer_request framebuffer_request;
}

bool framebuffer_init() {
    if (!framebuffer_request.response) {
        return false;
    }
    
    if (framebuffer_request.response->framebuffer_count == 0) {
        return false;
    }
    
    fb_global = framebuffer_request.response->framebuffers[0];
    fb_width = fb_global->width;
    fb_height = fb_global->height;
    fb_pitch = fb_global->pitch;
    
    return true;
}

bool framebuffer_available() {
    return fb_global != NULL;
}

void framebuffer_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!fb_global || x >= fb_width || y >= fb_height) {
        return;
    }
    
    uint32_t pixel_offset = y * fb_pitch + x * (fb_global->bpp / 8);
    uint32_t* pixel = (uint32_t*)((uint64_t)fb_global->address + pixel_offset);
    *pixel = color;
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
    
    uint32_t* pixels = (uint32_t*)((uint64_t)fb_global->address);
    uint32_t pixel_count = (fb_pitch / 4) * fb_height;
    
    for (uint32_t i = 0; i < pixel_count; i++) {
        pixels[i] = color;
    }
}

uint32_t framebuffer_get_width() {
    return fb_width;
}

uint32_t framebuffer_get_height() {
    return fb_height;
}

uint32_t framebuffer_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

uint32_t framebuffer_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b | ((uint32_t)a << 24);
}
