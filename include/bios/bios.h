#ifndef RIBON_BIOS_BIOS_H
#define RIBON_BIOS_BIOS_H

#include <bios/disk.h>
#include <bios/memory.h>
#include <bios/video.h>

enum RibonBiosStatus {
    RIBON_BIOS_STATUS_OK = 0,
    RIBON_BIOS_STATUS_BAD_ARGUMENT = -1,
    RIBON_BIOS_STATUS_UNSUPPORTED = -2,
};

const char *ribon_bios_adapter_name(void);
int ribon_bios_is_legacy_boot_supported(void);

#endif
