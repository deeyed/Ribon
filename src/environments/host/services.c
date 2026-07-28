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

struct HostLifecycleFixture {
    const unsigned char *source;
    uint64_t source_size;
    unsigned char metadata[64];
    uint64_t metadata_size;
    uint32_t writes;
    uint32_t flushes;
    uint32_t quiesces;
    uint32_t source_failures;
    uint32_t metadata_failures;
    uint32_t flush_failures;
    uint32_t quiesce_failures;
    uint64_t timer_ticks;
    uint64_t timer_step;
};

static struct HostLifecycleFixture host_lifecycle_fixture;

static int host_boot_source_read(
    void *context,
    const struct RibonBootSource *source,
    uint64_t offset,
    void *buffer,
    uint64_t size,
    uint64_t deadline_ticks) {
    (void)deadline_ticks;
    if (context != &host_lifecycle_fixture || source == 0 ||
        source->kind != RIBON_BOOT_MEDIA_MEMORY ||
        buffer == 0 ||
        size == 0u ||
        source->size != host_lifecycle_fixture.source_size ||
        host_lifecycle_fixture.source == 0 || offset > source->size ||
        size > source->size - offset) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    if (host_lifecycle_fixture.source_failures != 0u) {
        --host_lifecycle_fixture.source_failures;
        return RIBON_SERVICE_STATUS_IO;
    }
    for (uint64_t index = 0u; index < size; ++index) {
        ((unsigned char *)buffer)[index] = host_lifecycle_fixture.source[offset + index];
    }
    return RIBON_SERVICE_STATUS_OK;
}

/** @brief Host fixture의 deterministic monotonic tick을 반환한다. */
static int host_timer_now(void *context, uint64_t *ticks_out) {
    if (context != &host_lifecycle_fixture || ticks_out == 0) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    *ticks_out = host_lifecycle_fixture.timer_ticks;
    host_lifecycle_fixture.timer_ticks += host_lifecycle_fixture.timer_step;
    return RIBON_SERVICE_STATUS_OK;
}

static struct RibonBootSourceServiceOperations host_boot_source_operations = {
    .size = sizeof(host_boot_source_operations),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .context = &host_lifecycle_fixture,
    .read = host_boot_source_read,
};

static const struct RibonMonotonicTimerServiceOperations host_timer_operations = {
    .size = sizeof(host_timer_operations),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .context = &host_lifecycle_fixture,
    .frequency_hz = 1000000u,
    .now = host_timer_now,
};

/** @brief Host fixture의 bounded attempt metadata를 읽는다. */
static int host_metadata_read(void *context, uint64_t offset, void *buffer, uint64_t size) {
    if (context != &host_lifecycle_fixture || buffer == 0 || offset > host_lifecycle_fixture.metadata_size ||
        size > host_lifecycle_fixture.metadata_size - offset) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    for (uint64_t index = 0u; index < size; ++index) {
        ((unsigned char *)buffer)[index] = host_lifecycle_fixture.metadata[offset + index];
    }
    return RIBON_SERVICE_STATUS_OK;
}

/** @brief Host fixture의 bounded attempt metadata를 durable write로 기록한다. */
static int host_metadata_write(void *context, uint64_t offset, const void *buffer, uint64_t size) {
    if (context != &host_lifecycle_fixture || buffer == 0 || offset > sizeof(host_lifecycle_fixture.metadata) ||
        size > sizeof(host_lifecycle_fixture.metadata) - offset) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    if (host_lifecycle_fixture.metadata_failures != 0u) {
        const uint64_t partial_size = size / 2u;
        --host_lifecycle_fixture.metadata_failures;
        for (uint64_t index = 0u; index < partial_size; ++index) {
            host_lifecycle_fixture.metadata[offset + index] =
                ((const unsigned char *)buffer)[index];
        }
        if (offset + partial_size > host_lifecycle_fixture.metadata_size) {
            host_lifecycle_fixture.metadata_size = offset + partial_size;
        }
        return RIBON_SERVICE_STATUS_IO;
    }
    for (uint64_t index = 0u; index < size; ++index) {
        host_lifecycle_fixture.metadata[offset + index] = ((const unsigned char *)buffer)[index];
    }
    if (offset + size > host_lifecycle_fixture.metadata_size) {
        host_lifecycle_fixture.metadata_size = offset + size;
    }
    ++host_lifecycle_fixture.writes;
    return RIBON_SERVICE_STATUS_OK;
}

/** @brief Host fixture의 flush barrier 호출을 결정론적으로 기록한다. */
static int host_metadata_flush(void *context, uint32_t slot, uint64_t deadline_ticks) {
    (void)slot;
    (void)deadline_ticks;
    if (context != &host_lifecycle_fixture) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    if (host_lifecycle_fixture.flush_failures != 0u) {
        --host_lifecycle_fixture.flush_failures;
        return RIBON_SERVICE_STATUS_IO;
    }
    ++host_lifecycle_fixture.flushes;
    return RIBON_SERVICE_STATUS_OK;
}

/** @brief Host environment lifetime closure를 결정론적으로 기록한다. */
static int host_environment_quiesce(void *context) {
    if (context != &host_lifecycle_fixture) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    if (host_lifecycle_fixture.quiesce_failures != 0u) {
        --host_lifecycle_fixture.quiesce_failures;
        return RIBON_SERVICE_STATUS_IO;
    }
    ++host_lifecycle_fixture.quiesces;
    return RIBON_SERVICE_STATUS_OK;
}

static const struct RibonPersistentMetadataServiceOperations host_metadata_operations = {
    .size = sizeof(host_metadata_operations),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .context = &host_lifecycle_fixture,
    .read = host_metadata_read,
    .write = host_metadata_write,
};

static const struct RibonStorageFlushServiceOperations host_flush_operations = {
    .size = sizeof(host_flush_operations),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .context = &host_lifecycle_fixture,
    .flush = host_metadata_flush,
};

static const struct RibonEnvironmentQuiesceServiceOperations host_quiesce_operations = {
    .size = sizeof(host_quiesce_operations),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .context = &host_lifecycle_fixture,
    .quiesce = host_environment_quiesce,
};

/** @brief Host boot-source descriptor와 immutable operation table을 함께 검사한다. */
static int host_boot_source_validate(
    const struct RibonServiceDescriptor *descriptor) {
    const struct RibonBootSourceServiceOperations *operations;
    if (descriptor == 0 || descriptor->operations != &host_boot_source_operations ||
        descriptor->operations_size != sizeof(*operations) ||
        descriptor->operations_abi != RIBON_SERVICE_ABI_VERSION) {
        return 0;
    }
    operations = descriptor->operations;
    return operations->size == sizeof(*operations) &&
           operations->abi_version == RIBON_SERVICE_ABI_VERSION &&
           operations->context == &host_lifecycle_fixture &&
           operations->read == host_boot_source_read;
}

/** @brief Host monotonic-timer descriptor와 immutable operation table을 함께 검사한다. */
static int host_timer_validate(const struct RibonServiceDescriptor *descriptor) {
    const struct RibonMonotonicTimerServiceOperations *operations;
    if (descriptor == 0 || descriptor->operations != &host_timer_operations ||
        descriptor->operations_size != sizeof(*operations) ||
        descriptor->operations_abi != RIBON_SERVICE_ABI_VERSION) {
        return 0;
    }
    operations = descriptor->operations;
    return operations->size == sizeof(*operations) &&
           operations->abi_version == RIBON_SERVICE_ABI_VERSION &&
           operations->context == &host_lifecycle_fixture &&
           operations->frequency_hz == 1000000u &&
           operations->now == host_timer_now;
}

/** @brief Host metadata operation table의 durable write contract를 검사한다. */
static int host_metadata_validate(const struct RibonServiceDescriptor *descriptor) {
    const struct RibonPersistentMetadataServiceOperations *operations = descriptor == 0 ? 0 : descriptor->operations;
    return operations == &host_metadata_operations && descriptor->operations_size == sizeof(*operations) &&
           descriptor->operations_abi == RIBON_SERVICE_ABI_VERSION && operations->read == host_metadata_read &&
           operations->write == host_metadata_write;
}

/** @brief Host metadata flush operation table의 callback contract를 검사한다. */
static int host_flush_validate(const struct RibonServiceDescriptor *descriptor) {
    const struct RibonStorageFlushServiceOperations *operations = descriptor == 0 ? 0 : descriptor->operations;
    return operations == &host_flush_operations && descriptor->operations_size == sizeof(*operations) &&
           descriptor->operations_abi == RIBON_SERVICE_ABI_VERSION && operations->flush == host_metadata_flush;
}

/** @brief Host environment closure operation table의 callback contract를 검사한다. */
static int host_quiesce_validate(const struct RibonServiceDescriptor *descriptor) {
    const struct RibonEnvironmentQuiesceServiceOperations *operations = descriptor == 0 ? 0 : descriptor->operations;
    return operations == &host_quiesce_operations && descriptor->operations_size == sizeof(*operations) &&
           descriptor->operations_abi == RIBON_SERVICE_ABI_VERSION && operations->quiesce == host_environment_quiesce;
}

/** @brief Host fixture가 제공하는 typed boot-source authority다. */
const struct RibonServiceDescriptor ribon_host_boot_source_service_descriptor = {
    .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_host_boot_source_service_descriptor),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .kind = RIBON_SERVICE_KIND_BOOT_SOURCE,
    .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY,
    .lifetime = RIBON_SERVICE_LIFETIME_BOOT,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION,
    .id = "service.host.boot-source",
    .provides = RIBON_CAP_BOOT_SOURCE_READ,
    .architecture_mask = RIBON_ARCH_MASK_ALL,
    .environment_mask = RIBON_ENV_MASK_HOST,
    .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 2048u,
    .input_budget = 64ull * 1024ull * 1024ull,
    .output_budget = 4096u,
    .deadline_ms = 30000u,
    .operations = &host_boot_source_operations,
    .operations_size = sizeof(host_boot_source_operations),
    .operations_abi = RIBON_SERVICE_ABI_VERSION,
    .validate_operations = host_boot_source_validate,
};

/** @brief Host fixture가 제공하는 typed monotonic-timer authority다. */
const struct RibonServiceDescriptor ribon_host_monotonic_timer_service_descriptor = {
    .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_host_monotonic_timer_service_descriptor),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .kind = RIBON_SERVICE_KIND_MONOTONIC_TIMER,
    .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY,
    .lifetime = RIBON_SERVICE_LIFETIME_BOOT,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION,
    .id = "service.host.monotonic-timer",
    .provides = RIBON_CAP_MONOTONIC_TIMER,
    .architecture_mask = RIBON_ARCH_MASK_ALL,
    .environment_mask = RIBON_ENV_MASK_HOST,
    .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 2048u,
    .input_budget = 4096u,
    .output_budget = 4096u,
    .deadline_ms = 30000u,
    .operations = &host_timer_operations,
    .operations_size = sizeof(host_timer_operations),
    .operations_abi = RIBON_SERVICE_ABI_VERSION,
    .validate_operations = host_timer_validate,
};

/** @brief Host fixture가 제공하는 durable attempt metadata authority다. */
const struct RibonServiceDescriptor ribon_host_persistent_metadata_service_descriptor = {
    .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC, .size = sizeof(ribon_host_persistent_metadata_service_descriptor),
    .abi_version = RIBON_SERVICE_ABI_VERSION, .kind = RIBON_SERVICE_KIND_PERSISTENT_METADATA,
    .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY, .lifetime = RIBON_SERVICE_LIFETIME_BOOT,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION, .id = "service.host.persistent-metadata",
    .provides = RIBON_CAP_PERSISTENT_METADATA, .architecture_mask = RIBON_ARCH_MASK_ALL,
    .environment_mask = RIBON_ENV_MASK_HOST, .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 1024u, .input_budget = 64u, .output_budget = 64u, .deadline_ms = 30000u,
    .operations = &host_metadata_operations, .operations_size = sizeof(host_metadata_operations),
    .operations_abi = RIBON_SERVICE_ABI_VERSION, .validate_operations = host_metadata_validate,
};

/** @brief Host fixture가 제공하는 durable metadata flush authority다. */
const struct RibonServiceDescriptor ribon_host_storage_flush_service_descriptor = {
    .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC, .size = sizeof(ribon_host_storage_flush_service_descriptor),
    .abi_version = RIBON_SERVICE_ABI_VERSION, .kind = RIBON_SERVICE_KIND_STORAGE_FLUSH,
    .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY, .lifetime = RIBON_SERVICE_LIFETIME_BOOT,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION, .id = "service.host.storage-flush",
    .provides = RIBON_CAP_STORAGE_FLUSH, .architecture_mask = RIBON_ARCH_MASK_ALL,
    .environment_mask = RIBON_ENV_MASK_HOST, .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 1024u, .input_budget = 64u, .output_budget = 64u, .deadline_ms = 30000u,
    .operations = &host_flush_operations, .operations_size = sizeof(host_flush_operations),
    .operations_abi = RIBON_SERVICE_ABI_VERSION, .validate_operations = host_flush_validate,
};

/** @brief Host fixture가 제공하는 environment lifetime closure authority다. */
const struct RibonServiceDescriptor ribon_host_environment_quiesce_service_descriptor = {
    .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC, .size = sizeof(ribon_host_environment_quiesce_service_descriptor),
    .abi_version = RIBON_SERVICE_ABI_VERSION, .kind = RIBON_SERVICE_KIND_ENVIRONMENT_QUIESCE,
    .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY, .lifetime = RIBON_SERVICE_LIFETIME_QUIESCE,
    .phase = RIBON_PLUGIN_PHASE_QUIESCE, .id = "service.host.environment-quiesce",
    .provides = RIBON_CAP_ENVIRONMENT_QUIESCE, .architecture_mask = RIBON_ARCH_MASK_ALL,
    .environment_mask = RIBON_ENV_MASK_HOST, .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 1024u, .input_budget = 64u, .output_budget = 64u, .deadline_ms = 30000u,
    .operations = &host_quiesce_operations, .operations_size = sizeof(host_quiesce_operations),
    .operations_abi = RIBON_SERVICE_ABI_VERSION, .validate_operations = host_quiesce_validate,
};

static const struct RibonServiceDescriptor *const host_services[] = {
    &ribon_host_boot_source_service_descriptor,
    &ribon_host_environment_quiesce_service_descriptor,
    &ribon_host_monotonic_timer_service_descriptor,
    &ribon_host_persistent_metadata_service_descriptor,
    &ribon_host_storage_flush_service_descriptor,
};

static const struct RibonServiceDirectory host_service_directory = {
    .size = sizeof(host_service_directory),
    .abi_version = RIBON_SERVICE_DIRECTORY_ABI_VERSION,
    .services = host_services,
    .service_count = (uint32_t)(sizeof(host_services) / sizeof(host_services[0])),
};

/** @brief Host reference product의 immutable typed service directory를 반환한다. */
const struct RibonServiceDirectory *ribon_host_service_directory(void) {
    return &host_service_directory;
}

/** @brief Host in-memory source를 다음 bounded boot transaction에 결합한다. */
int ribon_host_boot_source_bind(const void *data, uint64_t size) {
    if (data == 0 || size == 0u) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    host_lifecycle_fixture.source = data;
    host_lifecycle_fixture.source_size = size;
    return RIBON_SERVICE_STATUS_OK;
}

/** @brief Host durable journal fixture의 observation을 초기화한다. */
void ribon_host_lifecycle_fixture_reset(void) {
    const unsigned char *source = host_lifecycle_fixture.source;
    const uint64_t source_size = host_lifecycle_fixture.source_size;
    host_lifecycle_fixture = (struct HostLifecycleFixture){
        .source = source,
        .source_size = source_size,
        .timer_ticks = 1u,
    };
}

/** @brief Host durable journal fixture가 기록한 write 수를 반환한다. */
uint32_t ribon_host_lifecycle_fixture_write_count(void) { return host_lifecycle_fixture.writes; }

/** @brief Host durable journal fixture가 기록한 flush 수를 반환한다. */
uint32_t ribon_host_lifecycle_fixture_flush_count(void) { return host_lifecycle_fixture.flushes; }

/** @brief Host durable journal fixture가 기록한 quiesce 수를 반환한다. */
uint32_t ribon_host_lifecycle_fixture_quiesce_count(void) { return host_lifecycle_fixture.quiesces; }

/** @brief Host fixture에 남은 partial 또는 committed metadata byte 수를 반환한다. */
uint64_t ribon_host_lifecycle_fixture_metadata_size(void) {
    return host_lifecycle_fixture.metadata_size;
}

/** @brief Host lifecycle fixture에 deterministic provider failure count를 설정한다. */
void ribon_host_lifecycle_fixture_set_failures(
    uint32_t source_reads,
    uint32_t metadata_writes,
    uint32_t flushes,
    uint32_t quiesces) {
    host_lifecycle_fixture.source_failures = source_reads;
    host_lifecycle_fixture.metadata_failures = metadata_writes;
    host_lifecycle_fixture.flush_failures = flushes;
    host_lifecycle_fixture.quiesce_failures = quiesces;
}

/** @brief Host lifecycle fixture timer가 operation 사이에 소비할 tick을 설정한다. */
void ribon_host_lifecycle_fixture_set_timer_step(uint64_t step) {
    host_lifecycle_fixture.timer_step = step;
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
    if (descriptor == 0 || descriptor->operations != &host_service_directory) {
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
        RIBON_CAP_MONOTONIC_TIMER |
        RIBON_CAP_PERSISTENT_METADATA |
        RIBON_CAP_STORAGE_FLUSH |
        RIBON_CAP_ENVIRONMENT_QUIESCE,
    .requires = RIBON_CAP_ARCHITECTURE,
    .architecture_mask = RIBON_ARCH_MASK_ALL,
    .environment_mask = RIBON_ENV_MASK_HOST,
    .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 4096u,
    .input_budget = 64ull * 1024ull * 1024ull,
    .output_budget = 4096u,
    .deadline_ms = 30000u,
    .operations = &host_service_directory,
    .operations_size = sizeof(host_service_directory),
    .operations_abi = RIBON_SERVICE_DIRECTORY_ABI_VERSION,
    .validate_operations = host_environment_validate,
};
