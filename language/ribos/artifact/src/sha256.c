#include "internal.h"

#include <string.h>

static uint32_t
ribos_artifact_sha_rotate_right(uint32_t value, unsigned shift)
{
    return (value >> shift) | (value << (32u - shift));
}

static void
ribos_artifact_sha_transform(
    RibosArtifactSha256 *context,
    const uint8_t block[64])
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
    size_t index;

    for (index = 0; index < 16; ++index) {
        words[index] =
            ((uint32_t)block[index * 4] << 24) |
            ((uint32_t)block[index * 4 + 1] << 16) |
            ((uint32_t)block[index * 4 + 2] << 8) |
            (uint32_t)block[index * 4 + 3];
    }
    for (index = 16; index < 64; ++index) {
        uint32_t first =
            ribos_artifact_sha_rotate_right(words[index - 15], 7) ^
            ribos_artifact_sha_rotate_right(words[index - 15], 18) ^
            (words[index - 15] >> 3);
        uint32_t second =
            ribos_artifact_sha_rotate_right(words[index - 2], 17) ^
            ribos_artifact_sha_rotate_right(words[index - 2], 19) ^
            (words[index - 2] >> 10);

        words[index] =
            words[index - 16] + first +
            words[index - 7] + second;
    }
    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];
    for (index = 0; index < 64; ++index) {
        uint32_t choose = (e & f) ^ (~e & g);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t sigma_e =
            ribos_artifact_sha_rotate_right(e, 6) ^
            ribos_artifact_sha_rotate_right(e, 11) ^
            ribos_artifact_sha_rotate_right(e, 25);
        uint32_t sigma_a =
            ribos_artifact_sha_rotate_right(a, 2) ^
            ribos_artifact_sha_rotate_right(a, 13) ^
            ribos_artifact_sha_rotate_right(a, 22);
        uint32_t first =
            h + sigma_e + choose + constants[index] + words[index];
        uint32_t second = sigma_a + majority;

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

void
ribos_artifact_sha256_initialize(RibosArtifactSha256 *context)
{
    static const uint32_t initial[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };

    memset(context, 0, sizeof(*context));
    memcpy(context->state, initial, sizeof(initial));
}

void
ribos_artifact_sha256_update(
    RibosArtifactSha256 *context,
    const uint8_t *input,
    size_t input_size)
{
    size_t offset = 0;

    while (offset < input_size) {
        size_t available = sizeof(context->block) - context->block_size;
        size_t amount = input_size - offset;

        if (amount > available) {
            amount = available;
        }
        memcpy(
            context->block + context->block_size,
            input + offset,
            amount);
        context->block_size += amount;
        context->bit_count += (uint64_t)amount * 8;
        offset += amount;
        if (context->block_size == sizeof(context->block)) {
            ribos_artifact_sha_transform(context, context->block);
            context->block_size = 0;
        }
    }
}

void
ribos_artifact_sha256_finish(
    RibosArtifactSha256 *context,
    uint8_t digest[RIBOS_SCHEMA_DIGEST_BYTES])
{
    size_t index;

    context->block[context->block_size++] = 0x80;
    if (context->block_size > 56) {
        memset(
            context->block + context->block_size,
            0,
            sizeof(context->block) - context->block_size);
        ribos_artifact_sha_transform(context, context->block);
        context->block_size = 0;
    }
    memset(context->block + context->block_size, 0, 56 - context->block_size);
    for (index = 0; index < 8; ++index) {
        context->block[63 - index] =
            (uint8_t)(context->bit_count >> (index * 8));
    }
    ribos_artifact_sha_transform(context, context->block);
    for (index = 0; index < 8; ++index) {
        digest[index * 4] =
            (uint8_t)(context->state[index] >> 24);
        digest[index * 4 + 1] =
            (uint8_t)(context->state[index] >> 16);
        digest[index * 4 + 2] =
            (uint8_t)(context->state[index] >> 8);
        digest[index * 4 + 3] =
            (uint8_t)context->state[index];
    }
}

void
ribos_artifact_sha256(
    const uint8_t *input,
    size_t input_size,
    uint8_t digest[RIBOS_SCHEMA_DIGEST_BYTES])
{
    RibosArtifactSha256 context;

    ribos_artifact_sha256_initialize(&context);
    if (input_size != 0) {
        ribos_artifact_sha256_update(&context, input, input_size);
    }
    ribos_artifact_sha256_finish(&context, digest);
}
