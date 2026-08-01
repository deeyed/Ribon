#include <Ribon/security/ed25519.h>
#include <Ribon/update/manifest.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MANIFEST_CAPACITY \
    (RIBON_UPDATE_MANIFEST_HEADER_BYTES + \
     RIBON_UPDATE_MANIFEST_BINDING_BYTES + \
     RIBON_UPDATE_MANIFEST_MAX_COMPONENTS * \
         RIBON_UPDATE_MANIFEST_COMPONENT_BYTES)
#define ENVELOPE_CAPACITY \
    (RIBON_UPDATE_SIGNATURE_ENVELOPE_HEADER_BYTES + \
     RIBON_UPDATE_SIGNATURE_KEY_ID_MAX_BYTES + RIBON_ED25519_SIGNATURE_BYTES)

static int failures;

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
static const uint8_t fixture_store_id[] = "update.manifest.tests.v1";

/** @brief Unit assertion 실패를 stable 이름과 함께 누적한다. */
static void
expect(int condition, const char *name)
{
    if (!condition) {
        (void)fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

/** @brief 한 canonical hexadecimal SHA-256을 caller-owned digest로 decode한다. */
static int
decode_digest(const char *text, uint8_t digest[32])
{
    size_t index;

    if (text == NULL || strlen(text) != 64u) {
        return 0;
    }
    for (index = 0u; index < 32u; ++index) {
        unsigned int value;

        if (sscanf(text + index * 2u, "%2x", &value) != 1) {
            return 0;
        }
        digest[index] = (uint8_t)value;
    }
    return 1;
}

/** @brief Test-only little-endian u16 mutation을 byte buffer에 쓴다. */
static void
write_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
}

/** @brief Test-only little-endian u64 mutation을 byte buffer에 쓴다. */
static void
write_u64(uint8_t *bytes, uint64_t value)
{
    uint32_t index;

    for (index = 0u; index < 8u; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
}

/** @brief Canonical cross-tool vector와 같은 native manifest 입력을 구성한다. */
static int
build_vector_input(
    struct RibonUpdateManifestInput *input,
    struct RibonUpdateComponent components[2])
{
    *components = (struct RibonUpdateComponent){
        .size = sizeof(components[0]),
        .abi_version = RIBON_UPDATE_MANIFEST_ABI_VERSION,
        .flags = RIBON_UPDATE_COMPONENT_REQUIRED,
        .install_order = 0u,
        .role = RIBON_UPDATE_COMPONENT_ROLE_KERNEL,
        .destination_class = RIBON_UPDATE_DESTINATION_KERNEL_SLOT,
        .image_format = RIBON_UPDATE_IMAGE_FORMAT_ELF64,
        .bundle_offset = 0u,
        .exact_size = 74u,
        .maximum_size = UINT64_C(1048576),
    };
    components[1] = (struct RibonUpdateComponent){
        .size = sizeof(components[1]),
        .abi_version = RIBON_UPDATE_MANIFEST_ABI_VERSION,
        .flags = RIBON_UPDATE_COMPONENT_REQUIRED,
        .install_order = 1u,
        .role = RIBON_UPDATE_COMPONENT_ROLE_POLICY,
        .destination_class = RIBON_UPDATE_DESTINATION_POLICY_SLOT,
        .image_format = RIBON_UPDATE_IMAGE_FORMAT_OPAQUE,
        .bundle_offset = 4096u,
        .exact_size = 69u,
        .maximum_size = UINT64_C(65536),
    };
    if (!decode_digest(
            "ad44d563759b9d777d9a356f982becadd3af2d07d1bc8df12efe9ded368fc8a2",
            components[0].logical_id_digest) ||
        !decode_digest(
            "d82a4dc818ae66d3ae55f37346bc7a4814de040394eb3c28abe8dc95decd43d6",
            components[0].content_digest) ||
        !decode_digest(
            "a2e6b6e9eb023219589c9626900256c5a04afcee2625b4112c18eac898ae8bb0",
            components[0].destination_id_digest) ||
        !decode_digest(
            "d05e58afb0e9977ef7e4df38dc8dddc81e937ba0bcb412c6b11841576a7163f6",
            components[0].entry_contract_digest) ||
        !decode_digest(
            "d4fac7a4650fad96cfe4006fbc2d690254c4a1a31c2b3c1d7b1f4e8b8d0a11d1",
            components[1].logical_id_digest) ||
        !decode_digest(
            "010c25a18fc974d27db612bdb8ce98eee667a4d2971e0a1bd58b7857ae3bd7ac",
            components[1].content_digest) ||
        !decode_digest(
            "765c793a1dd2b0797292516f5e96ea34f821f752c05e90c97fe514c27445e4b7",
            components[1].destination_id_digest) ||
        !decode_digest(
            "fce7d3e15cde07689ca645253d7cbe414250c7e18cf104df863ff973c93bd404",
            components[1].entry_contract_digest)) {
        return 0;
    }
    *input = (struct RibonUpdateManifestInput){
        .size = sizeof(*input),
        .abi_version = RIBON_UPDATE_MANIFEST_ABI_VERSION,
        .mode = RIBON_KEY_POLICY_MODE_RECOVERY,
        .bundle_generation = 9u,
        .predecessor_generation = 8u,
        .rollback_sequence = 42u,
        .creation_policy_version = 3u,
        .protocol_major = 1u,
        .protocol_minor = 0u,
        .minimum_hardware_revision = 2u,
        .maximum_hardware_revision = 7u,
        .components = components,
        .component_count = 2u,
    };
    return decode_digest(
               "c8dd6719a47d51c6192f03f41ddfeaa0d1c982ffdac7b60676a733286c10f0e7",
               input->schema_digest) &&
        decode_digest(
            "9397213ff737f7ab5542a5a8664c58009c52cfa873545ec7abb5084aa353ca53",
            input->product_digest) &&
        decode_digest(
            "13d89bdf8d710f5c1958b0479ee563f9def7801fd06418f77e9bc847d0355174",
            input->architecture_digest) &&
        decode_digest(
            "755472fa199203b54941b580f1dddebae7a44910e2fe48cad599f74f301f2a38",
            input->platform_digest) &&
        decode_digest(
            "75f172b8e412e85fd6a8539a542aa1df8983b49e635e49a117acd84d642b89de",
            input->environment_digest) &&
        decode_digest(
            "207e27ce8417fcc634ca38f9dfa3008333ffbf8e804aaab723a992bb64795895",
            input->protocol_digest) &&
        decode_digest(
            "75f75820e4d9ee2bff1e0feb0d05db3099b77f7f363ffb5b0eb2b32d1b44bce5",
            input->rollback_domain_digest);
}

/** @brief Canonical vector manifest를 caller-owned fixed buffer에 생성한다. */
static int
encode_vector(uint8_t manifest[MANIFEST_CAPACITY], size_t *size)
{
    struct RibonUpdateManifestInput input;
    struct RibonUpdateComponent components[2];

    return build_vector_input(&input, components) &&
        ribon_update_manifest_encode(
            &input,
            manifest,
            MANIFEST_CAPACITY,
            size) == RIBON_UPDATE_MANIFEST_STATUS_OK;
}

/** @brief Canonical vector의 deterministic encode와 independent open을 검사한다. */
static void
test_encode_and_open(void)
{
    uint8_t first[MANIFEST_CAPACITY];
    uint8_t second[MANIFEST_CAPACITY];
    struct RibonUpdateManifestView view;
    struct RibonUpdateComponentView component;
    size_t first_size = 0u;
    size_t second_size = 0u;

    expect(encode_vector(first, &first_size), "encode canonical vector");
    expect(encode_vector(second, &second_size), "repeat canonical vector");
    expect(first_size == 896u && first_size == second_size &&
               memcmp(first, second, first_size) == 0,
           "byte-identical deterministic encode");
    expect(ribon_update_manifest_open(first, first_size, &view) ==
               RIBON_UPDATE_MANIFEST_STATUS_OK &&
               view.component_count == 2u && view.bundle_generation == 9u &&
               view.rollback_sequence == 42u,
           "independent open derives manifest");
    expect(ribon_update_manifest_component_at(&view, 1u, &component) ==
               RIBON_UPDATE_MANIFEST_STATUS_OK &&
               component.role == RIBON_UPDATE_COMPONENT_ROLE_POLICY &&
               component.bundle_offset == 4096u && component.exact_size == 69u,
           "component row view");
    expect(ribon_update_manifest_component_at(&view, 2u, &component) ==
               RIBON_UPDATE_MANIFEST_STATUS_INVALID_ARGUMENT &&
               component.exact_size == 0u,
           "component index bound");
    view.component_count = RIBON_UPDATE_MANIFEST_MAX_COMPONENTS;
    view.components_offset = 0u;
    expect(ribon_update_manifest_component_at(&view, 1u, &component) ==
               RIBON_UPDATE_MANIFEST_STATUS_OK &&
               component.role == RIBON_UPDATE_COMPONENT_ROLE_POLICY,
           "component access reopens borrowed bytes");
    first[512u + 164u] = 1u;
    expect(ribon_update_manifest_component_at(&view, 0u, &component) ==
               RIBON_UPDATE_MANIFEST_STATUS_INVALID_ARGUMENT &&
               component.exact_size == 0u,
           "component access rejects post-open mutation");
}

/** @brief Encoder가 native singleton, overlap, wrap와 capacity 실패를 거부하는지 검사한다. */
static void
test_encode_failures(void)
{
    struct RibonUpdateManifestInput input;
    struct RibonUpdateComponent components[2];
    uint8_t output[MANIFEST_CAPACITY];
    size_t written = 77u;

    expect(build_vector_input(&input, components), "build encoder negative input");
    expect(ribon_update_manifest_encode(
               &input, output, 895u, &written) ==
               RIBON_UPDATE_MANIFEST_STATUS_CAPACITY && written == 0u,
           "short output capacity");
    components[1].role = RIBON_UPDATE_COMPONENT_ROLE_KERNEL;
    components[1].destination_class = RIBON_UPDATE_DESTINATION_KERNEL_SLOT;
    expect(ribon_update_manifest_encode(
               &input, output, sizeof(output), &written) ==
               RIBON_UPDATE_MANIFEST_STATUS_MALFORMED,
           "duplicate singleton role");
    expect(build_vector_input(&input, components), "restore overlap input");
    components[1].bundle_offset = 32u;
    expect(ribon_update_manifest_encode(
               &input, output, sizeof(output), &written) ==
               RIBON_UPDATE_MANIFEST_STATUS_MALFORMED,
           "component overlap");
    expect(build_vector_input(&input, components), "restore wrap input");
    components[1].bundle_offset = UINT64_MAX;
    expect(ribon_update_manifest_encode(
               &input, output, sizeof(output), &written) ==
               RIBON_UPDATE_MANIFEST_STATUS_INVALID_ARGUMENT,
           "component range wrap");
    expect(build_vector_input(&input, components), "restore zero input");
    components[0].exact_size = 0u;
    expect(ribon_update_manifest_encode(
               &input, output, sizeof(output), &written) ==
               RIBON_UPDATE_MANIFEST_STATUS_INVALID_ARGUMENT,
           "zero component size");
}

/** @brief Independent reader가 hostile wire mutation을 fail closed하는지 검사한다. */
static void
test_reader_hostile(void)
{
    uint8_t canonical[MANIFEST_CAPACITY + 1u];
    uint8_t mutated[MANIFEST_CAPACITY + 1u];
    struct RibonUpdateManifestView view;
    size_t size = 0u;

    expect(encode_vector(canonical, &size), "encode hostile baseline");
    expect(ribon_update_manifest_open(canonical, size - 1u, &view) !=
               RIBON_UPDATE_MANIFEST_STATUS_OK,
           "truncated manifest");
    canonical[size] = 0u;
    expect(ribon_update_manifest_open(canonical, size + 1u, &view) !=
               RIBON_UPDATE_MANIFEST_STATUS_OK,
           "trailing byte");

    memcpy(mutated, canonical, size);
    write_u64(mutated + 136u, UINT64_MAX);
    expect(ribon_update_manifest_open(mutated, size, &view) ==
               RIBON_UPDATE_MANIFEST_STATUS_MALFORMED,
           "section offset wrap/noncanonical");
    memcpy(mutated, canonical, size);
    write_u16(mutated + 512u + 152u, UINT16_C(0xffff));
    expect(ribon_update_manifest_open(mutated, size, &view) ==
               RIBON_UPDATE_MANIFEST_STATUS_MALFORMED,
           "unknown component role");
    memcpy(mutated, canonical, size);
    write_u16(mutated + 512u + 158u, UINT16_C(0x8000));
    expect(ribon_update_manifest_open(mutated, size, &view) ==
               RIBON_UPDATE_MANIFEST_STATUS_MALFORMED,
           "unknown required flag");
    memcpy(mutated, canonical, size);
    mutated[512u + 164u] = 1u;
    expect(ribon_update_manifest_open(mutated, size, &view) ==
               RIBON_UPDATE_MANIFEST_STATUS_MALFORMED,
           "nonzero component reserved");
    memcpy(mutated, canonical, size);
    memcpy(mutated + 704u, mutated + 512u, 32u);
    expect(ribon_update_manifest_open(mutated, size, &view) ==
               RIBON_UPDATE_MANIFEST_STATUS_MALFORMED,
           "duplicate logical ID");
    memcpy(mutated, canonical, size);
    write_u64(mutated + 704u + 128u, 32u);
    expect(ribon_update_manifest_open(mutated, size, &view) ==
               RIBON_UPDATE_MANIFEST_STATUS_MALFORMED,
           "wire component overlap");
    memcpy(mutated, canonical, size);
    write_u64(mutated + 512u + 136u, 0u);
    expect(ribon_update_manifest_open(mutated, size, &view) ==
               RIBON_UPDATE_MANIFEST_STATUS_MALFORMED,
           "wire zero component");
    memcpy(mutated, canonical, size);
    write_u16(mutated + 704u + 152u, RIBON_UPDATE_COMPONENT_ROLE_KERNEL);
    write_u16(mutated + 704u + 154u, RIBON_UPDATE_DESTINATION_KERNEL_SLOT);
    expect(ribon_update_manifest_open(mutated, size, &view) ==
               RIBON_UPDATE_MANIFEST_STATUS_MALFORMED,
           "wire duplicate singleton");
}

/** @brief Signed message와 detached envelope의 canonical binding을 검사한다. */
static void
test_message_and_envelope(void)
{
    uint8_t manifest[MANIFEST_CAPACITY];
    uint8_t message[RIBON_UPDATE_SIGNED_MESSAGE_V1_BYTES];
    uint8_t signature[RIBON_ED25519_SIGNATURE_BYTES];
    uint8_t envelope[ENVELOPE_CAPACITY];
    uint8_t mutated[ENVELOPE_CAPACITY];
    struct RibonUpdateManifestView manifest_view;
    struct RibonUpdateSignatureEnvelopeView envelope_view;
    struct RibonUpdateSignatureEnvelopeInput input;
    size_t manifest_size = 0u;
    size_t envelope_size = 0u;
    size_t index;

    expect(encode_vector(manifest, &manifest_size), "encode envelope baseline");
    expect(ribon_update_manifest_open(manifest, manifest_size, &manifest_view) ==
               RIBON_UPDATE_MANIFEST_STATUS_OK,
           "open envelope baseline");
    expect(ribon_update_manifest_signed_message_v1(
               &manifest_view,
               fixture_key_id,
               sizeof(fixture_key_id) - 1u,
               message) == RIBON_UPDATE_MANIFEST_STATUS_OK,
           "canonical update signed message");
    for (index = 0u; index < sizeof(signature); ++index) {
        signature[index] = (uint8_t)(index + 1u);
    }
    input = (struct RibonUpdateSignatureEnvelopeInput){
        .size = sizeof(input),
        .abi_version = RIBON_UPDATE_MANIFEST_ABI_VERSION,
        .manifest = manifest,
        .manifest_size = manifest_size,
        .key_id = fixture_key_id,
        .key_id_size = sizeof(fixture_key_id) - 1u,
        .signature = signature,
        .signature_size = sizeof(signature),
    };
    expect(ribon_update_signature_envelope_encode(
               &input,
               envelope,
               sizeof(envelope),
               &envelope_size) == RIBON_UPDATE_MANIFEST_STATUS_OK,
           "encode detached signature envelope");
    expect(ribon_update_signature_envelope_open(
               envelope,
               envelope_size,
               &envelope_view) == RIBON_UPDATE_MANIFEST_STATUS_OK &&
               envelope_view.manifest_size == manifest_size &&
               envelope_view.key_id_size == sizeof(fixture_key_id) - 1u &&
               memcmp(
                   envelope_view.key_id,
                   fixture_key_id,
                   sizeof(fixture_key_id) - 1u) == 0,
           "open detached signature envelope");
    memcpy(mutated, envelope, envelope_size);
    write_u64(mutated + 80u, UINT64_MAX);
    expect(ribon_update_signature_envelope_open(
               mutated,
               envelope_size,
               &envelope_view) == RIBON_UPDATE_MANIFEST_STATUS_MALFORMED,
           "envelope offset wrap");
    expect(ribon_update_signature_envelope_open(
               envelope,
               envelope_size - 1u,
               &envelope_view) == RIBON_UPDATE_MANIFEST_STATUS_MALFORMED,
           "envelope truncation");
}

/** @brief Manifest view에서 exact product expectation을 복사한다. */
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
    memcpy(
        expectation->rollback_domain_digest,
        view->rollback_domain_digest,
        32u);
}

/** @brief Vector identity로 one-key immutable update trust store를 구성한다. */
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
        .minimum_sequence = 42u,
        .maximum_sequence = 42u,
    };
    memcpy(record->public_key, fixture_public_key, sizeof(record->public_key));
    memcpy(
        record->key_identity_digest,
        fixture_key_identity,
        sizeof(record->key_identity_digest));
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
               store,
               store->canonical_digest) == RIBON_KEY_POLICY_STATUS_OK &&
        ribon_key_policy_store_validate(store) == RIBON_KEY_POLICY_STATUS_OK;
}

/** @brief Fixed-capacity test buffer로 한 binary input을 exact read한다. */
static int
read_file(const char *path, uint8_t *bytes, size_t capacity, size_t *size)
{
    FILE *stream;
    int extra;

    *size = 0u;
    stream = fopen(path, "rb");
    if (stream == NULL) {
        return 0;
    }
    *size = fread(bytes, 1u, capacity, stream);
    extra = fgetc(stream);
    if (ferror(stream) != 0 || extra != EOF || fclose(stream) != 0) {
        *size = 0u;
        return 0;
    }
    return *size != 0u;
}

/** @brief Real Ed25519 provider로 host-generated manifest와 envelope를 승인한다. */
static int
authorize_files(const char *manifest_path, const char *envelope_path)
{
    uint8_t manifest[MANIFEST_CAPACITY];
    uint8_t envelope[ENVELOPE_CAPACITY];
    struct RibonUpdateManifestView opened;
    struct RibonUpdateManifestView authorized;
    struct RibonUpdateManifestExpectation expectation;
    struct RibonKeyPolicyRecord record;
    struct RibonKeyPolicyStore store;
    struct RibonKeyPolicyDecision decision;
    struct RibonUpdateManifestAuthorization authorization;
    size_t manifest_size;
    size_t envelope_size;
    int status;

    if (!read_file(
            manifest_path,
            manifest,
            sizeof(manifest),
            &manifest_size) ||
        !read_file(
            envelope_path,
            envelope,
            sizeof(envelope),
            &envelope_size) ||
        ribon_update_manifest_open(manifest, manifest_size, &opened) !=
            RIBON_UPDATE_MANIFEST_STATUS_OK ||
        !store_from_view(&opened, &record, &store)) {
        return 1;
    }
    expectation_from_view(&opened, &expectation);
    authorization = (struct RibonUpdateManifestAuthorization){
        .size = sizeof(authorization),
        .abi_version = RIBON_UPDATE_MANIFEST_ABI_VERSION,
        .manifest = manifest,
        .manifest_size = manifest_size,
        .signature_envelope = envelope,
        .signature_envelope_size = envelope_size,
        .expectation = &expectation,
        .key_policy = &store,
        .signature_provider = &ribon_ed25519_signature_provider_descriptor,
    };
    status = ribon_update_manifest_authorize(
        &authorization,
        &authorized,
        &decision);
    if (status != RIBON_UPDATE_MANIFEST_STATUS_OK ||
        authorized.rollback_sequence != 42u || decision.record_index != 0u) {
        (void)fprintf(stderr, "RIBON-UPDATE-AUTHORIZE-FAIL status=%d\n", status);
        return 1;
    }
    (void)printf(
        "RIBON-UPDATE-AUTHORIZE-OK usage=update-manifest "
        "sequence=42 provider=ed25519\n");
    return 0;
}

/** @brief Hostile-corpus runner가 independent C reader를 호출할 수 있게 한다. */
static int
open_file(const char *path)
{
    uint8_t manifest[MANIFEST_CAPACITY];
    struct RibonUpdateManifestView view;
    size_t size;

    return read_file(path, manifest, sizeof(manifest), &size) &&
        ribon_update_manifest_open(manifest, size, &view) ==
            RIBON_UPDATE_MANIFEST_STATUS_OK ? 0 : 1;
}

/** @brief Canonical byte vector를 lowercase hexadecimal로 출력한다. */
static void
dump_hex(const uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0u; index < size; ++index) {
        (void)printf("%02x", bytes[index]);
    }
    (void)putchar('\n');
}

/** @brief C codec가 만든 canonical manifest 또는 signed-message vector를 출력한다. */
static int
dump_vector(int message_only)
{
    uint8_t manifest[MANIFEST_CAPACITY];
    uint8_t message[RIBON_UPDATE_SIGNED_MESSAGE_V1_BYTES];
    struct RibonUpdateManifestView view;
    size_t size = 0u;

    if (!encode_vector(manifest, &size)) {
        return 1;
    }
    if (!message_only) {
        dump_hex(manifest, size);
        return 0;
    }
    if (ribon_update_manifest_open(manifest, size, &view) !=
            RIBON_UPDATE_MANIFEST_STATUS_OK ||
        ribon_update_manifest_signed_message_v1(
            &view,
            fixture_key_id,
            sizeof(fixture_key_id) - 1u,
            message) != RIBON_UPDATE_MANIFEST_STATUS_OK) {
        return 1;
    }
    dump_hex(message, sizeof(message));
    return 0;
}

/** @brief Update manifest unit, vector와 hosted authorization entry를 실행한다. */
int
main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--dump-manifest-vector") == 0) {
        return dump_vector(0);
    }
    if (argc == 2 && strcmp(argv[1], "--dump-message-vector") == 0) {
        return dump_vector(1);
    }
    if (argc == 3 && strcmp(argv[1], "--open") == 0) {
        return open_file(argv[2]);
    }
    if (argc == 4 && strcmp(argv[1], "--authorize") == 0) {
        return authorize_files(argv[2], argv[3]);
    }
    if (argc != 1) {
        return 2;
    }
    test_encode_and_open();
    test_encode_failures();
    test_reader_hostile();
    test_message_and_envelope();
    if (failures != 0) {
        (void)fprintf(stderr, "RIBON-UPDATE-MANIFEST-TEST-FAIL count=%d\n", failures);
        return 1;
    }
    (void)printf(
        "RIBON-UPDATE-MANIFEST-TEST-OK components=2 sections=2 "
        "hostile=11 envelope=detached\n");
    return 0;
}
