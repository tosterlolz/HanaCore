#include "pit.hpp"
#include "pic.hpp"
#include "idt.hpp"
#include "../scheduler/scheduler.hpp"

// Assembly ISR wrapper declared with C linkage
extern "C" void pit_entry();

// I/O helpers
static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    asm volatile ("inb %1, %0" : "=a" (ret) : "Nd" (port));
    return ret;
}

static inline void outb(unsigned short port, unsigned char val) {
    asm volatile ("outb %0, %1" : : "a" (val), "Nd" (port));
}

// PIT ports
enum {
    PIT_CHANNEL0 = 0x40,
    PIT_COMMAND = 0x43,
    PIT_INPUT_FREQ = 1193182
};

// Forward declaration for IDT registration (vector 0x20 = IRQ0 after PIC remap)
static const int PIT_VECTOR = 0x20;

namespace hanacore { namespace arch { namespace pit {

void isr() {
    // Acknowledge PIC for IRQ0
    pic_send_eoi(0);
    // For now, do not perform a context switch inside the interrupt handler.
    // Performing context switches directly from IRQ context requires the
    // switch routine to restore an interrupt return frame for the new task.
    // To avoid crashes we'll keep scheduling cooperative: the timer just
    // increments the tick counter and returns. Future work: implement
    // irq-safe context switching or build interrupt frames for tasks.
    static volatile uint64_t ticks = 0;
    (void)ticks; // keep unused when optimizations remove it
    ++ticks;
}

void init(uint32_t freq) {
    if (freq == 0) return;
    // Remap the PIC so IRQs start at 0x20/0x28
    pic_remap();

    // Compute divisor
    uint16_t divisor = (uint16_t)(PIT_INPUT_FREQ / freq);

    // Set PIT to mode 2 (rate generator), access mode lobyte/hibyte, channel 0
    outb(PIT_COMMAND, 0x34);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    // Register ISR into IDT. Use the assembly wrapper `pit_entry` (defined
    // in pit_entry.S) which calls the C handler and then performs an iretq.
    idt_set_handler(PIT_VECTOR, pit_entry);
}

}}}

// C ABI wrappers that forward to namespaced implementations
extern "C" void pit_isr() {
    hanacore::arch::pit::isr();
}

extern "C" void pit_init(uint32_t freq) {
    hanacore::arch::pit::init(freq);
}
