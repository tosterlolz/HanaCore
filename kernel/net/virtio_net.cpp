#include "../drivers/virtio.hpp"
#include "../drivers/pci.hpp"
#include "netif.hpp"
#include "../utils/logger.hpp"
#include <stdint.h>

static int virtio_xmit(netif_t* nif, const void* pkt, size_t len) {
    // Not yet implemented: real virtio-net transmit requires setting up
    // virtqueues and DMA buffers. Return error for now.
    hanacore::utils::log_info_cpp("virtio-net: xmit called (%u bytes) - not implemented", (unsigned)len);
    return -1;
}

static void virtio_rx_stub(netif_t* nif, const void* pkt, size_t len) {
    hanacore::utils::log_info_cpp("virtio-net: rx (%u bytes) - stub", (unsigned)len);
}

static void pci_cb(uint8_t bus, uint8_t slot, uint8_t func, uint16_t vendor, uint16_t device) {
    if (vendor != VIRTIO_PCI_VENDOR) return;
    if (device != VIRTIO_ID_NET) return;
    hanacore::utils::log_ok_cpp("virtio-net: found virtio-net at %u:%u.%u", (unsigned)bus, (unsigned)slot, (unsigned)func);

    static netif_t virtif = { "vtnet0", virtio_rx_stub, virtio_xmit, nullptr };
    if (netif_register(&virtif) == 0) {
        hanacore::utils::log_ok_cpp("virtio-net: registered netif vtnet0");
    }
}

extern "C" void virtio_net_init() {
    hanacore::utils::log_info_cpp("virtio-net: scanning PCI for virtio devices");
    virtio_pci_enumerate(pci_cb);
}
