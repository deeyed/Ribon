#include <Ribon/security/ed25519.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void
expect(int condition, const char *name)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

static struct RibonSignatureVerification
request_for(
    const uint8_t public_key[32],
    const uint8_t *message,
    size_t message_size,
    const uint8_t signature[64])
{
    return (struct RibonSignatureVerification){
        .size = sizeof(struct RibonSignatureVerification),
        .abi_version = RIBON_SIGNATURE_PROVIDER_ABI_VERSION,
        .algorithm = RIBON_SIGNATURE_ALGORITHM_ED25519,
        .public_key = public_key,
        .public_key_size = RIBON_ED25519_PUBLIC_KEY_BYTES,
        .message = message,
        .message_size = message_size,
        .signature = signature,
        .signature_size = RIBON_ED25519_SIGNATURE_BYTES,
    };
}

static int
verify(const struct RibonSignatureVerification *request)
{
    return ribon_signature_verify(
        &ribon_ed25519_signature_provider_descriptor,
        request);
}

static void
write_u16(uint8_t *bytes, size_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1u] = (uint8_t)(value >> 8);
}

static void
write_u64(uint8_t *bytes, size_t offset, uint64_t value)
{
    size_t index;

    for (index = 0u; index < 8u; ++index) {
        bytes[offset + index] = (uint8_t)(value >> (index * 8u));
    }
}

static void
test_rfc8032(void)
{
    static const uint8_t public_key_1[32] = {
        0xd7u, 0x5au, 0x98u, 0x01u, 0x82u, 0xb1u, 0x0au, 0xb7u,
        0xd5u, 0x4bu, 0xfeu, 0xd3u, 0xc9u, 0x64u, 0x07u, 0x3au,
        0x0eu, 0xe1u, 0x72u, 0xf3u, 0xdau, 0xa6u, 0x23u, 0x25u,
        0xafu, 0x02u, 0x1au, 0x68u, 0xf7u, 0x07u, 0x51u, 0x1au,
    };
    static const uint8_t signature_1[64] = {
        0xe5u, 0x56u, 0x43u, 0x00u, 0xc3u, 0x60u, 0xacu, 0x72u,
        0x90u, 0x86u, 0xe2u, 0xccu, 0x80u, 0x6eu, 0x82u, 0x8au,
        0x84u, 0x87u, 0x7fu, 0x1eu, 0xb8u, 0xe5u, 0xd9u, 0x74u,
        0xd8u, 0x73u, 0xe0u, 0x65u, 0x22u, 0x49u, 0x01u, 0x55u,
        0x5fu, 0xb8u, 0x82u, 0x15u, 0x90u, 0xa3u, 0x3bu, 0xacu,
        0xc6u, 0x1eu, 0x39u, 0x70u, 0x1cu, 0xf9u, 0xb4u, 0x6bu,
        0xd2u, 0x5bu, 0xf5u, 0xf0u, 0x59u, 0x5bu, 0xbeu, 0x24u,
        0x65u, 0x51u, 0x41u, 0x43u, 0x8eu, 0x7au, 0x10u, 0x0bu,
    };
    static const uint8_t public_key_2[32] = {
        0x3du, 0x40u, 0x17u, 0xc3u, 0xe8u, 0x43u, 0x89u, 0x5au,
        0x92u, 0xb7u, 0x0au, 0xa7u, 0x4du, 0x1bu, 0x7eu, 0xbcu,
        0x9cu, 0x98u, 0x2cu, 0xcfu, 0x2eu, 0xc4u, 0x96u, 0x8cu,
        0xc0u, 0xcdu, 0x55u, 0xf1u, 0x2au, 0xf4u, 0x66u, 0x0cu,
    };
    static const uint8_t message_2[1] = {0x72u};
    static const uint8_t signature_2[64] = {
        0x92u, 0xa0u, 0x09u, 0xa9u, 0xf0u, 0xd4u, 0xcau, 0xb8u,
        0x72u, 0x0eu, 0x82u, 0x0bu, 0x5fu, 0x64u, 0x25u, 0x40u,
        0xa2u, 0xb2u, 0x7bu, 0x54u, 0x16u, 0x50u, 0x3fu, 0x8fu,
        0xb3u, 0x76u, 0x22u, 0x23u, 0xebu, 0xdbu, 0x69u, 0xdau,
        0x08u, 0x5au, 0xc1u, 0xe4u, 0x3eu, 0x15u, 0x99u, 0x6eu,
        0x45u, 0x8fu, 0x36u, 0x13u, 0xd0u, 0xf1u, 0x1du, 0x8cu,
        0x38u, 0x7bu, 0x2eu, 0xaeu, 0xb4u, 0x30u, 0x2au, 0xeeu,
        0xb0u, 0x0du, 0x29u, 0x16u, 0x12u, 0xbbu, 0x0cu, 0x00u,
    };
    struct RibonSignatureVerification request =
        request_for(public_key_1, NULL, 0u, signature_1);

    expect(verify(&request) == RIBON_SIGNATURE_STATUS_OK,
           "RFC 8032 empty-message vector");
    request = request_for(public_key_2, message_2, sizeof(message_2), signature_2);
    expect(verify(&request) == RIBON_SIGNATURE_STATUS_OK,
           "RFC 8032 one-byte vector");
}

static void
test_cross_tool_and_mutations(void)
{
    static const uint8_t public_key[32] = {
        0xd7u, 0x5au, 0x98u, 0x01u, 0x82u, 0xb1u, 0x0au, 0xb7u,
        0xd5u, 0x4bu, 0xfeu, 0xd3u, 0xc9u, 0x64u, 0x07u, 0x3au,
        0x0eu, 0xe1u, 0x72u, 0xf3u, 0xdau, 0xa6u, 0x23u, 0x25u,
        0xafu, 0x02u, 0x1au, 0x68u, 0xf7u, 0x07u, 0x51u, 0x1au,
    };
    static const uint8_t signature[64] = {
        0x10u, 0x5fu, 0x58u, 0x1fu, 0x17u, 0x4du, 0xb5u, 0xf2u,
        0x90u, 0xd7u, 0x99u, 0xb3u, 0x69u, 0xc9u, 0x1cu, 0xafu,
        0x56u, 0x67u, 0xa9u, 0x32u, 0x7bu, 0x11u, 0x7au, 0x67u,
        0x52u, 0xa8u, 0xfdu, 0xfeu, 0x23u, 0x01u, 0x89u, 0x7au,
        0x9du, 0x8au, 0x8eu, 0xe2u, 0x13u, 0x04u, 0xc0u, 0x19u,
        0x2cu, 0x2bu, 0x8fu, 0x42u, 0xcfu, 0x4eu, 0xa7u, 0x8bu,
        0x24u, 0xfeu, 0xa0u, 0x49u, 0xe8u, 0x1cu, 0x72u, 0x0cu,
        0xd4u, 0x60u, 0x35u, 0xf0u, 0x46u, 0x2fu, 0xafu, 0x0cu,
    };
    static const uint8_t key_id_hash[32] = {
        0x50u, 0xc3u, 0x80u, 0x26u, 0xf6u, 0x60u, 0xc1u, 0x58u,
        0x28u, 0x7eu, 0x49u, 0xfbu, 0x78u, 0xabu, 0x6bu, 0xfeu,
        0x69u, 0xeau, 0x46u, 0x01u, 0x58u, 0xa7u, 0xe7u, 0x8au,
        0xbbu, 0x5au, 0x14u, 0x90u, 0x91u, 0xf0u, 0x3fu, 0x70u,
    };
    uint8_t message[232] = {0u};
    uint8_t changed_message[232];
    uint8_t changed_key[32];
    uint8_t changed_signature[64];
    struct RibonSignatureVerification request;
    size_t index;

    memcpy(message, "RIBON-TRUST-MESSAGE-V1", 22u);
    write_u16(message, 32u, 1u);
    write_u16(message, 36u, 1u);
    write_u16(message, 40u, 1u);
    write_u16(message, 44u, 1u);
    write_u16(message, 48u, 1u);
    write_u16(message, 50u, 1u);
    write_u16(message, 52u, 1u);
    write_u16(message, 54u, 1u);
    write_u64(message, 56u, UINT64_C(0x0102030405060708));
    write_u64(message, 64u, UINT64_C(0x11223344));
    for (index = 0u; index < 32u; ++index) {
        message[72u + index] = (uint8_t)index;
        message[104u + index] = (uint8_t)(0x20u + index);
        message[136u + index] = (uint8_t)(0x40u + index);
        message[168u + index] = (uint8_t)(0x60u + index);
    }
    memcpy(message + 200u, key_id_hash, sizeof(key_id_hash));
    request = request_for(public_key, message, sizeof(message), signature);
    expect(verify(&request) == RIBON_SIGNATURE_STATUS_OK,
           "OpenSSL cross-tool trust-message vector");

    for (index = 0u; index < sizeof(message); ++index) {
        memcpy(changed_message, message, sizeof(message));
        changed_message[index] ^= 1u;
        request = request_for(
            public_key, changed_message, sizeof(changed_message), signature);
        expect(verify(&request) != RIBON_SIGNATURE_STATUS_OK,
               "message mutation rejected");
    }
    for (index = 0u; index < sizeof(changed_key); ++index) {
        memcpy(changed_key, public_key, sizeof(changed_key));
        changed_key[index] ^= 1u;
        request = request_for(
            changed_key, message, sizeof(message), signature);
        expect(verify(&request) != RIBON_SIGNATURE_STATUS_OK,
               "public-key mutation rejected");
    }
    for (index = 0u; index < sizeof(changed_signature); ++index) {
        memcpy(changed_signature, signature, sizeof(changed_signature));
        changed_signature[index] ^= 1u;
        request = request_for(
            public_key, message, sizeof(message), changed_signature);
        expect(verify(&request) != RIBON_SIGNATURE_STATUS_OK,
               "signature mutation rejected");
    }
}

static void
test_strict_rejections(void)
{
    static const uint8_t public_key[32] = {
        0xd7u, 0x5au, 0x98u, 0x01u, 0x82u, 0xb1u, 0x0au, 0xb7u,
        0xd5u, 0x4bu, 0xfeu, 0xd3u, 0xc9u, 0x64u, 0x07u, 0x3au,
        0x0eu, 0xe1u, 0x72u, 0xf3u, 0xdau, 0xa6u, 0x23u, 0x25u,
        0xafu, 0x02u, 0x1au, 0x68u, 0xf7u, 0x07u, 0x51u, 0x1au,
    };
    static const uint8_t signature[64] = {
        0xe5u, 0x56u, 0x43u, 0x00u, 0xc3u, 0x60u, 0xacu, 0x72u,
        0x90u, 0x86u, 0xe2u, 0xccu, 0x80u, 0x6eu, 0x82u, 0x8au,
        0x84u, 0x87u, 0x7fu, 0x1eu, 0xb8u, 0xe5u, 0xd9u, 0x74u,
        0xd8u, 0x73u, 0xe0u, 0x65u, 0x22u, 0x49u, 0x01u, 0x55u,
        0x5fu, 0xb8u, 0x82u, 0x15u, 0x90u, 0xa3u, 0x3bu, 0xacu,
        0xc6u, 0x1eu, 0x39u, 0x70u, 0x1cu, 0xf9u, 0xb4u, 0x6bu,
        0xd2u, 0x5bu, 0xf5u, 0xf0u, 0x59u, 0x5bu, 0xbeu, 0x24u,
        0x65u, 0x51u, 0x41u, 0x43u, 0x8eu, 0x7au, 0x10u, 0x0bu,
    };
    static const uint8_t order_l[32] = {
        0xedu, 0xd3u, 0xf5u, 0x5cu, 0x1au, 0x63u, 0x12u, 0x58u,
        0xd6u, 0x9cu, 0xf7u, 0xa2u, 0xdeu, 0xf9u, 0xdeu, 0x14u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x10u,
    };
    static const uint8_t small_order[][32] = {
        {0x00u},
        {0x01u},
        {
            0x26u, 0xe8u, 0x95u, 0x8fu, 0xc2u, 0xb2u, 0x27u, 0xb0u,
            0x45u, 0xc3u, 0xf4u, 0x89u, 0xf2u, 0xefu, 0x98u, 0xf0u,
            0xd5u, 0xdfu, 0xacu, 0x05u, 0xd3u, 0xc6u, 0x33u, 0x39u,
            0xb1u, 0x38u, 0x02u, 0x88u, 0x6du, 0x53u, 0xfcu, 0x05u,
        },
        {
            0xc7u, 0x17u, 0x6au, 0x70u, 0x3du, 0x4du, 0xd8u, 0x4fu,
            0xbau, 0x3cu, 0x0bu, 0x76u, 0x0du, 0x10u, 0x67u, 0x0fu,
            0x2au, 0x20u, 0x53u, 0xfau, 0x2cu, 0x39u, 0xccu, 0xc6u,
            0x4eu, 0xc7u, 0xfdu, 0x77u, 0x92u, 0xacu, 0x03u, 0x7au,
        },
        {
            0xecu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
            0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
            0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
            0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0x7fu,
        },
    };
    uint8_t noncanonical[32];
    uint8_t changed_signature[64];
    struct RibonSignatureVerification request;
    size_t index;

    for (index = 0u; index < sizeof(small_order) / sizeof(small_order[0]); ++index) {
        request = request_for(small_order[index], NULL, 0u, signature);
        expect(verify(&request) == RIBON_SIGNATURE_STATUS_INVALID_ENCODING,
               "small-order public key rejected");
        memcpy(changed_signature, signature, sizeof(changed_signature));
        memcpy(changed_signature, small_order[index], 32u);
        request = request_for(public_key, NULL, 0u, changed_signature);
        expect(verify(&request) == RIBON_SIGNATURE_STATUS_INVALID_ENCODING,
               "small-order R rejected");
    }
    memset(noncanonical, 0xff, sizeof(noncanonical));
    noncanonical[0] = 0xedu;
    noncanonical[31] = 0x7fu;
    request = request_for(noncanonical, NULL, 0u, signature);
    expect(verify(&request) == RIBON_SIGNATURE_STATUS_INVALID_ENCODING,
           "non-canonical public key rejected");
    memcpy(changed_signature, signature, sizeof(changed_signature));
    memcpy(changed_signature, noncanonical, sizeof(noncanonical));
    request = request_for(public_key, NULL, 0u, changed_signature);
    expect(verify(&request) == RIBON_SIGNATURE_STATUS_INVALID_ENCODING,
           "non-canonical R rejected");
    memcpy(changed_signature, signature, sizeof(changed_signature));
    memcpy(changed_signature + 32u, order_l, sizeof(order_l));
    request = request_for(public_key, NULL, 0u, changed_signature);
    expect(verify(&request) == RIBON_SIGNATURE_STATUS_INVALID_SIGNATURE,
           "non-canonical S rejected");
}

static void
test_provider_contract(void)
{
    static const uint8_t public_key[32] = {1u};
    static const uint8_t signature[64] = {1u};
    struct RibonSignatureProvider changed =
        ribon_ed25519_signature_provider_descriptor;
    struct RibonSignatureVerification request =
        request_for(public_key, NULL, 0u, signature);

    expect(ribon_signature_provider_is_valid(
               &ribon_ed25519_signature_provider_descriptor),
           "production provider descriptor valid");
    expect(ribon_ed25519_signature_provider_descriptor.provider_class ==
               RIBON_SIGNATURE_PROVIDER_CLASS_PRODUCTION,
           "provider class production");
    request.signature_size = 63u;
    expect(verify(&request) == RIBON_SIGNATURE_STATUS_INVALID_ARGUMENT,
           "wrong signature size rejected");
    request = request_for(public_key, NULL, 0u, signature);
    request.workspace = &changed;
    request.workspace_size = sizeof(changed);
    expect(verify(&request) == RIBON_SIGNATURE_STATUS_INVALID_ARGUMENT,
           "unrequested workspace rejected");
    changed.verify = NULL;
    expect(!ribon_signature_provider_is_valid(&changed),
           "missing callback rejected");
}

int
main(void)
{
    test_provider_contract();
    test_rfc8032();
    test_cross_tool_and_mutations();
    test_strict_rejections();
    if (failures != 0) {
        fprintf(stderr, "%d Ed25519 provider assertions failed\n", failures);
        return 1;
    }
    puts("RIBON-ED25519-PROVIDER-OK rfc8032=2 cross-tool=openssl mutations=328 strict=yes");
    return 0;
}
