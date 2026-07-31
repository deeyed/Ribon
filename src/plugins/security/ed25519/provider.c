#include <Ribon/security/ed25519.h>

#include "monocypher-ed25519.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Monocypher 4.0.3 accepts consensus-friendly non-canonical encodings. Ribon
 * applies a strict RFC 8032 product profile before the upstream equation.
 * The canonical and small-order filters follow libsodium 1.0.22's reviewed
 * ref10 predicates; all inputs are public and an early reject leaks no key.
 */
/** @brief 공개 Ed25519 point의 y encoding이 field modulus보다 작은지 검사한다. */
static int
ribon_ed25519_point_is_canonical(const uint8_t point[32])
{
    uint8_t c = (uint8_t)((point[31] & 0x7fu) ^ 0x7fu);
    uint8_t d;
    size_t index;

    for (index = 30u; index > 0u; --index) {
        c = (uint8_t)(c | (uint8_t)(point[index] ^ 0xffu));
    }
    c = (uint8_t)(((uint32_t)c - 1u) >> 8);
    d = (uint8_t)((0xedu - 1u - (uint32_t)point[0]) >> 8);
    return 1 - (int)(c & d & 1u);
}

/** @brief 공개 Ed25519 point가 strict profile의 low-order blacklist에 속하는지 검사한다. */
static int
ribon_ed25519_point_has_small_order(const uint8_t point[32])
{
    static const uint8_t small_order[7][32] = {
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
        {
            0xedu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
            0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
            0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
            0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0x7fu,
        },
        {
            0xeeu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
            0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
            0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
            0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0x7fu,
        },
    };
    uint8_t differences[7] = {0u};
    uint32_t matches = 0u;
    size_t candidate;
    size_t index;

    for (index = 0u; index < 31u; ++index) {
        for (candidate = 0u; candidate < 7u; ++candidate) {
            differences[candidate] |=
                (uint8_t)(point[index] ^ small_order[candidate][index]);
        }
    }
    for (candidate = 0u; candidate < 7u; ++candidate) {
        differences[candidate] |= (uint8_t)(
            (point[31] & 0x7fu) ^ small_order[candidate][31]);
        matches |= (uint32_t)differences[candidate] - 1u;
    }
    return (int)((matches >> 8) & 1u);
}

/**
 * @brief Strict public-input filter 뒤 Monocypher Ed25519 equation을 검증한다.
 *
 * Heap과 caller-visible scratch를 쓰지 않고 request의 immutable view를 호출 동안만 소비한다.
 */
static int
ribon_ed25519_verify(
    const struct RibonSignatureProvider *provider,
    const struct RibonSignatureVerification *request)
{
    (void)provider;

    if (!ribon_ed25519_point_is_canonical(request->public_key) ||
        !ribon_ed25519_point_is_canonical(request->signature) ||
        ribon_ed25519_point_has_small_order(request->public_key) ||
        ribon_ed25519_point_has_small_order(request->signature)) {
        return RIBON_SIGNATURE_STATUS_INVALID_ENCODING;
    }
    return crypto_ed25519_check(
               request->signature,
               request->public_key,
               request->message,
               request->message_size) == 0 ?
        RIBON_SIGNATURE_STATUS_OK :
        RIBON_SIGNATURE_STATUS_INVALID_SIGNATURE;
}

const struct RibonSignatureProvider
    ribon_ed25519_signature_provider_descriptor = {
        .magic = RIBON_SIGNATURE_PROVIDER_MAGIC,
        .size = sizeof(ribon_ed25519_signature_provider_descriptor),
        .abi_version = RIBON_SIGNATURE_PROVIDER_ABI_VERSION,
        .provider_class = RIBON_SIGNATURE_PROVIDER_CLASS_PRODUCTION,
        .algorithm = RIBON_SIGNATURE_ALGORITHM_ED25519,
        .id = "security.signature.ed25519.monocypher-4.0.3",
        .public_key_bytes = RIBON_ED25519_PUBLIC_KEY_BYTES,
        .signature_bytes = RIBON_ED25519_SIGNATURE_BYTES,
        .workspace_alignment = 1u,
        .verify = ribon_ed25519_verify,
    };
