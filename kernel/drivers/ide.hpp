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
bool ide_write_lba28(uint64_t lba, uint8_t count, const void* buf, bool master);

} // namespace drivers
} // namespace hanacore

// C wrappers for simple sector read/write/capacity used by filesystem code.
extern "C" {
	int ata_read_sector(uint32_t lba, void* buf);
	int ata_write_sector(uint32_t lba, const void* buf);
	int ata_read_sector_drive(uint32_t drive, uint32_t lba, void* buf);
	int ata_write_sector_drive(uint32_t drive, uint32_t lba, const void* buf);
	int32_t ata_get_sector_count();
	int32_t ata_get_sector_count_drive(int drive);
}
