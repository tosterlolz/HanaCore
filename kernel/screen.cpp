#include "screen.h"

static uint16_t* vga_buffer = (uint16_t*)0xb8000;
static uint8_t row = 0, col = 0;

static uint8_t make_color(uint8_t fg, uint8_t bg) {
    return fg | bg << 4;
}

static uint16_t make_vga_entry(char c, uint8_t color) {
    return (uint16_t)c | (uint16_t)color << 8;
}

void clear_screen() {
    uint8_t color = make_color(15, 0);
    for (int y = 0; y < 25; y++) {
        for (int x = 0; x < 80; x++) {
            vga_buffer[y * 80 + x] = make_vga_entry(' ', color);
        }
    }
    row = 0;
    col = 0;
}

void print(const char* str) {
    uint8_t color = make_color(15, 0);
    while (*str) {
        if (*str == '\n') {
            row++;
            col = 0;
        } else {
            vga_buffer[row * 80 + col] = make_vga_entry(*str, color);
            col++;
            if (col >= 80) {
                col = 0;
                row++;
            }
        }
        str++;
    }
}
