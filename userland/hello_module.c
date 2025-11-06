// Simple module program (ELF) that writes to serial port directly.
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

void _start(void) {
    const char *s = "Hello from ELF module!\n";
    for (const char *p = s; *p; ++p) outb(0x3f8, *p);
    // return to caller (module runner will continue)
}
