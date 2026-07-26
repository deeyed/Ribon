#include <Ribon/firmware.h>

static const struct RibonMemoryRegion host_memory_map[] = {
    {
        .base = 0x0000000040000000ull,
        .length = 0x0000000000100000ull,
        .kind = RIBON_MEMORY_REGION_RESERVED,
        .attributes = RIBON_MEMORY_ATTR_READ | RIBON_MEMORY_ATTR_WRITE,
    },
    {
        .base = 0x0000000000200000ull,
        .length = 0x0000000000E00000ull,
        .kind = RIBON_MEMORY_REGION_USABLE,
        .attributes = RIBON_MEMORY_ATTR_READ | RIBON_MEMORY_ATTR_WRITE,
    },
    {
        .base = 0x0000000001100000ull,
        .length = 0x000000001EF00000ull,
        .kind = RIBON_MEMORY_REGION_USABLE,
        .attributes = RIBON_MEMORY_ATTR_READ | RIBON_MEMORY_ATTR_WRITE,
    },
    {
        .base = 0x0000000000100000ull,
        .length = 0x0000000000100000ull,
        .kind = RIBON_MEMORY_REGION_FIRMWARE,
        .attributes = RIBON_MEMORY_ATTR_READ,
    },
    {
        .base = 0x0000000001000000ull,
        .length = 0x0000000000100000ull,
        .kind = RIBON_MEMORY_REGION_BOOT_MODULE,
        .attributes = RIBON_MEMORY_ATTR_READ,
    },
    {
        .base = 0x0000000020000000ull,
        .length = 0x0000000020000000ull,
        .kind = RIBON_MEMORY_REGION_USABLE,
        .attributes = RIBON_MEMORY_ATTR_READ | RIBON_MEMORY_ATTR_WRITE,
    },
};

static const struct RibonBootModule host_boot_modules[] = {
    {
        .name = "host-initrd",
        .physical_address = 0x0000000001000000ull,
        .size = 0x0000000000100000ull,
        .flags = 0,
    },
};

static int host_probe(const struct RibonArchDescriptor *arch) {
    return arch != 0;
}

static int host_collect(const struct RibonArchDescriptor *arch, struct RibonBootEnvironment *out) {
    if (arch == 0 || out == 0) {
        return RIBON_FIRMWARE_STATUS_BAD_ARGUMENT;
    }
    ribon_boot_environment_init(out, RIBON_FIRMWARE_HOST, arch);
    out->memory_map.regions = host_memory_map;
    out->memory_map.region_count = (uint32_t)(sizeof(host_memory_map) / sizeof(host_memory_map[0]));
    out->boot_media.kind = RIBON_BOOT_MEDIA_FILE;
    out->boot_media.path = "kernel/kernel.elf";
    out->boot_modules.modules = host_boot_modules;
    out->boot_modules.module_count = (uint32_t)(sizeof(host_boot_modules) / sizeof(host_boot_modules[0]));
    out->command_line.text = "profile=parus firmware=host";
    out->command_line.length = 28u;
    out->flags =
        RIBON_BOOT_ENV_HAS_MEMORY_MAP |
        RIBON_BOOT_ENV_HAS_BOOT_MEDIA |
        RIBON_BOOT_ENV_HAS_BOOT_MODULES |
        RIBON_BOOT_ENV_HAS_COMMAND_LINE;
    return RIBON_FIRMWARE_STATUS_OK;
}

static const struct RibonFirmwareAdapter host_adapter = {
    .firmware = RIBON_FIRMWARE_HOST,
    .name = "host",
    .probe = host_probe,
    .collect = host_collect,
};

const struct RibonFirmwareAdapter *ribon_firmware_host_adapter(void) {
    return &host_adapter;
}
