#include <Ribon/update/manifest.h>
#include <Ribon/update/storage.h>
#include <Ribon/service/directory.h>

#include "reference_storage.h"
#include "../../src/security/sha256.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MEDIA_BYTES UINT64_C(1048576)
#define TRANSFER_BYTES UINT64_C(4096)
#define MANIFEST_BYTES \
    (RIBON_UPDATE_MANIFEST_HEADER_BYTES + RIBON_UPDATE_MANIFEST_BINDING_BYTES + \
     RIBON_UPDATE_MANIFEST_COMPONENT_BYTES)

static int failures;
static uint8_t media[MEDIA_BYTES];

/** @brief Unit assertion 실패를 stable 이름과 함께 누적한다. */
static void
expect(int condition, const char *name)
{
    if (!condition) {
        (void)fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

/** @brief Test identity를 non-zero deterministic byte pattern으로 채운다. */
static void
fill_digest(uint8_t digest[32], uint8_t seed)
{
    uint32_t index;
    for (index = 0u; index < 32u; ++index) {
        digest[index] = (uint8_t)(seed + index);
    }
}

/** @brief Test-only little-endian u32 writer다. */
static void
store_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

/** @brief Test-only Castagnoli CRC32C를 계산한다. */
static uint32_t
test_crc32c(const uint8_t *bytes, size_t size)
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

/** @brief Hostile semantic wire의 integrity fields만 다시 계산한다. */
static void
refresh_metadata_integrity(uint8_t wire[512])
{
    ribon_security_sha256(wire, 384u, wire + 384u);
    store_u32(wire + 416u, test_crc32c(wire, 416u));
}

/** @brief D02 canonical layout input을 구성한다. */
static struct RibonUpdateLayoutInput
layout_input(void)
{
    return (struct RibonUpdateLayoutInput){
        .size = sizeof(struct RibonUpdateLayoutInput),
        .abi_version = RIBON_UPDATE_STORAGE_ABI_VERSION,
        .media_capacity_bytes = MEDIA_BYTES,
        .allocation_alignment = 4096u,
        .guard_gap_bytes = 4096u,
        .bootloader_bytes = 65536u,
        .immutable_recovery_bytes = 65536u,
        .slot_payload_bytes = 262144u,
        .slot_metadata_bytes = 4096u,
        .update_journal_bytes = 8192u,
        .minimum_trailing_reserved_bytes = 65536u,
    };
}

/** @brief Confirmed A와 empty B를 갖는 초기 metadata snapshot을 만든다. */
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
    fill_digest(metadata.slots[0].manifest_digest, 0x10u);
    fill_digest(metadata.slots[0].image_set_digest, 0x40u);
    memcpy(metadata.slots[0].layout_digest, layout->identity_digest, 32u);
    metadata.slots[1].slot_id = 1u;
    metadata.slots[1].state = RIBON_UPDATE_SLOT_EMPTY;
    return metadata;
}

/** @brief Slot B의 next identity를 갖는 transition request를 만든다. */
static struct RibonUpdateSlotTransition
transition_request(
    const struct RibonUpdateLayout *layout,
    enum RibonUpdateSlotState next_state)
{
    struct RibonUpdateSlotTransition transition = {
        .size = sizeof(transition),
        .abi_version = RIBON_UPDATE_STORAGE_ABI_VERSION,
        .slot_id = 1u,
        .next_state = next_state,
        .image_generation = 2u,
    };
    fill_digest(transition.manifest_digest, 0x70u);
    fill_digest(transition.image_set_digest, 0xa0u);
    memcpy(transition.layout_digest, layout->identity_digest, 32u);
    return transition;
}

/** @brief Single-component D01 manifest를 fixed test buffer에 만든다. */
static int
make_manifest(
    uint8_t bytes[MANIFEST_BYTES],
    size_t *written,
    struct RibonUpdateManifestView *view)
{
    struct RibonUpdateComponent component = {
        .size = sizeof(component),
        .abi_version = RIBON_UPDATE_MANIFEST_ABI_VERSION,
        .flags = RIBON_UPDATE_COMPONENT_REQUIRED,
        .install_order = 0u,
        .role = RIBON_UPDATE_COMPONENT_ROLE_KERNEL,
        .destination_class = RIBON_UPDATE_DESTINATION_KERNEL_SLOT,
        .image_format = RIBON_UPDATE_IMAGE_FORMAT_ELF64,
        .bundle_offset = 4096u,
        .exact_size = 1024u,
        .maximum_size = 200000u,
    };
    struct RibonUpdateManifestInput input = {
        .size = sizeof(input),
        .abi_version = RIBON_UPDATE_MANIFEST_ABI_VERSION,
        .mode = RIBON_KEY_POLICY_MODE_RECOVERY,
        .bundle_generation = 2u,
        .predecessor_generation = 1u,
        .rollback_sequence = 2u,
        .creation_policy_version = 1u,
        .protocol_major = 1u,
        .maximum_hardware_revision = UINT32_MAX,
        .components = &component,
        .component_count = 1u,
    };

    fill_digest(component.logical_id_digest, 1u);
    fill_digest(component.content_digest, 2u);
    fill_digest(component.destination_id_digest, 3u);
    fill_digest(component.entry_contract_digest, 4u);
    fill_digest(input.schema_digest, 5u);
    fill_digest(input.product_digest, 6u);
    fill_digest(input.architecture_digest, 7u);
    fill_digest(input.platform_digest, 8u);
    fill_digest(input.environment_digest, 9u);
    fill_digest(input.protocol_digest, 10u);
    fill_digest(input.rollback_domain_digest, 11u);
    return ribon_update_manifest_encode(
               &input, bytes, MANIFEST_BYTES, written) ==
            RIBON_UPDATE_MANIFEST_STATUS_OK &&
        ribon_update_manifest_open(bytes, *written, view) ==
            RIBON_UPDATE_MANIFEST_STATUS_OK;
}

/** @brief Layout calculator와 manifest capacity projection을 검증한다. */
static void
test_layout_and_manifest(void)
{
    struct RibonUpdateLayoutInput input = layout_input();
    struct RibonUpdateLayout layout;
    struct RibonUpdateLayout repeated;
    struct RibonUpdateLayout reopened;
    struct RibonUpdateStorageProductBinding binding = {
        .size = sizeof(binding),
        .abi_version = RIBON_UPDATE_STORAGE_ABI_VERSION,
        .provider_class = RIBON_UPDATE_STORAGE_PROVIDER_CLASS_REFERENCE,
        .layout_id = "layout.validation.ab-v1",
        .read_service_id = "service.read",
        .writer_service_id = "service.write",
        .metadata_service_id = "service.metadata",
        .flush_service_id = "service.flush",
    };
    struct RibonUpdateManifestView manifest;
    uint8_t manifest_bytes[MANIFEST_BYTES];
    uint8_t identity[512];
    uint8_t identity_repeated[512];
    uint64_t required = 0u;
    size_t written = 0u;
    uint32_t index;

    expect(ribon_update_layout_calculate(&input, &layout) ==
               RIBON_UPDATE_STORAGE_STATUS_OK,
           "calculate canonical A/B layout");
    expect(ribon_update_layout_calculate(&input, &repeated) ==
               RIBON_UPDATE_STORAGE_STATUS_OK &&
               ribon_update_layout_identity_encode(&layout, identity) ==
                   RIBON_UPDATE_STORAGE_STATUS_OK &&
               ribon_update_layout_identity_encode(&repeated, identity_repeated) ==
                   RIBON_UPDATE_STORAGE_STATUS_OK &&
               memcmp(identity, identity_repeated, sizeof(identity)) == 0 &&
               memcmp(layout.identity_digest, repeated.identity_digest, 32u) == 0,
           "layout identity deterministic");
    expect(ribon_update_layout_identity_open(
               identity, sizeof(identity), &reopened) ==
               RIBON_UPDATE_STORAGE_STATUS_OK &&
               reopened.media_capacity_bytes == layout.media_capacity_bytes &&
               memcmp(reopened.identity_digest, layout.identity_digest, 32u) == 0,
           "canonical layout identity independently reopens");
    memcpy(binding.layout_digest, layout.identity_digest,
        sizeof(binding.layout_digest));
    fill_digest(binding.media_identity_digest, 0xd0u);
    expect(ribon_update_storage_product_binding_is_valid(&binding),
           "product binding closes layout, media, and service identities");
    memset(binding.media_identity_digest, 0, sizeof(binding.media_identity_digest));
    expect(!ribon_update_storage_product_binding_is_valid(&binding),
           "zero media identity binding rejected");
    fill_digest(binding.media_identity_digest, 0xd0u);
    binding.provider_class = RIBON_UPDATE_STORAGE_PROVIDER_CLASS_INVALID;
    expect(!ribon_update_storage_product_binding_is_valid(&binding),
           "unknown product provider class rejected");
    binding.provider_class = RIBON_UPDATE_STORAGE_PROVIDER_CLASS_REFERENCE;
    identity[128u + 8u] ^= 1u;
    expect(ribon_update_layout_identity_open(identity, sizeof(identity), &reopened) ==
               RIBON_UPDATE_STORAGE_STATUS_MALFORMED,
           "mutated layout sequence rejected by independent reader");
    expect(ribon_update_layout_identity_open(
               identity_repeated, sizeof(identity_repeated) - 1u, &reopened) ==
               RIBON_UPDATE_STORAGE_STATUS_MALFORMED,
           "truncated layout identity rejected");
    for (index = 0u; index < RIBON_UPDATE_LAYOUT_REGION_COUNT; ++index) {
        expect(layout.regions[index].kind ==
                   (enum RibonUpdateLayoutRegionKind)(index + 1u) &&
                   layout.regions[index].length != 0u,
               "canonical region order and nonzero range");
    }
    expect(layout.regions[10].offset + layout.regions[10].length == MEDIA_BYTES,
           "trailing reserve consumes exact media capacity");
    expect(make_manifest(manifest_bytes, &written, &manifest),
           "build D01 manifest fixture");
    expect(ribon_update_manifest_required_slot_bytes(&manifest, 4096u, &required) ==
               RIBON_UPDATE_STORAGE_STATUS_OK &&
               required == 204800u &&
               ribon_update_layout_accepts_manifest(&layout, &manifest),
           "manifest maximum ranges fit both slots");

    input.allocation_alignment = 0u;
    expect(ribon_update_layout_calculate(&input, &layout) ==
               RIBON_UPDATE_STORAGE_STATUS_INVALID_ARGUMENT,
           "zero alignment rejected");
    input = layout_input();
    input.media_capacity_bytes = 4097u;
    expect(ribon_update_layout_calculate(&input, &layout) !=
               RIBON_UPDATE_STORAGE_STATUS_OK,
           "insufficient unaligned capacity rejected");
    input = layout_input();
    input.bootloader_bytes = UINT64_MAX;
    expect(ribon_update_layout_calculate(&input, &layout) ==
               RIBON_UPDATE_STORAGE_STATUS_OVERFLOW,
           "wrapping layout range rejected");
    input = layout_input();
    input.slot_metadata_bytes = 512u;
    expect(ribon_update_layout_calculate(&input, &layout) ==
               RIBON_UPDATE_STORAGE_STATUS_INVALID_ARGUMENT,
           "non-redundant metadata region rejected");
    input = layout_input();
    expect(ribon_update_layout_calculate(&input, &layout) == 0,
           "restore layout for structural hostile case");
    layout.regions[6].offset += 4096u;
    expect(ribon_update_layout_identity_encode(&layout, identity) ==
               RIBON_UPDATE_STORAGE_STATUS_INVALID_ARGUMENT,
           "gap and overlap layout mutation rejected");
}

/** @brief Metadata LE codec와 corruption/torn-write rejection을 검증한다. */
static void
test_metadata_codec(void)
{
    struct RibonUpdateLayoutInput input = layout_input();
    struct RibonUpdateLayout layout;
    struct RibonUpdateSlotMetadata metadata;
    struct RibonUpdateSlotMetadata opened;
    uint8_t wire[512];
    uint8_t mutated[512];

    expect(ribon_update_layout_calculate(&input, &layout) == 0,
           "metadata layout fixture");
    metadata = initial_metadata(&layout);
    expect(ribon_update_slot_metadata_encode(&metadata, wire) == 0 &&
               ribon_update_slot_metadata_open(wire, sizeof(wire), &opened) == 0 &&
               opened.active_slot == 0u && opened.slots[0].image_generation == 1u,
           "metadata LE encode and independent open");
    expect(ribon_update_slot_metadata_open(wire, sizeof(wire) - 1u, &opened) ==
               RIBON_UPDATE_STORAGE_STATUS_MALFORMED,
           "torn metadata rejected");
    memcpy(mutated, wire, sizeof(mutated));
    mutated[79] ^= 1u;
    expect(ribon_update_slot_metadata_open(mutated, sizeof(mutated), &opened) ==
               RIBON_UPDATE_STORAGE_STATUS_MALFORMED,
           "body corruption rejected");
    memcpy(mutated, wire, sizeof(mutated));
    mutated[500] = 1u;
    expect(ribon_update_slot_metadata_open(mutated, sizeof(mutated), &opened) ==
               RIBON_UPDATE_STORAGE_STATUS_MALFORMED,
           "reserved wire bytes rejected");
    memcpy(mutated, wire, sizeof(mutated));
    store_u32(mutated + 48u, 1u);
    refresh_metadata_integrity(mutated);
    expect(ribon_update_slot_metadata_open(mutated, sizeof(mutated), &opened) ==
               RIBON_UPDATE_STORAGE_STATUS_MALFORMED,
           "valid-integrity invalid active-slot semantics rejected");
    metadata.active_slot = 1u;
    expect(ribon_update_slot_metadata_encode(&metadata, wire) ==
               RIBON_UPDATE_STORAGE_STATUS_INVALID_ARGUMENT,
           "active slot must be confirmed");
    metadata = initial_metadata(&layout);
    metadata.slots[1].state = RIBON_UPDATE_SLOT_PENDING;
    expect(ribon_update_slot_metadata_encode(&metadata, wire) ==
               RIBON_UPDATE_STORAGE_STATUS_INVALID_ARGUMENT,
           "pending state requires complete identity and singleton marker");
}

/** @brief Slot lifecycle의 allowed edges와 identity binding을 검증한다. */
static void
test_transitions(void)
{
    struct RibonUpdateLayoutInput input = layout_input();
    struct RibonUpdateLayout layout;
    struct RibonUpdateSlotMetadata states[5];
    struct RibonUpdateSlotTransition transition;

    expect(ribon_update_layout_calculate(&input, &layout) == 0,
           "transition layout fixture");
    states[0] = initial_metadata(&layout);
    transition = transition_request(&layout, RIBON_UPDATE_SLOT_STAGING);
    expect(ribon_update_slot_metadata_transition(
               &states[0], &transition, &states[1]) == 0 &&
               states[1].metadata_generation == 2u &&
               states[1].slots[1].state == RIBON_UPDATE_SLOT_STAGING,
           "empty to staging");
    transition.next_state = RIBON_UPDATE_SLOT_VERIFIED;
    expect(ribon_update_slot_metadata_transition(
               &states[1], &transition, &states[2]) == 0,
           "staging to verified");
    transition.next_state = RIBON_UPDATE_SLOT_PENDING;
    transition.boot_attempts = 3u;
    expect(ribon_update_slot_metadata_transition(
               &states[2], &transition, &states[3]) == 0 &&
               states[3].pending_slot == 1u,
           "verified to pending");
    transition.next_state = RIBON_UPDATE_SLOT_CONFIRMED;
    transition.boot_attempts = 0u;
    expect(ribon_update_slot_metadata_transition(
               &states[3], &transition, &states[4]) == 0 &&
               states[4].active_slot == 1u &&
               states[4].pending_slot == RIBON_UPDATE_SLOT_NONE,
           "pending to confirmed atomically changes active slot");
    transition.slot_id = 1u;
    transition.next_state = RIBON_UPDATE_SLOT_STAGING;
    expect(ribon_update_slot_metadata_transition(
               &states[4], &transition, &states[0]) ==
               RIBON_UPDATE_STORAGE_STATUS_BAD_STATE,
           "active slot transition rejected");
    transition.slot_id = 0u;
    transition.next_state = RIBON_UPDATE_SLOT_VERIFIED;
    expect(ribon_update_slot_metadata_transition(
               &states[4], &transition, &states[0]) ==
               RIBON_UPDATE_STORAGE_STATUS_BAD_STATE,
           "undefined transition edge rejected");
    transition.slot_id = 0u;
    transition.next_state = RIBON_UPDATE_SLOT_STAGING;
    transition.image_set_digest[0] ^= 1u;
    expect(ribon_update_slot_metadata_transition(
               &states[4], &transition, &states[0]) == 0,
           "new staging generation may replace inactive identity");
}

/** @brief Semantic handle가 active slot과 stale generation을 보호하는지 검증한다. */
static void
test_reference_provider(void)
{
    struct RibonUpdateLayoutInput input = layout_input();
    struct RibonUpdateLayout layout;
    struct RibonUpdateSlotMetadata initial;
    struct RibonUpdateSlotMetadata staging;
    struct RibonUpdateSlotTransition transition;
    struct RibonTestStorage storage;
    struct RibonUpdateStorageSession session;
    struct RibonUpdateSlotHandle handle;
    struct RibonInactiveSlotStorageServiceOperations operations;
    struct RibonServiceDescriptor descriptor;
    uint8_t write_bytes[512];
    uint8_t read_bytes[512];

    memset(media, 0, sizeof(media));
    memset(write_bytes, 0xa5, sizeof(write_bytes));
    expect(ribon_update_layout_calculate(&input, &layout) == 0 &&
               ribon_test_storage_open_memory(
                   &storage, media, sizeof(media), 512u, TRANSFER_BYTES),
           "memory reference provider");
    operations = (struct RibonInactiveSlotStorageServiceOperations){
        .size = sizeof(operations),
        .abi_version = RIBON_SERVICE_ABI_VERSION,
        .provider = &storage.provider,
    };
    descriptor = (struct RibonServiceDescriptor){
        .kind = RIBON_SERVICE_KIND_INACTIVE_SLOT_STORAGE,
        .provides = RIBON_CAP_INACTIVE_SLOT_WRITE | RIBON_CAP_INACTIVE_SLOT_ERASE,
        .operations = &operations,
        .operations_size = sizeof(operations),
        .operations_abi = RIBON_SERVICE_ABI_VERSION,
    };
    expect(ribon_inactive_slot_storage_service_operations_are_valid(&descriptor),
           "typed service directory binds bounded provider");
    initial = initial_metadata(&layout);
    transition = transition_request(&layout, RIBON_UPDATE_SLOT_STAGING);
    expect(ribon_update_slot_metadata_transition(&initial, &transition, &staging) == 0,
           "session staging metadata");
    session = (struct RibonUpdateStorageSession){
        .size = sizeof(session),
        .abi_version = RIBON_UPDATE_STORAGE_ABI_VERSION,
        .provider = &storage.provider,
        .layout = &layout,
        .metadata = &staging,
    };
    expect(ribon_update_storage_session_is_valid(&session),
           "provider layout metadata identity closure");
    storage.provider.erase_alignment = 8192u;
    storage.provider.maximum_transfer_bytes = 8192u;
    expect(!ribon_update_storage_session_is_valid(&session),
           "provider and slot geometry mismatch rejected");
    storage.provider.erase_alignment = 512u;
    storage.provider.maximum_transfer_bytes = TRANSFER_BYTES;
    expect(ribon_update_storage_open_inactive_slot(&session, 0u, &handle) ==
               RIBON_UPDATE_STORAGE_STATUS_PROTECTED,
           "active confirmed slot protected");
    expect(ribon_update_storage_open_inactive_slot(&session, 1u, &handle) == 0,
           "open semantic inactive slot handle");
    expect(ribon_update_storage_write_inactive(
               &session, &handle, 0u, write_bytes, sizeof(write_bytes), 100u) == 0 &&
               ribon_update_storage_read(
                   &session, &handle, 0u, read_bytes, sizeof(read_bytes), 100u) == 0 &&
               memcmp(write_bytes, read_bytes, sizeof(write_bytes)) == 0,
           "exact aligned semantic slot IO");
    expect(ribon_update_storage_erase_inactive(
               &session, &handle, 0u, sizeof(read_bytes), 100u) == 0 &&
               ribon_update_storage_flush(&session, 100u) == 0 &&
               storage.erase_count == 1u && storage.flush_count == 1u,
           "explicit erase and durability barrier");
    expect(ribon_update_storage_write_inactive(
               &session, &handle, 1u, write_bytes, sizeof(write_bytes), 100u) ==
               RIBON_UPDATE_STORAGE_STATUS_ALIGNMENT,
           "misaligned write rejected before provider");
    expect(ribon_update_storage_read(
               &session, &handle, 262144u, read_bytes, sizeof(read_bytes), 100u) ==
               RIBON_UPDATE_STORAGE_STATUS_CAPACITY,
           "slot range overflow rejected");
    storage.fault = RIBON_TEST_STORAGE_FAULT_WRITE_SHORT;
    expect(ribon_update_storage_write_inactive(
               &session, &handle, 0u, write_bytes, sizeof(write_bytes), 100u) ==
               RIBON_UPDATE_STORAGE_STATUS_SHORT_IO,
           "short provider write fail-closed");
    storage.fault = RIBON_TEST_STORAGE_FAULT_READ_IO;
    expect(ribon_update_storage_read(
               &session, &handle, 0u, read_bytes, sizeof(read_bytes), 100u) ==
               RIBON_UPDATE_STORAGE_STATUS_IO,
           "provider read error mapped");
    storage.fault = RIBON_TEST_STORAGE_FAULT_NONE;
    ++staging.metadata_generation;
    expect(ribon_update_storage_write_inactive(
               &session, &handle, 0u, write_bytes, sizeof(write_bytes), 100u) ==
               RIBON_UPDATE_STORAGE_STATUS_INVALID_ARGUMENT,
           "stale handle generation rejected");
}

/** @brief File-backed reference provider가 같은 exact ABI를 구현하는지 검증한다. */
static void
test_file_provider(void)
{
    struct RibonTestStorage storage;
    FILE *file = tmpfile();

    expect(file != NULL, "create temporary reference media");
    if (file == NULL) {
        return;
    }
    expect(fseek(file, 65535L, SEEK_SET) == 0 && fputc(0, file) == 0 &&
               fflush(file) == 0 &&
               ribon_test_storage_open_file(&storage, file, 65536u, 512u, 4096u),
           "file reference provider validates");
    (void)fclose(file);
}

/** @brief Cross-tool inspection용 canonical metadata wire를 exact file로 쓴다. */
static int
emit_metadata(const char *path)
{
    struct RibonUpdateLayoutInput input = layout_input();
    struct RibonUpdateLayout layout;
    struct RibonUpdateSlotMetadata metadata;
    uint8_t wire[RIBON_UPDATE_SLOT_METADATA_BYTES];
    FILE *file;

    if (path == NULL || ribon_update_layout_calculate(&input, &layout) != 0) {
        return 0;
    }
    metadata = initial_metadata(&layout);
    if (ribon_update_slot_metadata_encode(&metadata, wire) != 0) {
        return 0;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        return 0;
    }
    if (fwrite(wire, 1u, sizeof(wire), file) != sizeof(wire) || fclose(file) != 0) {
        return 0;
    }
    return 1;
}

/** @brief Cross-tool comparison용 canonical layout identity를 exact file로 쓴다. */
static int
emit_layout(const char *path)
{
    struct RibonUpdateLayoutInput input = {
        .size = sizeof(input),
        .abi_version = RIBON_UPDATE_STORAGE_ABI_VERSION,
        .media_capacity_bytes = UINT64_C(67108864),
        .allocation_alignment = 4096u,
        .guard_gap_bytes = 4096u,
        .bootloader_bytes = UINT64_C(1048576),
        .immutable_recovery_bytes = UINT64_C(2097152),
        .slot_payload_bytes = UINT64_C(16777216),
        .slot_metadata_bytes = 4096u,
        .update_journal_bytes = 65536u,
        .minimum_trailing_reserved_bytes = UINT64_C(1048576),
    };
    struct RibonUpdateLayout layout;
    uint8_t identity[RIBON_UPDATE_LAYOUT_IDENTITY_BYTES];
    FILE *file;

    if (path == NULL || ribon_update_layout_calculate(&input, &layout) != 0 ||
        ribon_update_layout_identity_encode(&layout, identity) != 0) {
        return 0;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        return 0;
    }
    if (fwrite(identity, 1u, sizeof(identity), file) != sizeof(identity) ||
        fclose(file) != 0) {
        return 0;
    }
    return 1;
}

int
main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--emit-metadata") == 0) {
        return emit_metadata(argv[2]) ? 0 : 1;
    }
    if (argc == 3 && strcmp(argv[1], "--emit-layout") == 0) {
        return emit_layout(argv[2]) ? 0 : 1;
    }
    if (argc != 1) {
        (void)fprintf(
            stderr, "usage: storage_tests [--emit-metadata PATH|--emit-layout PATH]\n");
        return 2;
    }
    test_layout_and_manifest();
    test_metadata_codec();
    test_transitions();
    test_reference_provider();
    test_file_provider();
    if (failures != 0) {
        (void)fprintf(stderr, "RIBON-UPDATE-STORAGE-V1-FAIL failures=%d\n", failures);
        return 1;
    }
    (void)printf(
        "RIBON-UPDATE-STORAGE-V1-OK layout=ab metadata=le512 provider=memory,file\n");
    return 0;
}
