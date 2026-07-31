#include <Ribon/security/ed25519.h>
#include <Ribon/security/key_policy.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;
static int fixture_provider_calls;

static const uint8_t key_id_intermediate[] = "key-intermediate";
static const uint8_t key_id_new[] = "key-new";
static const uint8_t key_id_old[] = "key-old";
static const uint8_t key_id_root[] = "key-root";
static const uint8_t store_id[] = "trust.key-policy-tests.v1";
static const uint8_t domain_digests[][RIBON_KEY_POLICY_DIGEST_BYTES] = {
    {
        0xc3u, 0x88u, 0x4bu, 0x92u, 0xb6u, 0x4au, 0xa5u, 0xf2u,
        0x29u, 0x48u, 0xa6u, 0x07u, 0x0du, 0x8cu, 0x83u, 0xdau,
        0xffu, 0x5au, 0x60u, 0xd8u, 0x54u, 0xa1u, 0x24u, 0x67u,
        0x16u, 0x8bu, 0x2du, 0xaeu, 0xa6u, 0x74u, 0x8bu, 0x1au,
    },
};
static const uint8_t product_digest[RIBON_KEY_POLICY_DIGEST_BYTES] = {
    0xaau, 0xaau, 0xaau, 0xaau, 0xaau, 0xaau, 0xaau, 0xaau,
    0xaau, 0xaau, 0xaau, 0xaau, 0xaau, 0xaau, 0xaau, 0xaau,
    0xaau, 0xaau, 0xaau, 0xaau, 0xaau, 0xaau, 0xaau, 0xaau,
    0xaau, 0xaau, 0xaau, 0xaau, 0xaau, 0xaau, 0xaau, 0xaau,
};
static const uint8_t public_keys[4][RIBON_ED25519_PUBLIC_KEY_BYTES] = {
    {
        0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u,
        0x09u, 0x0au, 0x0bu, 0x0cu, 0x0du, 0x0eu, 0x0fu, 0x10u,
        0x11u, 0x12u, 0x13u, 0x14u, 0x15u, 0x16u, 0x17u, 0x18u,
        0x19u, 0x1au, 0x1bu, 0x1cu, 0x1du, 0x1eu, 0x1fu, 0x20u,
    },
    {
        0x21u, 0x22u, 0x23u, 0x24u, 0x25u, 0x26u, 0x27u, 0x28u,
        0x29u, 0x2au, 0x2bu, 0x2cu, 0x2du, 0x2eu, 0x2fu, 0x30u,
        0x31u, 0x32u, 0x33u, 0x34u, 0x35u, 0x36u, 0x37u, 0x38u,
        0x39u, 0x3au, 0x3bu, 0x3cu, 0x3du, 0x3eu, 0x3fu, 0x40u,
    },
    {
        0x41u, 0x42u, 0x43u, 0x44u, 0x45u, 0x46u, 0x47u, 0x48u,
        0x49u, 0x4au, 0x4bu, 0x4cu, 0x4du, 0x4eu, 0x4fu, 0x50u,
        0x51u, 0x52u, 0x53u, 0x54u, 0x55u, 0x56u, 0x57u, 0x58u,
        0x59u, 0x5au, 0x5bu, 0x5cu, 0x5du, 0x5eu, 0x5fu, 0x60u,
    },
    {
        0xd7u, 0x5au, 0x98u, 0x01u, 0x82u, 0xb1u, 0x0au, 0xb7u,
        0xd5u, 0x4bu, 0xfeu, 0xd3u, 0xc9u, 0x64u, 0x07u, 0x3au,
        0x0eu, 0xe1u, 0x72u, 0xf3u, 0xdau, 0xa6u, 0x23u, 0x25u,
        0xafu, 0x02u, 0x1au, 0x68u, 0xf7u, 0x07u, 0x51u, 0x1au,
    },
};
static const uint8_t key_identity_digests[4][RIBON_KEY_POLICY_DIGEST_BYTES] = {
    {
        0xaeu, 0x21u, 0x6cu, 0x2eu, 0xf5u, 0x24u, 0x7au, 0x37u,
        0x82u, 0xc1u, 0x35u, 0xefu, 0xa2u, 0x79u, 0xa3u, 0xe4u,
        0xcdu, 0xc6u, 0x10u, 0x94u, 0x27u, 0x0fu, 0x5du, 0x2bu,
        0xe5u, 0x8cu, 0x62u, 0x04u, 0xb7u, 0xa6u, 0x12u, 0xc9u,
    },
    {
        0x7eu, 0xeeu, 0x58u, 0x00u, 0xddu, 0xcdu, 0x3bu, 0x3cu,
        0xc9u, 0xfdu, 0x04u, 0x78u, 0x31u, 0xcdu, 0x85u, 0x36u,
        0xe3u, 0xc3u, 0xf5u, 0x7fu, 0x44u, 0xd7u, 0x46u, 0xf5u,
        0x15u, 0xdau, 0x93u, 0xf0u, 0x48u, 0xeeu, 0x9eu, 0x91u,
    },
    {
        0xceu, 0x55u, 0xa9u, 0xa1u, 0xd0u, 0x46u, 0xd0u, 0x91u,
        0x3bu, 0x70u, 0xb4u, 0x12u, 0x56u, 0xf6u, 0x41u, 0x55u,
        0x05u, 0xa3u, 0x27u, 0xafu, 0x3fu, 0x19u, 0x41u, 0x28u,
        0x9eu, 0x61u, 0xf9u, 0x63u, 0x6bu, 0x46u, 0xf7u, 0x94u,
    },
    {
        0x21u, 0xfeu, 0x31u, 0xdfu, 0xa1u, 0x54u, 0xa2u, 0x61u,
        0x62u, 0x6bu, 0xf8u, 0x54u, 0x04u, 0x6fu, 0xd2u, 0x27u,
        0x1bu, 0x7bu, 0xedu, 0x4bu, 0x6au, 0xbeu, 0x45u, 0xaau,
        0x58u, 0x87u, 0x7eu, 0xf4u, 0x7fu, 0x97u, 0x21u, 0xb9u,
    },
};
static const uint8_t rfc8032_signature[64] = {
    0xe5u, 0x56u, 0x43u, 0x00u, 0xc3u, 0x60u, 0xacu, 0x72u,
    0x90u, 0x86u, 0xe2u, 0xccu, 0x80u, 0x6eu, 0x82u, 0x8au,
    0x84u, 0x87u, 0x7fu, 0x1eu, 0xb8u, 0xe5u, 0xd9u, 0x74u,
    0xd8u, 0x73u, 0xe0u, 0x65u, 0x22u, 0x49u, 0x01u, 0x55u,
    0x5fu, 0xb8u, 0x82u, 0x15u, 0x90u, 0xa3u, 0x3bu, 0xacu,
    0xc6u, 0x1eu, 0x39u, 0x70u, 0x1cu, 0xf9u, 0xb4u, 0x6bu,
    0xd2u, 0x5bu, 0xf5u, 0xf0u, 0x59u, 0x5bu, 0xbeu, 0x24u,
    0x65u, 0x51u, 0x41u, 0x43u, 0x8eu, 0x7au, 0x10u, 0x0bu,
};

static void
expect(int condition, const char *name)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

static void
record_initialize(
    struct RibonKeyPolicyRecord *record,
    const uint8_t *key_id,
    size_t key_id_size,
    uint32_t key_index,
    enum RibonKeyPolicyLifecycle lifecycle,
    uint64_t minimum,
    uint64_t maximum,
    const uint8_t *issuer,
    size_t issuer_size,
    uint32_t depth)
{
    *record = (struct RibonKeyPolicyRecord){
        .size = sizeof(*record),
        .abi_version = RIBON_KEY_POLICY_ABI_VERSION,
        .flags = issuer == NULL ? RIBON_KEY_POLICY_RECORD_ROOT : 0u,
        .algorithm = RIBON_SIGNATURE_ALGORITHM_ED25519,
        .lifecycle = lifecycle,
        .mode_mask = 1u,
        .usage_mask = UINT64_C(1),
        .key_id = key_id,
        .key_id_size = key_id_size,
        .rollback_domain_digests = domain_digests,
        .rollback_domain_count = 1u,
        .delegation_depth = depth,
        .issuer_key_id = issuer,
        .issuer_key_id_size = issuer_size,
        .minimum_sequence = minimum,
        .maximum_sequence = maximum,
    };
    memcpy(record->public_key, public_keys[key_index], sizeof(record->public_key));
    memcpy(
        record->key_identity_digest,
        key_identity_digests[key_index],
        sizeof(record->key_identity_digest));
    memcpy(
        record->product_digest,
        product_digest,
        sizeof(record->product_digest));
}

static void
store_initialize(
    struct RibonKeyPolicyStore *store,
    struct RibonKeyPolicyRecord records[4])
{
    record_initialize(
        &records[0], key_id_intermediate, sizeof(key_id_intermediate) - 1u,
        0u, RIBON_KEY_POLICY_LIFECYCLE_ACTIVE, 0u, 100u,
        key_id_root, sizeof(key_id_root) - 1u, 1u);
    record_initialize(
        &records[1], key_id_new, sizeof(key_id_new) - 1u,
        1u, RIBON_KEY_POLICY_LIFECYCLE_ACTIVE, 18u, 100u,
        key_id_intermediate, sizeof(key_id_intermediate) - 1u, 2u);
    record_initialize(
        &records[2], key_id_old, sizeof(key_id_old) - 1u,
        2u, RIBON_KEY_POLICY_LIFECYCLE_RETIRING, 10u, 20u,
        key_id_root, sizeof(key_id_root) - 1u, 1u);
    record_initialize(
        &records[3], key_id_root, sizeof(key_id_root) - 1u,
        3u, RIBON_KEY_POLICY_LIFECYCLE_ACTIVE, 0u, 100u,
        NULL, 0u, 0u);
    *store = (struct RibonKeyPolicyStore){
        .magic = RIBON_KEY_POLICY_STORE_MAGIC,
        .size = sizeof(*store),
        .abi_version = RIBON_KEY_POLICY_ABI_VERSION,
        .id = store_id,
        .id_size = sizeof(store_id) - 1u,
        .generation = 7u,
        .records = records,
        .record_count = 4u,
    };
    expect(
        ribon_key_policy_store_canonical_digest(
            store,
            store->canonical_digest) == RIBON_KEY_POLICY_STATUS_OK,
        "canonical digest generation");
}

static struct RibonKeyPolicyRequest
request_for(const uint8_t *key_id, size_t key_id_size, uint64_t sequence)
{
    struct RibonKeyPolicyRequest request = {
        .size = sizeof(request),
        .abi_version = RIBON_KEY_POLICY_ABI_VERSION,
        .mode = RIBON_KEY_POLICY_MODE_NORMAL,
        .usage = RIBON_KEY_POLICY_USAGE_POLICY_NORMAL,
        .key_id = key_id,
        .key_id_size = key_id_size,
        .sequence = sequence,
    };

    memcpy(request.product_digest, product_digest, sizeof(request.product_digest));
    memcpy(
        request.rollback_domain_digest,
        domain_digests[0],
        sizeof(request.rollback_domain_digest));
    return request;
}

static void
refresh_store_digest(struct RibonKeyPolicyStore *store)
{
    expect(
        ribon_key_policy_store_canonical_digest(
            store,
            store->canonical_digest) == RIBON_KEY_POLICY_STATUS_OK,
        "refresh canonical digest");
}

static void
test_store_and_delegation(void)
{
    struct RibonKeyPolicyRecord records[4];
    struct RibonKeyPolicyStore store;
    struct RibonKeyPolicyDecision decision;
    struct RibonKeyPolicyRequest request;

    store_initialize(&store, records);
    expect(ribon_key_policy_store_validate(&store) == RIBON_KEY_POLICY_STATUS_OK,
           "valid bounded store");
    request = request_for(key_id_new, sizeof(key_id_new) - 1u, 18u);
    expect(ribon_key_policy_authorize(&store, &request, &decision) ==
               RIBON_KEY_POLICY_STATUS_OK &&
               decision.record_index == 1u && decision.delegation_depth == 2u,
           "two-edge delegated leaf");

    records[2].key_id = key_id_new;
    records[2].key_id_size = sizeof(key_id_new) - 1u;
    expect(ribon_key_policy_store_validate(&store) ==
               RIBON_KEY_POLICY_STATUS_INVALID_STORE,
           "duplicate key ID rejected");
    store_initialize(&store, records);
    records[3].flags = 0u;
    records[3].issuer_key_id = key_id_new;
    records[3].issuer_key_id_size = sizeof(key_id_new) - 1u;
    records[3].delegation_depth = 1u;
    expect(ribon_key_policy_store_validate(&store) ==
               RIBON_KEY_POLICY_STATUS_INVALID_STORE,
           "cycle and depth overflow rejected");
    store_initialize(&store, records);
    memcpy(
        records[2].public_key,
        records[1].public_key,
        sizeof(records[2].public_key));
    memcpy(
        records[2].key_identity_digest,
        records[1].key_identity_digest,
        sizeof(records[2].key_identity_digest));
    expect(ribon_key_policy_store_validate(&store) ==
               RIBON_KEY_POLICY_STATUS_INVALID_STORE,
           "duplicate non-revoked public key rejected");
    store_initialize(&store, records);
    records[2].lifecycle = RIBON_KEY_POLICY_LIFECYCLE_REVOKED;
    memcpy(
        records[2].public_key,
        records[1].public_key,
        sizeof(records[2].public_key));
    memcpy(
        records[2].key_identity_digest,
        records[1].key_identity_digest,
        sizeof(records[2].key_identity_digest));
    expect(ribon_key_policy_store_validate(&store) ==
               RIBON_KEY_POLICY_STATUS_INVALID_STORE,
           "revoked alias cannot bypass public-key identity revocation");
}

static void
test_rotation_and_revocation(void)
{
    struct RibonKeyPolicyRecord records[4];
    struct RibonKeyPolicyStore store;
    struct RibonKeyPolicyDecision decision;
    struct RibonKeyPolicyRequest old_request;
    struct RibonKeyPolicyRequest new_request;

    store_initialize(&store, records);
    records[2].lifecycle = RIBON_KEY_POLICY_LIFECYCLE_ACTIVE;
    refresh_store_digest(&store);
    old_request = request_for(key_id_old, sizeof(key_id_old) - 1u, 17u);
    new_request = request_for(key_id_new, sizeof(key_id_new) - 1u, 17u);
    expect(ribon_key_policy_authorize(&store, &old_request, &decision) ==
               RIBON_KEY_POLICY_STATUS_OK,
           "rotation before old key active");
    expect(ribon_key_policy_authorize(&store, &new_request, &decision) ==
               RIBON_KEY_POLICY_STATUS_KEY_POLICY,
           "rotation before new key sequence closed");

    records[2].lifecycle = RIBON_KEY_POLICY_LIFECYCLE_RETIRING;
    refresh_store_digest(&store);
    old_request.sequence = 18u;
    new_request.sequence = 18u;
    expect(ribon_key_policy_authorize(&store, &old_request, &decision) ==
               RIBON_KEY_POLICY_STATUS_OK &&
           ribon_key_policy_authorize(&store, &new_request, &decision) ==
               RIBON_KEY_POLICY_STATUS_OK,
           "rotation overlap accepts bounded old and new keys");

    records[2].lifecycle = RIBON_KEY_POLICY_LIFECYCLE_REVOKED;
    refresh_store_digest(&store);
    old_request.sequence = 19u;
    new_request.sequence = 21u;
    expect(ribon_key_policy_authorize(&store, &old_request, &decision) ==
               RIBON_KEY_POLICY_STATUS_KEY_POLICY,
           "rotation after revoked old key rejected");
    expect(ribon_key_policy_authorize(&store, &new_request, &decision) ==
               RIBON_KEY_POLICY_STATUS_OK,
           "rotation after new key remains active");

    records[0].lifecycle = RIBON_KEY_POLICY_LIFECYCLE_REVOKED;
    refresh_store_digest(&store);
    expect(ribon_key_policy_authorize(&store, &new_request, &decision) ==
               RIBON_KEY_POLICY_STATUS_KEY_POLICY,
           "revoked issuer rejects descendant");
}

static void
test_wrong_boundaries(void)
{
    struct RibonKeyPolicyRecord records[4];
    struct RibonKeyPolicyStore store;
    struct RibonKeyPolicyDecision decision;
    struct RibonKeyPolicyRequest request;

    store_initialize(&store, records);
    request = request_for(key_id_root, sizeof(key_id_root) - 1u, 18u);
    request.product_digest[0] ^= 1u;
    expect(ribon_key_policy_authorize(&store, &request, &decision) ==
               RIBON_KEY_POLICY_STATUS_IDENTITY_MISMATCH,
           "wrong product rejected");
    request = request_for(key_id_root, sizeof(key_id_root) - 1u, 18u);
    request.rollback_domain_digest[0] ^= 1u;
    expect(ribon_key_policy_authorize(&store, &request, &decision) ==
               RIBON_KEY_POLICY_STATUS_DOMAIN_MISMATCH,
           "wrong rollback domain rejected");
    request = request_for(key_id_root, sizeof(key_id_root) - 1u, 18u);
    request.mode = RIBON_KEY_POLICY_MODE_RECOVERY;
    expect(ribon_key_policy_authorize(&store, &request, &decision) ==
               RIBON_KEY_POLICY_STATUS_MODE_USAGE_MISMATCH,
           "wrong mode and policy usage rejected");
    request = request_for(key_id_root, sizeof(key_id_root) - 1u, 101u);
    expect(ribon_key_policy_authorize(&store, &request, &decision) ==
               RIBON_KEY_POLICY_STATUS_KEY_POLICY,
           "sequence above maximum rejected");
    request.key_id = (const uint8_t *)"unknown";
    request.key_id_size = 7u;
    expect(ribon_key_policy_authorize(&store, &request, &decision) ==
               RIBON_KEY_POLICY_STATUS_KEY_UNKNOWN,
           "unknown key rejected");
}

static int
fixture_verify(
    const struct RibonSignatureProvider *provider,
    const struct RibonSignatureVerification *request)
{
    (void)provider;
    (void)request;
    ++fixture_provider_calls;
    return RIBON_SIGNATURE_STATUS_OK;
}

static void
test_signature_order_and_real_ed25519(void)
{
    static const struct RibonSignatureProvider fixture_provider = {
        .magic = RIBON_SIGNATURE_PROVIDER_MAGIC,
        .size = sizeof(fixture_provider),
        .abi_version = RIBON_SIGNATURE_PROVIDER_ABI_VERSION,
        .provider_class = RIBON_SIGNATURE_PROVIDER_CLASS_FIXTURE,
        .algorithm = RIBON_SIGNATURE_ALGORITHM_ED25519,
        .id = "security.signature.fixture.key-policy-order",
        .public_key_bytes = RIBON_ED25519_PUBLIC_KEY_BYTES,
        .signature_bytes = RIBON_ED25519_SIGNATURE_BYTES,
        .workspace_alignment = 1u,
        .verify = fixture_verify,
    };
    struct RibonKeyPolicyRecord records[4];
    struct RibonKeyPolicyStore store;
    struct RibonKeyPolicyDecision decision;
    struct RibonKeyPolicyRequest request;
    uint8_t dummy_signature[RIBON_ED25519_SIGNATURE_BYTES] = {0u};
    struct RibonKeyPolicySignatureVerification verification;

    store_initialize(&store, records);
    request = request_for(key_id_root, sizeof(key_id_root) - 1u, 18u);
    verification = (struct RibonKeyPolicySignatureVerification){
        .size = sizeof(verification),
        .abi_version = RIBON_KEY_POLICY_ABI_VERSION,
        .policy = &request,
        .provider = &fixture_provider,
        .signature = dummy_signature,
        .signature_size = sizeof(dummy_signature),
    };
    request.product_digest[0] ^= 1u;
    fixture_provider_calls = 0;
    expect(ribon_key_policy_verify(&store, &verification, &decision) ==
               RIBON_KEY_POLICY_STATUS_IDENTITY_MISMATCH &&
               fixture_provider_calls == 0,
           "key policy failure precedes crypto callback");
    request.product_digest[0] ^= 1u;
    expect(ribon_key_policy_verify(&store, &verification, &decision) ==
               RIBON_KEY_POLICY_STATUS_OK && fixture_provider_calls == 1,
           "authorized key reaches selected provider once");

    verification.provider = &ribon_ed25519_signature_provider_descriptor;
    verification.signature = rfc8032_signature;
    verification.signature_size = sizeof(rfc8032_signature);
    expect(ribon_key_policy_verify(&store, &verification, &decision) ==
               RIBON_KEY_POLICY_STATUS_OK,
           "real RFC 8032 Ed25519 verification behind key policy");
    dummy_signature[0] = rfc8032_signature[0] ^ 1u;
    verification.signature = dummy_signature;
    expect(ribon_key_policy_verify(&store, &verification, &decision) ==
               RIBON_KEY_POLICY_STATUS_SIGNATURE_INVALID,
           "invalid signature rejected after key policy");
}

int
main(void)
{
    test_store_and_delegation();
    test_rotation_and_revocation();
    test_wrong_boundaries();
    test_signature_order_and_real_ed25519();
    if (failures != 0) {
        fprintf(stderr, "key policy failures=%d\n", failures);
        return 1;
    }
    puts(
        "RIBON-KEY-POLICY-OK records=4 delegation=2 "
        "rotation=before-during-after revocation=issuer-closed "
        "ed25519=real bounded=yes");
    return 0;
}
