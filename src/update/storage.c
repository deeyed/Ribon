#include <Ribon/update/storage.h>
#include <Ribon/service/directory.h>

#include "../security/sha256.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define LAYOUT_HEADER_BYTES 128u
#define LAYOUT_REGION_BYTES 24u
#define LAYOUT_REGION_CAPACITY 16u
#define METADATA_HEADER_BYTES 64u
#define METADATA_SLOT_BYTES 160u
#define METADATA_BODY_BYTES 384u
#define METADATA_DIGEST_OFFSET 384u
#define METADATA_CHECKSUM_OFFSET 416u
#define METADATA_RESERVED_OFFSET 420u
#define METADATA_MAX_BOOT_ATTEMPTS 32u

static const uint8_t ribon_update_layout_magic[32] =
    "RIBON-UPDATE-LAYOUT-V1";
static const uint8_t ribon_update_metadata_magic[32] =
    "RIBON-SLOT-METADATA-V1";

/** @brief Canonical little-endian byte view에서 u16을 읽는다. */
static uint16_t
load_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

/** @brief Canonical little-endian byte view에서 u32를 읽는다. */
static uint32_t
load_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

/** @brief Canonical little-endian byte view에서 u64를 읽는다. */
static uint64_t
load_u64(const uint8_t *bytes)
{
    return (uint64_t)load_u32(bytes) |
        ((uint64_t)load_u32(bytes + 4u) << 32);
}

/** @brief u16을 canonical little-endian byte 순서로 쓴다. */
static void
store_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

/** @brief u32를 canonical little-endian byte 순서로 쓴다. */
static void
store_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

/** @brief u64를 canonical little-endian byte 순서로 쓴다. */
static void
store_u64(uint8_t *bytes, uint64_t value)
{
    store_u32(bytes, (uint32_t)value);
    store_u32(bytes + 4u, (uint32_t)(value >> 32));
}

/** @brief Byte view가 전부 0인지 allocation 없이 검사한다. */
static int
bytes_zero(const void *pointer, size_t size)
{
    const uint8_t *bytes = pointer;
    uint8_t value = 0u;
    size_t index;

    for (index = 0u; index < size; ++index) {
        value |= bytes[index];
    }
    return value == 0u;
}

/** @brief 두 byte view를 data-independent loop로 비교한다. */
static int
bytes_equal(const void *left_pointer, const void *right_pointer, size_t size)
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

/** @brief 값이 non-zero power of two인지 검사한다. */
static int
power_of_two(uint64_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

/** @brief u64 덧셈을 overflow 없이 수행한다. */
static int
add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (result == NULL || left > UINT64_MAX - right) {
        return 0;
    }
    *result = left + right;
    return 1;
}

/** @brief u64 값을 power-of-two alignment에 overflow 없이 올림한다. */
static int
align_up_u64(uint64_t value, uint64_t alignment, uint64_t *result)
{
    uint64_t added;

    if (!power_of_two(alignment) ||
        !add_u64(value, alignment - 1u, &added)) {
        return 0;
    }
    *result = added & ~(alignment - 1u);
    return 1;
}

/** @brief Wire object의 torn/corrupt byte 검출용 CRC32C를 계산한다. */
static uint32_t
crc32c(const uint8_t *bytes, size_t size)
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

/** @brief Provider callback 결과를 stable storage status로 축약한다. */
static int
provider_status(int status)
{
    return status == 0 ? RIBON_UPDATE_STORAGE_STATUS_OK :
        RIBON_UPDATE_STORAGE_STATUS_IO;
}

/** @brief Provider ABI, capability, geometry와 callbacks를 fail-closed 검사한다. */
int
ribon_update_storage_provider_is_valid(
    const struct RibonUpdateStorageProvider *provider)
{
    if (provider == NULL || provider->size != sizeof(*provider) ||
        provider->abi_version != RIBON_UPDATE_STORAGE_ABI_VERSION ||
        provider->flags != 0u ||
        provider->capabilities != RIBON_UPDATE_STORAGE_CAP_ALL ||
        provider->capacity_bytes == 0u ||
        !power_of_two(provider->read_alignment) ||
        !power_of_two(provider->write_alignment) ||
        !power_of_two(provider->erase_alignment) ||
        provider->maximum_transfer_bytes == 0u ||
        provider->maximum_transfer_bytes < provider->read_alignment ||
        provider->maximum_transfer_bytes < provider->write_alignment ||
        provider->maximum_transfer_bytes < provider->erase_alignment ||
        bytes_zero(provider->media_identity_digest,
            sizeof(provider->media_identity_digest)) ||
        provider->context == NULL || provider->read == NULL ||
        provider->write == NULL || provider->erase == NULL ||
        provider->flush == NULL ||
        !bytes_zero(provider->reserved, sizeof(provider->reserved))) {
        return 0;
    }
    return 1;
}

/** @brief Layout에 aligned canonical region 하나를 append한다. */
static int
layout_append(
    struct RibonUpdateLayout *layout,
    uint32_t index,
    enum RibonUpdateLayoutRegionKind kind,
    uint64_t requested_length,
    uint64_t *cursor)
{
    uint64_t length;
    uint64_t end;

    if (layout == NULL || cursor == NULL ||
        index >= RIBON_UPDATE_LAYOUT_REGION_COUNT || requested_length == 0u ||
        !align_up_u64(requested_length, layout->allocation_alignment, &length) ||
        !add_u64(*cursor, length, &end) ||
        end > layout->media_capacity_bytes) {
        return RIBON_UPDATE_STORAGE_STATUS_CAPACITY;
    }
    layout->regions[index].kind = kind;
    layout->regions[index].offset = *cursor;
    layout->regions[index].length = length;
    *cursor = end;
    return RIBON_UPDATE_STORAGE_STATUS_OK;
}

/** @brief Layout native view의 structural invariants를 검사한다. */
static int
layout_is_structurally_valid(const struct RibonUpdateLayout *layout)
{
    uint64_t cursor = 0u;
    uint32_t index;

    if (layout == NULL || layout->size != sizeof(*layout) ||
        layout->abi_version != RIBON_UPDATE_STORAGE_ABI_VERSION ||
        layout->flags != 0u ||
        layout->region_count != RIBON_UPDATE_LAYOUT_REGION_COUNT ||
        layout->media_capacity_bytes == 0u ||
        !power_of_two(layout->allocation_alignment) ||
        !bytes_zero(layout->reserved, sizeof(layout->reserved))) {
        return 0;
    }
    for (index = 0u; index < layout->region_count; ++index) {
        const struct RibonUpdateLayoutRegion *region = &layout->regions[index];
        uint64_t end;

        if (region->kind != (enum RibonUpdateLayoutRegionKind)(index + 1u) ||
            region->flags != 0u || region->length == 0u ||
            region->offset != cursor ||
            (region->offset & (layout->allocation_alignment - 1u)) != 0u ||
            (region->length & (layout->allocation_alignment - 1u)) != 0u ||
            !add_u64(region->offset, region->length, &end) ||
            end > layout->media_capacity_bytes) {
            return 0;
        }
        cursor = end;
    }
    return cursor == layout->media_capacity_bytes &&
        layout->regions[RIBON_UPDATE_REGION_SLOT_METADATA - 1u].length >=
            2u * RIBON_UPDATE_SLOT_METADATA_BYTES &&
        layout->regions[RIBON_UPDATE_REGION_SLOT_A - 1u].length ==
            layout->regions[RIBON_UPDATE_REGION_SLOT_B - 1u].length;
}

/** @brief Layout을 canonical 512-byte little-endian identity로 직렬화한다. */
int
ribon_update_layout_identity_encode(
    const struct RibonUpdateLayout *layout,
    uint8_t output[RIBON_UPDATE_LAYOUT_IDENTITY_BYTES])
{
    uint32_t index;

    if (output != NULL) {
        memset(output, 0, RIBON_UPDATE_LAYOUT_IDENTITY_BYTES);
    }
    if (output == NULL || !layout_is_structurally_valid(layout)) {
        return RIBON_UPDATE_STORAGE_STATUS_INVALID_ARGUMENT;
    }
    memcpy(output, ribon_update_layout_magic, sizeof(ribon_update_layout_magic));
    store_u16(output + 32u, RIBON_UPDATE_STORAGE_ABI_VERSION);
    store_u16(output + 34u, LAYOUT_HEADER_BYTES);
    store_u32(output + 36u, RIBON_UPDATE_LAYOUT_IDENTITY_BYTES);
    store_u64(output + 40u, layout->media_capacity_bytes);
    store_u64(output + 48u, layout->allocation_alignment);
    store_u32(output + 56u, layout->region_count);
    for (index = 0u; index < layout->region_count; ++index) {
        uint8_t *row = output + LAYOUT_HEADER_BYTES +
            (size_t)index * LAYOUT_REGION_BYTES;
        store_u32(row, (uint32_t)layout->regions[index].kind);
        store_u32(row + 4u, layout->regions[index].flags);
        store_u64(row + 8u, layout->regions[index].offset);
        store_u64(row + 16u, layout->regions[index].length);
    }
    return RIBON_UPDATE_STORAGE_STATUS_OK;
}

/** @brief Source-neutral input에서 deterministic A/B layout을 계산한다. */
int
ribon_update_layout_calculate(
    const struct RibonUpdateLayoutInput *input,
    struct RibonUpdateLayout *layout)
{
    uint8_t identity[RIBON_UPDATE_LAYOUT_IDENTITY_BYTES];
    uint64_t requested[RIBON_UPDATE_LAYOUT_REGION_COUNT];
    uint64_t aligned_prefix = 0u;
    uint64_t aligned_trailing_minimum = 0u;
    uint64_t cursor = 0u;
    uint64_t trailing;
    uint32_t index;
    int status;

    if (layout != NULL) {
        memset(layout, 0, sizeof(*layout));
    }
    if (input == NULL || layout == NULL || input->size != sizeof(*input) ||
        input->abi_version != RIBON_UPDATE_STORAGE_ABI_VERSION ||
        input->flags != 0u || input->reserved0 != 0u ||
        input->media_capacity_bytes == 0u ||
        !power_of_two(input->allocation_alignment) ||
        input->guard_gap_bytes == 0u || input->bootloader_bytes == 0u ||
        input->immutable_recovery_bytes == 0u || input->slot_payload_bytes == 0u ||
        input->slot_metadata_bytes < 2u * RIBON_UPDATE_SLOT_METADATA_BYTES ||
        input->update_journal_bytes == 0u ||
        input->minimum_trailing_reserved_bytes == 0u ||
        !bytes_zero(input->reserved, sizeof(input->reserved))) {
        return RIBON_UPDATE_STORAGE_STATUS_INVALID_ARGUMENT;
    }

    requested[0] = input->bootloader_bytes;
    requested[1] = input->guard_gap_bytes;
    requested[2] = input->immutable_recovery_bytes;
    requested[3] = input->guard_gap_bytes;
    requested[4] = input->slot_payload_bytes;
    requested[5] = input->guard_gap_bytes;
    requested[6] = input->slot_payload_bytes;
    requested[7] = input->guard_gap_bytes;
    requested[8] = input->slot_metadata_bytes;
    requested[9] = input->update_journal_bytes;
    requested[10] = input->minimum_trailing_reserved_bytes;
    for (index = 0u; index < RIBON_UPDATE_LAYOUT_REGION_COUNT; ++index) {
        uint64_t aligned;
        if (!align_up_u64(requested[index], input->allocation_alignment, &aligned) ||
            !add_u64(aligned_prefix, aligned, &aligned_prefix)) {
            return RIBON_UPDATE_STORAGE_STATUS_OVERFLOW;
        }
        if (index == RIBON_UPDATE_LAYOUT_REGION_COUNT - 1u) {
            aligned_trailing_minimum = aligned;
        }
    }
    if (aligned_prefix > input->media_capacity_bytes) {
        return RIBON_UPDATE_STORAGE_STATUS_CAPACITY;
    }
    trailing = input->media_capacity_bytes -
        (aligned_prefix - aligned_trailing_minimum);
    if ((trailing & (input->allocation_alignment - 1u)) != 0u) {
        return RIBON_UPDATE_STORAGE_STATUS_ALIGNMENT;
    }
    requested[10] = trailing;

    layout->size = sizeof(*layout);
    layout->abi_version = RIBON_UPDATE_STORAGE_ABI_VERSION;
    layout->region_count = RIBON_UPDATE_LAYOUT_REGION_COUNT;
    layout->media_capacity_bytes = input->media_capacity_bytes;
    layout->allocation_alignment = input->allocation_alignment;
    for (index = 0u; index < RIBON_UPDATE_LAYOUT_REGION_COUNT; ++index) {
        status = layout_append(layout, index,
            (enum RibonUpdateLayoutRegionKind)(index + 1u),
            requested[index], &cursor);
        if (status != RIBON_UPDATE_STORAGE_STATUS_OK) {
            memset(layout, 0, sizeof(*layout));
            return status;
        }
    }
    status = ribon_update_layout_identity_encode(layout, identity);
    if (status != RIBON_UPDATE_STORAGE_STATUS_OK) {
        memset(layout, 0, sizeof(*layout));
        return status;
    }
    ribon_security_sha256(identity, sizeof(identity), layout->identity_digest);
    return RIBON_UPDATE_STORAGE_STATUS_OK;
}

/** @brief Manifest component maximum range가 요구하는 aligned slot byte 수를 계산한다. */
int
ribon_update_manifest_required_slot_bytes(
    const struct RibonUpdateManifestView *manifest,
    uint64_t alignment,
    uint64_t *required_bytes)
{
    struct RibonUpdateManifestView reopened;
    uint64_t maximum_end = 0u;
    uint32_t index;

    if (required_bytes != NULL) {
        *required_bytes = 0u;
    }
    if (manifest == NULL || required_bytes == NULL ||
        manifest->bytes == NULL || !power_of_two(alignment) ||
        ribon_update_manifest_open(manifest->bytes, manifest->byte_size, &reopened) !=
            RIBON_UPDATE_MANIFEST_STATUS_OK) {
        return RIBON_UPDATE_STORAGE_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0u; index < reopened.component_count; ++index) {
        struct RibonUpdateComponentView component;
        uint64_t end;
        if (ribon_update_manifest_component_at(&reopened, index, &component) !=
                RIBON_UPDATE_MANIFEST_STATUS_OK ||
            !add_u64(component.bundle_offset, component.maximum_size, &end)) {
            return RIBON_UPDATE_STORAGE_STATUS_OVERFLOW;
        }
        if (end > maximum_end) {
            maximum_end = end;
        }
    }
    if (!align_up_u64(maximum_end, alignment, required_bytes)) {
        return RIBON_UPDATE_STORAGE_STATUS_OVERFLOW;
    }
    return RIBON_UPDATE_STORAGE_STATUS_OK;
}

/** @brief Layout의 A/B slot이 manifest maximum component range를 수용하는지 검사한다. */
int
ribon_update_layout_accepts_manifest(
    const struct RibonUpdateLayout *layout,
    const struct RibonUpdateManifestView *manifest)
{
    uint8_t identity[RIBON_UPDATE_LAYOUT_IDENTITY_BYTES];
    uint8_t digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint64_t required;

    if (layout == NULL || manifest == NULL ||
        ribon_update_layout_identity_encode(layout, identity) !=
            RIBON_UPDATE_STORAGE_STATUS_OK) {
        return 0;
    }
    ribon_security_sha256(identity, sizeof(identity), digest);
    if (!bytes_equal(digest, layout->identity_digest, sizeof(digest)) ||
        ribon_update_manifest_required_slot_bytes(
            manifest, layout->allocation_alignment, &required) !=
            RIBON_UPDATE_STORAGE_STATUS_OK) {
        return 0;
    }
    return layout->regions[RIBON_UPDATE_REGION_SLOT_A - 1u].length >= required &&
        layout->regions[RIBON_UPDATE_REGION_SLOT_B - 1u].length >= required;
}

/** @brief Slot entry의 lifecycle와 identity invariants를 검사한다. */
static int
slot_entry_is_valid(
    const struct RibonUpdateSlotEntry *entry,
    uint32_t expected_slot,
    uint64_t metadata_generation)
{
    const int empty_identity =
        bytes_zero(entry->manifest_digest, sizeof(entry->manifest_digest)) &&
        bytes_zero(entry->image_set_digest, sizeof(entry->image_set_digest)) &&
        bytes_zero(entry->layout_digest, sizeof(entry->layout_digest));

    if (entry == NULL || entry->slot_id != expected_slot || entry->flags != 0u ||
        entry->state < RIBON_UPDATE_SLOT_EMPTY ||
        entry->state > RIBON_UPDATE_SLOT_BAD ||
        entry->boot_attempts > METADATA_MAX_BOOT_ATTEMPTS ||
        !bytes_zero(entry->reserved, sizeof(entry->reserved))) {
        return 0;
    }
    if (entry->state == RIBON_UPDATE_SLOT_EMPTY) {
        return entry->metadata_generation == 0u && entry->image_generation == 0u &&
            entry->boot_attempts == 0u && empty_identity;
    }
    if (entry->metadata_generation == 0u ||
        entry->metadata_generation > metadata_generation ||
        entry->image_generation == 0u || empty_identity ||
        bytes_zero(entry->manifest_digest, sizeof(entry->manifest_digest)) ||
        bytes_zero(entry->image_set_digest, sizeof(entry->image_set_digest)) ||
        bytes_zero(entry->layout_digest, sizeof(entry->layout_digest))) {
        return 0;
    }
    if (entry->state == RIBON_UPDATE_SLOT_PENDING) {
        return entry->boot_attempts != 0u;
    }
    return entry->boot_attempts == 0u;
}

/** @brief Native metadata object의 global invariants를 검사한다. */
static int
metadata_is_valid(const struct RibonUpdateSlotMetadata *metadata)
{
    uint32_t pending_count = 0u;
    uint32_t index;

    if (metadata == NULL || metadata->size != sizeof(*metadata) ||
        metadata->abi_version != RIBON_UPDATE_STORAGE_ABI_VERSION ||
        metadata->flags != 0u || metadata->slot_count != RIBON_UPDATE_SLOT_COUNT ||
        metadata->metadata_generation == 0u ||
        metadata->active_slot >= RIBON_UPDATE_SLOT_COUNT ||
        (metadata->pending_slot != RIBON_UPDATE_SLOT_NONE &&
         metadata->pending_slot >= RIBON_UPDATE_SLOT_COUNT) ||
        metadata->slots[metadata->active_slot].state != RIBON_UPDATE_SLOT_CONFIRMED ||
        !bytes_zero(metadata->reserved, sizeof(metadata->reserved))) {
        return 0;
    }
    for (index = 0u; index < RIBON_UPDATE_SLOT_COUNT; ++index) {
        if (!slot_entry_is_valid(
                &metadata->slots[index], index, metadata->metadata_generation)) {
            return 0;
        }
        if (metadata->slots[index].state == RIBON_UPDATE_SLOT_PENDING) {
            ++pending_count;
        }
    }
    if (metadata->pending_slot == RIBON_UPDATE_SLOT_NONE) {
        return pending_count == 0u;
    }
    return pending_count == 1u &&
        metadata->pending_slot != metadata->active_slot &&
        metadata->slots[metadata->pending_slot].state == RIBON_UPDATE_SLOT_PENDING;
}

/** @brief Native slot metadata를 canonical 512-byte LE wire로 직렬화한다. */
int
ribon_update_slot_metadata_encode(
    const struct RibonUpdateSlotMetadata *metadata,
    uint8_t output[RIBON_UPDATE_SLOT_METADATA_BYTES])
{
    uint8_t digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint32_t index;

    if (output != NULL) {
        memset(output, 0, RIBON_UPDATE_SLOT_METADATA_BYTES);
    }
    if (output == NULL || !metadata_is_valid(metadata)) {
        return RIBON_UPDATE_STORAGE_STATUS_INVALID_ARGUMENT;
    }
    memcpy(output, ribon_update_metadata_magic, sizeof(ribon_update_metadata_magic));
    store_u16(output + 32u, RIBON_UPDATE_STORAGE_ABI_VERSION);
    store_u16(output + 34u, METADATA_HEADER_BYTES);
    store_u32(output + 36u, RIBON_UPDATE_SLOT_METADATA_BYTES);
    store_u64(output + 40u, metadata->metadata_generation);
    store_u32(output + 48u, metadata->active_slot);
    store_u32(output + 52u, metadata->pending_slot);
    store_u32(output + 56u, metadata->slot_count);
    for (index = 0u; index < RIBON_UPDATE_SLOT_COUNT; ++index) {
        const struct RibonUpdateSlotEntry *entry = &metadata->slots[index];
        uint8_t *row = output + METADATA_HEADER_BYTES +
            (size_t)index * METADATA_SLOT_BYTES;
        store_u32(row, entry->slot_id);
        store_u32(row + 4u, (uint32_t)entry->state);
        store_u64(row + 8u, entry->metadata_generation);
        store_u64(row + 16u, entry->image_generation);
        memcpy(row + 24u, entry->manifest_digest, 32u);
        memcpy(row + 56u, entry->image_set_digest, 32u);
        memcpy(row + 88u, entry->layout_digest, 32u);
        store_u32(row + 120u, entry->boot_attempts);
        store_u32(row + 124u, entry->flags);
    }
    ribon_security_sha256(output, METADATA_BODY_BYTES, digest);
    memcpy(output + METADATA_DIGEST_OFFSET, digest, sizeof(digest));
    store_u32(output + METADATA_CHECKSUM_OFFSET,
        crc32c(output, METADATA_CHECKSUM_OFFSET));
    return RIBON_UPDATE_STORAGE_STATUS_OK;
}

/** @brief Untrusted slot metadata wire를 독립 검증해 native snapshot으로 연다. */
int
ribon_update_slot_metadata_open(
    const uint8_t *bytes,
    size_t size,
    struct RibonUpdateSlotMetadata *metadata)
{
    uint8_t digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint32_t index;

    if (metadata != NULL) {
        memset(metadata, 0, sizeof(*metadata));
    }
    if (bytes == NULL || metadata == NULL || size != RIBON_UPDATE_SLOT_METADATA_BYTES ||
        !bytes_equal(bytes, ribon_update_metadata_magic, 32u) ||
        load_u16(bytes + 32u) != RIBON_UPDATE_STORAGE_ABI_VERSION ||
        load_u16(bytes + 34u) != METADATA_HEADER_BYTES ||
        load_u32(bytes + 36u) != RIBON_UPDATE_SLOT_METADATA_BYTES ||
        load_u32(bytes + 60u) != 0u ||
        !bytes_zero(bytes + METADATA_RESERVED_OFFSET,
            RIBON_UPDATE_SLOT_METADATA_BYTES - METADATA_RESERVED_OFFSET)) {
        return RIBON_UPDATE_STORAGE_STATUS_MALFORMED;
    }
    ribon_security_sha256(bytes, METADATA_BODY_BYTES, digest);
    if (!bytes_equal(digest, bytes + METADATA_DIGEST_OFFSET, sizeof(digest)) ||
        load_u32(bytes + METADATA_CHECKSUM_OFFSET) !=
            crc32c(bytes, METADATA_CHECKSUM_OFFSET)) {
        return RIBON_UPDATE_STORAGE_STATUS_MALFORMED;
    }
    metadata->size = sizeof(*metadata);
    metadata->abi_version = RIBON_UPDATE_STORAGE_ABI_VERSION;
    metadata->metadata_generation = load_u64(bytes + 40u);
    metadata->active_slot = load_u32(bytes + 48u);
    metadata->pending_slot = load_u32(bytes + 52u);
    metadata->slot_count = load_u32(bytes + 56u);
    for (index = 0u; index < RIBON_UPDATE_SLOT_COUNT; ++index) {
        struct RibonUpdateSlotEntry *entry = &metadata->slots[index];
        const uint8_t *row = bytes + METADATA_HEADER_BYTES +
            (size_t)index * METADATA_SLOT_BYTES;
        entry->slot_id = load_u32(row);
        entry->state = (enum RibonUpdateSlotState)load_u32(row + 4u);
        entry->metadata_generation = load_u64(row + 8u);
        entry->image_generation = load_u64(row + 16u);
        memcpy(entry->manifest_digest, row + 24u, 32u);
        memcpy(entry->image_set_digest, row + 56u, 32u);
        memcpy(entry->layout_digest, row + 88u, 32u);
        entry->boot_attempts = load_u32(row + 120u);
        entry->flags = load_u32(row + 124u);
        if (!bytes_zero(row + 128u, 32u)) {
            memset(metadata, 0, sizeof(*metadata));
            return RIBON_UPDATE_STORAGE_STATUS_MALFORMED;
        }
    }
    memcpy(metadata->wire_digest, digest, sizeof(digest));
    if (!metadata_is_valid(metadata)) {
        memset(metadata, 0, sizeof(*metadata));
        return RIBON_UPDATE_STORAGE_STATUS_MALFORMED;
    }
    return RIBON_UPDATE_STORAGE_STATUS_OK;
}

/** @brief Transition source와 destination이 v1 edge인지 검사한다. */
static int
transition_is_allowed(
    enum RibonUpdateSlotState current,
    enum RibonUpdateSlotState next)
{
    switch (current) {
    case RIBON_UPDATE_SLOT_EMPTY:
    case RIBON_UPDATE_SLOT_BAD:
    case RIBON_UPDATE_SLOT_CONFIRMED:
        return next == RIBON_UPDATE_SLOT_STAGING;
    case RIBON_UPDATE_SLOT_STAGING:
        return next == RIBON_UPDATE_SLOT_VERIFIED || next == RIBON_UPDATE_SLOT_BAD;
    case RIBON_UPDATE_SLOT_VERIFIED:
        return next == RIBON_UPDATE_SLOT_PENDING || next == RIBON_UPDATE_SLOT_BAD;
    case RIBON_UPDATE_SLOT_PENDING:
        return next == RIBON_UPDATE_SLOT_CONFIRMED || next == RIBON_UPDATE_SLOT_BAD;
    default:
        return 0;
    }
}

/** @brief Transition identity가 entry identity와 exact match인지 검사한다. */
static int
transition_identity_matches(
    const struct RibonUpdateSlotEntry *entry,
    const struct RibonUpdateSlotTransition *transition)
{
    return entry->image_generation == transition->image_generation &&
        bytes_equal(entry->manifest_digest, transition->manifest_digest, 32u) &&
        bytes_equal(entry->image_set_digest, transition->image_set_digest, 32u) &&
        bytes_equal(entry->layout_digest, transition->layout_digest, 32u);
}

/** @brief Current metadata에서 허용된 단일 slot transition을 생성한다. */
int
ribon_update_slot_metadata_transition(
    const struct RibonUpdateSlotMetadata *current,
    const struct RibonUpdateSlotTransition *transition,
    struct RibonUpdateSlotMetadata *next)
{
    struct RibonUpdateSlotEntry *entry;
    uint8_t wire[RIBON_UPDATE_SLOT_METADATA_BYTES];
    uint8_t digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];

    if (next != NULL) {
        memset(next, 0, sizeof(*next));
    }
    if (!metadata_is_valid(current) || transition == NULL || next == NULL ||
        transition->size != sizeof(*transition) ||
        transition->abi_version != RIBON_UPDATE_STORAGE_ABI_VERSION ||
        transition->flags != 0u ||
        transition->slot_id >= RIBON_UPDATE_SLOT_COUNT ||
        transition->slot_id == current->active_slot ||
        transition->next_state < RIBON_UPDATE_SLOT_EMPTY ||
        transition->next_state > RIBON_UPDATE_SLOT_BAD ||
        transition->boot_attempts > METADATA_MAX_BOOT_ATTEMPTS ||
        transition->image_generation == 0u ||
        bytes_zero(transition->manifest_digest, 32u) ||
        bytes_zero(transition->image_set_digest, 32u) ||
        bytes_zero(transition->layout_digest, 32u) ||
        !bytes_zero(transition->reserved, sizeof(transition->reserved)) ||
        current->metadata_generation == UINT64_MAX ||
        !transition_is_allowed(
            current->slots[transition->slot_id].state, transition->next_state)) {
        return RIBON_UPDATE_STORAGE_STATUS_BAD_STATE;
    }
    if (transition->next_state == RIBON_UPDATE_SLOT_PENDING) {
        if (transition->boot_attempts == 0u ||
            current->pending_slot != RIBON_UPDATE_SLOT_NONE) {
            return RIBON_UPDATE_STORAGE_STATUS_BAD_STATE;
        }
    } else if (transition->boot_attempts != 0u) {
        return RIBON_UPDATE_STORAGE_STATUS_BAD_STATE;
    }
    if (transition->next_state != RIBON_UPDATE_SLOT_STAGING &&
        !transition_identity_matches(
            &current->slots[transition->slot_id], transition)) {
        return RIBON_UPDATE_STORAGE_STATUS_IDENTITY_MISMATCH;
    }

    *next = *current;
    next->metadata_generation = current->metadata_generation + 1u;
    memset(next->wire_digest, 0, sizeof(next->wire_digest));
    entry = &next->slots[transition->slot_id];
    entry->state = transition->next_state;
    entry->metadata_generation = next->metadata_generation;
    entry->boot_attempts = transition->boot_attempts;
    if (transition->next_state == RIBON_UPDATE_SLOT_STAGING) {
        entry->image_generation = transition->image_generation;
        memcpy(entry->manifest_digest, transition->manifest_digest, 32u);
        memcpy(entry->image_set_digest, transition->image_set_digest, 32u);
        memcpy(entry->layout_digest, transition->layout_digest, 32u);
    }
    if (transition->next_state == RIBON_UPDATE_SLOT_PENDING) {
        next->pending_slot = transition->slot_id;
    } else if (transition->next_state == RIBON_UPDATE_SLOT_CONFIRMED) {
        next->active_slot = transition->slot_id;
        next->pending_slot = RIBON_UPDATE_SLOT_NONE;
    } else if (transition->next_state == RIBON_UPDATE_SLOT_BAD) {
        next->pending_slot = RIBON_UPDATE_SLOT_NONE;
    }
    if (!metadata_is_valid(next) ||
        ribon_update_slot_metadata_encode(next, wire) != RIBON_UPDATE_STORAGE_STATUS_OK) {
        memset(next, 0, sizeof(*next));
        return RIBON_UPDATE_STORAGE_STATUS_BAD_STATE;
    }
    ribon_security_sha256(wire, METADATA_BODY_BYTES, digest);
    memcpy(next->wire_digest, digest, sizeof(digest));
    return RIBON_UPDATE_STORAGE_STATUS_OK;
}

/** @brief Layout의 slot region을 stable slot ID로 반환한다. */
static const struct RibonUpdateLayoutRegion *
slot_region(const struct RibonUpdateLayout *layout, uint32_t slot_id)
{
    const uint32_t kind = slot_id == 0u ?
        RIBON_UPDATE_REGION_SLOT_A : RIBON_UPDATE_REGION_SLOT_B;
    return &layout->regions[kind - 1u];
}

/** @brief Session ABI와 provider/layout/metadata identity 결속을 검사한다. */
int
ribon_update_storage_session_is_valid(
    const struct RibonUpdateStorageSession *session)
{
    uint8_t identity[RIBON_UPDATE_LAYOUT_IDENTITY_BYTES];
    uint8_t digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint32_t index;

    if (session == NULL || session->size != sizeof(*session) ||
        session->abi_version != RIBON_UPDATE_STORAGE_ABI_VERSION ||
        session->flags != 0u || session->reserved0 != 0u ||
        !bytes_zero(session->reserved, sizeof(session->reserved)) ||
        !ribon_update_storage_provider_is_valid(session->provider) ||
        !metadata_is_valid(session->metadata) ||
        ribon_update_layout_identity_encode(session->layout, identity) !=
            RIBON_UPDATE_STORAGE_STATUS_OK ||
        session->layout->media_capacity_bytes != session->provider->capacity_bytes) {
        return 0;
    }
    ribon_security_sha256(identity, sizeof(identity), digest);
    if (!bytes_equal(digest, session->layout->identity_digest, sizeof(digest))) {
        return 0;
    }
    for (index = 0u; index < RIBON_UPDATE_SLOT_COUNT; ++index) {
        const struct RibonUpdateLayoutRegion *region =
            slot_region(session->layout, index);
        if (session->metadata->slots[index].state != RIBON_UPDATE_SLOT_EMPTY &&
            !bytes_equal(session->metadata->slots[index].layout_digest,
                digest, sizeof(digest))) {
            return 0;
        }
        if ((region->offset & (session->provider->read_alignment - 1u)) != 0u ||
            (region->length & (session->provider->read_alignment - 1u)) != 0u ||
            (region->offset & (session->provider->write_alignment - 1u)) != 0u ||
            (region->length & (session->provider->write_alignment - 1u)) != 0u ||
            (region->offset & (session->provider->erase_alignment - 1u)) != 0u ||
            (region->length & (session->provider->erase_alignment - 1u)) != 0u) {
            return 0;
        }
    }
    return 1;
}

/** @brief STAGING 상태인 inactive slot을 semantic handle로 연다. */
int
ribon_update_storage_open_inactive_slot(
    const struct RibonUpdateStorageSession *session,
    uint32_t slot_id,
    struct RibonUpdateSlotHandle *handle)
{
    if (handle != NULL) {
        memset(handle, 0, sizeof(*handle));
    }
    if (!ribon_update_storage_session_is_valid(session) || handle == NULL ||
        slot_id >= RIBON_UPDATE_SLOT_COUNT) {
        return RIBON_UPDATE_STORAGE_STATUS_INVALID_ARGUMENT;
    }
    if (slot_id == session->metadata->active_slot) {
        return RIBON_UPDATE_STORAGE_STATUS_PROTECTED;
    }
    if (session->metadata->slots[slot_id].state != RIBON_UPDATE_SLOT_STAGING) {
        return RIBON_UPDATE_STORAGE_STATUS_BAD_STATE;
    }
    handle->size = sizeof(*handle);
    handle->abi_version = RIBON_UPDATE_STORAGE_ABI_VERSION;
    handle->slot_id = slot_id;
    handle->metadata_generation = session->metadata->metadata_generation;
    memcpy(handle->media_identity_digest,
        session->provider->media_identity_digest,
        sizeof(handle->media_identity_digest));
    memcpy(handle->layout_digest, session->layout->identity_digest,
        sizeof(handle->layout_digest));
    return RIBON_UPDATE_STORAGE_STATUS_OK;
}

/** @brief Handle가 현재 session의 inactive STAGING slot에 결속되는지 검사한다. */
static int
handle_is_valid(
    const struct RibonUpdateStorageSession *session,
    const struct RibonUpdateSlotHandle *handle)
{
    return ribon_update_storage_session_is_valid(session) && handle != NULL &&
        handle->size == sizeof(*handle) &&
        handle->abi_version == RIBON_UPDATE_STORAGE_ABI_VERSION &&
        handle->flags == 0u && handle->slot_id < RIBON_UPDATE_SLOT_COUNT &&
        handle->slot_id != session->metadata->active_slot &&
        session->metadata->slots[handle->slot_id].state == RIBON_UPDATE_SLOT_STAGING &&
        handle->metadata_generation == session->metadata->metadata_generation &&
        bytes_equal(handle->media_identity_digest,
            session->provider->media_identity_digest, 32u) &&
        bytes_equal(handle->layout_digest, session->layout->identity_digest, 32u);
}

/** @brief Slot-relative range와 provider alignment를 검사하고 media offset을 만든다. */
static int
resolve_range(
    const struct RibonUpdateStorageSession *session,
    const struct RibonUpdateSlotHandle *handle,
    uint64_t slot_offset,
    uint64_t size,
    uint64_t alignment,
    uint64_t *media_offset)
{
    const struct RibonUpdateLayoutRegion *region;
    uint64_t end;

    if (media_offset != NULL) {
        *media_offset = 0u;
    }
    if (!handle_is_valid(session, handle) || media_offset == NULL || size == 0u) {
        return RIBON_UPDATE_STORAGE_STATUS_INVALID_ARGUMENT;
    }
    if ((slot_offset & (alignment - 1u)) != 0u ||
        (size & (alignment - 1u)) != 0u) {
        return RIBON_UPDATE_STORAGE_STATUS_ALIGNMENT;
    }
    if (size > session->provider->maximum_transfer_bytes ||
        !add_u64(slot_offset, size, &end)) {
        return RIBON_UPDATE_STORAGE_STATUS_OVERFLOW;
    }
    region = slot_region(session->layout, handle->slot_id);
    if (end > region->length || !add_u64(region->offset, slot_offset, media_offset)) {
        return RIBON_UPDATE_STORAGE_STATUS_CAPACITY;
    }
    return RIBON_UPDATE_STORAGE_STATUS_OK;
}

/** @brief Semantic slot handle 안의 exact bounded range를 읽는다. */
int
ribon_update_storage_read(
    const struct RibonUpdateStorageSession *session,
    const struct RibonUpdateSlotHandle *handle,
    uint64_t slot_offset,
    void *buffer,
    uint64_t size,
    uint64_t deadline_ticks)
{
    uint64_t media_offset;
    uint64_t transferred = 0u;
    int status;

    if (buffer == NULL) {
        return RIBON_UPDATE_STORAGE_STATUS_INVALID_ARGUMENT;
    }
    status = resolve_range(session, handle, slot_offset, size,
        session != NULL && session->provider != NULL ?
            session->provider->read_alignment : 1u,
        &media_offset);
    if (status != RIBON_UPDATE_STORAGE_STATUS_OK) {
        return status;
    }
    status = provider_status(session->provider->read(session->provider->context,
        media_offset, buffer, size, &transferred, deadline_ticks));
    if (status != RIBON_UPDATE_STORAGE_STATUS_OK) {
        return status;
    }
    return transferred == size ? RIBON_UPDATE_STORAGE_STATUS_OK :
        RIBON_UPDATE_STORAGE_STATUS_SHORT_IO;
}

/** @brief Active confirmed slot을 제외한 STAGING handle에 aligned bytes를 쓴다. */
int
ribon_update_storage_write_inactive(
    const struct RibonUpdateStorageSession *session,
    const struct RibonUpdateSlotHandle *handle,
    uint64_t slot_offset,
    const void *buffer,
    uint64_t size,
    uint64_t deadline_ticks)
{
    uint64_t media_offset;
    uint64_t transferred = 0u;
    int status;

    if (buffer == NULL) {
        return RIBON_UPDATE_STORAGE_STATUS_INVALID_ARGUMENT;
    }
    status = resolve_range(session, handle, slot_offset, size,
        session != NULL && session->provider != NULL ?
            session->provider->write_alignment : 1u,
        &media_offset);
    if (status != RIBON_UPDATE_STORAGE_STATUS_OK) {
        return status;
    }
    status = provider_status(session->provider->write(session->provider->context,
        media_offset, buffer, size, &transferred, deadline_ticks));
    if (status != RIBON_UPDATE_STORAGE_STATUS_OK) {
        return status;
    }
    return transferred == size ? RIBON_UPDATE_STORAGE_STATUS_OK :
        RIBON_UPDATE_STORAGE_STATUS_SHORT_IO;
}

/** @brief Active confirmed slot을 제외한 STAGING handle range를 erase한다. */
int
ribon_update_storage_erase_inactive(
    const struct RibonUpdateStorageSession *session,
    const struct RibonUpdateSlotHandle *handle,
    uint64_t slot_offset,
    uint64_t size,
    uint64_t deadline_ticks)
{
    uint64_t media_offset;
    int status = resolve_range(session, handle, slot_offset, size,
        session != NULL && session->provider != NULL ?
            session->provider->erase_alignment : 1u,
        &media_offset);

    if (status != RIBON_UPDATE_STORAGE_STATUS_OK) {
        return status;
    }
    return provider_status(session->provider->erase(session->provider->context,
        media_offset, size, deadline_ticks));
}

/** @brief Selected provider의 explicit durability barrier를 실행한다. */
int
ribon_update_storage_flush(
    const struct RibonUpdateStorageSession *session,
    uint64_t deadline_ticks)
{
    if (!ribon_update_storage_session_is_valid(session)) {
        return RIBON_UPDATE_STORAGE_STATUS_INVALID_ARGUMENT;
    }
    return provider_status(session->provider->flush(
        session->provider->context, deadline_ticks));
}

/** @brief Update-storage status의 stable diagnostic name을 반환한다. */
const char *
ribon_update_storage_status_name(enum RibonUpdateStorageStatus status)
{
    switch (status) {
    case RIBON_UPDATE_STORAGE_STATUS_OK: return "ok";
    case RIBON_UPDATE_STORAGE_STATUS_INVALID_ARGUMENT: return "invalid-argument";
    case RIBON_UPDATE_STORAGE_STATUS_UNSUPPORTED: return "unsupported";
    case RIBON_UPDATE_STORAGE_STATUS_CAPACITY: return "capacity";
    case RIBON_UPDATE_STORAGE_STATUS_OVERFLOW: return "overflow";
    case RIBON_UPDATE_STORAGE_STATUS_ALIGNMENT: return "alignment";
    case RIBON_UPDATE_STORAGE_STATUS_OVERLAP: return "overlap";
    case RIBON_UPDATE_STORAGE_STATUS_MALFORMED: return "malformed";
    case RIBON_UPDATE_STORAGE_STATUS_SHORT_IO: return "short-io";
    case RIBON_UPDATE_STORAGE_STATUS_IO: return "io";
    case RIBON_UPDATE_STORAGE_STATUS_PROTECTED: return "protected";
    case RIBON_UPDATE_STORAGE_STATUS_BAD_STATE: return "bad-state";
    case RIBON_UPDATE_STORAGE_STATUS_IDENTITY_MISMATCH: return "identity-mismatch";
    default: return "unknown";
    }
}

/** @brief Inactive-slot service가 bounded update provider ABI를 정확히 고르는지 검사한다. */
int
ribon_inactive_slot_storage_service_operations_are_valid(
    const struct RibonServiceDescriptor *descriptor)
{
    const struct RibonInactiveSlotStorageServiceOperations *operations;

    if (descriptor == NULL ||
        descriptor->kind != RIBON_SERVICE_KIND_INACTIVE_SLOT_STORAGE ||
        descriptor->provides !=
            (RIBON_CAP_INACTIVE_SLOT_WRITE | RIBON_CAP_INACTIVE_SLOT_ERASE) ||
        descriptor->operations == NULL ||
        descriptor->operations_size != sizeof(*operations) ||
        descriptor->operations_abi != RIBON_SERVICE_ABI_VERSION) {
        return 0;
    }
    operations = descriptor->operations;
    return operations->size == sizeof(*operations) &&
        operations->abi_version == RIBON_SERVICE_ABI_VERSION &&
        ribon_update_storage_provider_is_valid(operations->provider);
}
