// Simple window manager demo for the framebuffer
// Controls: WASD to move the green cursor square, 'q' to quit.

#include "../drivers/framebuffer.hpp"
#include "../drivers/keyboard.hpp"
#include "../drivers/mouse.hpp"
#include "cursor.hpp"
#include "../mem/heap.hpp"
#include "../drivers/screen.hpp"
#include "../scheduler/scheduler.hpp"

extern "C" void print(const char*);

extern "C" void builtin_wm_cmd(const char* arg) {
    (void)arg;
    if (!framebuffer_available()) {
        print("wm: framebuffer not available\n");
        return;
    }

    // Clear background
    clear_screen();

    uint32_t width = framebuffer_get_width();
    uint32_t height = framebuffer_get_height();

    auto draw_desktop = [&]() {
        // Draw two sample windows and some decorations
        framebuffer_draw_rect(40, 40, 360, 220, framebuffer_rgb(200, 200, 200));
        framebuffer_draw_rect(42, 42, 356, 20, framebuffer_rgb(60, 100, 200)); // title bar
        framebuffer_draw_rect(420, 80, 300, 180, framebuffer_rgb(220, 220, 220));
        framebuffer_draw_rect(422, 82, 296, 18, framebuffer_rgb(80, 140, 80));

        // Decorative content
        framebuffer_draw_filled_circle(200, 150, 28, framebuffer_rgb(255, 100, 100));
    };

    // Initial desktop draw
    draw_desktop();

    // Interactive green cursor square
    int cx = (int)(width / 2);
    int cy = (int)(height / 2);
    const int step = 6;
    const int cursz = 12;

    // Initial cursor draw (using cursor bitmap)
    auto draw_cursor_at = [&](int x, int y) {
        for (uint32_t py = 0; py < hanacore::cursor::HEIGHT; ++py) {
            for (uint32_t px = 0; px < hanacore::cursor::WIDTH; ++px) {
                if (hanacore::cursor::bitmap[py][px]) {
                    framebuffer_put_pixel((uint32_t)(x + (int)px), (uint32_t)(y + (int)py), framebuffer_rgb(255,255,255));
                }
            }
        }
    };

    draw_cursor_at(cx, cy);

    // Initialize mouse
    mouse_init();

    int oldx = cx, oldy = cy;

    // Main loop: poll mouse (preferred) and keyboard (fallback) and update cursor.
    while (1) {
        bool moved = false;
        int mdx = 0, mdy = 0, mbtn = 0;
        if (mouse_poll_delta(&mdx, &mdy, &mbtn)) {
            // PS/2 reports relative movement; apply to cursor (invert Y)
            cx += mdx;
            cy -= mdy;
            moved = (mdx != 0 || mdy != 0);
            // Left button to quit
            if (mbtn & 0x1) break;
        }

        // Keyboard fallback: allow quitting via 'q'
        char kc = keyboard_poll_char();
        if (kc) {
            if (kc == 'q' || kc == 'Q') break;
        }

        // Clamp within screen
        if (cx < 0) cx = 0;
        if (cy < 0) cy = 0;
        if ((uint32_t)(cx + cursz) >= width) cx = (int)width - cursz - 1;
        if ((uint32_t)(cy + cursz) >= height) cy = (int)height - cursz - 1;

        if (moved) {
            // Redraw desktop and draw cursor at new position (simple but safe)
            draw_desktop();
            draw_cursor_at(cx, cy);
            oldx = cx; oldy = cy;
        }

        // Yield so other tasks (including shell) can run
        hanacore::scheduler::sched_yield();
    }

    // Exit: leave the screen as-is. Print a message to the console TTY as well.
    print("wm: exiting\n");
}
