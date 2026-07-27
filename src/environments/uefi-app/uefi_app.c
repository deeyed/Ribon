#include "uefi_app.h"

#include <Ribon/core/memory.h>
#include <Ribon/plugin/descriptor.h>

#include <string.h>

static struct RibonUefiAppContext *uefi_context;
static int uefi_services_initialized;

/** @brief UEFI memory type을 Ribon ownership kind로 변환한다. */
static enum RibonMemoryRegionKind uefi_memory_kind(UINT32 type) {
    switch (type) {
    case EfiConventionalMemory:
        return RIBON_MEMORY_REGION_USABLE;
    case EfiLoaderCode:
    case EfiLoaderData:
        return RIBON_MEMORY_REGION_BOOTLOADER;
    case EfiBootServicesCode:
    case EfiBootServicesData:
    case EfiRuntimeServicesCode:
    case EfiRuntimeServicesData:
        return RIBON_MEMORY_REGION_FIRMWARE;
    case EfiACPIReclaimMemory:
    case EfiACPIMemoryNVS:
        return RIBON_MEMORY_REGION_ACPI;
    case EfiMemoryMappedIO:
    case EfiMemoryMappedIOPortSpace:
        return RIBON_MEMORY_REGION_MMIO;
    default:
        return RIBON_MEMORY_REGION_RESERVED;
    }
}

/** @brief UEFI memory descriptor를 generic access/lifetime bit로 변환한다. */
static uint64_t uefi_memory_attributes(const EFI_MEMORY_DESCRIPTOR *descriptor) {
    uint64_t attributes = RIBON_MEMORY_ATTR_READ;
    if (descriptor->Type != EfiUnusableMemory) {
        attributes |= RIBON_MEMORY_ATTR_WRITE;
    }
    if (descriptor->Type == EfiMemoryMappedIO ||
        descriptor->Type == EfiMemoryMappedIOPortSpace) {
        attributes |= RIBON_MEMORY_ATTR_DEVICE;
    }
    if (descriptor->Type == EfiLoaderCode ||
        descriptor->Type == EfiBootServicesCode ||
        descriptor->Type == EfiRuntimeServicesCode) {
        attributes |= RIBON_MEMORY_ATTR_EXECUTE;
    }
    if (descriptor->Type == EfiLoaderCode ||
        descriptor->Type == EfiLoaderData ||
        descriptor->Type == EfiBootServicesCode ||
        descriptor->Type == EfiBootServicesData) {
        attributes |= RIBON_MEMORY_ATTR_BOOT_RECLAIMABLE;
    }
    if (descriptor->Type == EfiRuntimeServicesCode ||
        descriptor->Type == EfiRuntimeServicesData ||
        (descriptor->Attribute & EFI_MEMORY_RUNTIME) != 0u) {
        attributes |= RIBON_MEMORY_ATTR_FIRMWARE_RUNTIME;
    }
    return attributes;
}

/** @brief Embedded memory source에서 bounded byte range를 복사한다. */
static int uefi_boot_source_read(
    void *context,
    const struct RibonBootSource *source,
    uint64_t offset,
    void *buffer,
    uint64_t size,
    uint64_t deadline_ticks) {
    const struct RibonUefiAppContext *native =
        (const struct RibonUefiAppContext *)context;
    (void)deadline_ticks;
    if (native == 0 || source == 0 ||
        source->kind != RIBON_BOOT_MEDIA_MEMORY ||
        source->size != native->payload_size ||
        buffer == 0 || size == 0u ||
        offset > native->payload_size ||
        size > native->payload_size - offset) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    memcpy(
        buffer,
        (const unsigned char *)native->payload + offset,
        (size_t)size);
    return RIBON_SERVICE_STATUS_OK;
}

/** @brief UEFI monotonic service를 firmware-neutral timer callback으로 변환한다. */
static int uefi_timer_now(void *context, uint64_t *ticks_out) {
    const struct RibonUefiAppContext *native =
        (const struct RibonUefiAppContext *)context;
    UINT64 value = 0u;
    EFI_STATUS status;
    if (native == 0 || native->boot_services == 0 || ticks_out == 0 ||
        native->boot_services->GetNextMonotonicCount == 0) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    status = native->boot_services->GetNextMonotonicCount(&value);
    if (EFI_ERROR(status)) {
        return RIBON_SERVICE_STATUS_IO;
    }
    *ticks_out = value;
    return RIBON_SERVICE_STATUS_OK;
}

static struct RibonBootSourceServiceOperations uefi_boot_source_operations = {
    .size = sizeof(uefi_boot_source_operations),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .read = uefi_boot_source_read,
};

static struct RibonMonotonicTimerServiceOperations uefi_timer_operations = {
    .size = sizeof(uefi_timer_operations),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .frequency_hz = 1u,
    .now = uefi_timer_now,
};

/** @brief UEFI boot-source descriptor가 live native context를 참조하는지 검사한다. */
static int uefi_boot_source_validate(
    const struct RibonServiceDescriptor *descriptor) {
    const struct RibonBootSourceServiceOperations *operations;
    if (descriptor == 0 || descriptor->operations != &uefi_boot_source_operations ||
        descriptor->operations_size != sizeof(*operations) ||
        descriptor->operations_abi != RIBON_SERVICE_ABI_VERSION) {
        return 0;
    }
    operations = descriptor->operations;
    return uefi_services_initialized && operations->context == uefi_context &&
           operations->size == sizeof(*operations) &&
           operations->abi_version == RIBON_SERVICE_ABI_VERSION &&
           operations->read == uefi_boot_source_read;
}

/** @brief UEFI monotonic-timer descriptor가 live native context를 참조하는지 검사한다. */
static int uefi_timer_validate(const struct RibonServiceDescriptor *descriptor) {
    const struct RibonMonotonicTimerServiceOperations *operations;
    if (descriptor == 0 || descriptor->operations != &uefi_timer_operations ||
        descriptor->operations_size != sizeof(*operations) ||
        descriptor->operations_abi != RIBON_SERVICE_ABI_VERSION) {
        return 0;
    }
    operations = descriptor->operations;
    return uefi_services_initialized && operations->context == uefi_context &&
           operations->size == sizeof(*operations) &&
           operations->abi_version == RIBON_SERVICE_ABI_VERSION &&
           operations->frequency_hz == 1u && operations->now == uefi_timer_now;
}

/** @brief UEFI application이 제공하는 typed boot-source authority다. */
const struct RibonServiceDescriptor ribon_uefi_app_boot_source_service_descriptor = {
    .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_uefi_app_boot_source_service_descriptor),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .kind = RIBON_SERVICE_KIND_BOOT_SOURCE,
    .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY,
    .lifetime = RIBON_SERVICE_LIFETIME_QUIESCE,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION,
    .id = "service.uefi-app.boot-source",
    .provides = RIBON_CAP_BOOT_SOURCE_READ,
    .architecture_mask = RIBON_ARCH_MASK_X86_64,
    .environment_mask = RIBON_ENV_MASK_UEFI,
    .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 8192u,
    .input_budget = 64ull * 1024ull * 1024ull,
    .output_budget = 65536u,
    .deadline_ms = 30000u,
    .operations = &uefi_boot_source_operations,
    .operations_size = sizeof(uefi_boot_source_operations),
    .operations_abi = RIBON_SERVICE_ABI_VERSION,
    .validate_operations = uefi_boot_source_validate,
};

/** @brief UEFI application이 제공하는 typed monotonic-timer authority다. */
const struct RibonServiceDescriptor ribon_uefi_app_monotonic_timer_service_descriptor = {
    .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_uefi_app_monotonic_timer_service_descriptor),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .kind = RIBON_SERVICE_KIND_MONOTONIC_TIMER,
    .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY,
    .lifetime = RIBON_SERVICE_LIFETIME_QUIESCE,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION,
    .id = "service.uefi-app.monotonic-timer",
    .provides = RIBON_CAP_MONOTONIC_TIMER,
    .architecture_mask = RIBON_ARCH_MASK_X86_64,
    .environment_mask = RIBON_ENV_MASK_UEFI,
    .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 8192u,
    .input_budget = 4096u,
    .output_budget = 65536u,
    .deadline_ms = 30000u,
    .operations = &uefi_timer_operations,
    .operations_size = sizeof(uefi_timer_operations),
    .operations_abi = RIBON_SERVICE_ABI_VERSION,
    .validate_operations = uefi_timer_validate,
};

static const struct RibonServiceDescriptor *const uefi_services[] = {
    &ribon_uefi_app_boot_source_service_descriptor,
    &ribon_uefi_app_monotonic_timer_service_descriptor,
};

static const struct RibonServiceDirectory uefi_service_directory = {
    .size = sizeof(uefi_service_directory),
    .abi_version = RIBON_SERVICE_DIRECTORY_ABI_VERSION,
    .services = uefi_services,
    .service_count = (uint32_t)(sizeof(uefi_services) / sizeof(uefi_services[0])),
};

/** @brief UEFI native state와 memory-source service를 결합한다. */
int ribon_uefi_app_initialize(
    struct RibonUefiAppContext *context,
    EFI_HANDLE image_handle,
    EFI_SYSTEM_TABLE *system_table,
    const void *payload,
    uint64_t payload_size) {
    if (context == 0 || image_handle == 0 || system_table == 0 ||
        system_table->BootServices == 0 || payload == 0 ||
        payload_size == 0u || context->raw_memory_map == 0 ||
        context->raw_memory_map_capacity == 0u ||
        context->regions == 0 || context->region_capacity == 0u) {
        return RIBON_UEFI_APP_STATUS_BAD_ARGUMENT;
    }
    context->image_handle = image_handle;
    context->system_table = system_table;
    context->boot_services = system_table->BootServices;
    context->payload = payload;
    context->payload_size = payload_size;
    context->region_count = 0u;
    context->map_key = 0u;
    context->descriptor_size = 0u;
    context->descriptor_version = 0u;
    uefi_context = context;
    uefi_boot_source_operations.context = context;
    uefi_timer_operations.context = context;
    uefi_services_initialized = 1;
    return RIBON_UEFI_APP_STATUS_OK;
}

/** @brief UEFI descriptor buffer를 caller-owned generic region array로 변환한다. */
static int uefi_convert_memory_map(
    struct RibonUefiAppContext *context,
    UINTN map_size) {
    const UINTN count = map_size / context->descriptor_size;
    if (context->descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR) ||
        map_size % context->descriptor_size != 0u ||
        count > context->region_capacity) {
        return RIBON_UEFI_APP_STATUS_OUT_OF_CAPACITY;
    }
    for (UINTN index = 0u; index < count; ++index) {
        const EFI_MEMORY_DESCRIPTOR *descriptor =
            (const EFI_MEMORY_DESCRIPTOR *)
                ((const UINT8 *)context->raw_memory_map +
                 index * context->descriptor_size);
        context->regions[index] = (struct RibonMemoryRegion){
            .base = descriptor->PhysicalStart,
            .length = descriptor->NumberOfPages * 4096ull,
            .kind = uefi_memory_kind(descriptor->Type),
            .attributes = uefi_memory_attributes(descriptor),
        };
    }
    context->region_count = (uint32_t)count;
    return RIBON_UEFI_APP_STATUS_OK;
}

/** @brief UEFI memory map을 firmware-neutral environment로 capture한다. */
int ribon_uefi_app_capture_environment(
    struct RibonUefiAppContext *context,
    struct RibonBootEnvironment *out) {
    UINTN map_size;
    EFI_STATUS status;
    int convert_status;
    if (context == 0 || out == 0 || context->boot_services == 0 ||
        context->raw_memory_map_capacity > (uint64_t)(UINTN)-1) {
        return RIBON_UEFI_APP_STATUS_BAD_ARGUMENT;
    }
    map_size = (UINTN)context->raw_memory_map_capacity;
    status = context->boot_services->GetMemoryMap(
        &map_size,
        (EFI_MEMORY_DESCRIPTOR *)context->raw_memory_map,
        &context->map_key,
        &context->descriptor_size,
        &context->descriptor_version);
    if (status == EFI_BUFFER_TOO_SMALL) {
        return RIBON_UEFI_APP_STATUS_OUT_OF_CAPACITY;
    }
    if (EFI_ERROR(status)) {
        return RIBON_UEFI_APP_STATUS_FIRMWARE_ERROR;
    }
    convert_status = uefi_convert_memory_map(context, map_size);
    if (convert_status != RIBON_UEFI_APP_STATUS_OK) {
        return convert_status;
    }
    ribon_boot_environment_init(
        out,
        RIBON_ENVIRONMENT_UEFI,
        RIBON_ARCHITECTURE_X86_64);
    out->memory_map.regions = context->regions;
    out->memory_map.region_count = context->region_count;
    out->raw_memory_map.data = context->raw_memory_map;
    out->raw_memory_map.size = map_size;
    out->raw_memory_map.descriptor_size = (uint32_t)context->descriptor_size;
    out->raw_memory_map.descriptor_version = context->descriptor_version;
    out->boot_media.kind = RIBON_BOOT_MEDIA_MEMORY;
    out->boot_media.path = "boot/payload.elf";
    out->boot_media.physical_address = (uint64_t)(uintptr_t)context->payload;
    out->boot_media.size = context->payload_size;
    out->command_line.text = "environment=uefi-app";
    out->command_line.length = 20u;
    out->flags =
        RIBON_BOOT_ENV_HAS_MEMORY_MAP |
        RIBON_BOOT_ENV_HAS_RAW_MEMORY_MAP |
        RIBON_BOOT_ENV_HAS_BOOT_MEDIA |
        RIBON_BOOT_ENV_HAS_COMMAND_LINE;
    return RIBON_UEFI_APP_STATUS_OK;
}

/** @brief UEFI page allocation에 analyzed segment를 배치한다. */
int ribon_uefi_app_place_payload(
    struct RibonUefiAppContext *context,
    const struct RibonPayloadImage *payload,
    struct RibonLoadedPayload *layout) {
    uint64_t runtime_base = UINT64_MAX;
    uint64_t runtime_end = 0u;
    int entry_seen = 0;
    if (context == 0 || context->boot_services == 0 || payload == 0 ||
        payload->data == 0 || layout == 0 || layout->segments == 0 ||
        layout->segment_count == 0u) {
        return RIBON_UEFI_APP_STATUS_BAD_ARGUMENT;
    }
    for (uint32_t index = 0u; index < layout->segment_count; ++index) {
        struct RibonLoadSegment *segment = &layout->segments[index];
        const uint64_t page_base = ribon_align_down(segment->load_address, 4096u);
        const uint64_t page_offset = segment->load_address - page_base;
        uint64_t page_end;
        uint64_t segment_end;
        UINTN pages;
        EFI_PHYSICAL_ADDRESS allocation = page_base;
        EFI_STATUS status;
        if (segment->memory_size == 0u ||
            segment->load_address > UINT64_MAX - segment->memory_size ||
            ribon_align_up(
                segment->load_address + segment->memory_size,
                4096u,
                &page_end) != RIBON_MEMORY_STATUS_OK ||
            page_end <= page_base ||
            segment->file_offset > payload->size ||
            segment->file_size > payload->size - segment->file_offset ||
            segment->file_size > segment->memory_size) {
            return RIBON_UEFI_APP_STATUS_PAYLOAD_ERROR;
        }
        pages = (UINTN)((page_end - page_base) / 4096u);
        status = context->boot_services->AllocatePages(
            AllocateAddress,
            EfiLoaderCode,
            pages,
            &allocation);
        if (EFI_ERROR(status) || allocation != page_base) {
            allocation = 0u;
            status = context->boot_services->AllocatePages(
                AllocateAnyPages,
                EfiLoaderCode,
                pages,
                &allocation);
        }
        if (EFI_ERROR(status) || allocation == 0u) {
            return RIBON_UEFI_APP_STATUS_FIRMWARE_ERROR;
        }
        memset((void *)(uintptr_t)allocation, 0, pages * 4096u);
        memcpy(
            (void *)(uintptr_t)(allocation + page_offset),
            (const unsigned char *)payload->data + segment->file_offset,
            (size_t)segment->file_size);
        segment->runtime_address = allocation + page_offset;
        segment_end = segment->runtime_address + segment->memory_size;
        if (segment->runtime_address < runtime_base) {
            runtime_base = segment->runtime_address;
        }
        if (segment_end > runtime_end) {
            runtime_end = segment_end;
        }
        if (layout->entry_point >= segment->virtual_address &&
            layout->entry_point < segment->virtual_address + segment->memory_size &&
            (segment->flags & RIBON_LOAD_SEGMENT_EXECUTE) != 0u) {
            layout->runtime_entry_address =
                segment->runtime_address +
                (layout->entry_point - segment->virtual_address);
            entry_seen = 1;
        }
    }
    if (!entry_seen || runtime_end <= runtime_base) {
        return RIBON_UEFI_APP_STATUS_PAYLOAD_ERROR;
    }
    layout->runtime_load_base = runtime_base;
    layout->runtime_load_end = runtime_end;
    layout->memory_size = runtime_end - runtime_base;
    layout->load_plan_flags |=
        RIBON_LOAD_PLAN_SEGMENTS_PLACED |
        RIBON_LOAD_PLAN_RUNTIME_ENTRY_VALID;
    return RIBON_UEFI_APP_STATUS_OK;
}

/** @brief Final map과 ExitBootServices를 handoff refresh와 함께 재시도한다. */
int ribon_uefi_app_exit_boot_services(
    struct RibonUefiAppContext *context,
    struct RibonBootEnvironment *environment,
    RibonUefiRefreshPlanFn refresh,
    void *refresh_context) {
    if (context == 0 || environment == 0 || refresh == 0 ||
        context->boot_services == 0) {
        return RIBON_UEFI_APP_STATUS_BAD_ARGUMENT;
    }
    for (uint32_t attempt = 0u; attempt < RIBON_UEFI_EXIT_ATTEMPTS; ++attempt) {
        EFI_STATUS status;
        int capture_status = ribon_uefi_app_capture_environment(context, environment);
        if (capture_status != RIBON_UEFI_APP_STATUS_OK) {
            return capture_status;
        }
        if (refresh(refresh_context, environment) != 0) {
            return RIBON_UEFI_APP_STATUS_PAYLOAD_ERROR;
        }
        status = context->boot_services->ExitBootServices(
            context->image_handle,
            context->map_key);
        if (!EFI_ERROR(status)) {
            context->boot_services = 0;
            return RIBON_UEFI_APP_STATUS_OK;
        }
        if (status != EFI_INVALID_PARAMETER) {
            return RIBON_UEFI_APP_STATUS_FIRMWARE_ERROR;
        }
    }
    return RIBON_UEFI_APP_STATUS_RETRY_EXHAUSTED;
}

/** @brief 초기화된 UEFI application typed service directory를 반환한다. */
const struct RibonServiceDirectory *ribon_uefi_app_service_directory(void) {
    return uefi_services_initialized && uefi_context != 0 ?
        &uefi_service_directory :
        0;
}

/** @brief UEFI environment descriptor와 live typed directory를 검증한다. */
static int uefi_environment_validate(
    const struct RibonPluginDescriptor *descriptor) {
    return uefi_services_initialized &&
           descriptor != 0 &&
           descriptor->operations == &uefi_service_directory &&
           ribon_environment_plugin_operations_are_valid(descriptor);
}

/** @brief UEFI application environment consumer plugin descriptor다. */
const struct RibonPluginDescriptor ribon_uefi_app_environment_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_uefi_app_environment_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_ENVIRONMENT,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION,
    .id = "environment.uefi-app",
    .provides =
        RIBON_CAP_BOOT_SOURCE_READ |
        RIBON_CAP_MONOTONIC_TIMER,
    .requires =
        RIBON_CAP_ARCHITECTURE |
        RIBON_CAP_PLATFORM_FACTS,
    .architecture_mask = RIBON_ARCH_MASK_X86_64,
    .environment_mask = RIBON_ENV_MASK_UEFI,
    .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 16384u,
    .input_budget = 64ull * 1024ull * 1024ull,
    .output_budget = 65536ull,
    .deadline_ms = 30000u,
    .operations = &uefi_service_directory,
    .operations_size = sizeof(uefi_service_directory),
    .operations_abi = RIBON_SERVICE_DIRECTORY_ABI_VERSION,
    .validate_operations = uefi_environment_validate,
};
