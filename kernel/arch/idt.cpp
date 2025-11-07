// Minimal IDT setup for x86_64. Install an empty IDT so lidt has a valid table.
#include "idt.hpp"
#include <stdint.h>

// Logging helper (implemented in drivers/screen.cpp)
extern "C" void print(const char*);

struct __attribute__((packed)) idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
};

struct __attribute__((packed)) idt_ptr {
    uint16_t limit;
    uint64_t base;
};

static idt_entry idt[256];
static idt_ptr iptr;

// Provide a tiny ISR stub implemented in C using inline asm. It performs
// an iretq so that if an interrupt fires the CPU can return cleanly.
extern "C" void isr_common_stub() {
    asm volatile ("iretq");
}

static void set_idt_entry(int vec, void (*handler)(), uint16_t sel, uint8_t type_attr) {
    uint64_t addr = (uint64_t)handler;
    idt[vec].offset_low = (uint16_t)(addr & 0xFFFF);
    idt[vec].selector = sel;
    idt[vec].ist = 0;
    idt[vec].type_attr = type_attr;
    idt[vec].offset_mid = (uint16_t)((addr >> 16) & 0xFFFF);
    idt[vec].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    idt[vec].zero = 0;
}

// Exported helper to set a single IDT entry after the table has been created.
extern "C" void idt_set_handler(int vec, void (*handler)()) {
    // Use kernel code segment selector 0x08 and present interrupt gate 0x8E
    set_idt_entry(vec, handler, 0x08, 0x8E);
}

extern "C" void idt_install() {
    for (int i = 0; i < 256; ++i) {
        set_idt_entry(i, isr_common_stub, 0x08, 0x8E); // present, interrupt gate
    }

    iptr.limit = sizeof(idt) - 1;
    iptr.base = (uint64_t)&idt;
    asm volatile ("lidt %0" : : "m" (iptr));
    // Log success
    print("[OK] IDT installed\n");
}

// Namespaced C++ wrappers
namespace hanacore { namespace arch { namespace idt {
    void install() {
        idt_install();
    }

    void set_handler(int vec, void (*handler)()) {
        idt_set_handler(vec, handler);
    }

}}}
