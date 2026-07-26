#include "host.h"

#include <Ribon/plugin/descriptor.h>

#include <string.h>

static const struct RibonMemoryRegion host_memory_map[] = {
    {
        .base = 0x0000000040000000ull,
        .length = 0x0000000000100000ull,
        .kind = RIBON_MEMORY_REGION_RESERVED,
        .attributes = RIBON_MEMORY_ATTR_READ | RIBON_MEMORY_ATTR_WRITE,
    },
    {
        .base = 0x0000000000200000ull,
        .length = 0x0000000000e00000ull,
        .kind = RIBON_MEMORY_REGION_USABLE,
        .attributes = RIBON_MEMORY_ATTR_READ | RIBON_MEMORY_ATTR_WRITE,
    },
    {
        .base = 0x0000000001100000ull,
        .length = 0x000000001ef00000ull,
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
    },
};

static int host_boot_source_read(
    void *context,
    const struct RibonBootSource *source,
    uint64_t offset,
    void *buffer,
    uint64_t size,
    uint64_t deadline_ticks) {
    (void)context;
    (void)deadline_ticks;
    if (source == 0 ||
        buffer == 0 ||
        size == 0u ||
        offset > source->size ||
        size > source->size - offset) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    memset(buffer, 0, (size_t)size);
    return RIBON_SERVICE_STATUS_OK;
}

static int host_timer_now(void *context, uint64_t *ticks_out) {
    (void)context;
    if (ticks_out == 0) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    *ticks_out = 1u;
    return RIBON_SERVICE_STATUS_OK;
}

static struct RibonServiceTable host_services;
static int host_services_initialized;

/** @brief Host reference product의 immutable service table을 반환한다. */
const struct RibonServiceTable *ribon_host_services(void) {
    if (!host_services_initialized) {
        ribon_service_table_init_unsupported(&host_services, 0);
        host_services.capabilities =
            RIBON_CAP_BOOT_SOURCE_READ |
            RIBON_CAP_MONOTONIC_TIMER;
        host_services.timer_frequency_hz = 1000000u;
        host_services.boot_source_read = host_boot_source_read;
        host_services.timer_now = host_timer_now;
        host_services_initialized = 1;
    }
    return &host_services;
}

/** @brief Host reference product의 deterministic environment fixture를 수집한다. */
int ribon_host_environment_collect(
    enum RibonArchitectureId architecture,
    struct RibonBootEnvironment *out) {
    if (out == 0) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    ribon_boot_environment_init(out, RIBON_ENVIRONMENT_HOST, architecture);
    out->memory_map.regions = host_memory_map;
    out->memory_map.region_count =
        (uint32_t)(sizeof(host_memory_map) / sizeof(host_memory_map[0]));
    out->boot_media.kind = RIBON_BOOT_MEDIA_FILE;
    out->boot_media.path = "kernel/kernel.elf";
    out->boot_modules.modules = host_boot_modules;
    out->boot_modules.module_count =
        (uint32_t)(sizeof(host_boot_modules) / sizeof(host_boot_modules[0]));
    out->command_line.text = "protocol=synthetic environment=host";
    out->command_line.length = 35u;
    out->flags =
        RIBON_BOOT_ENV_HAS_MEMORY_MAP |
        RIBON_BOOT_ENV_HAS_BOOT_MEDIA |
        RIBON_BOOT_ENV_HAS_BOOT_MODULES |
        RIBON_BOOT_ENV_HAS_COMMAND_LINE;
    return RIBON_SERVICE_STATUS_OK;
}

static int host_environment_validate(
    const struct RibonPluginDescriptor *descriptor) {
    const struct RibonServiceTable *services = ribon_host_services();
    if (descriptor == 0 || descriptor->operations != services) {
        return 0;
    }
    return ribon_environment_plugin_operations_are_valid(descriptor);
}

/** @brief Host environment consumer plugin descriptor다. */
const struct RibonPluginDescriptor ribon_host_environment_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_host_environment_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_ENVIRONMENT,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION,
    .id = "environment.host",
    .provides =
        RIBON_CAP_BOOT_SOURCE_READ |
        RIBON_CAP_MONOTONIC_TIMER,
    .requires =
        RIBON_CAP_ARCHITECTURE |
        RIBON_CAP_PLATFORM_FACTS,
    .architecture_mask = RIBON_ARCH_MASK_ALL,
    .environment_mask = RIBON_ENV_MASK_HOST,
    .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 4096u,
    .input_budget = 64ull * 1024ull * 1024ull,
    .output_budget = 4096u,
    .deadline_ms = 30000u,
    .operations = &host_services,
    .operations_size = sizeof(host_services),
    .operations_abi = RIBON_SERVICE_TABLE_ABI_VERSION,
    .validate_operations = host_environment_validate,
};
