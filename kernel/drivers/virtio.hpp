#pragma once
#include <stdint.h>

// Minimal virtio PCI helpers and constants for virtio-net scaffolding.
// This is a small subset sufficient to detect virtio devices and perform
// further initialization in the driver.

#define VIRTIO_PCI_VENDOR 0x1AF4

// PCI device IDs for virtio
#define VIRTIO_ID_NET 0x1000

extern "C" {
void virtio_pci_enumerate(void (*cb)(uint8_t bus, uint8_t slot, uint8_t func, uint16_t vendor, uint16_t device));
}
