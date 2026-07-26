#ifndef RIBON_BIOS_VIDEO_H
#define RIBON_BIOS_VIDEO_H

#include <stdint.h>

struct RibonBiosVbeModeInfo {
    uint16_t mode;
    uint16_t width;
    uint16_t height;
    uint8_t bpp;
    uint64_t framebuffer;
    uint32_t pitch;
};

int ribon_bios_query_vbe_mode(uint16_t mode, struct RibonBiosVbeModeInfo *out);

#endif
