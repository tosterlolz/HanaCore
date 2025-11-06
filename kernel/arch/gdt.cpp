// Minimal GDT setup for x86_64 long mode.
#include "gdt.hpp"
#include "../drivers/screen.hpp"

// Logging helper (implemented in drivers/screen.cpp)
extern "C" void print(const char*);


struct __attribute__((packed)) gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
};

struct __attribute__((packed)) gdt_ptr {
    uint16_t limit;
    uint64_t base;
};

// Three entries: null, kernel code, kernel data
static gdt_entry gdt[3];
static gdt_ptr gp;

extern "C" void gdt_reload_segments(); // implemented in assembly
// Implement the segment reload helper in C using inline asm to avoid
// mixed assembler syntax issues. This performs a far-return to reload CS
// and then reloads the data segment registers.
extern "C" void gdt_reload_segments() {
    asm volatile (
        "pushq $0x08\n\t"
        "lea 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%ss\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        : : : "rax", "memory", "cc");
}

static void set_gdt_entry(int idx, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[idx].limit_low = (uint16_t)(limit & 0xFFFF);
    gdt[idx].base_low = (uint16_t)(base & 0xFFFF);
    gdt[idx].base_middle = (uint8_t)((base >> 16) & 0xFF);
    gdt[idx].access = access;
    gdt[idx].granularity = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[idx].base_high = (uint8_t)((base >> 24) & 0xFF);
}

extern "C" void gdt_install() {
    // Null descriptor
    set_gdt_entry(0, 0, 0, 0, 0);

    // Kernel code segment: access 0x9A, long mode bit set in granularity
    // gran: 0x20 for L bit; set upper nibble accordingly (0x20)
    set_gdt_entry(1, 0, 0, 0x9A, 0x20);

    // Kernel data segment: access 0x92, granularity 0
    set_gdt_entry(2, 0, 0, 0x92, 0x00);

    gp.limit = sizeof(gdt) - 1;
    gp.base = (uint64_t)&gdt;

    // Load GDT
    asm volatile ("lgdt %0" : : "m" (gp));

    // Reload segment registers (far jump) via assembly helper
    gdt_reload_segments();
    // Log success (debug port / screen will consume this when available)
    print("[OK] GDT installed\n");
}
