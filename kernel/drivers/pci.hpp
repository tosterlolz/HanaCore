#pragma once
#include <stdint.h>

extern "C" {
// Read a 32-bit value from PCI config space using CF8/CFC IO ports.
uint32_t pci_cfg_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
// Scan PCI bus and call `cb` for each device found. cb receives (bus,slot,func,vendor,device)
void pci_enumerate(void (*cb)(uint8_t, uint8_t, uint8_t, uint16_t, uint16_t));
}
