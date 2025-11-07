#include "pci.hpp"
#include "../utils/logger.hpp"
#include <stdint.h>

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t inl(uint16_t port) {
    uint32_t val;
    __asm__ volatile ("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

extern "C" uint32_t pci_cfg_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = (uint32_t)(
        (uint32_t)0x80000000u |
        ((uint32_t)bus << 16) |
        ((uint32_t)slot << 11) |
        ((uint32_t)func << 8) |
        (offset & 0xFC)
    );
    outl(0xCF8, addr);
    uint32_t data = inl(0xCFC);
    return data;
}

extern "C" void pci_enumerate(void (*cb)(uint8_t, uint8_t, uint8_t, uint16_t, uint16_t)) {
    // Conservative enumeration: most VM/hardware uses bus 0 for devices the
    // kernel cares about. Scanning all 256 buses produces a lot of output on
    // early debug builds and can flood the log. Keep enumeration limited to
    // bus 0 for now; expand later when a full PCI topology is required.
    for (uint8_t bus = 0; bus < 1; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint32_t d = pci_cfg_read32(bus, slot, func, 0x00);
                uint16_t vendor = (uint16_t)(d & 0xFFFF);
                if (vendor == 0xFFFF) {
                    // no device
                    if (func == 0) break; // skip multifunction probing for this slot
                    continue;
                }
                uint16_t device = (uint16_t)((d >> 16) & 0xFFFF);
                if (cb) cb(bus, slot, func, vendor, device);
            }
        }
    }
}
