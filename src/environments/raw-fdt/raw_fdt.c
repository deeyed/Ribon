#include "raw_fdt.h"

#include "../../common/sys/fdt/fdt.h"

static struct RibonRawFdtEntry *raw_fdt_entry;
static int raw_fdt_services_initialized;
static unsigned char raw_fdt_attempt_metadata[64];
static uint64_t raw_fdt_attempt_metadata_size;

/** @brief Memory boot source에서 bounded byte range를 복사한다. */
static int raw_fdt_boot_source_read(
    void *context,
    const struct RibonBootSource *source,
    uint64_t offset,
    void *buffer,
    uint64_t size,
    uint64_t deadline_ticks) {
    const struct RibonRawFdtEntry *entry =
        (const struct RibonRawFdtEntry *)context;
    const unsigned char *input;
    unsigned char *output;
    (void)deadline_ticks;
    if (entry == 0 || source == 0 || source->kind != RIBON_BOOT_MEDIA_MEMORY ||
        buffer == 0 || size == 0u || source->size != entry->payload_size ||
        offset > entry->payload_size || size > entry->payload_size - offset) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    input = (const unsigned char *)entry->payload + offset;
    output = (unsigned char *)buffer;
    if (output == input) {
        return RIBON_SERVICE_STATUS_OK;
    }
    for (uint64_t index = 0u; index < size; ++index) {
        output[index] = input[index];
    }
    return RIBON_SERVICE_STATUS_OK;
}

/** @brief Architecture backend의 monotonic counter를 service ABI로 변환한다. */
static int raw_fdt_timer_now(void *context, uint64_t *ticks_out) {
    const struct RibonRawFdtEntry *entry =
        (const struct RibonRawFdtEntry *)context;
    if (entry == 0 || ticks_out == 0 || entry->arch_ops == 0 ||
        entry->arch_ops->monotonic_counter == 0) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    *ticks_out = entry->arch_ops->monotonic_counter();
    return RIBON_SERVICE_STATUS_OK;
}

static struct RibonBootSourceServiceOperations raw_fdt_boot_source_operations = {
    .size = sizeof(raw_fdt_boot_source_operations),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .read = raw_fdt_boot_source_read,
};

static struct RibonMonotonicTimerServiceOperations raw_fdt_timer_operations = {
    .size = sizeof(raw_fdt_timer_operations),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .now = raw_fdt_timer_now,
};

/** @brief raw-FDT target-owned attempt metadata를 bounded range에서 읽는다. */
static int raw_fdt_metadata_read(void *context, uint64_t offset, void *buffer, uint64_t size) {
    if (context != raw_fdt_entry || buffer == 0 || offset > raw_fdt_attempt_metadata_size ||
        size > raw_fdt_attempt_metadata_size - offset) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    for (uint64_t index = 0u; index < size; ++index) {
        ((unsigned char *)buffer)[index] = raw_fdt_attempt_metadata[offset + index];
    }
    return RIBON_SERVICE_STATUS_OK;
}

/** @brief raw-FDT target-owned attempt metadata에 bounded write를 기록한다. */
static int raw_fdt_metadata_write(void *context, uint64_t offset, const void *buffer, uint64_t size) {
    if (context != raw_fdt_entry || buffer == 0 || offset > sizeof(raw_fdt_attempt_metadata) ||
        size > sizeof(raw_fdt_attempt_metadata) - offset) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    for (uint64_t index = 0u; index < size; ++index) {
        raw_fdt_attempt_metadata[offset + index] = ((const unsigned char *)buffer)[index];
    }
    if (offset + size > raw_fdt_attempt_metadata_size) {
        raw_fdt_attempt_metadata_size = offset + size;
    }
    return RIBON_SERVICE_STATUS_OK;
}

/** @brief raw-FDT in-memory metadata write의 ordering barrier를 확정한다. */
static int raw_fdt_metadata_flush(void *context, uint32_t slot, uint64_t deadline_ticks) {
    (void)slot;
    (void)deadline_ticks;
    return context == raw_fdt_entry ? RIBON_SERVICE_STATUS_OK : RIBON_SERVICE_STATUS_BAD_ARGUMENT;
}

/** @brief raw-FDT environment가 종료 전 native callback 사용을 닫는다. */
static int raw_fdt_environment_quiesce(void *context) {
    return context == raw_fdt_entry ? RIBON_SERVICE_STATUS_OK : RIBON_SERVICE_STATUS_BAD_ARGUMENT;
}

static struct RibonPersistentMetadataServiceOperations raw_fdt_metadata_operations = {
    .size = sizeof(raw_fdt_metadata_operations), .abi_version = RIBON_SERVICE_ABI_VERSION,
    .read = raw_fdt_metadata_read, .write = raw_fdt_metadata_write,
};
static struct RibonStorageFlushServiceOperations raw_fdt_flush_operations = {
    .size = sizeof(raw_fdt_flush_operations), .abi_version = RIBON_SERVICE_ABI_VERSION,
    .flush = raw_fdt_metadata_flush,
};
static struct RibonEnvironmentQuiesceServiceOperations raw_fdt_quiesce_operations = {
    .size = sizeof(raw_fdt_quiesce_operations), .abi_version = RIBON_SERVICE_ABI_VERSION,
    .quiesce = raw_fdt_environment_quiesce,
};

static int raw_fdt_boot_source_validate(
    const struct RibonServiceDescriptor *descriptor) {
    const struct RibonBootSourceServiceOperations *operations;
    if (descriptor == 0 || descriptor->operations != &raw_fdt_boot_source_operations ||
        descriptor->operations_size != sizeof(*operations) ||
        descriptor->operations_abi != RIBON_SERVICE_ABI_VERSION) {
        return 0;
    }
    operations = descriptor->operations;
    return raw_fdt_services_initialized && operations->context == raw_fdt_entry &&
           operations->size == sizeof(*operations) &&
           operations->abi_version == RIBON_SERVICE_ABI_VERSION &&
           operations->read == raw_fdt_boot_source_read;
}

static int raw_fdt_timer_validate(
    const struct RibonServiceDescriptor *descriptor) {
    const struct RibonMonotonicTimerServiceOperations *operations;
    if (descriptor == 0 || descriptor->operations != &raw_fdt_timer_operations ||
        descriptor->operations_size != sizeof(*operations) ||
        descriptor->operations_abi != RIBON_SERVICE_ABI_VERSION) {
        return 0;
    }
    operations = descriptor->operations;
    return raw_fdt_services_initialized && operations->context == raw_fdt_entry &&
           operations->size == sizeof(*operations) &&
           operations->abi_version == RIBON_SERVICE_ABI_VERSION &&
           operations->frequency_hz == raw_fdt_entry->timer_frequency_hz &&
           operations->now == raw_fdt_timer_now;
}

/** @brief raw-FDT lifecycle metadata operation table가 live context를 쓰는지 검사한다. */
static int raw_fdt_metadata_validate(const struct RibonServiceDescriptor *descriptor) {
    const struct RibonPersistentMetadataServiceOperations *operations = descriptor == 0 ? 0 : descriptor->operations;
    return raw_fdt_services_initialized && operations == &raw_fdt_metadata_operations &&
           descriptor->operations_size == sizeof(*operations) && operations->context == raw_fdt_entry &&
           operations->read == raw_fdt_metadata_read && operations->write == raw_fdt_metadata_write;
}

/** @brief raw-FDT lifecycle flush operation table가 live context를 쓰는지 검사한다. */
static int raw_fdt_flush_validate(const struct RibonServiceDescriptor *descriptor) {
    const struct RibonStorageFlushServiceOperations *operations = descriptor == 0 ? 0 : descriptor->operations;
    return raw_fdt_services_initialized && operations == &raw_fdt_flush_operations &&
           descriptor->operations_size == sizeof(*operations) && operations->context == raw_fdt_entry &&
           operations->flush == raw_fdt_metadata_flush;
}

/** @brief raw-FDT lifecycle quiesce operation table가 live context를 쓰는지 검사한다. */
static int raw_fdt_quiesce_validate(const struct RibonServiceDescriptor *descriptor) {
    const struct RibonEnvironmentQuiesceServiceOperations *operations = descriptor == 0 ? 0 : descriptor->operations;
    return raw_fdt_services_initialized && operations == &raw_fdt_quiesce_operations &&
           descriptor->operations_size == sizeof(*operations) && operations->context == raw_fdt_entry &&
           operations->quiesce == raw_fdt_environment_quiesce;
}

/** @brief raw-FDT consumer가 제공하는 typed boot-source authority다. */
const struct RibonServiceDescriptor ribon_raw_fdt_boot_source_service_descriptor = {
    .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_raw_fdt_boot_source_service_descriptor),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .kind = RIBON_SERVICE_KIND_BOOT_SOURCE,
    .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY,
    .lifetime = RIBON_SERVICE_LIFETIME_BOOT,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION,
    .id = "service.raw-fdt.boot-source",
    .provides = RIBON_CAP_BOOT_SOURCE_READ,
    .architecture_mask = RIBON_ARCH_MASK_AARCH64 | RIBON_ARCH_MASK_RISCV64,
    .environment_mask = RIBON_ENV_MASK_RAW_FDT,
    .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 4096u,
    .input_budget = 64ull * 1024ull * 1024ull,
    .output_budget = 8192u,
    .deadline_ms = 30000u,
    .operations = &raw_fdt_boot_source_operations,
    .operations_size = sizeof(raw_fdt_boot_source_operations),
    .operations_abi = RIBON_SERVICE_ABI_VERSION,
    .validate_operations = raw_fdt_boot_source_validate,
};

/** @brief raw-FDT consumer가 제공하는 typed monotonic-timer authority다. */
const struct RibonServiceDescriptor ribon_raw_fdt_monotonic_timer_service_descriptor = {
    .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_raw_fdt_monotonic_timer_service_descriptor),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .kind = RIBON_SERVICE_KIND_MONOTONIC_TIMER,
    .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY,
    .lifetime = RIBON_SERVICE_LIFETIME_BOOT,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION,
    .id = "service.raw-fdt.monotonic-timer",
    .provides = RIBON_CAP_MONOTONIC_TIMER,
    .architecture_mask = RIBON_ARCH_MASK_AARCH64 | RIBON_ARCH_MASK_RISCV64,
    .environment_mask = RIBON_ENV_MASK_RAW_FDT,
    .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 4096u,
    .input_budget = 4096u,
    .output_budget = 8192u,
    .deadline_ms = 30000u,
    .operations = &raw_fdt_timer_operations,
    .operations_size = sizeof(raw_fdt_timer_operations),
    .operations_abi = RIBON_SERVICE_ABI_VERSION,
    .validate_operations = raw_fdt_timer_validate,
};

/** @brief raw-FDT consumer가 제공하는 durable attempt metadata authority다. */
const struct RibonServiceDescriptor ribon_raw_fdt_persistent_metadata_service_descriptor = {
    .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC, .size = sizeof(ribon_raw_fdt_persistent_metadata_service_descriptor),
    .abi_version = RIBON_SERVICE_ABI_VERSION, .kind = RIBON_SERVICE_KIND_PERSISTENT_METADATA,
    .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY, .lifetime = RIBON_SERVICE_LIFETIME_BOOT,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION, .id = "service.raw-fdt.persistent-metadata",
    .provides = RIBON_CAP_PERSISTENT_METADATA, .architecture_mask = RIBON_ARCH_MASK_AARCH64 | RIBON_ARCH_MASK_RISCV64,
    .environment_mask = RIBON_ENV_MASK_RAW_FDT, .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 1024u, .input_budget = 64u, .output_budget = 64u, .deadline_ms = 30000u,
    .operations = &raw_fdt_metadata_operations, .operations_size = sizeof(raw_fdt_metadata_operations),
    .operations_abi = RIBON_SERVICE_ABI_VERSION, .validate_operations = raw_fdt_metadata_validate,
};

/** @brief raw-FDT consumer가 제공하는 metadata flush authority다. */
const struct RibonServiceDescriptor ribon_raw_fdt_storage_flush_service_descriptor = {
    .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC, .size = sizeof(ribon_raw_fdt_storage_flush_service_descriptor),
    .abi_version = RIBON_SERVICE_ABI_VERSION, .kind = RIBON_SERVICE_KIND_STORAGE_FLUSH,
    .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY, .lifetime = RIBON_SERVICE_LIFETIME_BOOT,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION, .id = "service.raw-fdt.storage-flush",
    .provides = RIBON_CAP_STORAGE_FLUSH, .architecture_mask = RIBON_ARCH_MASK_AARCH64 | RIBON_ARCH_MASK_RISCV64,
    .environment_mask = RIBON_ENV_MASK_RAW_FDT, .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 1024u, .input_budget = 64u, .output_budget = 64u, .deadline_ms = 30000u,
    .operations = &raw_fdt_flush_operations, .operations_size = sizeof(raw_fdt_flush_operations),
    .operations_abi = RIBON_SERVICE_ABI_VERSION, .validate_operations = raw_fdt_flush_validate,
};

/** @brief raw-FDT consumer가 제공하는 environment closure authority다. */
const struct RibonServiceDescriptor ribon_raw_fdt_environment_quiesce_service_descriptor = {
    .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC, .size = sizeof(ribon_raw_fdt_environment_quiesce_service_descriptor),
    .abi_version = RIBON_SERVICE_ABI_VERSION, .kind = RIBON_SERVICE_KIND_ENVIRONMENT_QUIESCE,
    .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY, .lifetime = RIBON_SERVICE_LIFETIME_QUIESCE,
    .phase = RIBON_PLUGIN_PHASE_QUIESCE, .id = "service.raw-fdt.environment-quiesce",
    .provides = RIBON_CAP_ENVIRONMENT_QUIESCE, .architecture_mask = RIBON_ARCH_MASK_AARCH64 | RIBON_ARCH_MASK_RISCV64,
    .environment_mask = RIBON_ENV_MASK_RAW_FDT, .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 1024u, .input_budget = 64u, .output_budget = 64u, .deadline_ms = 30000u,
    .operations = &raw_fdt_quiesce_operations, .operations_size = sizeof(raw_fdt_quiesce_operations),
    .operations_abi = RIBON_SERVICE_ABI_VERSION, .validate_operations = raw_fdt_quiesce_validate,
};

static const struct RibonServiceDescriptor *const raw_fdt_services[] = {
    &ribon_raw_fdt_boot_source_service_descriptor,
    &ribon_raw_fdt_environment_quiesce_service_descriptor,
    &ribon_raw_fdt_monotonic_timer_service_descriptor,
    &ribon_raw_fdt_persistent_metadata_service_descriptor,
    &ribon_raw_fdt_storage_flush_service_descriptor,
};

static const struct RibonServiceDirectory raw_fdt_service_directory = {
    .size = sizeof(raw_fdt_service_directory),
    .abi_version = RIBON_SERVICE_DIRECTORY_ABI_VERSION,
    .services = raw_fdt_services,
    .service_count = (uint32_t)(sizeof(raw_fdt_services) / sizeof(raw_fdt_services[0])),
};

/** @brief 한 reserved range의 end를 overflow 없이 계산한다. */
static int raw_fdt_range_end(
    const struct RibonRawFdtReservation *range,
    uint64_t *out) {
    if (range == 0 || out == 0 || range->size == 0u ||
        range->base > UINT64_MAX - range->size) {
        return 0;
    }
    *out = range->base + range->size;
    return 1;
}

/** @brief Reserved range를 physical start 순서로 정렬한다. */
static void raw_fdt_sort_reservations(
    struct RibonRawFdtReservation *ranges,
    uint32_t count) {
    for (uint32_t index = 1u; index < count; ++index) {
        struct RibonRawFdtReservation value = ranges[index];
        uint32_t cursor = index;
        while (cursor > 0u && ranges[cursor - 1u].base > value.base) {
            ranges[cursor] = ranges[cursor - 1u];
            --cursor;
        }
        ranges[cursor] = value;
    }
}

/** @brief Caller-owned memory-map storage에 한 region을 append한다. */
static int raw_fdt_append_region(
    struct RibonRawFdtEntry *entry,
    uint32_t *count,
    uint64_t base,
    uint64_t size,
    enum RibonMemoryRegionKind kind) {
    uint64_t attributes = RIBON_MEMORY_ATTR_READ;
    if (size == 0u) {
        return RIBON_RAW_FDT_STATUS_OK;
    }
    if (*count >= entry->memory_region_capacity) {
        return RIBON_RAW_FDT_STATUS_OUT_OF_CAPACITY;
    }
    if (kind == RIBON_MEMORY_REGION_USABLE ||
        kind == RIBON_MEMORY_REGION_BOOTLOADER ||
        kind == RIBON_MEMORY_REGION_KERNEL_IMAGE) {
        attributes |= RIBON_MEMORY_ATTR_WRITE;
    }
    entry->memory_regions[*count] = (struct RibonMemoryRegion){
        .base = base,
        .length = size,
        .kind = kind,
        .attributes = attributes,
    };
    ++(*count);
    return RIBON_RAW_FDT_STATUS_OK;
}

/** @brief FDT memory range에서 target reservation을 제외한 memory map을 만든다. */
static int raw_fdt_build_memory_map(
    struct RibonRawFdtEntry *entry,
    const struct RibonFdtFacts *facts,
    uint32_t *region_count_out) {
    struct RibonRawFdtReservation ranges[RIBON_RAW_FDT_MAX_RESERVATIONS];
    uint32_t range_count = entry->reservation_count;
    uint32_t region_count = 0u;
    uint64_t memory_end;
    uint64_t cursor;

    if (range_count > RIBON_RAW_FDT_MAX_TARGET_RESERVATIONS ||
        facts->reservation_count > RIBON_FDT_RESERVATION_CAPACITY ||
        range_count + facts->reservation_count + 1u >
            RIBON_RAW_FDT_MAX_RESERVATIONS ||
        facts->memory_size == 0u ||
        facts->memory_base > UINT64_MAX - facts->memory_size) {
        return RIBON_RAW_FDT_STATUS_BAD_RESERVATION;
    }
    memory_end = facts->memory_base + facts->memory_size;
    for (uint32_t index = 0u; index < range_count; ++index) {
        ranges[index] = entry->reservations[index];
    }
    for (uint32_t index = 0u; index < facts->reservation_count; ++index) {
        const struct RibonFdtReservation *reservation =
            &facts->reservations[index];
        uint64_t reservation_end;
        if (reservation->base > UINT64_MAX - reservation->size) {
            return RIBON_RAW_FDT_STATUS_BAD_RESERVATION;
        }
        reservation_end = reservation->base + reservation->size;
        if (reservation_end <= facts->memory_base ||
            reservation->base >= memory_end) {
            continue;
        }
        if (reservation->base < facts->memory_base ||
            reservation_end > memory_end) {
            return RIBON_RAW_FDT_STATUS_BAD_RESERVATION;
        }
        ranges[range_count++] = (struct RibonRawFdtReservation){
            .base = reservation->base,
            .size = reservation->size,
            .kind = RIBON_MEMORY_REGION_FIRMWARE,
        };
    }
    ranges[range_count++] = (struct RibonRawFdtReservation){
        .base = (uint64_t)(uintptr_t)entry->fdt,
        .size = facts->total_size,
        .kind = RIBON_MEMORY_REGION_FIRMWARE,
    };
    raw_fdt_sort_reservations(ranges, range_count);
    cursor = facts->memory_base;
    for (uint32_t index = 0u; index < range_count; ++index) {
        uint64_t range_end;
        int status;
        if (!raw_fdt_range_end(&ranges[index], &range_end) ||
            ranges[index].base < facts->memory_base ||
            range_end > memory_end ||
            ranges[index].base < cursor) {
            return RIBON_RAW_FDT_STATUS_BAD_RESERVATION;
        }
        status = raw_fdt_append_region(
            entry,
            &region_count,
            cursor,
            ranges[index].base - cursor,
            RIBON_MEMORY_REGION_USABLE);
        if (status != RIBON_RAW_FDT_STATUS_OK) {
            return status;
        }
        status = raw_fdt_append_region(
            entry,
            &region_count,
            ranges[index].base,
            ranges[index].size,
            ranges[index].kind);
        if (status != RIBON_RAW_FDT_STATUS_OK) {
            return status;
        }
        cursor = range_end;
    }
    if (cursor < memory_end) {
        int status = raw_fdt_append_region(
            entry,
            &region_count,
            cursor,
            memory_end - cursor,
            RIBON_MEMORY_REGION_USABLE);
        if (status != RIBON_RAW_FDT_STATUS_OK) {
            return status;
        }
    }
    *region_count_out = region_count;
    return RIBON_RAW_FDT_STATUS_OK;
}

/** @brief Native FDT와 target reservation을 firmware-neutral environment로 동결한다. */
int ribon_raw_fdt_environment_capture(
    struct RibonRawFdtEntry *entry,
    struct RibonBootEnvironment *out) {
    struct RibonFdtFacts facts;
    uint32_t region_count;
    int status;
    if (entry == 0 || out == 0 || entry->fdt == 0 ||
        entry->arch_ops == 0 || entry->payload == 0 ||
        entry->payload_size == 0u || entry->timer_frequency_hz == 0u ||
        entry->memory_regions == 0 || entry->memory_region_capacity == 0u ||
        (entry->reservation_count != 0u && entry->reservations == 0)) {
        return RIBON_RAW_FDT_STATUS_BAD_ARGUMENT;
    }
    status = ribon_fdt_parse(entry->fdt, entry->fdt_capacity, &facts);
    if (status != RIBON_FDT_STATUS_OK) {
        return RIBON_RAW_FDT_STATUS_BAD_FDT;
    }
    status = raw_fdt_build_memory_map(entry, &facts, &region_count);
    if (status != RIBON_RAW_FDT_STATUS_OK) {
        return status;
    }
    ribon_boot_environment_init(
        out,
        RIBON_ENVIRONMENT_RAW_FDT,
        entry->architecture);
    out->memory_map.regions = entry->memory_regions;
    out->memory_map.region_count = region_count;
    out->device_tree.physical_address = (uint64_t)(uintptr_t)entry->fdt;
    out->device_tree.size = facts.total_size;
    out->device_tree.data = entry->fdt;
    out->boot_media.kind = RIBON_BOOT_MEDIA_MEMORY;
    out->boot_media.path = entry->payload_name;
    out->boot_media.physical_address = (uint64_t)(uintptr_t)entry->payload;
    out->boot_media.size = entry->payload_size;
    out->boot_cpu_id = entry->boot_cpu_id;
    out->flags =
        RIBON_BOOT_ENV_HAS_MEMORY_MAP |
        RIBON_BOOT_ENV_HAS_DEVICE_TREE |
        RIBON_BOOT_ENV_HAS_BOOT_MEDIA |
        RIBON_BOOT_ENV_HAS_BOOT_CPU_ID;
    if (facts.boot_arguments != 0) {
        out->command_line.text = facts.boot_arguments;
        out->command_line.length = facts.boot_arguments_size;
        out->flags |= RIBON_BOOT_ENV_HAS_COMMAND_LINE;
    }
    raw_fdt_entry = entry;
    raw_fdt_boot_source_operations.context = entry;
    raw_fdt_timer_operations.context = entry;
    raw_fdt_timer_operations.frequency_hz = entry->timer_frequency_hz;
    raw_fdt_metadata_operations.context = entry;
    raw_fdt_flush_operations.context = entry;
    raw_fdt_quiesce_operations.context = entry;
    raw_fdt_services_initialized = 1;
    return RIBON_RAW_FDT_STATUS_OK;
}

/** @brief 초기화된 raw-FDT typed service directory를 반환한다. */
const struct RibonServiceDirectory *ribon_raw_fdt_service_directory(void) {
    return raw_fdt_services_initialized && raw_fdt_entry != 0 ?
        &raw_fdt_service_directory :
        0;
}

/** @brief raw-FDT descriptor와 live typed directory의 방향을 검증한다. */
static int raw_fdt_environment_validate(
    const struct RibonPluginDescriptor *descriptor) {
    return raw_fdt_services_initialized &&
           descriptor != 0 &&
           descriptor->operations == &raw_fdt_service_directory &&
           ribon_environment_plugin_operations_are_valid(descriptor);
}

/** @brief raw-FDT environment consumer plugin descriptor다. */
const struct RibonPluginDescriptor ribon_raw_fdt_environment_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_raw_fdt_environment_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_ENVIRONMENT,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION,
    .id = "environment.raw-fdt",
    .provides =
        RIBON_CAP_BOOT_SOURCE_READ |
        RIBON_CAP_MONOTONIC_TIMER |
        RIBON_CAP_PERSISTENT_METADATA |
        RIBON_CAP_STORAGE_FLUSH |
        RIBON_CAP_ENVIRONMENT_QUIESCE,
    .requires = RIBON_CAP_ARCHITECTURE,
    .architecture_mask = RIBON_ARCH_MASK_AARCH64 | RIBON_ARCH_MASK_RISCV64,
    .environment_mask = RIBON_ENV_MASK_RAW_FDT,
    .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 8192u,
    .input_budget = 64ull * 1024ull * 1024ull,
    .output_budget = 8192u,
    .deadline_ms = 30000u,
    .operations = &raw_fdt_service_directory,
    .operations_size = sizeof(raw_fdt_service_directory),
    .operations_abi = RIBON_SERVICE_DIRECTORY_ABI_VERSION,
    .validate_operations = raw_fdt_environment_validate,
};
