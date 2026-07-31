#include "sha256.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct RibonSecuritySha256 {
    uint32_t state[8];
    uint64_t total_bytes;
    uint8_t block[64];
    size_t used;
};

/** @brief SHA-256 word를 오른쪽으로 회전한다. */
static uint32_t
rotate_right(uint32_t value, uint32_t count)
{
    return (value >> count) | (value << (32u - count));
}

/** @brief 한 64-byte SHA-256 block을 state에 압축한다. */
static void
transform(struct RibonSecuritySha256 *context, const uint8_t block[64])
{
    static const uint32_t constants[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };
    uint32_t words[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    uint32_t index;

    for (index = 0u; index < 16u; ++index) {
        const uint32_t offset = index * 4u;
        words[index] = ((uint32_t)block[offset] << 24) |
            ((uint32_t)block[offset + 1u] << 16) |
            ((uint32_t)block[offset + 2u] << 8) |
            (uint32_t)block[offset + 3u];
    }
    for (index = 16u; index < 64u; ++index) {
        const uint32_t first = words[index - 15u];
        const uint32_t second = words[index - 2u];
        words[index] = words[index - 16u] +
            (rotate_right(first, 7u) ^ rotate_right(first, 18u) ^ (first >> 3)) +
            words[index - 7u] +
            (rotate_right(second, 17u) ^ rotate_right(second, 19u) ^ (second >> 10));
    }
    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];
    for (index = 0u; index < 64u; ++index) {
        const uint32_t first = h +
            (rotate_right(e, 6u) ^ rotate_right(e, 11u) ^ rotate_right(e, 25u)) +
            ((e & f) ^ ((~e) & g)) + constants[index] + words[index];
        const uint32_t second =
            (rotate_right(a, 2u) ^ rotate_right(a, 13u) ^ rotate_right(a, 22u)) +
            ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

/** @brief Bounded byte view를 streaming SHA-256 context에 추가한다. */
static void
update(struct RibonSecuritySha256 *context, const uint8_t *bytes, size_t size)
{
    size_t offset = 0u;

    context->total_bytes += size;
    while (offset < size) {
        size_t count = sizeof(context->block) - context->used;
        if (count > size - offset) {
            count = size - offset;
        }
        memcpy(context->block + context->used, bytes + offset, count);
        context->used += count;
        offset += count;
        if (context->used == sizeof(context->block)) {
            transform(context, context->block);
            context->used = 0u;
        }
    }
}

/** @brief Immutable byte view의 one-shot SHA-256 digest를 반환한다. */
void
ribon_security_sha256(const uint8_t *bytes, size_t size, uint8_t digest[32])
{
    struct RibonSecuritySha256 context = {
        .state = {
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
        },
    };
    const uint64_t bit_length = (uint64_t)size * UINT64_C(8);
    uint8_t padding[128] = {0x80u};
    uint8_t length[8];
    size_t padding_size;
    uint32_t index;

    update(&context, bytes, size);
    padding_size = context.used < 56u ? 56u - context.used : 120u - context.used;
    for (index = 0u; index < 8u; ++index) {
        length[7u - index] = (uint8_t)(bit_length >> (index * 8u));
    }
    update(&context, padding, padding_size);
    update(&context, length, sizeof(length));
    for (index = 0u; index < 8u; ++index) {
        digest[index * 4u] = (uint8_t)(context.state[index] >> 24);
        digest[index * 4u + 1u] = (uint8_t)(context.state[index] >> 16);
        digest[index * 4u + 2u] = (uint8_t)(context.state[index] >> 8);
        digest[index * 4u + 3u] = (uint8_t)context.state[index];
    }
}
