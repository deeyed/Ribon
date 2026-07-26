#include <Ribon/arch.h>
#include <Ribon/firmware.h>

const char *ribon_firmware_name(enum RibonFirmwareKind firmware) {
    switch (firmware) {
    case RIBON_FIRMWARE_HOST:
        return "host";
    case RIBON_FIRMWARE_UEFI:
        return "uefi";
    case RIBON_FIRMWARE_BIOS:
        return "bios";
    case RIBON_FIRMWARE_RASPBERRY_PI:
        return "raspberry-pi";
    default:
        return "unknown";
    }
}

const char *ribon_boot_media_name(enum RibonBootMediaKind media) {
    switch (media) {
    case RIBON_BOOT_MEDIA_NONE:
        return "none";
    case RIBON_BOOT_MEDIA_FILE:
        return "file";
    case RIBON_BOOT_MEDIA_BLOCK:
        return "block";
    case RIBON_BOOT_MEDIA_MEMORY:
        return "memory";
    case RIBON_BOOT_MEDIA_NETWORK:
        return "network";
    default:
        return "unknown";
    }
}

uint32_t ribon_firmware_arch_mask(enum RibonFirmwareKind firmware) {
    switch (firmware) {
    case RIBON_FIRMWARE_HOST:
        return RIBON_ARCH_FIRMWARE_HOST_TEST;
    case RIBON_FIRMWARE_UEFI:
        return RIBON_ARCH_FIRMWARE_UEFI;
    case RIBON_FIRMWARE_BIOS:
        return RIBON_ARCH_FIRMWARE_BIOS;
    case RIBON_FIRMWARE_RASPBERRY_PI:
        return RIBON_ARCH_FIRMWARE_RASPBERRY_PI;
    default:
        return 0u;
    }
}

void ribon_boot_environment_init(
    struct RibonBootEnvironment *environment,
    enum RibonFirmwareKind firmware,
    const struct RibonArchDescriptor *arch) {
    if (environment == 0) {
        return;
    }
    environment->firmware = firmware;
    environment->arch = arch;
    environment->memory_map.regions = 0;
    environment->memory_map.region_count = 0;
    environment->raw_memory_map.data = 0;
    environment->raw_memory_map.size = 0;
    environment->raw_memory_map.descriptor_size = 0;
    environment->raw_memory_map.descriptor_version = 0;
    environment->device_tree.physical_address = 0;
    environment->device_tree.size = 0;
    environment->device_tree.data = 0;
    environment->framebuffer.physical_address = 0;
    environment->framebuffer.width = 0;
    environment->framebuffer.height = 0;
    environment->framebuffer.pitch = 0;
    environment->framebuffer.bits_per_pixel = 0;
    environment->framebuffer.backend = RIBON_FRAMEBUFFER_BACKEND_UNKNOWN;
    environment->framebuffer.rgb.red_position = 0;
    environment->framebuffer.rgb.red_mask_size = 0;
    environment->framebuffer.rgb.green_position = 0;
    environment->framebuffer.rgb.green_mask_size = 0;
    environment->framebuffer.rgb.blue_position = 0;
    environment->framebuffer.rgb.blue_mask_size = 0;
    environment->acpi_rsdp.physical_address = 0;
    environment->acpi_rsdp.data = 0;
    environment->acpi_rsdp.size = 0;
    environment->acpi_rsdp.revision = 0;
    environment->boot_media.kind = RIBON_BOOT_MEDIA_NONE;
    environment->boot_media.path = 0;
    environment->boot_media.physical_address = 0;
    environment->boot_media.size = 0;
    environment->boot_media.block_size = 0;
    environment->boot_modules.modules = 0;
    environment->boot_modules.module_count = 0;
    environment->command_line.text = 0;
    environment->command_line.length = 0;
    environment->flags = 0;
}

int ribon_boot_environment_is_valid(const struct RibonBootEnvironment *environment) {
    if (environment == 0 || environment->arch == 0) {
        return 0;
    }
    if (!ribon_arch_has_firmware_mask(environment->arch, ribon_firmware_arch_mask(environment->firmware))) {
        return 0;
    }
    if ((environment->flags & RIBON_BOOT_ENV_HAS_MEMORY_MAP) != 0u &&
        (environment->memory_map.regions == 0 || environment->memory_map.region_count == 0u)) {
        return 0;
    }
    if ((environment->flags & RIBON_BOOT_ENV_HAS_BOOT_MEDIA) != 0u &&
        environment->boot_media.kind == RIBON_BOOT_MEDIA_NONE) {
        return 0;
    }
    if ((environment->flags & RIBON_BOOT_ENV_HAS_BOOT_MODULES) != 0u &&
        (environment->boot_modules.modules == 0 || environment->boot_modules.module_count == 0u)) {
        return 0;
    }
    return 1;
}

int ribon_firmware_adapter_supports_arch(
    const struct RibonFirmwareAdapter *adapter,
    const struct RibonArchDescriptor *arch) {
    if (adapter == 0 || arch == 0) {
        return 0;
    }
    return ribon_arch_has_firmware_mask(arch, ribon_firmware_arch_mask(adapter->firmware));
}
