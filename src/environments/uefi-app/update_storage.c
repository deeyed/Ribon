#include "update_storage.h"

#include <Ribon/core/capability.h>
#include <Ribon/plugin/manifest.h>

#include "../../security/sha256.h"

#include <string.h>

#define UPDATE_ANCHOR_HASH_OFFSET 640u
#define UPDATE_ANCHOR_CRC_OFFSET 672u
#define UPDATE_ANCHOR_HASHED_BYTES 640u
#define UPDATE_ANCHOR_CRC_BYTES 672u
#define UPDATE_MAXIMUM_TRANSFER UINT64_C(65536)

static const uint8_t update_anchor_magic[32] =
    "RIBON-UEFI-UPDATE-MEDIA-V1";
static int service_metadata_read(
    void *opaque, uint64_t offset, void *buffer, uint64_t size);
static int service_metadata_write(
    void *opaque, uint64_t offset, const void *buffer, uint64_t size);
static int service_flush(
    void *opaque, uint32_t domain, uint64_t deadline_ticks);
static struct RibonUefiUpdateStorageContext *selected_context;
static struct RibonUefiUpdateStorageContext discovery_probe;
static _Alignas(4096) uint8_t metadata_copies[
    2u * RIBON_UPDATE_SLOT_METADATA_BYTES];
static struct RibonInactiveSlotStorageServiceOperations writer_operations = {
    .size = sizeof(writer_operations),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
};
static struct RibonPersistentMetadataServiceOperations metadata_operations = {
    .size = sizeof(metadata_operations),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .read = service_metadata_read,
    .write = service_metadata_write,
};
static struct RibonStorageFlushServiceOperations flush_operations = {
    .size = sizeof(flush_operations),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .flush = service_flush,
};

static uint16_t load_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t load_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t load_u64(const uint8_t *bytes)
{
    return (uint64_t)load_u32(bytes) | ((uint64_t)load_u32(bytes + 4u) << 32);
}

static int bytes_zero(const void *pointer, size_t size)
{
    const uint8_t *bytes = pointer;
    uint8_t value = 0u;
    size_t index;
    for (index = 0u; index < size; ++index) {
        value |= bytes[index];
    }
    return value == 0u;
}

static int bytes_equal(const void *left_pointer, const void *right_pointer, size_t size)
{
    const uint8_t *left = left_pointer;
    const uint8_t *right = right_pointer;
    uint8_t difference = 0u;
    size_t index;
    for (index = 0u; index < size; ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == 0u;
}

static uint32_t crc32c(const uint8_t *bytes, size_t size)
{
    uint32_t crc = UINT32_MAX;
    size_t index;
    uint32_t bit;
    for (index = 0u; index < size; ++index) {
        crc ^= bytes[index];
        for (bit = 0u; bit < 8u; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (UINT32_C(0x82f63b78) & mask);
        }
    }
    return ~crc;
}

static int block_geometry_valid(EFI_BLOCK_IO_PROTOCOL *block_io, uint64_t capacity)
{
    EFI_BLOCK_IO_MEDIA *media;
    uint64_t blocks;
    if (block_io == NULL || block_io->Media == NULL ||
        block_io->ReadBlocks == NULL || block_io->WriteBlocks == NULL ||
        block_io->FlushBlocks == NULL) {
        return 0;
    }
    media = block_io->Media;
    if (!media->MediaPresent || media->LogicalPartition || media->ReadOnly ||
        media->BlockSize != RIBON_UEFI_UPDATE_BLOCK_BYTES ||
        (media->IoAlign != 0u &&
            (media->IoAlign > 4096u ||
             (media->IoAlign & (media->IoAlign - 1u)) != 0u)) ||
        media->LastBlock == UINT64_MAX) {
        return 0;
    }
    blocks = media->LastBlock + 1u;
    return blocks <= UINT64_MAX / media->BlockSize &&
        blocks * media->BlockSize == capacity;
}

static int block_read_exact(
    EFI_BLOCK_IO_PROTOCOL *block_io, uint64_t offset, void *buffer, uint64_t size)
{
    EFI_BLOCK_IO_MEDIA *media;
    if (block_io == NULL || buffer == NULL || size == 0u ||
        size > (uint64_t)(UINTN)-1 || (offset % RIBON_UEFI_UPDATE_BLOCK_BYTES) != 0u ||
        (size % RIBON_UEFI_UPDATE_BLOCK_BYTES) != 0u) {
        return 0;
    }
    media = block_io->Media;
    return media != NULL && !EFI_ERROR(block_io->ReadBlocks(
        block_io, media->MediaId, offset / media->BlockSize, (UINTN)size, buffer));
}

static int anchor_open(
    struct RibonUefiUpdateStorageContext *context,
    EFI_BLOCK_IO_PROTOCOL *block_io,
    const struct RibonUpdateStorageProductBinding *binding)
{
    uint8_t digest[32];
    uint64_t capacity;
    if (!block_read_exact(block_io, RIBON_UEFI_UPDATE_MEDIA_ANCHOR_OFFSET,
            context->anchor, sizeof(context->anchor)) ||
        !bytes_equal(context->anchor, update_anchor_magic, sizeof(update_anchor_magic)) ||
        load_u16(context->anchor + 32u) != RIBON_UPDATE_STORAGE_ABI_VERSION ||
        load_u16(context->anchor + 34u) != RIBON_UEFI_UPDATE_MEDIA_ANCHOR_BYTES ||
        load_u32(context->anchor + 36u) != RIBON_UEFI_UPDATE_MEDIA_ANCHOR_BYTES ||
        load_u64(context->anchor + 40u) != RIBON_UEFI_UPDATE_MEDIA_ANCHOR_OFFSET ||
        load_u32(context->anchor + 56u) != RIBON_UEFI_UPDATE_BLOCK_BYTES ||
        load_u32(context->anchor + 60u) != 0u ||
        !bytes_zero(context->anchor + UPDATE_ANCHOR_CRC_OFFSET + 4u,
            sizeof(context->anchor) - UPDATE_ANCHOR_CRC_OFFSET - 4u)) {
        return 0;
    }
    capacity = load_u64(context->anchor + 48u);
    if (!block_geometry_valid(block_io, capacity) ||
        !bytes_equal(context->anchor + 64u, binding->media_identity_digest, 32u) ||
        !bytes_equal(context->anchor + 96u, binding->layout_digest, 32u) ||
        crc32c(context->anchor, UPDATE_ANCHOR_CRC_BYTES) !=
            load_u32(context->anchor + UPDATE_ANCHOR_CRC_OFFSET)) {
        return 0;
    }
    ribon_security_sha256(context->anchor, UPDATE_ANCHOR_HASHED_BYTES, digest);
    if (!bytes_equal(digest, context->anchor + UPDATE_ANCHOR_HASH_OFFSET, 32u) ||
        ribon_update_layout_identity_open(context->anchor + 128u,
            RIBON_UPDATE_LAYOUT_IDENTITY_BYTES, &context->layout) !=
            RIBON_UPDATE_STORAGE_STATUS_OK ||
        !bytes_equal(context->layout.identity_digest, binding->layout_digest, 32u) ||
        context->layout.media_capacity_bytes != capacity) {
        memset(&context->layout, 0, sizeof(context->layout));
        return 0;
    }
    return 1;
}

static int provider_read(void *opaque, uint64_t offset, void *buffer,
    uint64_t size, uint64_t *transferred, uint64_t deadline_ticks)
{
    struct RibonUefiUpdateStorageContext *context = opaque;
    (void)deadline_ticks;
    if (transferred != NULL) {
        *transferred = 0u;
    }
    if (context == NULL || transferred == NULL ||
        !block_read_exact(context->block_io, offset, buffer, size)) {
        return -1;
    }
    *transferred = size;
    return 0;
}

static int provider_write(void *opaque, uint64_t offset, const void *buffer,
    uint64_t size, uint64_t *transferred, uint64_t deadline_ticks)
{
    struct RibonUefiUpdateStorageContext *context = opaque;
    EFI_BLOCK_IO_MEDIA *media;
    (void)deadline_ticks;
    if (transferred != NULL) {
        *transferred = 0u;
    }
    if (context == NULL || context->block_io == NULL ||
        context->block_io->Media == NULL || context->block_io->WriteBlocks == NULL ||
        buffer == NULL || transferred == NULL || size == 0u ||
        size > (uint64_t)(UINTN)-1 ||
        (offset % RIBON_UEFI_UPDATE_BLOCK_BYTES) != 0u ||
        (size % RIBON_UEFI_UPDATE_BLOCK_BYTES) != 0u) {
        return -1;
    }
    media = context->block_io->Media;
    if (EFI_ERROR(context->block_io->WriteBlocks(context->block_io,
            media->MediaId, offset / media->BlockSize, (UINTN)size,
            (VOID *)(uintptr_t)buffer))) {
        return -1;
    }
    *transferred = size;
    return 0;
}

static int provider_erase(void *opaque, uint64_t offset, uint64_t size,
    uint64_t deadline_ticks)
{
    struct RibonUefiUpdateStorageContext *context = opaque;
    uint64_t cursor = 0u;
    while (context != NULL && cursor < size) {
        uint64_t transferred = 0u;
        if (provider_write(context, offset + cursor, context->zero_block,
                sizeof(context->zero_block), &transferred, deadline_ticks) != 0 ||
            transferred != sizeof(context->zero_block)) {
            return -1;
        }
        cursor += sizeof(context->zero_block);
    }
    return cursor == size ? 0 : -1;
}

static int provider_flush(void *opaque, uint64_t deadline_ticks)
{
    struct RibonUefiUpdateStorageContext *context = opaque;
    (void)deadline_ticks;
    return context == NULL || context->block_io == NULL ||
        context->block_io->FlushBlocks == NULL ||
        EFI_ERROR(context->block_io->FlushBlocks(context->block_io)) ? -1 : 0;
}

int ribon_uefi_update_storage_discover(
    struct RibonUefiUpdateStorageContext *context,
    EFI_BOOT_SERVICES *boot_services,
    const struct RibonUpdateStorageProductBinding *binding)
{
    EFI_GUID guid = EFI_BLOCK_IO_PROTOCOL_GUID;
    EFI_HANDLE handles[RIBON_UEFI_UPDATE_HANDLE_CAPACITY];
    UINTN bytes = sizeof(handles);
    UINTN count;
    UINTN index;
    UINTN matches = 0u;
    EFI_STATUS status;
    selected_context = NULL;
    writer_operations.provider = NULL;
    metadata_operations.context = NULL;
    flush_operations.context = NULL;
    if (context == NULL || boot_services == NULL || boot_services->LocateHandle == NULL ||
        boot_services->HandleProtocol == NULL ||
        !ribon_update_storage_product_binding_is_valid(binding) ||
        binding->provider_class != RIBON_UPDATE_STORAGE_PROVIDER_CLASS_FIRMWARE) {
        return RIBON_UEFI_UPDATE_STORAGE_BAD_ARGUMENT;
    }
    memset(context, 0, sizeof(*context));
    status = boot_services->LocateHandle(ByProtocol, &guid, NULL, &bytes, handles);
    if (status == EFI_BUFFER_TOO_SMALL || bytes > sizeof(handles)) {
        return RIBON_UEFI_UPDATE_STORAGE_AMBIGUOUS;
    }
    if (EFI_ERROR(status)) {
        return status == EFI_NOT_FOUND ? RIBON_UEFI_UPDATE_STORAGE_NOT_FOUND :
            RIBON_UEFI_UPDATE_STORAGE_FIRMWARE_ERROR;
    }
    count = bytes / sizeof(handles[0]);
    for (index = 0u; index < count; ++index) {
        EFI_BLOCK_IO_PROTOCOL *candidate = NULL;
        if (EFI_ERROR(boot_services->HandleProtocol(
                handles[index], &guid, (VOID **)&candidate)) || candidate == NULL) {
            continue;
        }
        memset(&discovery_probe, 0, sizeof(discovery_probe));
        if (anchor_open(&discovery_probe, candidate, binding)) {
            if (matches == 0u) {
                context->block_io = candidate;
                context->layout = discovery_probe.layout;
                memcpy(context->anchor, discovery_probe.anchor,
                    sizeof(context->anchor));
            }
            ++matches;
        }
    }
    if (matches != 1u) {
        memset(context, 0, sizeof(*context));
        return matches == 0u ? RIBON_UEFI_UPDATE_STORAGE_NOT_FOUND :
            RIBON_UEFI_UPDATE_STORAGE_AMBIGUOUS;
    }
    context->boot_services = boot_services;
    context->binding = binding;
    context->provider = (struct RibonUpdateStorageProvider){
        .size = sizeof(context->provider),
        .abi_version = RIBON_UPDATE_STORAGE_ABI_VERSION,
        .capabilities = RIBON_UPDATE_STORAGE_CAP_ALL,
        .capacity_bytes = context->layout.media_capacity_bytes,
        .read_alignment = RIBON_UEFI_UPDATE_BLOCK_BYTES,
        .write_alignment = RIBON_UEFI_UPDATE_BLOCK_BYTES,
        .erase_alignment = RIBON_UEFI_UPDATE_BLOCK_BYTES,
        .maximum_transfer_bytes = UPDATE_MAXIMUM_TRANSFER,
        .context = context,
        .read = provider_read,
        .write = provider_write,
        .erase = provider_erase,
        .flush = provider_flush,
    };
    memcpy(context->provider.media_identity_digest,
        binding->media_identity_digest, 32u);
    if (!ribon_update_storage_provider_is_valid(&context->provider)) {
        memset(context, 0, sizeof(*context));
        return RIBON_UEFI_UPDATE_STORAGE_MEDIA_MISMATCH;
    }
    selected_context = context;
    writer_operations.provider = &context->provider;
    metadata_operations.context = context;
    flush_operations.context = context;
    return RIBON_UEFI_UPDATE_STORAGE_OK;
}

int ribon_uefi_update_storage_read_metadata(
    struct RibonUefiUpdateStorageContext *context,
    struct RibonUpdateSlotMetadata *metadata)
{
    uint64_t transferred = 0u;
    const struct RibonUpdateLayoutRegion *region;
    if (context == NULL || metadata == NULL || context != selected_context) {
        return RIBON_UEFI_UPDATE_STORAGE_BAD_ARGUMENT;
    }
    region = &context->layout.regions[RIBON_UPDATE_REGION_SLOT_METADATA - 1u];
    if (provider_read(context, region->offset, metadata_copies,
            sizeof(metadata_copies), &transferred, 1u) != 0 ||
        transferred != sizeof(metadata_copies) ||
        !bytes_equal(metadata_copies,
            metadata_copies + RIBON_UPDATE_SLOT_METADATA_BYTES,
            RIBON_UPDATE_SLOT_METADATA_BYTES) ||
        ribon_update_slot_metadata_open(metadata_copies,
            RIBON_UPDATE_SLOT_METADATA_BYTES, metadata) !=
            RIBON_UPDATE_STORAGE_STATUS_OK) {
        return RIBON_UEFI_UPDATE_STORAGE_METADATA;
    }
    return RIBON_UEFI_UPDATE_STORAGE_OK;
}

int ribon_uefi_update_storage_write_metadata(
    struct RibonUefiUpdateStorageContext *context,
    const struct RibonUpdateSlotMetadata *metadata)
{
    uint64_t transferred = 0u;
    const struct RibonUpdateLayoutRegion *region;
    if (context == NULL || metadata == NULL || context != selected_context ||
        ribon_update_slot_metadata_encode(metadata, metadata_copies) !=
            RIBON_UPDATE_STORAGE_STATUS_OK) {
        return RIBON_UEFI_UPDATE_STORAGE_BAD_ARGUMENT;
    }
    memcpy(metadata_copies + RIBON_UPDATE_SLOT_METADATA_BYTES, metadata_copies,
        RIBON_UPDATE_SLOT_METADATA_BYTES);
    region = &context->layout.regions[RIBON_UPDATE_REGION_SLOT_METADATA - 1u];
    if (provider_write(context, region->offset, metadata_copies,
            sizeof(metadata_copies), &transferred, 1u) != 0 ||
        transferred != sizeof(metadata_copies) ||
        provider_flush(context, 1u) != 0) {
        return RIBON_UEFI_UPDATE_STORAGE_IO;
    }
    return RIBON_UEFI_UPDATE_STORAGE_OK;
}

static int service_metadata_read(void *opaque, uint64_t offset, void *buffer, uint64_t size)
{
    struct RibonUefiUpdateStorageContext *context = opaque;
    uint64_t transferred = 0u;
    const struct RibonUpdateLayoutRegion *region;
    if (context == NULL || buffer == NULL || size == 0u) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    region = &context->layout.regions[RIBON_UPDATE_REGION_SLOT_METADATA - 1u];
    return offset <= region->length && size <= region->length - offset &&
        provider_read(context, region->offset + offset, buffer, size,
            &transferred, 1u) == 0 && transferred == size ?
        RIBON_SERVICE_STATUS_OK : RIBON_SERVICE_STATUS_IO;
}

static int service_metadata_write(void *opaque, uint64_t offset,
    const void *buffer, uint64_t size)
{
    struct RibonUefiUpdateStorageContext *context = opaque;
    uint64_t transferred = 0u;
    const struct RibonUpdateLayoutRegion *region;
    if (context == NULL || buffer == NULL || size == 0u) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    region = &context->layout.regions[RIBON_UPDATE_REGION_SLOT_METADATA - 1u];
    return offset <= region->length && size <= region->length - offset &&
        provider_write(context, region->offset + offset, buffer, size,
            &transferred, 1u) == 0 && transferred == size ?
        RIBON_SERVICE_STATUS_OK : RIBON_SERVICE_STATUS_IO;
}

static int service_flush(void *opaque, uint32_t domain, uint64_t deadline_ticks)
{
    (void)domain;
    return provider_flush(opaque, deadline_ticks) == 0 ?
        RIBON_SERVICE_STATUS_OK : RIBON_SERVICE_STATUS_IO;
}

static int metadata_operations_valid(const struct RibonServiceDescriptor *descriptor)
{
    const struct RibonPersistentMetadataServiceOperations *operations =
        descriptor == NULL ? NULL : descriptor->operations;
    return operations != NULL && operations->size == sizeof(*operations) &&
        operations->abi_version == RIBON_SERVICE_ABI_VERSION &&
        operations->context != NULL && operations->read != NULL &&
        operations->write != NULL;
}

static int flush_operations_valid(const struct RibonServiceDescriptor *descriptor)
{
    const struct RibonStorageFlushServiceOperations *operations =
        descriptor == NULL ? NULL : descriptor->operations;
    return operations != NULL && operations->size == sizeof(*operations) &&
        operations->abi_version == RIBON_SERVICE_ABI_VERSION &&
        operations->context != NULL && operations->flush != NULL;
}

const struct RibonServiceDescriptor
ribon_uefi_update_inactive_slot_storage_service_descriptor = {
    .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC,
    .size = sizeof(struct RibonServiceDescriptor),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .kind = RIBON_SERVICE_KIND_INACTIVE_SLOT_STORAGE,
    .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY,
    .lifetime = RIBON_SERVICE_LIFETIME_BOOT,
    .phase = RIBON_PLUGIN_PHASE_BOOT,
    .id = "service.uefi-update.inactive-slot-storage",
    .provides = RIBON_CAP_INACTIVE_SLOT_WRITE | RIBON_CAP_INACTIVE_SLOT_ERASE,
    .architecture_mask = RIBON_ARCH_MASK_X86_64,
    .environment_mask = RIBON_ENV_MASK_UEFI,
    .mode_mask = RIBON_MODE_MASK(RIBON_MODE_RECOVERY),
    .arena_budget = 1u,
    .input_budget = UPDATE_MAXIMUM_TRANSFER,
    .output_budget = UPDATE_MAXIMUM_TRANSFER,
    .deadline_ms = 30000u,
    .operations = &writer_operations,
    .operations_size = sizeof(writer_operations),
    .operations_abi = RIBON_SERVICE_ABI_VERSION,
    .validate_operations = ribon_inactive_slot_storage_service_operations_are_valid,
};

const struct RibonServiceDescriptor
ribon_uefi_update_persistent_metadata_service_descriptor = {
    .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC,
    .size = sizeof(struct RibonServiceDescriptor),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .kind = RIBON_SERVICE_KIND_PERSISTENT_METADATA,
    .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY,
    .lifetime = RIBON_SERVICE_LIFETIME_BOOT,
    .phase = RIBON_PLUGIN_PHASE_BOOT,
    .id = "service.uefi-update.persistent-metadata",
    .provides = RIBON_CAP_PERSISTENT_METADATA,
    .architecture_mask = RIBON_ARCH_MASK_X86_64,
    .environment_mask = RIBON_ENV_MASK_UEFI,
    .mode_mask = RIBON_MODE_MASK(RIBON_MODE_RECOVERY),
    .arena_budget = 1u,
    .input_budget = 4096u,
    .output_budget = 4096u,
    .deadline_ms = 30000u,
    .operations = &metadata_operations,
    .operations_size = sizeof(metadata_operations),
    .operations_abi = RIBON_SERVICE_ABI_VERSION,
    .validate_operations = metadata_operations_valid,
};

const struct RibonServiceDescriptor
ribon_uefi_update_storage_flush_service_descriptor = {
    .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC,
    .size = sizeof(struct RibonServiceDescriptor),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .kind = RIBON_SERVICE_KIND_STORAGE_FLUSH,
    .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY,
    .lifetime = RIBON_SERVICE_LIFETIME_BOOT,
    .phase = RIBON_PLUGIN_PHASE_BOOT,
    .id = "service.uefi-update.storage-flush",
    .provides = RIBON_CAP_STORAGE_FLUSH,
    .architecture_mask = RIBON_ARCH_MASK_X86_64,
    .environment_mask = RIBON_ENV_MASK_UEFI,
    .mode_mask = RIBON_MODE_MASK(RIBON_MODE_RECOVERY),
    .arena_budget = 1u,
    .input_budget = 1u,
    .output_budget = 1u,
    .deadline_ms = 30000u,
    .operations = &flush_operations,
    .operations_size = sizeof(flush_operations),
    .operations_abi = RIBON_SERVICE_ABI_VERSION,
    .validate_operations = flush_operations_valid,
};
