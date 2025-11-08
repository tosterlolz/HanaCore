// Minimal polled PS/2 mouse driver
// Provides mouse_init() and mouse_poll_delta(). This is a simple implementation
// suitable for a hobby kernel: it enables data reporting and parses 3-byte
// PS/2 mouse packets (no wheel or extended features).

#include "mouse.hpp"
#include <stddef.h>
#include <stdint.h>

extern "C" void print(const char*);

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a" (ret) : "Nd" (port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a" (val), "Nd" (port));
}

static inline void io_wait(void) {
    asm volatile ("outb %%al, $0x80" : : "a" (0));
}

enum { PS2_DATA = 0x60, PS2_STATUS = 0x64 };

// Internal packet buffer
static uint8_t pkt[3];
static int pkt_idx = 0;

// Helper to wait for input buffer to be clear before writing controller
static int wait_input_clear(int timeout) {
    while (timeout-- > 0) {
        uint8_t s = inb(PS2_STATUS);
        if ((s & 0x02) == 0) return 1; // input buffer clear
        io_wait();
    }
    return 0;
}

// Helper to read ACK (0xFA) from device with small timeout
static int read_ack(int timeout) {
    while (timeout-- > 0) {
        uint8_t s = inb(PS2_STATUS);
        if (s & 1) {
            uint8_t b = inb(PS2_DATA);
            if (b == 0xFA) return 1;
            // Some devices may echo other values; accept 0xFA only
        }
        io_wait();
    }
    return 0;
}

void mouse_init(void) {
    // Drain any pending data
    for (int i = 0; i < 32; ++i) {
        uint8_t s = inb(PS2_STATUS);
        if (s & 1) (void)inb(PS2_DATA);
        else break;
    }
    // Enable auxiliary device (mouse) port on controller.
    if (!wait_input_clear(500)) { print("mouse: controller busy\n"); return; }
    outb(PS2_STATUS, 0xA8); // enable auxiliary device
    io_wait();

    // Read controller configuration byte, set the auxiliary IRQ enable bit
    if (!wait_input_clear(500)) { print("mouse: cannot read cmd byte\n"); return; }
    outb(PS2_STATUS, 0x20); // command 0x20: read command byte
    io_wait();
    if (!(inb(PS2_STATUS) & 1)) { print("mouse: no cmd byte\n"); }
    uint8_t cmd = inb(PS2_DATA);
    // Set bit 1 (enable IRQ12 / auxiliary) and clear disable flags if any
    cmd |= 0x02;
    if (!wait_input_clear(500)) { print("mouse: cannot write cmd byte\n"); return; }
    outb(PS2_STATUS, 0x60); // command 0x60: write command byte
    io_wait();
    if (!wait_input_clear(500)) { print("mouse: cmd write busy\n"); return; }
    outb(PS2_DATA, cmd);
    io_wait();

    // Tell the mouse to use defaults and enable data reporting (0xF6 then 0xF4)
    if (!wait_input_clear(500)) { print("mouse: device busy before D4\n"); }
    outb(PS2_STATUS, 0xD4); // next byte goes to mouse
    io_wait();
    if (!wait_input_clear(500)) { print("mouse: device busy before F6\n"); }
    outb(PS2_DATA, 0xF6); // set defaults
    io_wait();
    (void)read_ack(200);

    if (!wait_input_clear(500)) { print("mouse: device busy before enable\n"); }
    outb(PS2_STATUS, 0xD4);
    io_wait();
    if (!wait_input_clear(500)) { print("mouse: device busy before F4\n"); }
    outb(PS2_DATA, 0xF4); // enable data reporting
    io_wait();
    if (read_ack(500)) print("mouse: enabled\n"); else print("mouse: enabled (no ack)\n");

    // Reset packet state
    pkt_idx = 0;
}

int mouse_poll_delta(int* dx, int* dy, int* buttons) {
    (void)buttons;
    // If no data available, return 0
    uint8_t s = inb(PS2_STATUS);
    if (!(s & 1)) return 0;

    uint8_t b = inb(PS2_DATA);

    // Ignore stray values until we detect a valid first packet byte
    if (pkt_idx == 0) {
        // First byte should have bit 3 set (0x08) per protocol
        if ((b & 0x08) == 0) return 0;
    }

    pkt[pkt_idx++] = b;
    if (pkt_idx < 3) return 0;

    // Parse 3-byte PS/2 packet
    uint8_t b0 = pkt[0];
    uint8_t b1 = pkt[1];
    uint8_t b2 = pkt[2];

    // Buttons: bits 0..2
    int btn = b0 & 0x07;

    // X/Y are signed 8-bit with possible overflow bits in b0; handle sign via cast
    int sdx = (int8_t)b1;
    int sdy = (int8_t)b2;

    // Y is negative when moving up; invert if you prefer screen coords
    // We'll keep mouse Y as negative-up so moving down increases Y

    if (dx) *dx = sdx;
    if (dy) *dy = sdy;
    if (buttons) *buttons = btn;

    pkt_idx = 0;
    return 1;
}
