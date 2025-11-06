#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "../screen.h"

#ifdef __cplusplus
extern "C" {
#endif

// Framebuffer initialization and status
bool framebuffer_init();
bool framebuffer_available();

// Basic pixel operations
void framebuffer_put_pixel(uint32_t x, uint32_t y, uint32_t color);

// Drawing primitives
void framebuffer_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
void framebuffer_draw_filled_circle(uint32_t cx, uint32_t cy, uint32_t radius, uint32_t color);
void framebuffer_draw_line(int x1, int y1, int x2, int y2, uint32_t color);

// Framebuffer management
void framebuffer_clear(uint32_t color);
uint32_t framebuffer_get_width();
uint32_t framebuffer_get_height();

// Color helpers
uint32_t framebuffer_rgb(uint8_t r, uint8_t g, uint8_t b);
uint32_t framebuffer_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

#ifdef __cplusplus
}
#endif
