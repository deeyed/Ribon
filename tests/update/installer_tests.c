#include <Ribon/security/ed25519.h>
#include <Ribon/update/installer.h>

#include "reference_storage.h"
#include "../../src/security/sha256.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCRATCH_BYTES 65536u

static const uint8_t fixture_key_id[] = "ribon-update-release-2026q3";
static const uint8_t fixture_public_key[RIBON_ED25519_PUBLIC_KEY_BYTES] = {
    0xd7u, 0x5au, 0x98u, 0x01u, 0x82u, 0xb1u, 0x0au, 0xb7u,
    0xd5u, 0x4bu, 0xfeu, 0xd3u, 0xc9u, 0x64u, 0x07u, 0x3au,
    0x0eu, 0xe1u, 0x72u, 0xf3u, 0xdau, 0xa6u, 0x23u, 0x25u,
    0xafu, 0x02u, 0x1au, 0x68u, 0xf7u, 0x07u, 0x51u, 0x1au,
};
static const uint8_t fixture_key_identity[32] = {
    0x21u, 0xfeu, 0x31u, 0xdfu, 0xa1u, 0x54u, 0xa2u, 0x61u,
    0x62u, 0x6bu, 0xf8u, 0x54u, 0x04u, 0x6fu, 0xd2u, 0x27u,
    0x1bu, 0x7bu, 0xedu, 0x4bu, 0x6au, 0xbeu, 0x45u, 0xaau,
    0x58u, 0x87u, 0x7eu, 0xf4u, 0x7fu, 0x97u, 0x21u, 0xb9u,
};
static const uint8_t fixture_store_id[] = "update.installer.tests.v1";

struct FileBytes {
    uint8_t *bytes;
    size_t size;
};

struct BundleContext {
    const uint8_t *bytes;
    size_t size;
    int short_read;
};

static int failures;

/** @brief Unit assertion 실패를 stable 이름과 함께 누적한다. */
static void
expect(int condition, const char *name)
{
    if (!condition) {
        (void)fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

/** @brief Exact fixture file을 caller-owned heap buffer로 읽는다. */
static int
read_file(const char *path, struct FileBytes *file)
{
    FILE *stream;
    long length;

    memset(file, 0, sizeof(*file));
    stream = fopen(path, "rb");
    if (stream == NULL || fseek(stream, 0L, SEEK_END) != 0 ||
        (length = ftell(stream)) <= 0L || fseek(stream, 0L, SEEK_SET) != 0) {
        if (stream != NULL) {
            (void)fclose(stream);
        }
        return 0;
    }
    file->bytes = malloc((size_t)length);
    file->size = (size_t)length;
    if (file->bytes == NULL ||
        fread(file->bytes, 1u, file->size, stream) != file->size ||
        fgetc(stream) != EOF || fclose(stream) != 0) {
        free(file->bytes);
        memset(file, 0, sizeof(*file));
        return 0;
    }
    return 1;
}

/** @brief Test bundle에서 exact range를 읽고 선택적으로 short I/O를 주입한다. */
static int
bundle_read(
    void *context,
    uint64_t offset,
    void *buffer,
    uint64_t size,
    uint64_t *transferred,
    uint64_t deadline_ticks)
{
    struct BundleContext *bundle = context;
    size_t copied;

    (void)deadline_ticks;
    if (transferred != NULL) {
        *transferred = 0u;
    }
    if (bundle == NULL || buffer == NULL || transferred == NULL || size == 0u ||
        offset > bundle->size || size > bundle->size - offset || size > SIZE_MAX) {
        return -1;
    }
    copied = (size_t)size;
    if (bundle->short_read && copied != 0u) {
        --copied;
    }
    memcpy(buffer, bundle->bytes + (size_t)offset, copied);
    *transferred = copied;
    return 0;
}

/** @brief Opened manifest와 exact 일치하는 immutable expectation을 만든다. */
static void
expectation_from_view(
    const struct RibonUpdateManifestView *view,
    struct RibonUpdateManifestExpectation *expectation)
{
    *expectation = (struct RibonUpdateManifestExpectation){
        .size = sizeof(*expectation),
        .abi_version = RIBON_UPDATE_MANIFEST_ABI_VERSION,
        .mode = view->mode,
        .protocol_major = view->protocol_major,
        .protocol_minor = view->protocol_minor,
        .hardware_revision = view->minimum_hardware_revision,
    };
    memcpy(expectation->schema_digest, view->schema_digest, 32u);
    memcpy(expectation->product_digest, view->product_digest, 32u);
    memcpy(expectation->architecture_digest, view->architecture_digest, 32u);
    memcpy(expectation->platform_digest, view->platform_digest, 32u);
    memcpy(expectation->environment_digest, view->environment_digest, 32u);
    memcpy(expectation->protocol_digest, view->protocol_digest, 32u);
    memcpy(expectation->rollback_domain_digest,
        view->rollback_domain_digest, 32u);
}

/** @brief Fixture identity로 one-key update-manifest trust store를 만든다. */
static int
store_from_view(
    const struct RibonUpdateManifestView *view,
    struct RibonKeyPolicyRecord *record,
    struct RibonKeyPolicyStore *store)
{
    *record = (struct RibonKeyPolicyRecord){
        .size = sizeof(*record),
        .abi_version = RIBON_KEY_POLICY_ABI_VERSION,
        .flags = RIBON_KEY_POLICY_RECORD_ROOT,
        .algorithm = RIBON_SIGNATURE_ALGORITHM_ED25519,
        .lifecycle = RIBON_KEY_POLICY_LIFECYCLE_ACTIVE,
        .mode_mask = UINT32_C(1) << (RIBON_KEY_POLICY_MODE_RECOVERY - 1u),
        .usage_mask = UINT64_C(1) <<
            (RIBON_KEY_POLICY_USAGE_UPDATE_MANIFEST - 1u),
        .key_id = fixture_key_id,
        .key_id_size = sizeof(fixture_key_id) - 1u,
        .rollback_domain_digests =
            (const uint8_t (*)[RIBON_KEY_POLICY_DIGEST_BYTES])
                &view->rollback_domain_digest,
        .rollback_domain_count = 1u,
        .minimum_sequence = view->rollback_sequence,
        .maximum_sequence = view->rollback_sequence,
    };
    memcpy(record->public_key, fixture_public_key, sizeof(record->public_key));
    memcpy(record->key_identity_digest,
        fixture_key_identity, sizeof(record->key_identity_digest));
    memcpy(record->product_digest, view->product_digest, 32u);
    *store = (struct RibonKeyPolicyStore){
        .magic = RIBON_KEY_POLICY_STORE_MAGIC,
        .size = sizeof(*store),
        .abi_version = RIBON_KEY_POLICY_ABI_VERSION,
        .id = fixture_store_id,
        .id_size = sizeof(fixture_store_id) - 1u,
        .generation = 1u,
        .records = record,
        .record_count = 1u,
    };
    return ribon_key_policy_store_canonical_digest(
               store, store->canonical_digest) == RIBON_KEY_POLICY_STATUS_OK &&
        ribon_key_policy_store_validate(store) == RIBON_KEY_POLICY_STATUS_OK;
}

/** @brief Confirmed A와 empty B인 initial metadata를 만든다. */
static struct RibonUpdateSlotMetadata
initial_metadata(const struct RibonUpdateLayout *layout)
{
    struct RibonUpdateSlotMetadata metadata = {
        .size = sizeof(metadata),
        .abi_version = RIBON_UPDATE_STORAGE_ABI_VERSION,
        .active_slot = 0u,
        .pending_slot = RIBON_UPDATE_SLOT_NONE,
        .slot_count = RIBON_UPDATE_SLOT_COUNT,
        .metadata_generation = 1u,
    };
    metadata.slots[0].slot_id = 0u;
    metadata.slots[0].state = RIBON_UPDATE_SLOT_CONFIRMED;
    metadata.slots[0].metadata_generation = 1u;
    metadata.slots[0].image_generation = 1u;
    ribon_security_sha256((const uint8_t *)"factory-manifest", 16u,
        metadata.slots[0].manifest_digest);
    ribon_security_sha256((const uint8_t *)"factory-image-set", 17u,
        metadata.slots[0].image_set_digest);
    memcpy(metadata.slots[0].layout_digest, layout->identity_digest, 32u);
    metadata.slots[1].slot_id = 1u;
    metadata.slots[1].state = RIBON_UPDATE_SLOT_EMPTY;
    return metadata;
}

/** @brief Signed fixture install의 positive path와 bounded failures를 검증한다. */
static void
test_installer(
    const struct FileBytes *manifest_file,
    const struct FileBytes *envelope_file,
    const struct FileBytes *bundle_file,
    const struct RibonUpdateLayout *layout)
{
    struct RibonUpdateManifestView view;
    struct RibonUpdateManifestExpectation expectation;
    struct RibonKeyPolicyRecord record;
    struct RibonKeyPolicyStore store;
    struct RibonUpdateSlotMetadata metadata;
    struct RibonTestStorage storage;
    struct BundleContext bundle_context = {
        .bytes = bundle_file->bytes,
        .size = bundle_file->size,
    };
    struct RibonUpdateBundleSource bundle = {
        .size = sizeof(bundle),
        .abi_version = RIBON_UPDATE_INSTALLER_ABI_VERSION,
        .byte_size = bundle_file->size,
        .context = &bundle_context,
        .read = bundle_read,
    };
    struct RibonUpdateInstallRequest request;
    struct RibonUpdateInstallResult result;
    uint8_t *media = calloc(1u, (size_t)layout->media_capacity_bytes);
    uint8_t *scratch = aligned_alloc(4096u, SCRATCH_BYTES);
    uint8_t active_before[32];
    uint8_t active_after[32];
    uint8_t *hostile_envelope = malloc(envelope_file->size);
    const struct RibonUpdateLayoutRegion *slot_a =
        &layout->regions[RIBON_UPDATE_REGION_SLOT_A - 1u];
    const struct RibonUpdateLayoutRegion *slot_b =
        &layout->regions[RIBON_UPDATE_REGION_SLOT_B - 1u];
    uint32_t index;

    expect(media != NULL && scratch != NULL && hostile_envelope != NULL,
           "allocate host-only installer fixture storage");
    if (media == NULL || scratch == NULL || hostile_envelope == NULL) {
        free(media);
        free(scratch);
        free(hostile_envelope);
        return;
    }
    expect(ribon_update_manifest_open(
               manifest_file->bytes, manifest_file->size, &view) ==
               RIBON_UPDATE_MANIFEST_STATUS_OK &&
               store_from_view(&view, &record, &store),
           "open signed installer manifest and trust store");
    expectation_from_view(&view, &expectation);
    memset(media + (size_t)slot_a->offset, 0xa5, (size_t)slot_a->length);
    ribon_security_sha256(media + (size_t)slot_a->offset,
        (size_t)slot_a->length, active_before);
    expect(ribon_test_storage_open_memory(&storage, media,
               (size_t)layout->media_capacity_bytes, 512u, SCRATCH_BYTES),
           "open bounded host reference media");
    metadata = initial_metadata(layout);
    request = (struct RibonUpdateInstallRequest){
        .size = sizeof(request),
        .abi_version = RIBON_UPDATE_INSTALLER_ABI_VERSION,
        .target_slot = 1u,
        .authorization = {
            .size = sizeof(request.authorization),
            .abi_version = RIBON_UPDATE_MANIFEST_ABI_VERSION,
            .manifest = manifest_file->bytes,
            .manifest_size = manifest_file->size,
            .signature_envelope = envelope_file->bytes,
            .signature_envelope_size = envelope_file->size,
            .expectation = &expectation,
            .key_policy = &store,
            .signature_provider = &ribon_ed25519_signature_provider_descriptor,
        },
        .bundle = &bundle,
        .provider = &storage.provider,
        .layout = layout,
        .current_metadata = &metadata,
        .scratch = scratch,
        .scratch_size = SCRATCH_BYTES,
        .deadline_ticks = 100u,
    };
    expect(ribon_update_install_signed_bundle(&request, &result) ==
               RIBON_UPDATE_INSTALL_STATUS_OK &&
               result.component_count == view.component_count &&
               result.verified_metadata.slots[1].state ==
                   RIBON_UPDATE_SLOT_VERIFIED &&
               result.verified_metadata.metadata_generation == 3u &&
               storage.flush_count == 1u,
           "signed bundle reaches inactive VERIFIED state");
    ribon_security_sha256(media + (size_t)slot_a->offset,
        (size_t)slot_a->length, active_after);
    expect(memcmp(active_before, active_after, sizeof(active_before)) == 0,
           "active slot bytes remain unchanged");
    for (index = 0u; index < view.component_count; ++index) {
        struct RibonUpdateComponentView component;
        expect(ribon_update_manifest_component_at(&view, index, &component) == 0 &&
                   memcmp(media + (size_t)(slot_b->offset + component.bundle_offset),
                       bundle_file->bytes + (size_t)component.bundle_offset,
                       (size_t)component.exact_size) == 0,
               "installed component exact bytes match bundle");
    }

    media[(size_t)slot_b->offset] = 0u;
    ((uint8_t *)bundle_context.bytes)[0] ^= 1u;
    expect(ribon_update_install_signed_bundle(&request, &result) ==
               RIBON_UPDATE_INSTALL_STATUS_COMPONENT_DIGEST,
           "corrupted component digest fails closed");
    ((uint8_t *)bundle_context.bytes)[0] ^= 1u;
    bundle_context.short_read = 1;
    expect(ribon_update_install_signed_bundle(&request, &result) ==
               RIBON_UPDATE_INSTALL_STATUS_BUNDLE_IO,
           "short bundle read fails closed");
    bundle_context.short_read = 0;
    storage.fault = RIBON_TEST_STORAGE_FAULT_WRITE_SHORT;
    expect(ribon_update_install_signed_bundle(&request, &result) ==
               RIBON_UPDATE_INSTALL_STATUS_STORAGE_IO,
           "short provider write fails closed");
    storage.fault = RIBON_TEST_STORAGE_FAULT_NONE;
    request.scratch_size = 512u;
    expect(ribon_update_install_signed_bundle(&request, &result) ==
               RIBON_UPDATE_INSTALL_STATUS_CAPACITY,
           "undersized scratch closure fails closed");
    request.scratch_size = SCRATCH_BYTES;
    request.target_slot = 0u;
    expect(ribon_update_install_signed_bundle(&request, &result) ==
               RIBON_UPDATE_INSTALL_STATUS_STATE,
           "active slot install target rejected");
    request.target_slot = 1u;
    memcpy(hostile_envelope, envelope_file->bytes, envelope_file->size);
    hostile_envelope[envelope_file->size - 1u] ^= 1u;
    request.authorization.signature_envelope = hostile_envelope;
    expect(ribon_update_install_signed_bundle(&request, &result) ==
               RIBON_UPDATE_INSTALL_STATUS_AUTHORIZATION,
           "corrupted signature envelope fails closed");

    free(hostile_envelope);
    free(scratch);
    free(media);
}

int
main(int argc, char **argv)
{
    struct FileBytes manifest;
    struct FileBytes envelope;
    struct FileBytes bundle;
    struct FileBytes identity;
    struct RibonUpdateLayout layout;

    if (argc != 5 || !read_file(argv[1], &manifest) ||
        !read_file(argv[2], &envelope) || !read_file(argv[3], &bundle) ||
        !read_file(argv[4], &identity) ||
        ribon_update_layout_identity_open(
            identity.bytes, identity.size, &layout) !=
            RIBON_UPDATE_STORAGE_STATUS_OK) {
        (void)fprintf(stderr,
            "usage: installer_tests MANIFEST ENVELOPE BUNDLE LAYOUT\n");
        return 2;
    }
    test_installer(&manifest, &envelope, &bundle, &layout);
    free(identity.bytes);
    free(bundle.bytes);
    free(envelope.bytes);
    free(manifest.bytes);
    if (failures != 0) {
        (void)fprintf(stderr,
            "RIBON-UPDATE-INSTALLER-FAIL failures=%d\n", failures);
        return 1;
    }
    (void)printf(
        "RIBON-UPDATE-INSTALLER-OK positive=1 hostile=6 active-unchanged=1\n");
    return 0;
}
