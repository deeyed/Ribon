#include "bios_client.h"

#define RIBON_BIOS_E820_USABLE 1u
#define RIBON_BIOS_E820_ACPI_RECLAIM 3u
#define RIBON_BIOS_E820_ACPI_NVS 4u
#define RIBON_BIOS_E820_MAX_ENTRIES 256u

static struct RibonBiosClientContext *bios_context;
static struct RibonServiceTable bios_services;
static int bios_services_initialized;

/** @brief BIOS E820 type을 generic ownership kind로 변환한다. */
static enum RibonMemoryRegionKind bios_memory_kind(uint32_t type) {
    switch (type) {
    case RIBON_BIOS_E820_USABLE:
        return RIBON_MEMORY_REGION_USABLE;
    case RIBON_BIOS_E820_ACPI_RECLAIM:
    case RIBON_BIOS_E820_ACPI_NVS:
        return RIBON_MEMORY_REGION_ACPI;
    default:
        return RIBON_MEMORY_REGION_RESERVED;
    }
}

/** @brief EDD sector service를 bounded Boot Source read로 변환한다. */
static int bios_boot_source_read(
    void *opaque,
    const struct RibonBootSource *source,
    uint64_t offset,
    void *buffer,
    uint64_t size,
    uint64_t deadline_ticks) {
    const struct RibonBiosClientContext *context =
        (const struct RibonBiosClientContext *)opaque;
    uint64_t lba;
    uint64_t sectors;
    (void)deadline_ticks;
    if (context == 0 || context->native == 0 ||
        context->native->edd_read == 0 || source == 0 ||
        source->kind != RIBON_BOOT_MEDIA_BLOCK || buffer == 0 ||
        size == 0u || context->block_size == 0u ||
        offset % context->block_size != 0u ||
        size % context->block_size != 0u ||
        offset > source->size || size > source->size - offset) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    lba = offset / context->block_size;
    sectors = size / context->block_size;
    if (sectors > UINT32_MAX ||
        context->native->edd_read(
            context->native_context,
            lba,
            buffer,
            (uint32_t)sectors) != 0) {
        return RIBON_SERVICE_STATUS_IO;
    }
    return RIBON_SERVICE_STATUS_OK;
}

/** @brief Native preboot counter를 firmware-neutral timer callback으로 변환한다. */
static int bios_timer_now(void *opaque, uint64_t *ticks_out) {
    const struct RibonBiosClientContext *context =
        (const struct RibonBiosClientContext *)opaque;
    if (context == 0 || context->native == 0 ||
        context->native->monotonic_counter == 0 || ticks_out == 0) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    *ticks_out = context->native->monotonic_counter(context->native_context);
    return RIBON_SERVICE_STATUS_OK;
}

/** @brief E820과 EDD boundary를 firmware-neutral environment로 capture한다. */
int ribon_bios_client_capture(
    struct RibonBiosClientContext *context,
    struct RibonBootEnvironment *out) {
    uint32_t continuation = 0u;
    uint32_t count = 0u;
    if (context == 0 || out == 0 || context->native == 0 ||
        context->native->size != sizeof(*context->native) ||
        context->native->e820_next == 0 ||
        context->native->edd_read == 0 ||
        context->native->monotonic_counter == 0 ||
        context->regions == 0 || context->region_capacity == 0u ||
        context->block_size == 0u || context->media_sectors == 0u) {
        return RIBON_BIOS_CLIENT_STATUS_BAD_ARGUMENT;
    }
    for (uint32_t attempt = 0u; attempt < RIBON_BIOS_E820_MAX_ENTRIES; ++attempt) {
        struct RibonBiosE820Entry entry;
        const int status = context->native->e820_next(
            context->native_context,
            &continuation,
            &entry);
        if (status == 1) {
            break;
        }
        if (status != 0 || entry.length == 0u ||
            entry.base > UINT64_MAX - entry.length) {
            return RIBON_BIOS_CLIENT_STATUS_NATIVE_ERROR;
        }
        if (count >= context->region_capacity) {
            return RIBON_BIOS_CLIENT_STATUS_OUT_OF_CAPACITY;
        }
        context->regions[count++] = (struct RibonMemoryRegion){
            .base = entry.base,
            .length = entry.length,
            .kind = bios_memory_kind(entry.type),
            .attributes =
                RIBON_MEMORY_ATTR_READ |
                (entry.type == RIBON_BIOS_E820_USABLE ?
                     RIBON_MEMORY_ATTR_WRITE :
                     0u),
        };
        if (continuation == 0u) {
            break;
        }
    }
    if (count == 0u ||
        context->media_sectors > UINT64_MAX / context->block_size) {
        return RIBON_BIOS_CLIENT_STATUS_BAD_MEMORY_MAP;
    }
    context->region_count = count;
    ribon_boot_environment_init(
        out,
        RIBON_ENVIRONMENT_BIOS,
        RIBON_ARCHITECTURE_X86_64);
    out->memory_map.regions = context->regions;
    out->memory_map.region_count = count;
    out->boot_media.kind = RIBON_BOOT_MEDIA_BLOCK;
    out->boot_media.path = "bios-edd";
    out->boot_media.size = context->media_sectors * context->block_size;
    out->boot_media.block_size = context->block_size;
    out->flags =
        RIBON_BOOT_ENV_HAS_MEMORY_MAP |
        RIBON_BOOT_ENV_HAS_BOOT_MEDIA;
    bios_context = context;
    ribon_service_table_init_unsupported(&bios_services, context);
    bios_services.capabilities =
        RIBON_CAP_BOOT_SOURCE_READ |
        RIBON_CAP_MONOTONIC_TIMER;
    bios_services.timer_frequency_hz = 18u;
    bios_services.boot_source_read = bios_boot_source_read;
    bios_services.timer_now = bios_timer_now;
    bios_services_initialized = 1;
    return RIBON_BIOS_CLIENT_STATUS_OK;
}

/** @brief BIOS protected/long-mode transfer precondition을 fail-closed로 검사한다. */
int ribon_bios_long_mode_contract_is_valid(
    const struct RibonBiosLongModeContract *contract) {
    return contract != 0 &&
           contract->size == sizeof(*contract) &&
           contract->a20_enabled == 1u &&
           contract->long_mode_supported == 1u &&
           contract->interrupts_masked == 1u &&
           contract->page_table_base != 0u &&
           (contract->page_table_base & 0xfffu) == 0u &&
           contract->entry_point != 0u;
}

/** @brief 초기화된 BIOS client service table을 반환한다. */
const struct RibonServiceTable *ribon_bios_client_services(void) {
    return bios_services_initialized && bios_context != 0 ?
        &bios_services :
        0;
}

/** @brief BIOS environment descriptor와 live service table을 검증한다. */
static int bios_environment_validate(
    const struct RibonPluginDescriptor *descriptor) {
    return bios_services_initialized &&
           descriptor != 0 &&
           descriptor->operations == &bios_services &&
           ribon_environment_plugin_operations_are_valid(descriptor);
}

/** @brief BIOS client environment consumer plugin descriptor다. */
const struct RibonPluginDescriptor ribon_bios_client_environment_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_bios_client_environment_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_ENVIRONMENT,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION,
    .id = "environment.bios-client",
    .provides =
        RIBON_CAP_BOOT_SOURCE_READ |
        RIBON_CAP_MONOTONIC_TIMER,
    .requires =
        RIBON_CAP_ARCHITECTURE |
        RIBON_CAP_PLATFORM_FACTS,
    .architecture_mask = RIBON_ARCH_MASK_X86_64,
    .environment_mask = RIBON_ENV_MASK_BIOS,
    .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 8192u,
    .input_budget = 64ull * 1024ull * 1024ull,
    .output_budget = 65536u,
    .deadline_ms = 30000u,
    .operations = &bios_services,
    .operations_size = sizeof(bios_services),
    .operations_abi = RIBON_SERVICE_TABLE_ABI_VERSION,
    .validate_operations = bios_environment_validate,
};
