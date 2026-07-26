#ifndef RIBON_BIOS_DISK_H
#define RIBON_BIOS_DISK_H

#include <stdint.h>

struct RibonBiosDiskAddressPacket {
    uint8_t size;
    uint8_t reserved;
    uint16_t block_count;
    uint16_t buffer_offset;
    uint16_t buffer_segment;
    uint64_t lba;
};

int ribon_bios_read_lba(uint8_t drive, const struct RibonBiosDiskAddressPacket *packet);

#endif
