#include <Ribon/firmware.h>
#include <bios/bios.h>

const char *ribon_bios_adapter_name(void) {
    return "ribon-bios";
}

int ribon_bios_is_legacy_boot_supported(void) {
    return 0;
}

int ribon_bios_collect_e820(struct RibonBiosE820Table *table) {
    if (table == 0) {
        return RIBON_BIOS_STATUS_BAD_ARGUMENT;
    }
    table->count = 0;
    return RIBON_BIOS_STATUS_UNSUPPORTED;
}

int ribon_bios_read_lba(uint8_t drive, const struct RibonBiosDiskAddressPacket *packet) {
    (void)drive;
    if (packet == 0) {
        return RIBON_BIOS_STATUS_BAD_ARGUMENT;
    }
    return RIBON_BIOS_STATUS_UNSUPPORTED;
}

int ribon_bios_query_vbe_mode(uint16_t mode, struct RibonBiosVbeModeInfo *out) {
    (void)mode;
    if (out == 0) {
        return RIBON_BIOS_STATUS_BAD_ARGUMENT;
    }
    out->mode = 0;
    out->width = 0;
    out->height = 0;
    out->bpp = 0;
    out->framebuffer = 0;
    out->pitch = 0;
    return RIBON_BIOS_STATUS_UNSUPPORTED;
}

static int bios_probe(const struct RibonArchDescriptor *arch) {
    return ribon_firmware_adapter_supports_arch(ribon_firmware_bios_adapter(), arch);
}

static int bios_collect(const struct RibonArchDescriptor *arch, struct RibonBootEnvironment *out) {
    if (arch == 0 || out == 0) {
        return RIBON_FIRMWARE_STATUS_BAD_ARGUMENT;
    }
    ribon_boot_environment_init(out, RIBON_FIRMWARE_BIOS, arch);
    return RIBON_FIRMWARE_STATUS_UNSUPPORTED;
}

static const struct RibonFirmwareAdapter bios_adapter = {
    .firmware = RIBON_FIRMWARE_BIOS,
    .name = "bios",
    .probe = bios_probe,
    .collect = bios_collect,
};

const struct RibonFirmwareAdapter *ribon_firmware_bios_adapter(void) {
    return &bios_adapter;
}
