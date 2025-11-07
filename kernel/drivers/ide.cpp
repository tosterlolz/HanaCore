// Minimal PIO-ATA (IDE) driver implementation (LBA28 PIO reads).
// Note: this is small and blocking; it's intended for boot-time use
// to read a disk image from QEMU-attached IDE devices.

#include "ide.hpp"
#include "../utils/logger.hpp"


static inline unsigned char inb(unsigned short port) {
    unsigned char v;
    __asm__ volatile ("inb %1, %0" : "=a" (v) : "dN" (port));
    return v;
}

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a" (val), "dN" (port));
}

static inline unsigned short inw(unsigned short port) {
    unsigned short v;
    __asm__ volatile ("inw %1, %0" : "=a" (v) : "dN" (port));
    return v;
}

static inline void outw(unsigned short port, unsigned short val) {
    __asm__ volatile ("outw %0, %1" : : "a" (val), "dN" (port));
}

static inline void io_wait(void) {
    // port 0x80 is safe for short delays on x86
    __asm__ volatile ("outb %%al, $0x80" : : "a" (0));
}

namespace hanacore {
namespace drivers {

// Primary channel I/O ports
static const unsigned short ATA_DATA = 0x1F0;
static const unsigned short ATA_ERROR = 0x1F1;
static const unsigned short ATA_SECTOR_COUNT = 0x1F2;
static const unsigned short ATA_LBA_LOW = 0x1F3;
static const unsigned short ATA_LBA_MID = 0x1F4;
static const unsigned short ATA_LBA_HIGH = 0x1F5;
static const unsigned short ATA_DRIVE = 0x1F6;
static const unsigned short ATA_COMMAND = 0x1F7;
static const unsigned short ATA_ALT_STATUS = 0x3F6;

static inline unsigned char ata_status() {
    return inb(ATA_COMMAND);
}

static bool ata_wait_not_busy(unsigned int timeout_ms) {
    // simple polling with a crude timeout
    unsigned int loops = timeout_ms * 1000u;
    while (loops--) {
        unsigned char s = ata_status();
        if (!(s & 0x80)) return true; // BSY cleared
    }
    return false;
}

bool ide_init() {
    // Very small probe: try IDENTIFY on master and see if it returns.
    // Write drive/head with master select and issue IDENTIFY (0xEC).
    outb(ATA_DRIVE, 0xA0); // master, LBA mode off for IDENTIFY
    io_wait();
    outb(ATA_COMMAND, 0xEC); // IDENTIFY
    io_wait();
    // Poll status for DRQ or ERR
    unsigned char s = inb(ATA_COMMAND);
    if (s == 0) return false; // no device
    // If ERR set, no ATA device
    if (s & 0x01) return false;
    // If DRQ set, device responded
    if (s & 0x08) return true;
    // Otherwise attempt a short wait
    if (!ata_wait_not_busy(50)) return false;
    s = inb(ATA_COMMAND);
    if (s & 0x08) return true;
    return false;
}

// Read up to 256 sectors using LBA28 (count 0 => 256). master==true selects master.
bool ide_read_lba28(uint64_t lba64, uint8_t count, void* buf, bool master) {
    if (count == 0) count = 256;
    if (count > 256) return false;
    uint32_t lba = (uint32_t)lba64;
    // only support 28-bit LBA here
    if (lba64 >> 28) return false;

    // Wait until not busy
    if (!ata_wait_not_busy(500)) return false;

    // select drive and top 4 bits of LBA
    unsigned char drive = 0xE0 | ((master ? 0x0 : 0x10)) | ((lba >> 24) & 0x0F);
    outb(ATA_DRIVE, drive);
    io_wait();

    outb(ATA_SECTOR_COUNT, count);
    outb(ATA_LBA_LOW, (unsigned char)(lba & 0xFF));
    outb(ATA_LBA_MID, (unsigned char)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (unsigned char)((lba >> 16) & 0xFF));

    outb(ATA_COMMAND, 0x20); // READ SECTORS (PIO)

    unsigned char* dst = (unsigned char*)buf;
    for (unsigned int s = 0; s < count; ++s) {
        // wait for BSY clear and DRQ set
        unsigned int tries = 1000000;
        while (tries--) {
            unsigned char st = inb(ATA_COMMAND);
            if (st & 0x01) return false; // ERR
            if (!(st & 0x80) && (st & 0x08)) break; // not BSY and DRQ
        }

        // read 256 words (512 bytes)
        for (int w = 0; w < 256; ++w) {
            unsigned short word = inw(ATA_DATA);
            dst[0] = (unsigned char)(word & 0xFF);
            dst[1] = (unsigned char)((word >> 8) & 0xFF);
            dst += 2;
        }
    }

    return true;
}

// Write up to 256 sectors using LBA28 (count 0 => 256). master==true selects master.
bool ide_write_lba28(uint64_t lba64, uint8_t count, const void* buf, bool master) {
    if (count == 0) count = 256;
    if (count > 256) return false;
    uint32_t lba = (uint32_t)lba64;
    if (lba64 >> 28) return false;

    if (!ata_wait_not_busy(500)) return false;

    unsigned char drive = 0xE0 | ((master ? 0x0 : 0x10)) | ((lba >> 24) & 0x0F);
    outb(ATA_DRIVE, drive);
    io_wait();

    outb(ATA_SECTOR_COUNT, count);
    outb(ATA_LBA_LOW, (unsigned char)(lba & 0xFF));
    outb(ATA_LBA_MID, (unsigned char)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (unsigned char)((lba >> 16) & 0xFF));

    outb(ATA_COMMAND, 0x30); // WRITE SECTORS (PIO)

    const unsigned char* src = (const unsigned char*)buf;
    for (unsigned int s = 0; s < count; ++s) {
        unsigned int tries = 1000000;
        while (tries--) {
            unsigned char st = inb(ATA_COMMAND);
            if (st & 0x01) return false; // ERR
            if (!(st & 0x80) && (st & 0x08)) break; // not BSY and DRQ
        }

        // write 256 words (512 bytes)
        for (int w = 0; w < 256; ++w) {
            unsigned short word = (unsigned short)src[0] | ((unsigned short)src[1] << 8);
            outw(ATA_DATA, word);
            src += 2;
        }
    }

    return true;
}

} // namespace drivers
} // namespace hanacore

// C wrapper for legacy callers (e.g. fat32.cpp) that expect a plain
// `ata_read_sector(uint32_t, void*)` symbol. Returns 0 on success, -1 on
// failure to match the convention used by the FAT32 code.
extern "C" int ata_read_sector(uint32_t lba, void* buf) {
    // Ensure the IDE driver is initialized before attempting reads.
    static bool ide_initialized = false;
    if (!ide_initialized) {
        ide_initialized = hanacore::drivers::ide_init();
        if (!ide_initialized) {
            // Log driver init failure for diagnostics
            hanacore::utils::log_info_cpp("[IDE] ide_init() failed — ATA device not available");
        }
    }
    if (!ide_initialized) return -1;

    // Call the C++ namespaced driver implementation. Read a single sector
    // from the master device.
    bool ok = hanacore::drivers::ide_read_lba28((uint64_t)lba, 1, buf, true);
    return ok ? 0 : -1;
}

// C wrapper for writing a single sector. Returns 0 on success, -1 on failure.
extern "C" int ata_write_sector(uint32_t lba, const void* buf) {
    static bool ide_initialized = false;
    if (!ide_initialized) {
        ide_initialized = hanacore::drivers::ide_init();
        if (!ide_initialized) {
            hanacore::utils::log_info_cpp("[IDE] ide_init() failed — ATA device not available");
            return -1;
        }
    }
    bool ok = hanacore::drivers::ide_write_lba28((uint64_t)lba, 1, buf, true);
    return ok ? 0 : -1;
}

// Return the total number of user-addressable sectors reported by IDENTIFY (LBA28).
// Returns -1 on failure.
extern "C" int32_t ata_get_sector_count() {
    unsigned short ident[256];

    // Issue IDENTIFY to master
    outb(hanacore::drivers::ATA_DRIVE, 0xA0);
    io_wait();
    outb(hanacore::drivers::ATA_COMMAND, 0xEC);
    io_wait();

    unsigned char s = inb(hanacore::drivers::ATA_COMMAND);
    if (s == 0) return -1;
    if (s & 0x01) return -1;

    if (!hanacore::drivers::ata_wait_not_busy(500)) return -1;

    // Wait for DRQ
    unsigned int tries = 1000000;
    while (tries--) {
        unsigned char st = inb(hanacore::drivers::ATA_COMMAND);
        if (st & 0x08) break;
        if (st & 0x01) return -1;
    }

    // Read 256 words
    for (int i = 0; i < 256; ++i) {
        ident[i] = inw(hanacore::drivers::ATA_DATA);
    }

    // Words 60-61 contain total number of user-addressable sectors (LBA28)
    uint32_t low = ident[60];
    uint32_t high = ident[61];
    uint32_t total = (high << 16) | low;
    return (int32_t)total;
}
