#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Use the Limine-provided definitions for requests and responses. */
#include "../../boot/limine.h"

#ifdef __cplusplus
extern "C" {
#endif

void clear_screen();
void print(const char* str);
// Cursor and simple framebuffer helpers (for GUI mode)
void screen_fill_rect(int x, int y, int w, int h, uint32_t color);
void screen_draw_rect(int x, int y, int w, int h, uint32_t color);
// GUI mode: when active, `print()` will be routed to the GUI terminal and
// the framebuffer main console will be cleared. Call from the shell when
// entering/exiting GUI mode.
// GUI terminal helpers: create/destroy a flanterm-backed terminal in a
// rectangle on the framebuffer. Returns 1 on success.
#ifdef __cplusplus
}
#endif
