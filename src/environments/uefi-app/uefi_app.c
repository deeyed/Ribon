#include "uefi_app.h"

#include <Ribon/core/memory.h>
#include <Ribon/plugin/descriptor.h>

#include <Protocol/LoadedImage.h>

#include <string.h>

static struct RibonUefiAppContext *uefi_context;
static int uefi_services_initialized;
static unsigned char uefi_attempt_metadata[64];
static uint64_t uefi_attempt_metadata_size;

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

/** @brief Canonical ASCII absolute path를 bounded UEFI UTF-16 path로 변환한다. */
static int uefi_path_to_utf16(const char *path, CHAR16 out[RIBON_UEFI_PATH_CAPACITY]) {
    uint32_t component_bytes = 0u;
    uint32_t components = 0u;
    uint32_t dots = 0u;
    if (path == 0 || out == 0 || path[0] != '/' || path[1] == '\0') {
        return 0;
    }
    for (uint32_t index = 1u; index < RIBON_UEFI_PATH_CAPACITY; ++index) {
        const unsigned char byte = (unsigned char)path[index];
        if (byte == '/' || byte == '\0') {
            if (component_bytes == 0u || dots == component_bytes ||
                ++components > RIBON_UEFI_FILE_SOURCE_CAPACITY * 2u) {
                return 0;
            }
            out[index - 1u] = byte == '\0' ? 0u : (CHAR16)'\\';
            if (byte == '\0') {
                return 1;
            }
            component_bytes = 0u;
            dots = 0u;
            continue;
        }
        if (byte < 0x21u || byte > 0x7eu || byte == '\\' || ++component_bytes >= 64u) {
            return 0;
        }
        if (byte == '.') {
            ++dots;
        }
        out[index - 1u] = (CHAR16)byte;
    }
    return 0;
}

/** @brief UEFI root handle에서 one canonical path를 read-only로 연다. */
static int uefi_open_file(
    struct RibonUefiAppContext *context,
    const char *path,
    EFI_FILE_PROTOCOL **out) {
    CHAR16 native_path[RIBON_UEFI_PATH_CAPACITY];
    EFI_STATUS status;
    if (context == 0 || context->boot_services == 0 || context->root == 0 || out == 0 ||
        !uefi_path_to_utf16(path, native_path) || context->root->Open == 0) {
        return RIBON_UEFI_APP_STATUS_BAD_ARGUMENT;
    }
    *out = 0;
    status = context->root->Open(
        context->root,
        out,
        native_path,
        EFI_FILE_MODE_READ,
        0u);
    return EFI_ERROR(status) || *out == 0 ?
        RIBON_UEFI_APP_STATUS_FIRMWARE_ERROR : RIBON_UEFI_APP_STATUS_OK;
}

/** @brief UEFI file handle의 exact byte size를 seek-only query로 검사한다. */
static int uefi_file_size(EFI_FILE_PROTOCOL *file, uint64_t *out) {
    EFI_STATUS status;
    if (file == 0 || out == 0 || file->SetPosition == 0 || file->GetPosition == 0) {
        return RIBON_UEFI_APP_STATUS_BAD_ARGUMENT;
    }
    status = file->SetPosition(file, UINT64_MAX);
    if (EFI_ERROR(status)) {
        return RIBON_UEFI_APP_STATUS_FIRMWARE_ERROR;
    }
    status = file->GetPosition(file, out);
    if (EFI_ERROR(status) || *out == 0u || EFI_ERROR(file->SetPosition(file, 0u))) {
        return RIBON_UEFI_APP_STATUS_FIRMWARE_ERROR;
    }
    return RIBON_UEFI_APP_STATUS_OK;
}

/** @brief UEFI file handle에서 exact byte range를 partial read 없이 복사한다. */
static int uefi_file_read_exact(
    EFI_FILE_PROTOCOL *file,
    uint64_t offset,
    void *buffer,
    uint64_t size) {
    UINTN native_size;
    EFI_STATUS status;
    if (file == 0 || buffer == 0 || size == 0u || size > (uint64_t)(UINTN)-1 ||
        file->SetPosition == 0 || file->Read == 0) {
        return RIBON_UEFI_APP_STATUS_BAD_ARGUMENT;
    }
    status = file->SetPosition(file, offset);
    if (EFI_ERROR(status)) {
        return RIBON_UEFI_APP_STATUS_FIRMWARE_ERROR;
    }
    native_size = (UINTN)size;
    status = file->Read(file, &native_size, buffer);
    return EFI_ERROR(status) || native_size != (UINTN)size ?
        RIBON_UEFI_APP_STATUS_FIRMWARE_ERROR : RIBON_UEFI_APP_STATUS_OK;
}

/** @brief UEFI Block I/O를 exact bounded generic read callback으로 변환한다. */
static int uefi_block_read(
    void *context,
    uint64_t first_block,
    void *buffer,
    uint32_t block_count,
    uint64_t deadline_ticks) {
    struct RibonUefiAppContext *native = context;
    EFI_BLOCK_IO_MEDIA *media;
    uint64_t bytes;
    (void)deadline_ticks;
    if (native == 0 || native->boot_services == 0 || native->block_io == 0 ||
        native->block_io->Media == 0 || native->block_io->ReadBlocks == 0 || buffer == 0 ||
        block_count == 0u) {
        return RIBON_BLOCK_STATUS_BAD_ARGUMENT;
    }
    media = native->block_io->Media;
    if (!media->MediaPresent || media->BlockSize == 0u || first_block > media->LastBlock ||
        block_count - 1u > media->LastBlock - first_block ||
        (uint64_t)block_count > UINT64_MAX / media->BlockSize ||
        (bytes = (uint64_t)block_count * media->BlockSize) > (uint64_t)(UINTN)-1) {
        return RIBON_BLOCK_STATUS_OUT_OF_RANGE;
    }
    return EFI_ERROR(native->block_io->ReadBlocks(
            native->block_io,
            media->MediaId,
            first_block,
            (UINTN)bytes,
            buffer)) ? RIBON_BLOCK_STATUS_IO : RIBON_BLOCK_STATUS_OK;
}

/** @brief UEFI file source slot에서 bounded byte range를 exact copy한다. */
static int uefi_boot_source_read(
    void *context,
    const struct RibonBootSource *source,
    uint64_t offset,
    void *buffer,
    uint64_t size,
    uint64_t deadline_ticks) {
    struct RibonUefiAppContext *native = context;
    const struct RibonUefiFileSource *file;
    (void)deadline_ticks;
    if (native == 0 || native->boot_services == 0 || source == 0 ||
        source->kind != RIBON_BOOT_MEDIA_FILE || source->source_id == 0u ||
        source->source_id > RIBON_UEFI_FILE_SOURCE_CAPACITY ||
        buffer == 0 || size == 0u ||
        (file = &native->files[source->source_id - 1u])->handle == 0 ||
        source->size != file->size || offset > file->size || size > file->size - offset) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    return uefi_file_read_exact(file->handle, offset, buffer, size) == RIBON_UEFI_APP_STATUS_OK ?
        RIBON_SERVICE_STATUS_OK : RIBON_SERVICE_STATUS_IO;
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

/** @brief UEFI consumer attempt metadata를 caller-owned static range에서 읽는다. */
static int uefi_metadata_read(void *context, uint64_t offset, void *buffer, uint64_t size) {
    if (context != uefi_context || buffer == 0 || offset > uefi_attempt_metadata_size ||
        size > uefi_attempt_metadata_size - offset) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    for (uint64_t index = 0u; index < size; ++index) {
        ((unsigned char *)buffer)[index] = uefi_attempt_metadata[offset + index];
    }
    return RIBON_SERVICE_STATUS_OK;
}

/** @brief UEFI consumer attempt metadata를 bounded durable candidate로 기록한다. */
static int uefi_metadata_write(void *context, uint64_t offset, const void *buffer, uint64_t size) {
    if (context != uefi_context || buffer == 0 || offset > sizeof(uefi_attempt_metadata) ||
        size > sizeof(uefi_attempt_metadata) - offset) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    for (uint64_t index = 0u; index < size; ++index) {
        uefi_attempt_metadata[offset + index] = ((const unsigned char *)buffer)[index];
    }
    if (offset + size > uefi_attempt_metadata_size) {
        uefi_attempt_metadata_size = offset + size;
    }
    return RIBON_SERVICE_STATUS_OK;
}

/** @brief UEFI consumer metadata flush ordering을 transaction boundary로 확정한다. */
static int uefi_metadata_flush(void *context, uint32_t slot, uint64_t deadline_ticks) {
    (void)slot;
    (void)deadline_ticks;
    return context == uefi_context ? RIBON_SERVICE_STATUS_OK : RIBON_SERVICE_STATUS_BAD_ARGUMENT;
}

/** @brief ExitBootServices 이후에도 native handle을 호출하지 않는 closure boundary다. */
static int uefi_environment_quiesce(void *context) {
    return context == uefi_context && uefi_context->boot_services == 0 ?
        RIBON_SERVICE_STATUS_OK : RIBON_SERVICE_STATUS_BAD_ARGUMENT;
}

static struct RibonPersistentMetadataServiceOperations uefi_metadata_operations = {
    .size = sizeof(uefi_metadata_operations), .abi_version = RIBON_SERVICE_ABI_VERSION,
    .read = uefi_metadata_read, .write = uefi_metadata_write,
};
static struct RibonStorageFlushServiceOperations uefi_flush_operations = {
    .size = sizeof(uefi_flush_operations), .abi_version = RIBON_SERVICE_ABI_VERSION,
    .flush = uefi_metadata_flush,
};
static struct RibonEnvironmentQuiesceServiceOperations uefi_quiesce_operations = {
    .size = sizeof(uefi_quiesce_operations), .abi_version = RIBON_SERVICE_ABI_VERSION,
    .quiesce = uefi_environment_quiesce,
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

/** @brief UEFI metadata operation table가 live context와 exact callback을 쓰는지 검사한다. */
static int uefi_metadata_validate(const struct RibonServiceDescriptor *descriptor) {
    const struct RibonPersistentMetadataServiceOperations *operations = descriptor == 0 ? 0 : descriptor->operations;
    return uefi_services_initialized && operations == &uefi_metadata_operations &&
           descriptor->operations_size == sizeof(*operations) && operations->context == uefi_context &&
           operations->read == uefi_metadata_read && operations->write == uefi_metadata_write;
}

/** @brief UEFI flush operation table가 live context와 exact callback을 쓰는지 검사한다. */
static int uefi_flush_validate(const struct RibonServiceDescriptor *descriptor) {
    const struct RibonStorageFlushServiceOperations *operations = descriptor == 0 ? 0 : descriptor->operations;
    return uefi_services_initialized && operations == &uefi_flush_operations &&
           descriptor->operations_size == sizeof(*operations) && operations->context == uefi_context &&
           operations->flush == uefi_metadata_flush;
}

/** @brief UEFI closure operation table가 live context와 exact callback을 쓰는지 검사한다. */
static int uefi_quiesce_validate(const struct RibonServiceDescriptor *descriptor) {
    const struct RibonEnvironmentQuiesceServiceOperations *operations = descriptor == 0 ? 0 : descriptor->operations;
    return uefi_services_initialized && operations == &uefi_quiesce_operations &&
           descriptor->operations_size == sizeof(*operations) && operations->context == uefi_context &&
           operations->quiesce == uefi_environment_quiesce;
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

/** @brief UEFI application이 제공하는 durable attempt metadata authority다. */
const struct RibonServiceDescriptor ribon_uefi_app_persistent_metadata_service_descriptor = {
    .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC, .size = sizeof(ribon_uefi_app_persistent_metadata_service_descriptor),
    .abi_version = RIBON_SERVICE_ABI_VERSION, .kind = RIBON_SERVICE_KIND_PERSISTENT_METADATA,
    .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY, .lifetime = RIBON_SERVICE_LIFETIME_BOOT,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION, .id = "service.uefi-app.persistent-metadata",
    .provides = RIBON_CAP_PERSISTENT_METADATA, .architecture_mask = RIBON_ARCH_MASK_X86_64,
    .environment_mask = RIBON_ENV_MASK_UEFI, .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 1024u, .input_budget = 64u, .output_budget = 64u, .deadline_ms = 30000u,
    .operations = &uefi_metadata_operations, .operations_size = sizeof(uefi_metadata_operations),
    .operations_abi = RIBON_SERVICE_ABI_VERSION, .validate_operations = uefi_metadata_validate,
};

/** @brief UEFI application이 제공하는 metadata flush authority다. */
const struct RibonServiceDescriptor ribon_uefi_app_storage_flush_service_descriptor = {
    .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC, .size = sizeof(ribon_uefi_app_storage_flush_service_descriptor),
    .abi_version = RIBON_SERVICE_ABI_VERSION, .kind = RIBON_SERVICE_KIND_STORAGE_FLUSH,
    .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY, .lifetime = RIBON_SERVICE_LIFETIME_BOOT,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION, .id = "service.uefi-app.storage-flush",
    .provides = RIBON_CAP_STORAGE_FLUSH, .architecture_mask = RIBON_ARCH_MASK_X86_64,
    .environment_mask = RIBON_ENV_MASK_UEFI, .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 1024u, .input_budget = 64u, .output_budget = 64u, .deadline_ms = 30000u,
    .operations = &uefi_flush_operations, .operations_size = sizeof(uefi_flush_operations),
    .operations_abi = RIBON_SERVICE_ABI_VERSION, .validate_operations = uefi_flush_validate,
};

/** @brief UEFI application이 제공하는 environment closure authority다. */
const struct RibonServiceDescriptor ribon_uefi_app_environment_quiesce_service_descriptor = {
    .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC, .size = sizeof(ribon_uefi_app_environment_quiesce_service_descriptor),
    .abi_version = RIBON_SERVICE_ABI_VERSION, .kind = RIBON_SERVICE_KIND_ENVIRONMENT_QUIESCE,
    .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY, .lifetime = RIBON_SERVICE_LIFETIME_QUIESCE,
    .phase = RIBON_PLUGIN_PHASE_QUIESCE, .id = "service.uefi-app.environment-quiesce",
    .provides = RIBON_CAP_ENVIRONMENT_QUIESCE, .architecture_mask = RIBON_ARCH_MASK_X86_64,
    .environment_mask = RIBON_ENV_MASK_UEFI, .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 1024u, .input_budget = 64u, .output_budget = 64u, .deadline_ms = 30000u,
    .operations = &uefi_quiesce_operations, .operations_size = sizeof(uefi_quiesce_operations),
    .operations_abi = RIBON_SERVICE_ABI_VERSION, .validate_operations = uefi_quiesce_validate,
};

static const struct RibonServiceDescriptor *const uefi_services[] = {
    &ribon_uefi_app_boot_source_service_descriptor,
    &ribon_uefi_app_environment_quiesce_service_descriptor,
    &ribon_uefi_app_monotonic_timer_service_descriptor,
    &ribon_uefi_app_persistent_metadata_service_descriptor,
    &ribon_uefi_app_storage_flush_service_descriptor,
};

static const struct RibonServiceDirectory uefi_service_directory = {
    .size = sizeof(uefi_service_directory),
    .abi_version = RIBON_SERVICE_DIRECTORY_ABI_VERSION,
    .services = uefi_services,
    .service_count = (uint32_t)(sizeof(uefi_services) / sizeof(uefi_services[0])),
};

/** @brief UEFI native state와 loaded-image volume의 read-only service를 결합한다. */
int ribon_uefi_app_initialize(
    struct RibonUefiAppContext *context,
    EFI_HANDLE image_handle,
    EFI_SYSTEM_TABLE *system_table) {
    EFI_GUID loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID file_system_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_GUID block_io_guid = EFI_BLOCK_IO_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image = 0;
    EFI_STATUS status;
    if (context == 0 || image_handle == 0 || system_table == 0 ||
        system_table->BootServices == 0 || system_table->BootServices->HandleProtocol == 0 ||
        context->raw_memory_map == 0 ||
        context->raw_memory_map_capacity == 0u ||
        context->regions == 0 || context->region_capacity == 0u) {
        return RIBON_UEFI_APP_STATUS_BAD_ARGUMENT;
    }
    context->image_handle = image_handle;
    context->system_table = system_table;
    context->boot_services = system_table->BootServices;
    context->file_system = 0;
    context->root = 0;
    context->block_io = 0;
    for (uint32_t index = 0u; index < RIBON_UEFI_FILE_SOURCE_CAPACITY; ++index) {
        context->files[index] = (struct RibonUefiFileSource){0};
    }
    context->region_count = 0u;
    context->map_key = 0u;
    context->descriptor_size = 0u;
    context->descriptor_version = 0u;
    status = context->boot_services->HandleProtocol(
        image_handle,
        &loaded_image_guid,
        (void **)&loaded_image);
    if (EFI_ERROR(status) || loaded_image == 0 || loaded_image->DeviceHandle == 0) {
        return RIBON_UEFI_APP_STATUS_FIRMWARE_ERROR;
    }
    status = context->boot_services->HandleProtocol(
        loaded_image->DeviceHandle,
        &file_system_guid,
        (void **)&context->file_system);
    if (EFI_ERROR(status) || context->file_system == 0 || context->file_system->OpenVolume == 0 ||
        EFI_ERROR(context->file_system->OpenVolume(context->file_system, &context->root)) ||
        context->root == 0) {
        return RIBON_UEFI_APP_STATUS_FIRMWARE_ERROR;
    }
    status = context->boot_services->HandleProtocol(
        loaded_image->DeviceHandle,
        &block_io_guid,
        (void **)&context->block_io);
    if (EFI_ERROR(status)) {
        context->block_io = 0;
    }
    uefi_context = context;
    uefi_boot_source_operations.context = context;
    uefi_timer_operations.context = context;
    uefi_metadata_operations.context = context;
    uefi_flush_operations.context = context;
    uefi_quiesce_operations.context = context;
    uefi_services_initialized = 1;
    return RIBON_UEFI_APP_STATUS_OK;
}

/** @brief Canonical UEFI file을 one-shot bounded read로 caller-owned bytes에 복사한다. */
int ribon_uefi_app_read_file(
    struct RibonUefiAppContext *context,
    const char *path,
    void *buffer,
    uint64_t buffer_capacity,
    uint64_t *size_out) {
    EFI_FILE_PROTOCOL *file = 0;
    uint64_t size;
    int status;
    if (size_out != 0) {
        *size_out = 0u;
    }
    if (context == 0 || buffer == 0 || buffer_capacity == 0u || size_out == 0) {
        return RIBON_UEFI_APP_STATUS_BAD_ARGUMENT;
    }
    status = uefi_open_file(context, path, &file);
    if (status != RIBON_UEFI_APP_STATUS_OK ||
        uefi_file_size(file, &size) != RIBON_UEFI_APP_STATUS_OK || size > buffer_capacity ||
        uefi_file_read_exact(file, 0u, buffer, size) != RIBON_UEFI_APP_STATUS_OK) {
        if (file != 0 && file->Close != 0) {
            (void)file->Close(file);
        }
        return RIBON_UEFI_APP_STATUS_FIRMWARE_ERROR;
    }
    if (file->Close == 0 || EFI_ERROR(file->Close(file))) {
        return RIBON_UEFI_APP_STATUS_FIRMWARE_ERROR;
    }
    *size_out = size;
    return RIBON_UEFI_APP_STATUS_OK;
}

/** @brief UEFI file handle을 transaction이 소비할 opaque bounded source slot으로 고정한다. */
int ribon_uefi_app_open_boot_source(
    struct RibonUefiAppContext *context,
    const char *path,
    struct RibonBootSource *out) {
    EFI_FILE_PROTOCOL *file = 0;
    uint64_t size;
    int status;
    if (out != 0) {
        *out = (struct RibonBootSource){0};
    }
    if (context == 0 || out == 0) {
        return RIBON_UEFI_APP_STATUS_BAD_ARGUMENT;
    }
    for (uint32_t index = 0u; index < RIBON_UEFI_FILE_SOURCE_CAPACITY; ++index) {
        if (context->files[index].handle != 0) {
            continue;
        }
        status = uefi_open_file(context, path, &file);
        if (status != RIBON_UEFI_APP_STATUS_OK ||
            uefi_file_size(file, &size) != RIBON_UEFI_APP_STATUS_OK) {
            if (file != 0 && file->Close != 0) {
                (void)file->Close(file);
            }
            return RIBON_UEFI_APP_STATUS_FIRMWARE_ERROR;
        }
        context->files[index] = (struct RibonUefiFileSource){
            .handle = file,
            .size = size,
        };
        *out = (struct RibonBootSource){
            .kind = RIBON_BOOT_MEDIA_FILE,
            .source_id = index + 1u,
            .size = size,
            .block_size = 0u,
        };
        return RIBON_UEFI_APP_STATUS_OK;
    }
    return RIBON_UEFI_APP_STATUS_OUT_OF_CAPACITY;
}

/** @brief Canonical file을 page allocation에 exact read해 typed module로 고정한다. */
int ribon_uefi_app_load_boot_module(
    struct RibonUefiAppContext *context,
    const char *path,
    enum RibonBootModuleRole role,
    struct RibonBootModule *out) {
    EFI_FILE_PROTOCOL *file = 0;
    EFI_PHYSICAL_ADDRESS allocation = 0u;
    uint64_t size = 0u;
    UINTN pages;
    EFI_STATUS allocation_status;
    int status;
    if (out != 0) {
        *out = (struct RibonBootModule){0};
    }
    if (context == 0 || context->boot_services == 0 || path == 0 || out == 0 ||
        (role != RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE &&
         role != RIBON_BOOT_MODULE_ROLE_AUXILIARY)) {
        return RIBON_UEFI_APP_STATUS_BAD_ARGUMENT;
    }
    status = uefi_open_file(context, path, &file);
    if (status != RIBON_UEFI_APP_STATUS_OK ||
        uefi_file_size(file, &size) != RIBON_UEFI_APP_STATUS_OK ||
        size == 0u || size > RIBON_UEFI_BOOT_MODULE_MAX_SIZE ||
        size > UINT64_MAX - 4095u) {
        if (file != 0 && file->Close != 0) {
            (void)file->Close(file);
        }
        return RIBON_UEFI_APP_STATUS_FIRMWARE_ERROR;
    }
    pages = (UINTN)((size + 4095u) / 4096u);
    allocation_status = context->boot_services->AllocatePages(
        AllocateAnyPages,
        EfiLoaderData,
        pages,
        &allocation);
    if (EFI_ERROR(allocation_status) || allocation == 0u) {
        if (file->Close != 0) {
            (void)file->Close(file);
        }
        return RIBON_UEFI_APP_STATUS_FIRMWARE_ERROR;
    }
    memset((void *)(uintptr_t)allocation, 0, pages * 4096u);
    status = uefi_file_read_exact(
        file,
        0u,
        (void *)(uintptr_t)allocation,
        size);
    if (file->Close == 0 || EFI_ERROR(file->Close(file))) {
        status = RIBON_UEFI_APP_STATUS_FIRMWARE_ERROR;
    }
    if (status != RIBON_UEFI_APP_STATUS_OK) {
        if (context->boot_services->FreePages != 0) {
            (void)context->boot_services->FreePages(allocation, pages);
        }
        return RIBON_UEFI_APP_STATUS_FIRMWARE_ERROR;
    }
    *out = (struct RibonBootModule){
        .name = path,
        .physical_address = allocation,
        .size = size,
        .role = role,
    };
    return RIBON_UEFI_APP_STATUS_OK;
}

/** @brief Captured UEFI Block I/O를 immutable generic read-only block descriptor로 반환한다. */
int ribon_uefi_app_read_only_block_device(
    struct RibonUefiAppContext *context,
    struct RibonReadOnlyBlockDevice *out) {
    EFI_BLOCK_IO_MEDIA *media;
    if (out != 0) {
        *out = (struct RibonReadOnlyBlockDevice){0};
    }
    if (context == 0 || out == 0 || context->boot_services == 0 || context->block_io == 0 ||
        context->block_io->Media == 0) {
        return RIBON_UEFI_APP_STATUS_BAD_ARGUMENT;
    }
    media = context->block_io->Media;
    if (!media->MediaPresent || media->BlockSize < 512u || media->BlockSize > 4096u ||
        (media->BlockSize & (media->BlockSize - 1u)) != 0u || media->LastBlock == UINT64_MAX) {
        return RIBON_UEFI_APP_STATUS_FIRMWARE_ERROR;
    }
    *out = (struct RibonReadOnlyBlockDevice){
        .size = sizeof(*out),
        .abi_version = RIBON_READ_ONLY_BLOCK_DEVICE_ABI_VERSION,
        .logical_block_bytes = media->BlockSize,
        .max_read_blocks = 128u,
        .block_count = media->LastBlock + 1u,
        .context = context,
        .read = uefi_block_read,
    };
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
    out->command_line.text = "environment=uefi-app";
    out->command_line.length = 20u;
    out->flags =
        RIBON_BOOT_ENV_HAS_MEMORY_MAP |
        RIBON_BOOT_ENV_HAS_RAW_MEMORY_MAP |
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
    const struct RibonBootEnvironmentPersistentInputs *persistent_inputs,
    RibonUefiRefreshPlanFn refresh,
    void *refresh_context) {
    if (context == 0 || environment == 0 || persistent_inputs == 0 ||
        refresh == 0 ||
        context->boot_services == 0) {
        return RIBON_UEFI_APP_STATUS_BAD_ARGUMENT;
    }
    for (uint32_t attempt = 0u; attempt < RIBON_UEFI_EXIT_ATTEMPTS; ++attempt) {
        EFI_STATUS status;
        int capture_status = ribon_uefi_app_capture_environment(context, environment);
        if (capture_status != RIBON_UEFI_APP_STATUS_OK) {
            return capture_status;
        }
        if (!ribon_boot_environment_apply_persistent_inputs(
                environment,
                persistent_inputs)) {
            return RIBON_UEFI_APP_STATUS_PAYLOAD_ERROR;
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
        RIBON_CAP_MONOTONIC_TIMER |
        RIBON_CAP_PERSISTENT_METADATA |
        RIBON_CAP_STORAGE_FLUSH |
        RIBON_CAP_ENVIRONMENT_QUIESCE,
    .requires = RIBON_CAP_ARCHITECTURE,
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
