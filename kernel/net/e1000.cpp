#include "e1000.hpp"
#include "../drivers/pci.hpp"
#include "../utils/logger.hpp"

static void pci_cb(uint8_t bus, uint8_t slot, uint8_t func, uint16_t vendor, uint16_t device) {
    // Detect Intel PRO/1000 (common virtualbox model: vendor 0x8086, device 0x100E)
    if (vendor == 0x8086 && device == 0x100E) {
        hanacore::utils::log_ok_cpp("e1000: found device at %u:%u.%u (vendor=0x%04x device=0x%04x)", (unsigned)bus, (unsigned)slot, (unsigned)func, vendor, device);
        // Read BAR0
        uint32_t bar0 = pci_cfg_read32(bus, slot, func, 0x10);
        hanacore::utils::log_hex64_cpp("e1000: BAR0 raw", (uint64_t)bar0);
        // For MMIO BAR, the low bit cleared and base is top bits
        uint32_t mmio = bar0 & 0xFFFFFFF0u;
        hanacore::utils::log_hex64_cpp("e1000: MMIO base", (uint64_t)mmio);
        // We won't touch device registers yet; that requires DMA setup.
    }
}

extern "C" void e1000_init() {
    hanacore::utils::log_info_cpp("e1000: scanning PCI bus");
    pci_enumerate(pci_cb);
}
