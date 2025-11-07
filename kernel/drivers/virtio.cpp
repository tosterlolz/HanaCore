#include "virtio.hpp"
#include "pci.hpp"
#include "../utils/logger.hpp"

static void pci_cb_forward(uint8_t b, uint8_t s, uint8_t f, uint16_t v, uint16_t d) {
    (void)b;(void)s;(void)f;(void)v;(void)d;
    // placeholder
}

extern "C" void virtio_pci_enumerate(void (*cb)(uint8_t bus, uint8_t slot, uint8_t func, uint16_t vendor, uint16_t device)) {
    // Leverage existing pci_enumerate but filter by vendor 0x1AF4 in caller.
    // For now just call pci_enumerate() and let caller inspect vendor/device.
    pci_enumerate(cb ? cb : pci_cb_forward);
}
