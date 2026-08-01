#include <Ribon/update/manifest.h>

#include "../security/sha256.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define RIBON_UPDATE_MANIFEST_MAJOR UINT16_C(1)
#define RIBON_UPDATE_MANIFEST_MINOR UINT16_C(0)
#define RIBON_UPDATE_SIGNATURE_ENVELOPE_MAJOR UINT16_C(1)
#define RIBON_UPDATE_SIGNATURE_ENVELOPE_MINOR UINT16_C(0)
#define RIBON_UPDATE_SIGNED_MESSAGE_MAJOR UINT16_C(1)
#define RIBON_UPDATE_SIGNED_MESSAGE_MINOR UINT16_C(0)
#define RIBON_UPDATE_HASH_SHA256 UINT16_C(1)
#define RIBON_UPDATE_SIGNATURE_ED25519 UINT16_C(1)
#define RIBON_UPDATE_DIRECTORY_OFFSET 128u
#define RIBON_UPDATE_DIRECTORY_ENTRY_BYTES 32u
#define RIBON_UPDATE_DIRECTORY_CAPACITY 4u
#define RIBON_UPDATE_BINDING_OFFSET RIBON_UPDATE_MANIFEST_HEADER_BYTES
#define RIBON_UPDATE_COMPONENTS_OFFSET \
    (RIBON_UPDATE_MANIFEST_HEADER_BYTES + RIBON_UPDATE_MANIFEST_BINDING_BYTES)

static const uint8_t ribon_update_manifest_magic[32] =
    "RIBON-UPDATE-MANIFEST-V1";
static const uint8_t ribon_update_message_magic[32] =
    "RIBON-UPDATE-MESSAGE-V1";
static const uint8_t ribon_update_envelope_magic[32] =
    "RIBON-UPDATE-SIGNATURE-V1";

/** @brief Unaligned little-endian u16을 읽는다. */
static uint16_t
ribon_update_read_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8u);
}

/** @brief Unaligned little-endian u32를 읽는다. */
static uint32_t
ribon_update_read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8u) |
        ((uint32_t)bytes[2] << 16u) |
        ((uint32_t)bytes[3] << 24u);
}

/** @brief Unaligned little-endian u64를 읽는다. */
static uint64_t
ribon_update_read_u64(const uint8_t *bytes)
{
    uint64_t value = 0u;
    uint32_t index;

    for (index = 0u; index < 8u; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8u);
    }
    return value;
}

/** @brief Caller-owned byte buffer에 little-endian u16을 쓴다. */
static void
ribon_update_write_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
}

/** @brief Caller-owned byte buffer에 little-endian u32를 쓴다. */
static void
ribon_update_write_u32(uint8_t *bytes, uint32_t value)
{
    uint32_t index;

    for (index = 0u; index < 4u; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
}

/** @brief Caller-owned byte buffer에 little-endian u64를 쓴다. */
static void
ribon_update_write_u64(uint8_t *bytes, uint64_t value)
{
    uint32_t index;

    for (index = 0u; index < 8u; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
}

/** @brief Immutable byte view가 모두 0인지 allocation 없이 검사한다. */
static int
ribon_update_bytes_are_zero(const uint8_t *bytes, size_t size)
{
    uint8_t value = 0u;
    size_t index;

    for (index = 0u; index < size; ++index) {
        value |= bytes[index];
    }
    return value == 0u;
}

/** @brief 두 immutable byte view를 일정 시간형 loop로 비교한다. */
static int
ribon_update_bytes_equal(
    const uint8_t *left,
    const uint8_t *right,
    size_t size)
{
    uint8_t difference = 0u;
    size_t index;

    for (index = 0u; index < size; ++index) {
        difference |= (uint8_t)(left[index] ^ right[index]);
    }
    return difference == 0u;
}

/** @brief Stable identity digest가 all-zero가 아닌지 검사한다. */
static int
ribon_update_digest_is_valid(
    const uint8_t digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES])
{
    return digest != NULL && !ribon_update_bytes_are_zero(
        digest,
        RIBON_UPDATE_MANIFEST_DIGEST_BYTES);
}

/** @brief Stable execution mode registry가 update message에 유효한지 검사한다. */
static int
ribon_update_mode_is_valid(enum RibonKeyPolicyMode mode)
{
    return mode == RIBON_KEY_POLICY_MODE_NORMAL ||
        mode == RIBON_KEY_POLICY_MODE_RECOVERY ||
        mode == RIBON_KEY_POLICY_MODE_PROVISIONING ||
        mode == RIBON_KEY_POLICY_MODE_DIAGNOSTIC;
}

/** @brief Component role과 semantic destination 조합이 exact한지 검사한다. */
static int
ribon_update_role_destination_is_valid(
    enum RibonUpdateComponentRole role,
    enum RibonUpdateDestinationClass destination)
{
    return (role == RIBON_UPDATE_COMPONENT_ROLE_KERNEL &&
            destination == RIBON_UPDATE_DESTINATION_KERNEL_SLOT) ||
        (role == RIBON_UPDATE_COMPONENT_ROLE_BOOT_MODULE &&
         destination == RIBON_UPDATE_DESTINATION_MODULE_SLOT) ||
        (role == RIBON_UPDATE_COMPONENT_ROLE_POLICY &&
         destination == RIBON_UPDATE_DESTINATION_POLICY_SLOT) ||
        (role == RIBON_UPDATE_COMPONENT_ROLE_FIRMWARE &&
         destination == RIBON_UPDATE_DESTINATION_FIRMWARE_SLOT) ||
        (role == RIBON_UPDATE_COMPONENT_ROLE_RECOVERY_IMAGE &&
         destination == RIBON_UPDATE_DESTINATION_RECOVERY_SLOT);
}

/** @brief Image-format registry value가 알려진 bounded value인지 검사한다. */
static int
ribon_update_image_format_is_valid(enum RibonUpdateImageFormat format)
{
    return format >= RIBON_UPDATE_IMAGE_FORMAT_OPAQUE &&
        format <= RIBON_UPDATE_IMAGE_FORMAT_RAW;
}

/** @brief Native component 입력의 shape와 byte range를 검사한다. */
static int
ribon_update_component_input_is_valid(
    const struct RibonUpdateComponent *component,
    uint32_t expected_order)
{
    if (component == NULL || component->size != sizeof(*component) ||
        component->abi_version != RIBON_UPDATE_MANIFEST_ABI_VERSION ||
        (component->flags & ~RIBON_UPDATE_COMPONENT_REQUIRED) != 0u ||
        component->install_order != expected_order ||
        component->reserved0 != 0u ||
        !ribon_update_bytes_are_zero(
            (const uint8_t *)component->reserved,
            sizeof(component->reserved)) ||
        !ribon_update_role_destination_is_valid(
            component->role,
            component->destination_class) ||
        !ribon_update_image_format_is_valid(component->image_format) ||
        component->exact_size == 0u ||
        component->exact_size > component->maximum_size ||
        component->bundle_offset > UINT64_MAX - component->exact_size ||
        !ribon_update_digest_is_valid(component->logical_id_digest) ||
        !ribon_update_digest_is_valid(component->content_digest) ||
        !ribon_update_digest_is_valid(component->destination_id_digest) ||
        !ribon_update_digest_is_valid(component->entry_contract_digest)) {
        return 0;
    }
    return 1;
}

/** @brief 두 half-open component byte range가 겹치는지 검사한다. */
static int
ribon_update_ranges_overlap(
    uint64_t left_offset,
    uint64_t left_size,
    uint64_t right_offset,
    uint64_t right_size)
{
    return left_offset < right_offset + right_size &&
        right_offset < left_offset + left_size;
}

/** @brief Native manifest 입력의 singleton, identity와 range closure를 검사한다. */
static int
ribon_update_manifest_input_validate(
    const struct RibonUpdateManifestInput *input)
{
    uint32_t singleton_kernel = 0u;
    uint32_t singleton_policy = 0u;
    uint32_t singleton_recovery = 0u;
    uint32_t index;

    if (input == NULL || input->size != sizeof(*input) ||
        input->abi_version != RIBON_UPDATE_MANIFEST_ABI_VERSION ||
        input->flags != 0u || !ribon_update_mode_is_valid(input->mode) ||
        input->bundle_generation == 0u ||
        input->predecessor_generation >= input->bundle_generation ||
        input->minimum_hardware_revision > input->maximum_hardware_revision ||
        input->components == NULL || input->component_count == 0u ||
        input->component_count > RIBON_UPDATE_MANIFEST_MAX_COMPONENTS ||
        input->reserved0 != 0u ||
        !ribon_update_bytes_are_zero(
            (const uint8_t *)input->reserved,
            sizeof(input->reserved)) ||
        !ribon_update_digest_is_valid(input->schema_digest) ||
        !ribon_update_digest_is_valid(input->product_digest) ||
        !ribon_update_digest_is_valid(input->architecture_digest) ||
        !ribon_update_digest_is_valid(input->platform_digest) ||
        !ribon_update_digest_is_valid(input->environment_digest) ||
        !ribon_update_digest_is_valid(input->protocol_digest) ||
        !ribon_update_digest_is_valid(input->rollback_domain_digest)) {
        return RIBON_UPDATE_MANIFEST_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0u; index < input->component_count; ++index) {
        const struct RibonUpdateComponent *component = &input->components[index];
        uint32_t other;

        if (!ribon_update_component_input_is_valid(component, index)) {
            return RIBON_UPDATE_MANIFEST_STATUS_INVALID_ARGUMENT;
        }
        singleton_kernel += component->role == RIBON_UPDATE_COMPONENT_ROLE_KERNEL;
        singleton_policy += component->role == RIBON_UPDATE_COMPONENT_ROLE_POLICY;
        singleton_recovery +=
            component->role == RIBON_UPDATE_COMPONENT_ROLE_RECOVERY_IMAGE;
        if (singleton_kernel > 1u || singleton_policy > 1u ||
            singleton_recovery > 1u) {
            return RIBON_UPDATE_MANIFEST_STATUS_MALFORMED;
        }
        for (other = 0u; other < index; ++other) {
            const struct RibonUpdateComponent *previous = &input->components[other];

            if (ribon_update_bytes_equal(
                    component->logical_id_digest,
                    previous->logical_id_digest,
                    RIBON_UPDATE_MANIFEST_DIGEST_BYTES) ||
                ribon_update_ranges_overlap(
                    component->bundle_offset,
                    component->exact_size,
                    previous->bundle_offset,
                    previous->exact_size)) {
                return RIBON_UPDATE_MANIFEST_STATUS_MALFORMED;
            }
        }
    }
    return RIBON_UPDATE_MANIFEST_STATUS_OK;
}

/** @brief 한 canonical section-directory entry를 쓴다. */
static void
ribon_update_write_directory_entry(
    uint8_t *entry,
    enum RibonUpdateManifestSectionType type,
    uint64_t offset,
    uint64_t length,
    uint32_t count,
    uint32_t entry_size)
{
    ribon_update_write_u32(entry, (uint32_t)type);
    ribon_update_write_u32(entry + 4u, 0u);
    ribon_update_write_u64(entry + 8u, offset);
    ribon_update_write_u64(entry + 16u, length);
    ribon_update_write_u32(entry + 24u, count);
    ribon_update_write_u32(entry + 28u, entry_size);
}

/** @brief 한 native component를 canonical 192-byte row로 쓴다. */
static void
ribon_update_encode_component(
    const struct RibonUpdateComponent *component,
    uint8_t row[RIBON_UPDATE_MANIFEST_COMPONENT_BYTES])
{
    memcpy(row, component->logical_id_digest, 32u);
    memcpy(row + 32u, component->content_digest, 32u);
    memcpy(row + 64u, component->destination_id_digest, 32u);
    memcpy(row + 96u, component->entry_contract_digest, 32u);
    ribon_update_write_u64(row + 128u, component->bundle_offset);
    ribon_update_write_u64(row + 136u, component->exact_size);
    ribon_update_write_u64(row + 144u, component->maximum_size);
    ribon_update_write_u16(row + 152u, (uint16_t)component->role);
    ribon_update_write_u16(
        row + 154u,
        (uint16_t)component->destination_class);
    ribon_update_write_u16(row + 156u, (uint16_t)component->image_format);
    ribon_update_write_u16(row + 158u, (uint16_t)component->flags);
    ribon_update_write_u32(row + 160u, component->install_order);
}

/** @brief Caller-owned buffer에 canonical manifest v1을 직렬화한다. */
int
ribon_update_manifest_encode(
    const struct RibonUpdateManifestInput *input,
    uint8_t *output,
    size_t capacity,
    size_t *written)
{
    uint64_t component_bytes;
    uint64_t total_size;
    uint32_t index;
    int status;

    if (written == NULL) {
        return RIBON_UPDATE_MANIFEST_STATUS_INVALID_ARGUMENT;
    }
    *written = 0u;
    status = ribon_update_manifest_input_validate(input);
    if (status != RIBON_UPDATE_MANIFEST_STATUS_OK) {
        return status;
    }
    component_bytes =
        (uint64_t)input->component_count * RIBON_UPDATE_MANIFEST_COMPONENT_BYTES;
    total_size = RIBON_UPDATE_COMPONENTS_OFFSET + component_bytes;
    if (output == NULL || total_size > SIZE_MAX || capacity < (size_t)total_size) {
        return RIBON_UPDATE_MANIFEST_STATUS_CAPACITY;
    }
    memset(output, 0, (size_t)total_size);
    memcpy(output, ribon_update_manifest_magic, sizeof(ribon_update_manifest_magic));
    ribon_update_write_u16(output + 32u, RIBON_UPDATE_MANIFEST_MAJOR);
    ribon_update_write_u16(output + 34u, RIBON_UPDATE_MANIFEST_MINOR);
    ribon_update_write_u32(output + 36u, RIBON_UPDATE_MANIFEST_HEADER_BYTES);
    ribon_update_write_u64(output + 40u, total_size);
    ribon_update_write_u32(output + 48u, RIBON_UPDATE_MANIFEST_SECTION_COUNT);
    ribon_update_write_u32(output + 52u, input->component_count);
    ribon_update_write_u64(output + 56u, input->bundle_generation);
    ribon_update_write_u64(output + 64u, input->predecessor_generation);
    ribon_update_write_u64(output + 72u, input->rollback_sequence);
    ribon_update_write_u64(output + 80u, input->creation_policy_version);
    ribon_update_write_u16(output + 88u, RIBON_UPDATE_HASH_SHA256);
    ribon_update_write_u16(output + 90u, RIBON_UPDATE_SIGNATURE_ED25519);
    ribon_update_write_u16(output + 92u, (uint16_t)input->mode);
    memcpy(output + 96u, input->schema_digest, 32u);
    ribon_update_write_directory_entry(
        output + RIBON_UPDATE_DIRECTORY_OFFSET,
        RIBON_UPDATE_SECTION_BINDING,
        RIBON_UPDATE_BINDING_OFFSET,
        RIBON_UPDATE_MANIFEST_BINDING_BYTES,
        1u,
        RIBON_UPDATE_MANIFEST_BINDING_BYTES);
    ribon_update_write_directory_entry(
        output + RIBON_UPDATE_DIRECTORY_OFFSET +
            RIBON_UPDATE_DIRECTORY_ENTRY_BYTES,
        RIBON_UPDATE_SECTION_COMPONENTS,
        RIBON_UPDATE_COMPONENTS_OFFSET,
        component_bytes,
        input->component_count,
        RIBON_UPDATE_MANIFEST_COMPONENT_BYTES);
    memcpy(output + RIBON_UPDATE_BINDING_OFFSET, input->product_digest, 32u);
    memcpy(
        output + RIBON_UPDATE_BINDING_OFFSET + 32u,
        input->architecture_digest,
        32u);
    memcpy(
        output + RIBON_UPDATE_BINDING_OFFSET + 64u,
        input->platform_digest,
        32u);
    memcpy(
        output + RIBON_UPDATE_BINDING_OFFSET + 96u,
        input->environment_digest,
        32u);
    memcpy(
        output + RIBON_UPDATE_BINDING_OFFSET + 128u,
        input->protocol_digest,
        32u);
    memcpy(
        output + RIBON_UPDATE_BINDING_OFFSET + 160u,
        input->rollback_domain_digest,
        32u);
    ribon_update_write_u16(
        output + RIBON_UPDATE_BINDING_OFFSET + 192u,
        input->protocol_major);
    ribon_update_write_u16(
        output + RIBON_UPDATE_BINDING_OFFSET + 194u,
        input->protocol_minor);
    ribon_update_write_u32(
        output + RIBON_UPDATE_BINDING_OFFSET + 196u,
        input->minimum_hardware_revision);
    ribon_update_write_u32(
        output + RIBON_UPDATE_BINDING_OFFSET + 200u,
        input->maximum_hardware_revision);
    for (index = 0u; index < input->component_count; ++index) {
        ribon_update_encode_component(
            &input->components[index],
            output + RIBON_UPDATE_COMPONENTS_OFFSET +
                (size_t)index * RIBON_UPDATE_MANIFEST_COMPONENT_BYTES);
    }
    *written = (size_t)total_size;
    return RIBON_UPDATE_MANIFEST_STATUS_OK;
}

/** @brief 한 canonical component row를 caller-owned view로 decode한다. */
static void
ribon_update_decode_component(
    const uint8_t row[RIBON_UPDATE_MANIFEST_COMPONENT_BYTES],
    struct RibonUpdateComponentView *component)
{
    memset(component, 0, sizeof(*component));
    memcpy(component->logical_id_digest, row, 32u);
    memcpy(component->content_digest, row + 32u, 32u);
    memcpy(component->destination_id_digest, row + 64u, 32u);
    memcpy(component->entry_contract_digest, row + 96u, 32u);
    component->bundle_offset = ribon_update_read_u64(row + 128u);
    component->exact_size = ribon_update_read_u64(row + 136u);
    component->maximum_size = ribon_update_read_u64(row + 144u);
    component->role = (enum RibonUpdateComponentRole)
        ribon_update_read_u16(row + 152u);
    component->destination_class = (enum RibonUpdateDestinationClass)
        ribon_update_read_u16(row + 154u);
    component->image_format = (enum RibonUpdateImageFormat)
        ribon_update_read_u16(row + 156u);
    component->flags = ribon_update_read_u16(row + 158u);
    component->install_order = ribon_update_read_u32(row + 160u);
}

/** @brief Untrusted component row의 canonical reserved와 scalar shape를 검사한다. */
static int
ribon_update_component_view_is_valid(
    const uint8_t row[RIBON_UPDATE_MANIFEST_COMPONENT_BYTES],
    const struct RibonUpdateComponentView *component,
    uint32_t expected_order)
{
    return component->install_order == expected_order &&
        (component->flags & ~RIBON_UPDATE_COMPONENT_REQUIRED) == 0u &&
        ribon_update_role_destination_is_valid(
            component->role,
            component->destination_class) &&
        ribon_update_image_format_is_valid(component->image_format) &&
        component->exact_size != 0u &&
        component->exact_size <= component->maximum_size &&
        component->bundle_offset <= UINT64_MAX - component->exact_size &&
        ribon_update_digest_is_valid(component->logical_id_digest) &&
        ribon_update_digest_is_valid(component->content_digest) &&
        ribon_update_digest_is_valid(component->destination_id_digest) &&
        ribon_update_digest_is_valid(component->entry_contract_digest) &&
        ribon_update_bytes_are_zero(row + 164u, 28u);
}

/** @brief Untrusted manifest bytes를 independent하게 열어 모든 section bound를 검증한다. */
int
ribon_update_manifest_open(
    const uint8_t *bytes,
    size_t size,
    struct RibonUpdateManifestView *view)
{
    const uint8_t *binding;
    const uint8_t *binding_directory;
    const uint8_t *component_directory;
    uint32_t component_count;
    uint64_t component_bytes;
    uint64_t expected_size;
    uint32_t singleton_kernel = 0u;
    uint32_t singleton_policy = 0u;
    uint32_t singleton_recovery = 0u;
    uint32_t index;

    if (bytes == NULL || view == NULL) {
        return RIBON_UPDATE_MANIFEST_STATUS_INVALID_ARGUMENT;
    }
    memset(view, 0, sizeof(*view));
    if (size < RIBON_UPDATE_COMPONENTS_OFFSET +
            RIBON_UPDATE_MANIFEST_COMPONENT_BYTES ||
        size > RIBON_UPDATE_COMPONENTS_OFFSET +
            RIBON_UPDATE_MANIFEST_MAX_COMPONENTS *
                RIBON_UPDATE_MANIFEST_COMPONENT_BYTES) {
        return RIBON_UPDATE_MANIFEST_STATUS_MALFORMED;
    }
    if (!ribon_update_bytes_equal(bytes, ribon_update_manifest_magic, 32u)) {
        return RIBON_UPDATE_MANIFEST_STATUS_MALFORMED;
    }
    if (ribon_update_read_u16(bytes + 32u) != RIBON_UPDATE_MANIFEST_MAJOR ||
        ribon_update_read_u16(bytes + 34u) != RIBON_UPDATE_MANIFEST_MINOR) {
        return RIBON_UPDATE_MANIFEST_STATUS_UNSUPPORTED_VERSION;
    }
    if (ribon_update_read_u32(bytes + 36u) !=
            RIBON_UPDATE_MANIFEST_HEADER_BYTES ||
        ribon_update_read_u64(bytes + 40u) != (uint64_t)size ||
        ribon_update_read_u32(bytes + 48u) !=
            RIBON_UPDATE_MANIFEST_SECTION_COUNT) {
        return RIBON_UPDATE_MANIFEST_STATUS_MALFORMED;
    }
    if (ribon_update_read_u16(bytes + 88u) != RIBON_UPDATE_HASH_SHA256 ||
        ribon_update_read_u16(bytes + 90u) !=
            RIBON_UPDATE_SIGNATURE_ED25519) {
        return RIBON_UPDATE_MANIFEST_STATUS_UNSUPPORTED_ALGORITHM;
    }
    component_count = ribon_update_read_u32(bytes + 52u);
    if (component_count == 0u ||
        component_count > RIBON_UPDATE_MANIFEST_MAX_COMPONENTS ||
        !ribon_update_mode_is_valid((enum RibonKeyPolicyMode)
            ribon_update_read_u16(bytes + 92u)) ||
        ribon_update_read_u16(bytes + 94u) != 0u ||
        !ribon_update_digest_is_valid(bytes + 96u)) {
        return RIBON_UPDATE_MANIFEST_STATUS_MALFORMED;
    }
    if (ribon_update_read_u64(bytes + 56u) == 0u ||
        ribon_update_read_u64(bytes + 64u) >=
            ribon_update_read_u64(bytes + 56u)) {
        return RIBON_UPDATE_MANIFEST_STATUS_MALFORMED;
    }
    component_bytes =
        (uint64_t)component_count * RIBON_UPDATE_MANIFEST_COMPONENT_BYTES;
    expected_size = RIBON_UPDATE_COMPONENTS_OFFSET + component_bytes;
    if (expected_size != (uint64_t)size) {
        return RIBON_UPDATE_MANIFEST_STATUS_MALFORMED;
    }
    binding_directory = bytes + RIBON_UPDATE_DIRECTORY_OFFSET;
    component_directory =
        binding_directory + RIBON_UPDATE_DIRECTORY_ENTRY_BYTES;
    if (ribon_update_read_u32(binding_directory) !=
            RIBON_UPDATE_SECTION_BINDING ||
        ribon_update_read_u32(binding_directory + 4u) != 0u ||
        ribon_update_read_u64(binding_directory + 8u) !=
            RIBON_UPDATE_BINDING_OFFSET ||
        ribon_update_read_u64(binding_directory + 16u) !=
            RIBON_UPDATE_MANIFEST_BINDING_BYTES ||
        ribon_update_read_u32(binding_directory + 24u) != 1u ||
        ribon_update_read_u32(binding_directory + 28u) !=
            RIBON_UPDATE_MANIFEST_BINDING_BYTES ||
        ribon_update_read_u32(component_directory) !=
            RIBON_UPDATE_SECTION_COMPONENTS ||
        ribon_update_read_u32(component_directory + 4u) != 0u ||
        ribon_update_read_u64(component_directory + 8u) !=
            RIBON_UPDATE_COMPONENTS_OFFSET ||
        ribon_update_read_u64(component_directory + 16u) != component_bytes ||
        ribon_update_read_u32(component_directory + 24u) != component_count ||
        ribon_update_read_u32(component_directory + 28u) !=
            RIBON_UPDATE_MANIFEST_COMPONENT_BYTES ||
        !ribon_update_bytes_are_zero(
            component_directory + RIBON_UPDATE_DIRECTORY_ENTRY_BYTES,
            2u * RIBON_UPDATE_DIRECTORY_ENTRY_BYTES)) {
        return RIBON_UPDATE_MANIFEST_STATUS_MALFORMED;
    }
    binding = bytes + RIBON_UPDATE_BINDING_OFFSET;
    if (!ribon_update_digest_is_valid(binding) ||
        !ribon_update_digest_is_valid(binding + 32u) ||
        !ribon_update_digest_is_valid(binding + 64u) ||
        !ribon_update_digest_is_valid(binding + 96u) ||
        !ribon_update_digest_is_valid(binding + 128u) ||
        !ribon_update_digest_is_valid(binding + 160u) ||
        ribon_update_read_u32(binding + 196u) >
            ribon_update_read_u32(binding + 200u) ||
        !ribon_update_bytes_are_zero(binding + 204u, 52u)) {
        return RIBON_UPDATE_MANIFEST_STATUS_MALFORMED;
    }
    for (index = 0u; index < component_count; ++index) {
        const uint8_t *row = bytes + RIBON_UPDATE_COMPONENTS_OFFSET +
            (size_t)index * RIBON_UPDATE_MANIFEST_COMPONENT_BYTES;
        struct RibonUpdateComponentView component;
        uint32_t other;

        ribon_update_decode_component(row, &component);
        if (!ribon_update_component_view_is_valid(row, &component, index)) {
            return RIBON_UPDATE_MANIFEST_STATUS_MALFORMED;
        }
        singleton_kernel += component.role == RIBON_UPDATE_COMPONENT_ROLE_KERNEL;
        singleton_policy += component.role == RIBON_UPDATE_COMPONENT_ROLE_POLICY;
        singleton_recovery +=
            component.role == RIBON_UPDATE_COMPONENT_ROLE_RECOVERY_IMAGE;
        if (singleton_kernel > 1u || singleton_policy > 1u ||
            singleton_recovery > 1u) {
            return RIBON_UPDATE_MANIFEST_STATUS_MALFORMED;
        }
        for (other = 0u; other < index; ++other) {
            const uint8_t *previous_row = bytes + RIBON_UPDATE_COMPONENTS_OFFSET +
                (size_t)other * RIBON_UPDATE_MANIFEST_COMPONENT_BYTES;
            struct RibonUpdateComponentView previous;

            ribon_update_decode_component(previous_row, &previous);
            if (ribon_update_bytes_equal(
                    component.logical_id_digest,
                    previous.logical_id_digest,
                    RIBON_UPDATE_MANIFEST_DIGEST_BYTES) ||
                ribon_update_ranges_overlap(
                    component.bundle_offset,
                    component.exact_size,
                    previous.bundle_offset,
                    previous.exact_size)) {
                return RIBON_UPDATE_MANIFEST_STATUS_MALFORMED;
            }
        }
    }
    view->bytes = bytes;
    view->byte_size = size;
    view->bundle_generation = ribon_update_read_u64(bytes + 56u);
    view->predecessor_generation = ribon_update_read_u64(bytes + 64u);
    view->rollback_sequence = ribon_update_read_u64(bytes + 72u);
    view->creation_policy_version = ribon_update_read_u64(bytes + 80u);
    view->mode = (enum RibonKeyPolicyMode)ribon_update_read_u16(bytes + 92u);
    view->protocol_major = ribon_update_read_u16(binding + 192u);
    view->protocol_minor = ribon_update_read_u16(binding + 194u);
    view->minimum_hardware_revision = ribon_update_read_u32(binding + 196u);
    view->maximum_hardware_revision = ribon_update_read_u32(binding + 200u);
    view->component_count = component_count;
    view->components_offset = RIBON_UPDATE_COMPONENTS_OFFSET;
    memcpy(view->schema_digest, bytes + 96u, 32u);
    memcpy(view->product_digest, binding, 32u);
    memcpy(view->architecture_digest, binding + 32u, 32u);
    memcpy(view->platform_digest, binding + 64u, 32u);
    memcpy(view->environment_digest, binding + 96u, 32u);
    memcpy(view->protocol_digest, binding + 128u, 32u);
    memcpy(view->rollback_domain_digest, binding + 160u, 32u);
    return RIBON_UPDATE_MANIFEST_STATUS_OK;
}

/** @brief 검증된 manifest의 한 component row를 caller-owned view로 복사한다. */
int
ribon_update_manifest_component_at(
    const struct RibonUpdateManifestView *view,
    uint32_t index,
    struct RibonUpdateComponentView *component)
{
    struct RibonUpdateManifestView reopened;
    size_t offset;

    if (component != NULL) {
        memset(component, 0, sizeof(*component));
    }
    if (view == NULL || component == NULL || view->bytes == NULL ||
        ribon_update_manifest_open(
            view->bytes,
            view->byte_size,
            &reopened) != RIBON_UPDATE_MANIFEST_STATUS_OK ||
        index >= reopened.component_count) {
        return RIBON_UPDATE_MANIFEST_STATUS_INVALID_ARGUMENT;
    }
    offset = reopened.components_offset +
        (size_t)index * RIBON_UPDATE_MANIFEST_COMPONENT_BYTES;
    if (offset > reopened.byte_size ||
        reopened.byte_size - offset < RIBON_UPDATE_MANIFEST_COMPONENT_BYTES) {
        return RIBON_UPDATE_MANIFEST_STATUS_INVALID_ARGUMENT;
    }
    ribon_update_decode_component(reopened.bytes + offset, component);
    return RIBON_UPDATE_MANIFEST_STATUS_OK;
}

/** @brief Opaque key ID가 bounded non-NUL byte string인지 검사한다. */
static int
ribon_update_key_id_is_valid(const uint8_t *key_id, size_t key_id_size)
{
    size_t index;

    if (key_id == NULL || key_id_size == 0u ||
        key_id_size > RIBON_UPDATE_SIGNATURE_KEY_ID_MAX_BYTES) {
        return 0;
    }
    for (index = 0u; index < key_id_size; ++index) {
        if (key_id[index] == 0u) {
            return 0;
        }
    }
    return 1;
}

/** @brief Update-manifest usage 전용 canonical 256-byte signed message를 만든다. */
int
ribon_update_manifest_signed_message_v1(
    const struct RibonUpdateManifestView *view,
    const uint8_t *key_id,
    size_t key_id_size,
    uint8_t message[RIBON_UPDATE_SIGNED_MESSAGE_V1_BYTES])
{
    struct RibonUpdateManifestView reopened;
    uint8_t manifest_digest[32];
    uint8_t key_id_digest[32];

    if (view == NULL || message == NULL || view->bytes == NULL ||
        !ribon_update_key_id_is_valid(key_id, key_id_size) ||
        ribon_update_manifest_open(
            view->bytes,
            view->byte_size,
            &reopened) != RIBON_UPDATE_MANIFEST_STATUS_OK) {
        return RIBON_UPDATE_MANIFEST_STATUS_INVALID_ARGUMENT;
    }
    memset(message, 0, RIBON_UPDATE_SIGNED_MESSAGE_V1_BYTES);
    ribon_security_sha256(view->bytes, view->byte_size, manifest_digest);
    ribon_security_sha256(key_id, key_id_size, key_id_digest);
    memcpy(message, ribon_update_message_magic, 32u);
    ribon_update_write_u16(message + 32u, RIBON_UPDATE_SIGNED_MESSAGE_MAJOR);
    ribon_update_write_u16(message + 34u, RIBON_UPDATE_SIGNED_MESSAGE_MINOR);
    ribon_update_write_u16(message + 36u, RIBON_UPDATE_MANIFEST_MAJOR);
    ribon_update_write_u16(message + 38u, RIBON_UPDATE_MANIFEST_MINOR);
    ribon_update_write_u16(message + 40u, RIBON_UPDATE_HASH_SHA256);
    ribon_update_write_u16(message + 42u, RIBON_UPDATE_SIGNATURE_ED25519);
    ribon_update_write_u16(message + 44u, (uint16_t)reopened.mode);
    ribon_update_write_u16(
        message + 46u,
        RIBON_KEY_POLICY_USAGE_UPDATE_MANIFEST);
    ribon_update_write_u64(message + 56u, reopened.rollback_sequence);
    ribon_update_write_u64(message + 64u, (uint64_t)reopened.byte_size);
    ribon_update_write_u64(message + 72u, reopened.bundle_generation);
    ribon_update_write_u64(message + 80u, reopened.predecessor_generation);
    ribon_update_write_u64(message + 88u, reopened.creation_policy_version);
    memcpy(message + 96u, manifest_digest, 32u);
    memcpy(message + 128u, reopened.product_digest, 32u);
    memcpy(message + 160u, reopened.schema_digest, 32u);
    memcpy(message + 192u, reopened.rollback_domain_digest, 32u);
    memcpy(message + 224u, key_id_digest, 32u);
    return RIBON_UPDATE_MANIFEST_STATUS_OK;
}

/** @brief Detached signature envelope를 explicit little-endian bytes로 직렬화한다. */
int
ribon_update_signature_envelope_encode(
    const struct RibonUpdateSignatureEnvelopeInput *input,
    uint8_t *output,
    size_t capacity,
    size_t *written)
{
    struct RibonUpdateManifestView view;
    uint8_t message[RIBON_UPDATE_SIGNED_MESSAGE_V1_BYTES];
    uint8_t manifest_digest[32];
    uint8_t message_digest[32];
    size_t signature_offset;
    size_t total_size;

    if (written == NULL) {
        return RIBON_UPDATE_MANIFEST_STATUS_INVALID_ARGUMENT;
    }
    *written = 0u;
    if (input == NULL || input->size != sizeof(*input) ||
        input->abi_version != RIBON_UPDATE_MANIFEST_ABI_VERSION ||
        input->flags != 0u || input->reserved0 != 0u ||
        input->signature == NULL ||
        input->signature_size != RIBON_ED25519_SIGNATURE_BYTES ||
        !ribon_update_key_id_is_valid(input->key_id, input->key_id_size) ||
        !ribon_update_bytes_are_zero(
            (const uint8_t *)input->reserved,
            sizeof(input->reserved)) ||
        ribon_update_manifest_open(
            input->manifest,
            input->manifest_size,
            &view) != RIBON_UPDATE_MANIFEST_STATUS_OK ||
        ribon_update_manifest_signed_message_v1(
            &view,
            input->key_id,
            input->key_id_size,
            message) != RIBON_UPDATE_MANIFEST_STATUS_OK) {
        return RIBON_UPDATE_MANIFEST_STATUS_INVALID_ARGUMENT;
    }
    signature_offset =
        RIBON_UPDATE_SIGNATURE_ENVELOPE_HEADER_BYTES + input->key_id_size;
    if (signature_offset > SIZE_MAX - input->signature_size) {
        return RIBON_UPDATE_MANIFEST_STATUS_CAPACITY;
    }
    total_size = signature_offset + input->signature_size;
    if (output == NULL || capacity < total_size) {
        return RIBON_UPDATE_MANIFEST_STATUS_CAPACITY;
    }
    memset(output, 0, total_size);
    ribon_security_sha256(input->manifest, input->manifest_size, manifest_digest);
    ribon_security_sha256(message, sizeof(message), message_digest);
    memcpy(output, ribon_update_envelope_magic, 32u);
    ribon_update_write_u16(
        output + 32u,
        RIBON_UPDATE_SIGNATURE_ENVELOPE_MAJOR);
    ribon_update_write_u16(
        output + 34u,
        RIBON_UPDATE_SIGNATURE_ENVELOPE_MINOR);
    ribon_update_write_u32(
        output + 36u,
        RIBON_UPDATE_SIGNATURE_ENVELOPE_HEADER_BYTES);
    ribon_update_write_u64(output + 40u, total_size);
    ribon_update_write_u64(output + 48u, input->manifest_size);
    ribon_update_write_u16(output + 56u, RIBON_UPDATE_HASH_SHA256);
    ribon_update_write_u16(output + 58u, RIBON_UPDATE_SIGNATURE_ED25519);
    ribon_update_write_u16(
        output + 60u,
        RIBON_KEY_POLICY_USAGE_UPDATE_MANIFEST);
    ribon_update_write_u16(output + 62u, (uint16_t)view.mode);
    ribon_update_write_u16(output + 64u, (uint16_t)input->key_id_size);
    ribon_update_write_u16(output + 66u, (uint16_t)input->signature_size);
    ribon_update_write_u64(
        output + 72u,
        RIBON_UPDATE_SIGNATURE_ENVELOPE_HEADER_BYTES);
    ribon_update_write_u64(output + 80u, signature_offset);
    memcpy(output + 88u, manifest_digest, 32u);
    memcpy(output + 120u, message_digest, 32u);
    memcpy(
        output + RIBON_UPDATE_SIGNATURE_ENVELOPE_HEADER_BYTES,
        input->key_id,
        input->key_id_size);
    memcpy(output + signature_offset, input->signature, input->signature_size);
    *written = total_size;
    return RIBON_UPDATE_MANIFEST_STATUS_OK;
}

/** @brief Untrusted detached signature envelope의 shape와 digest binding을 검증한다. */
int
ribon_update_signature_envelope_open(
    const uint8_t *bytes,
    size_t size,
    struct RibonUpdateSignatureEnvelopeView *view)
{
    uint16_t key_id_size;
    uint16_t signature_size;
    uint64_t key_offset;
    uint64_t signature_offset;

    if (bytes == NULL || view == NULL) {
        return RIBON_UPDATE_MANIFEST_STATUS_INVALID_ARGUMENT;
    }
    memset(view, 0, sizeof(*view));
    if (size < RIBON_UPDATE_SIGNATURE_ENVELOPE_HEADER_BYTES + 1u +
            RIBON_ED25519_SIGNATURE_BYTES ||
        size > RIBON_UPDATE_SIGNATURE_ENVELOPE_HEADER_BYTES +
            RIBON_UPDATE_SIGNATURE_KEY_ID_MAX_BYTES +
            RIBON_ED25519_SIGNATURE_BYTES ||
        !ribon_update_bytes_equal(bytes, ribon_update_envelope_magic, 32u)) {
        return RIBON_UPDATE_MANIFEST_STATUS_MALFORMED;
    }
    if (ribon_update_read_u16(bytes + 32u) !=
            RIBON_UPDATE_SIGNATURE_ENVELOPE_MAJOR ||
        ribon_update_read_u16(bytes + 34u) !=
            RIBON_UPDATE_SIGNATURE_ENVELOPE_MINOR) {
        return RIBON_UPDATE_MANIFEST_STATUS_UNSUPPORTED_VERSION;
    }
    if (ribon_update_read_u32(bytes + 36u) !=
            RIBON_UPDATE_SIGNATURE_ENVELOPE_HEADER_BYTES ||
        ribon_update_read_u64(bytes + 40u) != (uint64_t)size ||
        ribon_update_read_u64(bytes + 48u) == 0u ||
        ribon_update_read_u64(bytes + 48u) > SIZE_MAX ||
        ribon_update_read_u16(bytes + 60u) !=
            RIBON_KEY_POLICY_USAGE_UPDATE_MANIFEST ||
        !ribon_update_mode_is_valid((enum RibonKeyPolicyMode)
            ribon_update_read_u16(bytes + 62u)) ||
        ribon_update_read_u32(bytes + 68u) != 0u ||
        !ribon_update_bytes_are_zero(bytes + 152u, 8u)) {
        return RIBON_UPDATE_MANIFEST_STATUS_MALFORMED;
    }
    if (ribon_update_read_u16(bytes + 56u) != RIBON_UPDATE_HASH_SHA256 ||
        ribon_update_read_u16(bytes + 58u) !=
            RIBON_UPDATE_SIGNATURE_ED25519) {
        return RIBON_UPDATE_MANIFEST_STATUS_UNSUPPORTED_ALGORITHM;
    }
    key_id_size = ribon_update_read_u16(bytes + 64u);
    signature_size = ribon_update_read_u16(bytes + 66u);
    key_offset = ribon_update_read_u64(bytes + 72u);
    signature_offset = ribon_update_read_u64(bytes + 80u);
    if (key_id_size == 0u ||
        key_id_size > RIBON_UPDATE_SIGNATURE_KEY_ID_MAX_BYTES ||
        signature_size != RIBON_ED25519_SIGNATURE_BYTES ||
        key_offset != RIBON_UPDATE_SIGNATURE_ENVELOPE_HEADER_BYTES ||
        signature_offset != key_offset + key_id_size ||
        signature_offset > SIZE_MAX ||
        signature_offset + signature_size != (uint64_t)size ||
        !ribon_update_key_id_is_valid(
            bytes + (size_t)key_offset,
            key_id_size) ||
        ribon_update_bytes_are_zero(bytes + 88u, 32u) ||
        ribon_update_bytes_are_zero(bytes + 120u, 32u)) {
        return RIBON_UPDATE_MANIFEST_STATUS_MALFORMED;
    }
    view->bytes = bytes;
    view->byte_size = size;
    view->key_id = bytes + (size_t)key_offset;
    view->key_id_size = key_id_size;
    view->signature = bytes + (size_t)signature_offset;
    view->signature_size = signature_size;
    view->manifest_size = (size_t)ribon_update_read_u64(bytes + 48u);
    view->mode = (enum RibonKeyPolicyMode)ribon_update_read_u16(bytes + 62u);
    memcpy(view->manifest_digest, bytes + 88u, 32u);
    memcpy(view->signed_message_digest, bytes + 120u, 32u);
    return RIBON_UPDATE_MANIFEST_STATUS_OK;
}

/** @brief Product expectation struct와 reserved fields를 검사한다. */
static int
ribon_update_expectation_is_valid(
    const struct RibonUpdateManifestExpectation *expectation)
{
    return expectation != NULL && expectation->size == sizeof(*expectation) &&
        expectation->abi_version == RIBON_UPDATE_MANIFEST_ABI_VERSION &&
        expectation->flags == 0u && ribon_update_mode_is_valid(expectation->mode) &&
        ribon_update_digest_is_valid(expectation->schema_digest) &&
        ribon_update_digest_is_valid(expectation->product_digest) &&
        ribon_update_digest_is_valid(expectation->architecture_digest) &&
        ribon_update_digest_is_valid(expectation->platform_digest) &&
        ribon_update_digest_is_valid(expectation->environment_digest) &&
        ribon_update_digest_is_valid(expectation->protocol_digest) &&
        ribon_update_digest_is_valid(expectation->rollback_domain_digest) &&
        ribon_update_bytes_are_zero(
            (const uint8_t *)expectation->reserved,
            sizeof(expectation->reserved));
}

/** @brief Manifest view가 selected product expectation과 exact match하는지 검사한다. */
static int
ribon_update_manifest_matches_expectation(
    const struct RibonUpdateManifestView *view,
    const struct RibonUpdateManifestExpectation *expectation)
{
    if (view->mode != expectation->mode) {
        return RIBON_UPDATE_MANIFEST_STATUS_MODE_USAGE_MISMATCH;
    }
    if (!ribon_update_bytes_equal(
            view->product_digest,
            expectation->product_digest,
            32u) ||
        !ribon_update_bytes_equal(
            view->schema_digest,
            expectation->schema_digest,
            32u) ||
        !ribon_update_bytes_equal(
            view->architecture_digest,
            expectation->architecture_digest,
            32u) ||
        !ribon_update_bytes_equal(
            view->platform_digest,
            expectation->platform_digest,
            32u) ||
        !ribon_update_bytes_equal(
            view->environment_digest,
            expectation->environment_digest,
            32u) ||
        !ribon_update_bytes_equal(
            view->protocol_digest,
            expectation->protocol_digest,
            32u) ||
        view->protocol_major != expectation->protocol_major ||
        view->protocol_minor != expectation->protocol_minor ||
        expectation->hardware_revision < view->minimum_hardware_revision ||
        expectation->hardware_revision > view->maximum_hardware_revision) {
        return RIBON_UPDATE_MANIFEST_STATUS_IDENTITY_MISMATCH;
    }
    if (!ribon_update_bytes_equal(
            view->rollback_domain_digest,
            expectation->rollback_domain_digest,
            32u)) {
        return RIBON_UPDATE_MANIFEST_STATUS_DOMAIN_MISMATCH;
    }
    return RIBON_UPDATE_MANIFEST_STATUS_OK;
}

/** @brief Key-policy 실패를 update authorization stable class로 축약한다. */
static int
ribon_update_map_key_policy_status(int status)
{
    if (status == RIBON_KEY_POLICY_STATUS_IDENTITY_MISMATCH) {
        return RIBON_UPDATE_MANIFEST_STATUS_IDENTITY_MISMATCH;
    }
    if (status == RIBON_KEY_POLICY_STATUS_MODE_USAGE_MISMATCH) {
        return RIBON_UPDATE_MANIFEST_STATUS_MODE_USAGE_MISMATCH;
    }
    if (status == RIBON_KEY_POLICY_STATUS_DOMAIN_MISMATCH) {
        return RIBON_UPDATE_MANIFEST_STATUS_DOMAIN_MISMATCH;
    }
    if (status == RIBON_KEY_POLICY_STATUS_UNSUPPORTED_ALGORITHM) {
        return RIBON_UPDATE_MANIFEST_STATUS_UNSUPPORTED_ALGORITHM;
    }
    if (status == RIBON_KEY_POLICY_STATUS_SIGNATURE_INVALID) {
        return RIBON_UPDATE_MANIFEST_STATUS_SIGNATURE_INVALID;
    }
    return RIBON_UPDATE_MANIFEST_STATUS_KEY_POLICY;
}

/** @brief Product binding, update key usage와 Ed25519 signature를 순서대로 승인한다. */
int
ribon_update_manifest_authorize(
    const struct RibonUpdateManifestAuthorization *authorization,
    struct RibonUpdateManifestView *view,
    struct RibonKeyPolicyDecision *decision)
{
    struct RibonUpdateManifestView manifest_view;
    struct RibonUpdateSignatureEnvelopeView envelope_view;
    struct RibonKeyPolicyRequest policy_request = {0};
    struct RibonKeyPolicySignatureVerification verification = {0};
    uint8_t manifest_digest[32];
    uint8_t message[RIBON_UPDATE_SIGNED_MESSAGE_V1_BYTES];
    uint8_t message_digest[32];
    int status;

    if (view != NULL) {
        memset(view, 0, sizeof(*view));
    }
    if (decision != NULL) {
        memset(decision, 0, sizeof(*decision));
    }
    if (authorization == NULL || view == NULL || decision == NULL ||
        authorization->size != sizeof(*authorization) ||
        authorization->abi_version != RIBON_UPDATE_MANIFEST_ABI_VERSION ||
        authorization->flags != 0u || authorization->reserved0 != 0u ||
        authorization->key_policy == NULL ||
        authorization->signature_provider == NULL ||
        !ribon_update_expectation_is_valid(authorization->expectation) ||
        !ribon_update_bytes_are_zero(
            (const uint8_t *)authorization->reserved,
            sizeof(authorization->reserved))) {
        return RIBON_UPDATE_MANIFEST_STATUS_INVALID_ARGUMENT;
    }
    status = ribon_update_manifest_open(
        authorization->manifest,
        authorization->manifest_size,
        &manifest_view);
    if (status != RIBON_UPDATE_MANIFEST_STATUS_OK) {
        return status;
    }
    status = ribon_update_manifest_matches_expectation(
        &manifest_view,
        authorization->expectation);
    if (status != RIBON_UPDATE_MANIFEST_STATUS_OK) {
        return status;
    }
    status = ribon_update_signature_envelope_open(
        authorization->signature_envelope,
        authorization->signature_envelope_size,
        &envelope_view);
    if (status != RIBON_UPDATE_MANIFEST_STATUS_OK) {
        return status;
    }
    if (envelope_view.mode != manifest_view.mode ||
        envelope_view.manifest_size != manifest_view.byte_size) {
        return RIBON_UPDATE_MANIFEST_STATUS_MODE_USAGE_MISMATCH;
    }
    ribon_security_sha256(
        manifest_view.bytes,
        manifest_view.byte_size,
        manifest_digest);
    if (!ribon_update_bytes_equal(
            manifest_digest,
            envelope_view.manifest_digest,
            32u)) {
        return RIBON_UPDATE_MANIFEST_STATUS_DIGEST_MISMATCH;
    }
    status = ribon_update_manifest_signed_message_v1(
        &manifest_view,
        envelope_view.key_id,
        envelope_view.key_id_size,
        message);
    if (status != RIBON_UPDATE_MANIFEST_STATUS_OK) {
        return status;
    }
    ribon_security_sha256(message, sizeof(message), message_digest);
    if (!ribon_update_bytes_equal(
            message_digest,
            envelope_view.signed_message_digest,
            32u)) {
        return RIBON_UPDATE_MANIFEST_STATUS_DIGEST_MISMATCH;
    }
    policy_request.size = sizeof(policy_request);
    policy_request.abi_version = RIBON_KEY_POLICY_ABI_VERSION;
    policy_request.mode = manifest_view.mode;
    policy_request.usage = RIBON_KEY_POLICY_USAGE_UPDATE_MANIFEST;
    policy_request.key_id = envelope_view.key_id;
    policy_request.key_id_size = envelope_view.key_id_size;
    policy_request.sequence = manifest_view.rollback_sequence;
    memcpy(policy_request.product_digest, manifest_view.product_digest, 32u);
    memcpy(
        policy_request.rollback_domain_digest,
        manifest_view.rollback_domain_digest,
        32u);
    verification.size = sizeof(verification);
    verification.abi_version = RIBON_KEY_POLICY_ABI_VERSION;
    verification.policy = &policy_request;
    verification.provider = authorization->signature_provider;
    verification.message = message;
    verification.message_size = sizeof(message);
    verification.signature = envelope_view.signature;
    verification.signature_size = envelope_view.signature_size;
    verification.workspace = authorization->workspace;
    verification.workspace_size = authorization->workspace_size;
    status = ribon_key_policy_verify(
        authorization->key_policy,
        &verification,
        decision);
    if (status != RIBON_KEY_POLICY_STATUS_OK) {
        memset(decision, 0, sizeof(*decision));
        return ribon_update_map_key_policy_status(status);
    }
    *view = manifest_view;
    return RIBON_UPDATE_MANIFEST_STATUS_OK;
}
