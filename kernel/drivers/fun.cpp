#include "fun.hpp"
#include "framebuffer.hpp"

void lol() {
    uint32_t width = framebuffer_get_width();
    uint32_t height = framebuffer_get_height();

    // Draw some graphics
    uint32_t red = framebuffer_rgb(255, 0, 0);
    uint32_t green = framebuffer_rgb(0, 255, 0);
    uint32_t blue = framebuffer_rgb(0, 0, 255);
    uint32_t yellow = framebuffer_rgb(255, 255, 0);

    // Draw circles
    framebuffer_draw_filled_circle(width / 2, height / 2, 100, red);
    framebuffer_draw_filled_circle(width / 4, height / 4, 50, green);
    framebuffer_draw_filled_circle(3 * width / 4, height / 4, 50, blue);
    framebuffer_draw_filled_circle(width / 4, 3 * height / 4, 50, yellow);
}