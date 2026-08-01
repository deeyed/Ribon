#include <Ribon/update/installer.h>

#include "../security/sha256.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/** @brief Byte view가 모두 0인지 allocation 없이 검사한다. */
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

/** @brief Digest를 data-independent loop로 비교한다. */
static int digest_equal(const uint8_t *left, const uint8_t *right)
{
    uint8_t difference = 0u;
    size_t index;
    for (index = 0u; index < RIBON_UPDATE_MANIFEST_DIGEST_BYTES; ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == 0u;
}

/** @brief Power-of-two alignment에 overflow 없이 올림한다. */
static int align_up(uint64_t value, uint64_t alignment, uint64_t *result)
{
    uint64_t added;
    if (result == NULL || alignment == 0u ||
        (alignment & (alignment - 1u)) != 0u ||
        value > UINT64_MAX - (alignment - 1u)) {
        return 0;
    }
    added = value + alignment - 1u;
    *result = added & ~(alignment - 1u);
    return 1;
}

/** @brief Installer request의 pointer-free shape와 caller storage를 검사한다. */
static int request_is_valid(const struct RibonUpdateInstallRequest *request)
{
    const struct RibonUpdateBundleSource *bundle;
    if (request == NULL || request->size != sizeof(*request) ||
        request->abi_version != RIBON_UPDATE_INSTALLER_ABI_VERSION ||
        request->flags != 0u || request->target_slot >= RIBON_UPDATE_SLOT_COUNT ||
        request->bundle == NULL || request->provider == NULL ||
        request->layout == NULL || request->current_metadata == NULL ||
        request->scratch == NULL || request->scratch_size == 0u ||
        request->deadline_ticks == 0u ||
        !bytes_zero(request->reserved, sizeof(request->reserved))) {
        return 0;
    }
    bundle = request->bundle;
    return bundle->size == sizeof(*bundle) &&
        bundle->abi_version == RIBON_UPDATE_INSTALLER_ABI_VERSION &&
        bundle->flags == 0u && bundle->reserved0 == 0u &&
        bundle->byte_size != 0u && bundle->context != NULL &&
        bundle->read != NULL && bytes_zero(bundle->reserved, sizeof(bundle->reserved)) &&
        ribon_update_storage_provider_is_valid(request->provider) &&
        request->provider->capacity_bytes == request->layout->media_capacity_bytes &&
        ((uintptr_t)request->scratch &
            (request->provider->write_alignment - 1u)) == 0u;
}

/** @brief Component content digests의 ordered bounded identity를 계산한다. */
static int image_set_identity(
    const struct RibonUpdateManifestView *manifest,
    uint8_t output[RIBON_UPDATE_MANIFEST_DIGEST_BYTES])
{
    uint8_t bytes[4u + RIBON_UPDATE_MANIFEST_MAX_COMPONENTS *
        RIBON_UPDATE_MANIFEST_DIGEST_BYTES] = {0};
    uint32_t index;
    bytes[0] = (uint8_t)manifest->component_count;
    for (index = 0u; index < manifest->component_count; ++index) {
        struct RibonUpdateComponentView component;
        if (ribon_update_manifest_component_at(manifest, index, &component) !=
                RIBON_UPDATE_MANIFEST_STATUS_OK) {
            return 0;
        }
        memcpy(bytes + 4u + (size_t)index * RIBON_UPDATE_MANIFEST_DIGEST_BYTES,
            component.content_digest, RIBON_UPDATE_MANIFEST_DIGEST_BYTES);
    }
    ribon_security_sha256(bytes,
        4u + (size_t)manifest->component_count *
            RIBON_UPDATE_MANIFEST_DIGEST_BYTES,
        output);
    return 1;
}

/** @brief Signed bundle을 inactive slot에 설치하고 full readback 뒤 VERIFIED로 닫는다. */
int ribon_update_install_signed_bundle(
    const struct RibonUpdateInstallRequest *request,
    struct RibonUpdateInstallResult *result)
{
    struct RibonUpdateManifestView manifest;
    struct RibonKeyPolicyDecision decision;
    struct RibonUpdateSlotTransition transition = {0};
    struct RibonUpdateSlotMetadata staging;
    struct RibonUpdateStorageSession session;
    struct RibonUpdateSlotHandle handle;
    uint8_t manifest_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t image_set_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint64_t exact_total = 0u;
    uint64_t backing_total = 0u;
    uint32_t index;
    int status;

    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
    if (result == NULL || !request_is_valid(request)) {
        return RIBON_UPDATE_INSTALL_STATUS_INVALID_ARGUMENT;
    }
    status = ribon_update_manifest_authorize(
        &request->authorization, &manifest, &decision);
    if (status != RIBON_UPDATE_MANIFEST_STATUS_OK) {
        return RIBON_UPDATE_INSTALL_STATUS_AUTHORIZATION;
    }
    if (!ribon_update_layout_accepts_manifest(request->layout, &manifest)) {
        return RIBON_UPDATE_INSTALL_STATUS_LAYOUT;
    }
    if (request->current_metadata->active_slot == request->target_slot ||
        request->current_metadata->slots[request->target_slot].state !=
            RIBON_UPDATE_SLOT_EMPTY) {
        return RIBON_UPDATE_INSTALL_STATUS_STATE;
    }
    ribon_security_sha256(manifest.bytes, manifest.byte_size, manifest_digest);
    if (!image_set_identity(&manifest, image_set_digest)) {
        return RIBON_UPDATE_INSTALL_STATUS_LAYOUT;
    }
    transition.size = sizeof(transition);
    transition.abi_version = RIBON_UPDATE_STORAGE_ABI_VERSION;
    transition.slot_id = request->target_slot;
    transition.next_state = RIBON_UPDATE_SLOT_STAGING;
    transition.image_generation = manifest.bundle_generation;
    memcpy(transition.manifest_digest, manifest_digest, sizeof(manifest_digest));
    memcpy(transition.image_set_digest, image_set_digest, sizeof(image_set_digest));
    memcpy(transition.layout_digest, request->layout->identity_digest,
        sizeof(transition.layout_digest));
    if (ribon_update_slot_metadata_transition(
            request->current_metadata, &transition, &staging) !=
            RIBON_UPDATE_STORAGE_STATUS_OK) {
        return RIBON_UPDATE_INSTALL_STATUS_STATE;
    }
    session = (struct RibonUpdateStorageSession){
        .size = sizeof(session),
        .abi_version = RIBON_UPDATE_STORAGE_ABI_VERSION,
        .provider = request->provider,
        .layout = request->layout,
        .metadata = &staging,
    };
    if (ribon_update_storage_open_inactive_slot(
            &session, request->target_slot, &handle) !=
            RIBON_UPDATE_STORAGE_STATUS_OK) {
        return RIBON_UPDATE_INSTALL_STATUS_STATE;
    }
    for (index = 0u; index < manifest.component_count; ++index) {
        struct RibonUpdateComponentView component;
        uint64_t backing_size;
        uint64_t transferred = 0u;
        uint8_t *scratch = request->scratch;
        uint64_t tail;

        if (ribon_update_manifest_component_at(&manifest, index, &component) !=
                RIBON_UPDATE_MANIFEST_STATUS_OK ||
            !align_up(component.exact_size, request->provider->write_alignment,
                &backing_size) ||
            backing_size > request->scratch_size ||
            backing_size > request->provider->maximum_transfer_bytes ||
            component.bundle_offset > request->bundle->byte_size ||
            component.exact_size >
                request->bundle->byte_size - component.bundle_offset ||
            exact_total > UINT64_MAX - component.exact_size ||
            backing_total > UINT64_MAX - backing_size) {
            return RIBON_UPDATE_INSTALL_STATUS_CAPACITY;
        }
        if (request->bundle->read(request->bundle->context,
                component.bundle_offset, scratch, component.exact_size,
                &transferred, request->deadline_ticks) != 0 ||
            transferred != component.exact_size) {
            return RIBON_UPDATE_INSTALL_STATUS_BUNDLE_IO;
        }
        ribon_security_sha256(scratch, (size_t)component.exact_size, digest);
        if (!digest_equal(digest, component.content_digest)) {
            return RIBON_UPDATE_INSTALL_STATUS_COMPONENT_DIGEST;
        }
        tail = backing_size - component.exact_size;
        if (tail != 0u) {
            memset(scratch + (size_t)component.exact_size, 0, (size_t)tail);
        }
        if (ribon_update_storage_erase_inactive(&session, &handle,
                component.bundle_offset, backing_size,
                request->deadline_ticks) != RIBON_UPDATE_STORAGE_STATUS_OK ||
            ribon_update_storage_write_inactive(&session, &handle,
                component.bundle_offset, scratch, backing_size,
                request->deadline_ticks) != RIBON_UPDATE_STORAGE_STATUS_OK ||
            ribon_update_storage_read(&session, &handle,
                component.bundle_offset, scratch, backing_size,
                request->deadline_ticks) != RIBON_UPDATE_STORAGE_STATUS_OK) {
            return RIBON_UPDATE_INSTALL_STATUS_STORAGE_IO;
        }
        ribon_security_sha256(scratch, (size_t)component.exact_size, digest);
        if (!digest_equal(digest, component.content_digest) ||
            (tail != 0u && !bytes_zero(
                scratch + (size_t)component.exact_size, (size_t)tail))) {
            return RIBON_UPDATE_INSTALL_STATUS_READBACK_DIGEST;
        }
        exact_total += component.exact_size;
        backing_total += backing_size;
    }
    if (ribon_update_storage_flush(&session, request->deadline_ticks) !=
            RIBON_UPDATE_STORAGE_STATUS_OK) {
        return RIBON_UPDATE_INSTALL_STATUS_STORAGE_IO;
    }
    transition.next_state = RIBON_UPDATE_SLOT_VERIFIED;
    if (ribon_update_slot_metadata_transition(
            &staging, &transition, &result->verified_metadata) !=
            RIBON_UPDATE_STORAGE_STATUS_OK) {
        memset(result, 0, sizeof(*result));
        return RIBON_UPDATE_INSTALL_STATUS_STATE;
    }
    result->size = sizeof(*result);
    result->abi_version = RIBON_UPDATE_INSTALLER_ABI_VERSION;
    result->target_slot = request->target_slot;
    result->component_count = manifest.component_count;
    result->installed_exact_bytes = exact_total;
    result->installed_backing_bytes = backing_total;
    memcpy(result->manifest_digest, manifest_digest, sizeof(manifest_digest));
    memcpy(result->image_set_digest, image_set_digest, sizeof(image_set_digest));
    return RIBON_UPDATE_INSTALL_STATUS_OK;
}

/** @brief Installer status의 stable diagnostic name을 반환한다. */
const char *ribon_update_install_status_name(enum RibonUpdateInstallStatus status)
{
    switch (status) {
    case RIBON_UPDATE_INSTALL_STATUS_OK: return "ok";
    case RIBON_UPDATE_INSTALL_STATUS_INVALID_ARGUMENT: return "invalid-argument";
    case RIBON_UPDATE_INSTALL_STATUS_AUTHORIZATION: return "authorization";
    case RIBON_UPDATE_INSTALL_STATUS_LAYOUT: return "layout";
    case RIBON_UPDATE_INSTALL_STATUS_CAPACITY: return "capacity";
    case RIBON_UPDATE_INSTALL_STATUS_BUNDLE_IO: return "bundle-io";
    case RIBON_UPDATE_INSTALL_STATUS_COMPONENT_DIGEST: return "component-digest";
    case RIBON_UPDATE_INSTALL_STATUS_STORAGE_IO: return "storage-io";
    case RIBON_UPDATE_INSTALL_STATUS_READBACK_DIGEST: return "readback-digest";
    case RIBON_UPDATE_INSTALL_STATUS_STATE: return "state";
    default: return "unknown";
    }
}
