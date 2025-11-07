// Minimal PIO-ATA (IDE) driver: supports reading sectors from the primary
// ATA channel (master/slave) using LBA28 reads. This is intentionally small
// and only implements what's needed to read disk images provided by QEMU.

#pragma once
#include <stdint.h>

namespace hanacore {
namespace drivers {

// Initialize IDE driver (probe). Returns true if a device is present.
bool ide_init();

// Read `count` sectors starting at `lba` into `buf`. `count` must be >0
// and <=256. Returns true on success.
bool ide_read_lba28(uint64_t lba, uint8_t count, void* buf, bool master);

} // namespace drivers
} // namespace hanacore
