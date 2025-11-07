#include <stdint.h>

extern "C" void syscall_entry();

static inline void write_msr(uint32_t msr, uint64_t val) {
    uint32_t lo = (uint32_t)(val & 0xFFFFFFFFULL);
    uint32_t hi = (uint32_t)(val >> 32);
    asm volatile ("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

extern "C" void init_syscall() {
    // IA32_STAR (0xC0000081): set kernel/user cs selectors
    // Kernel CS = 0x08, User CS = 0x1B
    uint64_t star = ((uint64_t)0x08 << 32) | ((uint64_t)0x1B << 48);
    write_msr(0xC0000081, star);

    // IA32_LSTAR (0xC0000082): pointer to syscall entry point
    write_msr(0xC0000082, (uint64_t)(uintptr_t)syscall_entry);

    // IA32_FMASK (0xC0000084): mask flags during syscall entry (clear TF, IF as desired)
    write_msr(0xC0000084, 0);
}
