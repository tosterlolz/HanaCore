#include "screen.h"

extern "C" void kernel_main() {
    // Initialize VGA
    clear_screen();
    print("HanaCore Kernel Initialized \n");
    print("Welcome to HanaCore — minimalist C++ OS kernel.\n");
    print("System ready.\n");

    auto outb = [](unsigned short port, unsigned char val) {
        asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
    };
    auto serial_init = [&]() {
        outb(0x3f8 + 1, 0x00);    // Disable all interrupts
        outb(0x3f8 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
        outb(0x3f8 + 0, 0x01);    // Divisor low byte (115200 baud)
        outb(0x3f8 + 1, 0x00);    // Divisor high byte
        outb(0x3f8 + 3, 0x03);    // 8 bits, no parity, one stop bit
        outb(0x3f8 + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
        outb(0x3f8 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
    };
    auto serial_write = [&](const char* s) {
        while (*s) {
            // wait for Transmitter Holding Register empty
            while (!([] (void) {
                unsigned char status;
                asm volatile ("inb %1, %0" : "=a"(status) : "Nd"(0x3f8 + 5));
                return (status & 0x20);
            }())) {}
            outb(0x3f8, *s);
            s++;
        }
    };
    serial_init();
    serial_write("[serial] kernel_main entered\n");

    while (true) {
        // nothing
    }
}
