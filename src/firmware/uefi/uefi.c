#include <Ribon/firmware.h>

static int uefi_probe(const struct RibonArchDescriptor *arch) {
    return ribon_firmware_adapter_supports_arch(ribon_firmware_uefi_adapter(), arch);
}

static int uefi_collect(const struct RibonArchDescriptor *arch, struct RibonBootEnvironment *out) {
    if (arch == 0 || out == 0) {
        return RIBON_FIRMWARE_STATUS_BAD_ARGUMENT;
    }
    ribon_boot_environment_init(out, RIBON_FIRMWARE_UEFI, arch);
    return RIBON_FIRMWARE_STATUS_UNSUPPORTED;
}

static const struct RibonFirmwareAdapter uefi_adapter = {
    .firmware = RIBON_FIRMWARE_UEFI,
    .name = "uefi",
    .probe = uefi_probe,
    .collect = uefi_collect,
};

const struct RibonFirmwareAdapter *ribon_firmware_uefi_adapter(void) {
    return &uefi_adapter;
}
