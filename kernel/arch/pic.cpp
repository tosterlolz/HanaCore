#include "pic.hpp"

// Minimal port I/O helpers
static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    asm volatile ("inb %1, %0" : "=a" (ret) : "Nd" (port));
    return ret;
}

static inline void outb(unsigned short port, unsigned char val) {
    asm volatile ("outb %0, %1" : : "a" (val), "Nd" (port));
}

// PIC ports
enum {
    PIC1_CMD = 0x20,
    PIC1_DATA = 0x21,
    PIC2_CMD = 0xA0,
    PIC2_DATA = 0xA1,
    ICW1_INIT = 0x11,
    ICW4_8086 = 0x01
};

namespace hanacore { namespace arch { namespace pic {

void remap() {
    // Save masks
    unsigned char a1 = inb(PIC1_DATA);
    unsigned char a2 = inb(PIC2_DATA);

    // Start initialization sequence (in cascade mode)
    outb(PIC1_CMD, ICW1_INIT);
    outb(PIC2_CMD, ICW1_INIT);
    // Set vector offset: remap PIC1 to 0x20, PIC2 to 0x28
    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);
    // Tell PIC1 about PIC2 at IRQ2 (0000 0100)
    outb(PIC1_DATA, 0x04);
    // Tell PIC2 its cascade identity (0000 0010)
    outb(PIC2_DATA, 0x02);

    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);

    // Restore saved masks
    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, a2);
}

void send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_CMD, 0x20);
    }
    outb(PIC1_CMD, 0x20);
}

}}}

// C ABI wrappers
extern "C" void pic_remap() {
    hanacore::arch::pic::remap();
}

extern "C" void pic_send_eoi(uint8_t irq) {
    hanacore::arch::pic::send_eoi(irq);
}
