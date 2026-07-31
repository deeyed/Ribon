#include <Ribon/security/key_policy.h>

#include <stddef.h>
#include <stdint.h>

#define RIBON_KEY_POLICY_ALL_MODE_MASK UINT32_C(0x0f)
#define RIBON_KEY_POLICY_ALL_USAGE_MASK UINT64_C(0x3f)

struct RibonKeyPolicySha256 {
    uint32_t state[8];
    uint64_t total_bytes;
    uint8_t block[64];
    size_t used;
};

/** @brief SHA-256 word를 오른쪽으로 회전한다. */
static uint32_t
ribon_key_policy_rotate_right(uint32_t value, uint32_t count)
{
    return (value >> count) | (value << (32u - count));
}

/** @brief Product trust-store canonical digest용 SHA-256 block을 압축한다. */
static void
ribon_key_policy_sha256_transform(
    struct RibonKeyPolicySha256 *context,
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
        const uint32_t sigma0 =
            ribon_key_policy_rotate_right(first, 7u) ^
            ribon_key_policy_rotate_right(first, 18u) ^ (first >> 3);
        const uint32_t sigma1 =
            ribon_key_policy_rotate_right(second, 17u) ^
            ribon_key_policy_rotate_right(second, 19u) ^ (second >> 10);

        words[index] = words[index - 16u] + sigma0 +
            words[index - 7u] + sigma1;
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
        const uint32_t sum1 = ribon_key_policy_rotate_right(e, 6u) ^
            ribon_key_policy_rotate_right(e, 11u) ^
            ribon_key_policy_rotate_right(e, 25u);
        const uint32_t choice = (e & f) ^ ((~e) & g);
        const uint32_t first = h + sum1 + choice + constants[index] +
            words[index];
        const uint32_t sum0 = ribon_key_policy_rotate_right(a, 2u) ^
            ribon_key_policy_rotate_right(a, 13u) ^
            ribon_key_policy_rotate_right(a, 22u);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t second = sum0 + majority;

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

/** @brief Canonical trust-store SHA-256 context를 초기화한다. */
static void
ribon_key_policy_sha256_initialize(struct RibonKeyPolicySha256 *context)
{
    *context = (struct RibonKeyPolicySha256){
        .state = {
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
        },
    };
}

/** @brief Bounded canonical byte stream을 SHA-256 context에 추가한다. */
static void
ribon_key_policy_sha256_update(
    struct RibonKeyPolicySha256 *context,
    const uint8_t *bytes,
    size_t size)
{
    size_t index = 0u;

    context->total_bytes += size;
    while (index < size) {
        size_t available = sizeof(context->block) - context->used;
        size_t count = size - index;

        if (count > available) {
            count = available;
        }
        for (size_t copy = 0u; copy < count; ++copy) {
            context->block[context->used + copy] = bytes[index + copy];
        }
        context->used += count;
        index += count;
        if (context->used == sizeof(context->block)) {
            ribon_key_policy_sha256_transform(context, context->block);
            context->used = 0u;
        }
    }
}

/** @brief SHA-256 padding을 적용하고 caller-owned 32-byte digest를 봉인한다. */
static void
ribon_key_policy_sha256_finish(
    struct RibonKeyPolicySha256 *context,
    uint8_t digest[RIBON_KEY_POLICY_DIGEST_BYTES])
{
    const uint64_t bit_length = context->total_bytes * UINT64_C(8);
    uint8_t padding[128] = {0x80u};
    uint8_t length[8];
    size_t padding_size;
    uint32_t index;

    padding_size = context->used < 56u ? 56u - context->used :
        120u - context->used;
    for (index = 0u; index < 8u; ++index) {
        length[7u - index] = (uint8_t)(bit_length >> (index * 8u));
    }
    ribon_key_policy_sha256_update(context, padding, padding_size);
    ribon_key_policy_sha256_update(context, length, sizeof(length));
    for (index = 0u; index < 8u; ++index) {
        digest[index * 4u] = (uint8_t)(context->state[index] >> 24);
        digest[index * 4u + 1u] =
            (uint8_t)(context->state[index] >> 16);
        digest[index * 4u + 2u] =
            (uint8_t)(context->state[index] >> 8);
        digest[index * 4u + 3u] = (uint8_t)context->state[index];
    }
}

/** @brief 한 immutable byte view의 SHA-256을 계산한다. */
static void
ribon_key_policy_sha256(
    const uint8_t *bytes,
    size_t size,
    uint8_t digest[RIBON_KEY_POLICY_DIGEST_BYTES])
{
    struct RibonKeyPolicySha256 context;

    ribon_key_policy_sha256_initialize(&context);
    ribon_key_policy_sha256_update(&context, bytes, size);
    ribon_key_policy_sha256_finish(&context, digest);
}

/** @brief Byte view가 모두 0인지 allocation 없이 검사한다. */
static int
ribon_key_policy_bytes_are_zero(const uint8_t *bytes, size_t size)
{
    uint8_t value = 0u;
    size_t index;

    for (index = 0u; index < size; ++index) {
        value |= bytes[index];
    }
    return value == 0u;
}

/** @brief 두 immutable byte view를 길이만큼 비교한다. */
static int
ribon_key_policy_bytes_equal(
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

/** @brief Opaque key ID byte ordering을 locale 없이 비교한다. */
static int
ribon_key_policy_id_compare(
    const uint8_t *left,
    size_t left_size,
    const uint8_t *right,
    size_t right_size)
{
    const size_t shared = left_size < right_size ? left_size : right_size;
    size_t index;

    for (index = 0u; index < shared; ++index) {
        if (left[index] < right[index]) {
            return -1;
        }
        if (left[index] > right[index]) {
            return 1;
        }
    }
    if (left_size < right_size) {
        return -1;
    }
    return left_size > right_size ? 1 : 0;
}

/** @brief Key ID와 product store ID에 NUL byte가 없는지 검사한다. */
static int
ribon_key_policy_id_is_valid(const uint8_t *id, size_t size)
{
    size_t index;

    if (id == NULL || size == 0u ||
        size > RIBON_KEY_POLICY_MAX_KEY_ID_BYTES) {
        return 0;
    }
    for (index = 0u; index < size; ++index) {
        if (id[index] == 0u) {
            return 0;
        }
    }
    return 1;
}

/** @brief Canonical digest table에서 exact domain을 찾는다. */
static int
ribon_key_policy_record_has_domain(
    const struct RibonKeyPolicyRecord *record,
    const uint8_t domain[RIBON_KEY_POLICY_DIGEST_BYTES])
{
    uint32_t index;

    for (index = 0u; index < record->rollback_domain_count; ++index) {
        const int order = ribon_key_policy_id_compare(
            record->rollback_domain_digests[index],
            RIBON_KEY_POLICY_DIGEST_BYTES,
            domain,
            RIBON_KEY_POLICY_DIGEST_BYTES);

        if (order == 0) {
            return 1;
        }
        if (order > 0) {
            return 0;
        }
    }
    return 0;
}

/** @brief Stable key-ID 순 table에서 하나의 record index를 찾는다. */
static int
ribon_key_policy_find_record(
    const struct RibonKeyPolicyStore *store,
    const uint8_t *key_id,
    size_t key_id_size,
    uint32_t *index_out)
{
    uint32_t index;

    for (index = 0u; index < store->record_count; ++index) {
        const struct RibonKeyPolicyRecord *record = &store->records[index];
        const int order = ribon_key_policy_id_compare(
            record->key_id,
            record->key_id_size,
            key_id,
            key_id_size);

        if (order == 0) {
            *index_out = index;
            return 1;
        }
        if (order > 0) {
            return 0;
        }
    }
    return 0;
}

/** @brief Issuer record가 child의 전체 정적 authority를 포함하는지 검사한다. */
static int
ribon_key_policy_record_contains(
    const struct RibonKeyPolicyRecord *issuer,
    const struct RibonKeyPolicyRecord *child)
{
    uint32_t index;

    if ((child->usage_mask & ~issuer->usage_mask) != 0u ||
        (child->mode_mask & ~issuer->mode_mask) != 0u ||
        child->minimum_sequence < issuer->minimum_sequence ||
        child->maximum_sequence > issuer->maximum_sequence ||
        !ribon_key_policy_bytes_equal(
            issuer->product_digest,
            child->product_digest,
            RIBON_KEY_POLICY_DIGEST_BYTES)) {
        return 0;
    }
    for (index = 0u; index < child->rollback_domain_count; ++index) {
        if (!ribon_key_policy_record_has_domain(
                issuer,
                child->rollback_domain_digests[index])) {
            return 0;
        }
    }
    return 1;
}

/** @brief SHA-256 stream에 unsigned little-endian u16을 추가한다. */
static void
ribon_key_policy_hash_u16(
    struct RibonKeyPolicySha256 *hash,
    uint16_t value)
{
    const uint8_t bytes[2] = {
        (uint8_t)value,
        (uint8_t)(value >> 8),
    };

    ribon_key_policy_sha256_update(hash, bytes, sizeof(bytes));
}

/** @brief SHA-256 stream에 unsigned little-endian u32를 추가한다. */
static void
ribon_key_policy_hash_u32(
    struct RibonKeyPolicySha256 *hash,
    uint32_t value)
{
    uint8_t bytes[4];
    uint32_t index;

    for (index = 0u; index < 4u; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
    ribon_key_policy_sha256_update(hash, bytes, sizeof(bytes));
}

/** @brief SHA-256 stream에 unsigned little-endian u64를 추가한다. */
static void
ribon_key_policy_hash_u64(
    struct RibonKeyPolicySha256 *hash,
    uint64_t value)
{
    uint8_t bytes[8];
    uint32_t index;

    for (index = 0u; index < 8u; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
    ribon_key_policy_sha256_update(hash, bytes, sizeof(bytes));
}

/** @brief Store의 pointer-free canonical serialization digest를 재계산한다. */
static void
ribon_key_policy_store_digest(
    const struct RibonKeyPolicyStore *store,
    uint8_t digest[RIBON_KEY_POLICY_DIGEST_BYTES])
{
    static const uint8_t magic[32] = "RIBON-KEY-STORE-V1";
    struct RibonKeyPolicySha256 hash;
    uint8_t id_digest[RIBON_KEY_POLICY_DIGEST_BYTES];
    uint8_t issuer_digest[RIBON_KEY_POLICY_DIGEST_BYTES];
    uint32_t record_index;

    ribon_key_policy_sha256_initialize(&hash);
    ribon_key_policy_sha256_update(&hash, magic, sizeof(magic));
    ribon_key_policy_hash_u16(&hash, 1u);
    ribon_key_policy_hash_u16(&hash, 0u);
    ribon_key_policy_hash_u32(&hash, store->record_count);
    ribon_key_policy_hash_u64(&hash, store->generation);
    ribon_key_policy_sha256(store->id, store->id_size, id_digest);
    ribon_key_policy_sha256_update(&hash, id_digest, sizeof(id_digest));
    for (record_index = 0u; record_index < store->record_count;
         ++record_index) {
        const struct RibonKeyPolicyRecord *record =
            &store->records[record_index];
        uint32_t domain_index;

        ribon_key_policy_sha256(
            record->key_id,
            record->key_id_size,
            id_digest);
        ribon_key_policy_sha256_update(&hash, id_digest, sizeof(id_digest));
        ribon_key_policy_sha256_update(
            &hash,
            record->public_key,
            sizeof(record->public_key));
        ribon_key_policy_sha256_update(
            &hash,
            record->key_identity_digest,
            sizeof(record->key_identity_digest));
        ribon_key_policy_sha256_update(
            &hash,
            record->product_digest,
            sizeof(record->product_digest));
        ribon_key_policy_hash_u64(&hash, record->usage_mask);
        ribon_key_policy_hash_u32(&hash, record->mode_mask);
        ribon_key_policy_hash_u32(&hash, (uint32_t)record->lifecycle);
        ribon_key_policy_hash_u32(&hash, record->flags);
        ribon_key_policy_hash_u64(&hash, record->minimum_sequence);
        ribon_key_policy_hash_u64(&hash, record->maximum_sequence);
        if ((record->flags & RIBON_KEY_POLICY_RECORD_ROOT) != 0u) {
            for (domain_index = 0u;
                 domain_index < sizeof(issuer_digest);
                 ++domain_index) {
                issuer_digest[domain_index] = 0u;
            }
        } else {
            ribon_key_policy_sha256(
                record->issuer_key_id,
                record->issuer_key_id_size,
                issuer_digest);
        }
        ribon_key_policy_sha256_update(
            &hash,
            issuer_digest,
            sizeof(issuer_digest));
        ribon_key_policy_hash_u32(&hash, record->delegation_depth);
        ribon_key_policy_hash_u32(&hash, record->rollback_domain_count);
        for (domain_index = 0u;
             domain_index < record->rollback_domain_count;
             ++domain_index) {
            ribon_key_policy_sha256_update(
                &hash,
                record->rollback_domain_digests[domain_index],
                RIBON_KEY_POLICY_DIGEST_BYTES);
        }
    }
    ribon_key_policy_sha256_finish(&hash, digest);
}

/** @brief 한 generated key record의 fixed shape와 digest를 검사한다. */
static int
ribon_key_policy_record_shape_is_valid(
    const struct RibonKeyPolicyRecord *record)
{
    uint8_t public_key_digest[RIBON_KEY_POLICY_DIGEST_BYTES];
    uint32_t index;

    if (record->size != sizeof(*record) ||
        record->abi_version != RIBON_KEY_POLICY_ABI_VERSION ||
        (record->flags & ~RIBON_KEY_POLICY_RECORD_ROOT) != 0u ||
        record->algorithm != RIBON_SIGNATURE_ALGORITHM_ED25519 ||
        (record->lifecycle != RIBON_KEY_POLICY_LIFECYCLE_ACTIVE &&
         record->lifecycle != RIBON_KEY_POLICY_LIFECYCLE_RETIRING &&
         record->lifecycle != RIBON_KEY_POLICY_LIFECYCLE_REVOKED) ||
        record->mode_mask == 0u ||
        (record->mode_mask & ~RIBON_KEY_POLICY_ALL_MODE_MASK) != 0u ||
        record->usage_mask == 0u ||
        (record->usage_mask & ~RIBON_KEY_POLICY_ALL_USAGE_MASK) != 0u ||
        !ribon_key_policy_id_is_valid(
            record->key_id,
            record->key_id_size) ||
        ribon_key_policy_bytes_are_zero(
            record->public_key,
            sizeof(record->public_key)) ||
        ribon_key_policy_bytes_are_zero(
            record->key_identity_digest,
            sizeof(record->key_identity_digest)) ||
        ribon_key_policy_bytes_are_zero(
            record->product_digest,
            sizeof(record->product_digest)) ||
        record->rollback_domain_digests == NULL ||
        record->rollback_domain_count == 0u ||
        record->rollback_domain_count > RIBON_KEY_POLICY_MAX_DOMAINS ||
        record->minimum_sequence > record->maximum_sequence ||
        !ribon_key_policy_bytes_are_zero(
            (const uint8_t *)record->reserved,
            sizeof(record->reserved))) {
        return 0;
    }
    ribon_key_policy_sha256(
        record->public_key,
        sizeof(record->public_key),
        public_key_digest);
    if (!ribon_key_policy_bytes_equal(
            public_key_digest,
            record->key_identity_digest,
            sizeof(public_key_digest))) {
        return 0;
    }
    if ((record->flags & RIBON_KEY_POLICY_RECORD_ROOT) != 0u) {
        if (record->issuer_key_id != NULL || record->issuer_key_id_size != 0u ||
            record->delegation_depth != 0u) {
            return 0;
        }
    } else if (!ribon_key_policy_id_is_valid(
                   record->issuer_key_id,
                   record->issuer_key_id_size) ||
               record->delegation_depth == 0u ||
               record->delegation_depth >
                   RIBON_KEY_POLICY_MAX_DELEGATION_EDGES) {
        return 0;
    }
    for (index = 0u; index < record->rollback_domain_count; ++index) {
        if (ribon_key_policy_bytes_are_zero(
                record->rollback_domain_digests[index],
                RIBON_KEY_POLICY_DIGEST_BYTES) ||
            (index > 0u && ribon_key_policy_id_compare(
                record->rollback_domain_digests[index - 1u],
                RIBON_KEY_POLICY_DIGEST_BYTES,
                record->rollback_domain_digests[index],
                RIBON_KEY_POLICY_DIGEST_BYTES) >= 0)) {
            return 0;
        }
    }
    return 1;
}

/** @brief Stored digest를 제외한 store shape와 bounded issuer graph를 검사한다. */
static int
ribon_key_policy_store_structure_is_valid(
    const struct RibonKeyPolicyStore *store)
{
    uint32_t index;

    if (store == NULL || store->magic != RIBON_KEY_POLICY_STORE_MAGIC ||
        store->size != sizeof(*store) ||
        store->abi_version != RIBON_KEY_POLICY_ABI_VERSION ||
        store->flags != 0u ||
        !ribon_key_policy_id_is_valid(store->id, store->id_size) ||
        store->generation == 0u || store->records == NULL ||
        store->record_count == 0u ||
        store->record_count > RIBON_KEY_POLICY_MAX_RECORDS ||
        store->reserved0 != 0u ||
        !ribon_key_policy_bytes_are_zero(
            (const uint8_t *)store->reserved,
            sizeof(store->reserved))) {
        return RIBON_KEY_POLICY_STATUS_INVALID_STORE;
    }
    for (index = 0u; index < store->record_count; ++index) {
        const struct RibonKeyPolicyRecord *record = &store->records[index];
        uint32_t other;

        if (!ribon_key_policy_record_shape_is_valid(record) ||
            (index > 0u && ribon_key_policy_id_compare(
                store->records[index - 1u].key_id,
                store->records[index - 1u].key_id_size,
                record->key_id,
                record->key_id_size) >= 0)) {
            return RIBON_KEY_POLICY_STATUS_INVALID_STORE;
        }
        for (other = 0u; other < index; ++other) {
            const struct RibonKeyPolicyRecord *candidate =
                &store->records[other];

            if (ribon_key_policy_bytes_equal(
                    record->key_identity_digest,
                    candidate->key_identity_digest,
                    RIBON_KEY_POLICY_DIGEST_BYTES)) {
                return RIBON_KEY_POLICY_STATUS_INVALID_STORE;
            }
        }
    }
    for (index = 0u; index < store->record_count; ++index) {
        const struct RibonKeyPolicyRecord *record = &store->records[index];
        const struct RibonKeyPolicyRecord *cursor = record;
        uint32_t depth = 0u;

        while ((cursor->flags & RIBON_KEY_POLICY_RECORD_ROOT) == 0u) {
            uint32_t issuer_index;
            const struct RibonKeyPolicyRecord *issuer;

            if (depth >= RIBON_KEY_POLICY_MAX_DELEGATION_EDGES ||
                !ribon_key_policy_find_record(
                    store,
                    cursor->issuer_key_id,
                    cursor->issuer_key_id_size,
                    &issuer_index)) {
                return RIBON_KEY_POLICY_STATUS_INVALID_STORE;
            }
            issuer = &store->records[issuer_index];
            if (issuer == cursor ||
                !ribon_key_policy_record_contains(issuer, cursor)) {
                return RIBON_KEY_POLICY_STATUS_INVALID_STORE;
            }
            cursor = issuer;
            ++depth;
        }
        if (record->delegation_depth != depth) {
            return RIBON_KEY_POLICY_STATUS_INVALID_STORE;
        }
    }
    return RIBON_KEY_POLICY_STATUS_OK;
}

/**
 * @brief Struct layout과 pointer를 제외한 canonical store digest를 계산한다.
 *
 * 최대 32 record와 두 edge의 structure가 유효할 때만 caller-owned output을 쓴다.
 */
int
ribon_key_policy_store_canonical_digest(
    const struct RibonKeyPolicyStore *store,
    uint8_t digest[RIBON_KEY_POLICY_DIGEST_BYTES])
{
    const int status = ribon_key_policy_store_structure_is_valid(store);

    if (status != RIBON_KEY_POLICY_STATUS_OK || digest == NULL) {
        return status != RIBON_KEY_POLICY_STATUS_OK ? status :
            RIBON_KEY_POLICY_STATUS_INVALID_ARGUMENT;
    }
    ribon_key_policy_store_digest(store, digest);
    return RIBON_KEY_POLICY_STATUS_OK;
}

/**
 * @brief Store의 정렬, digest, delegation과 authority containment를 재검산한다.
 *
 * Generated C를 신뢰하지 않고 canonical digest를 runtime에서 다시 계산한다.
 */
int
ribon_key_policy_store_validate(const struct RibonKeyPolicyStore *store)
{
    uint8_t digest[RIBON_KEY_POLICY_DIGEST_BYTES];
    int status;

    status = ribon_key_policy_store_canonical_digest(store, digest);
    if (status != RIBON_KEY_POLICY_STATUS_OK ||
        ribon_key_policy_bytes_are_zero(
            store->canonical_digest,
            sizeof(store->canonical_digest)) ||
        !ribon_key_policy_bytes_equal(
            digest,
            store->canonical_digest,
            sizeof(digest))) {
        return status != RIBON_KEY_POLICY_STATUS_OK ? status :
            RIBON_KEY_POLICY_STATUS_INVALID_STORE;
    }
    return RIBON_KEY_POLICY_STATUS_OK;
}

/** @brief Request의 fixed shape와 reserved-zero contract를 검사한다. */
static int
ribon_key_policy_request_is_valid(
    const struct RibonKeyPolicyRequest *request)
{
    return request != NULL && request->size == sizeof(*request) &&
        request->abi_version == RIBON_KEY_POLICY_ABI_VERSION &&
        request->mode >= RIBON_KEY_POLICY_MODE_NORMAL &&
        request->mode <= RIBON_KEY_POLICY_MODE_DIAGNOSTIC &&
        request->usage >= RIBON_KEY_POLICY_USAGE_POLICY_NORMAL &&
        request->usage <= RIBON_KEY_POLICY_USAGE_BOOT_IMAGE &&
        request->flags == 0u && request->reserved0 == 0u &&
        ribon_key_policy_id_is_valid(request->key_id, request->key_id_size) &&
        !ribon_key_policy_bytes_are_zero(
            request->product_digest,
            sizeof(request->product_digest)) &&
        !ribon_key_policy_bytes_are_zero(
            request->rollback_domain_digest,
            sizeof(request->rollback_domain_digest)) &&
        ribon_key_policy_bytes_are_zero(
            (const uint8_t *)request->reserved,
            sizeof(request->reserved));
}

/** @brief 한 chain record가 exact request tuple을 승인하는지 검사한다. */
static int
ribon_key_policy_record_allows_request(
    const struct RibonKeyPolicyRecord *record,
    const struct RibonKeyPolicyRequest *request)
{
    const uint32_t mode_bit = UINT32_C(1) << ((uint32_t)request->mode - 1u);
    const uint64_t usage_bit =
        UINT64_C(1) << ((uint32_t)request->usage - 1u);

    return record->lifecycle != RIBON_KEY_POLICY_LIFECYCLE_REVOKED &&
        (record->mode_mask & mode_bit) != 0u &&
        (record->usage_mask & usage_bit) != 0u &&
        request->sequence >= record->minimum_sequence &&
        request->sequence <= record->maximum_sequence &&
        ribon_key_policy_bytes_equal(
            record->product_digest,
            request->product_digest,
            RIBON_KEY_POLICY_DIGEST_BYTES) &&
        ribon_key_policy_record_has_domain(
            record,
            request->rollback_domain_digest);
}

/**
 * @brief Product, mode, usage, domain, sequence와 issuer chain을 승인한다.
 *
 * Store validation과 request-specific lookup 모두 고정 상한 안에서 수행한다.
 */
int
ribon_key_policy_authorize(
    const struct RibonKeyPolicyStore *store,
    const struct RibonKeyPolicyRequest *request,
    struct RibonKeyPolicyDecision *decision)
{
    const struct RibonKeyPolicyRecord *record;
    const struct RibonKeyPolicyRecord *cursor;
    uint32_t record_index;
    uint32_t depth = 0u;
    int status;

    if (decision == NULL) {
        return RIBON_KEY_POLICY_STATUS_INVALID_ARGUMENT;
    }
    *decision = (struct RibonKeyPolicyDecision){
        .size = sizeof(*decision),
        .abi_version = RIBON_KEY_POLICY_ABI_VERSION,
        .record_index = UINT32_MAX,
    };
    status = ribon_key_policy_store_validate(store);
    if (status != RIBON_KEY_POLICY_STATUS_OK) {
        return status;
    }
    if (!ribon_key_policy_request_is_valid(request)) {
        return RIBON_KEY_POLICY_STATUS_INVALID_ARGUMENT;
    }
    if (request->usage <= RIBON_KEY_POLICY_USAGE_POLICY_DIAGNOSTIC &&
        (uint32_t)request->mode != (uint32_t)request->usage) {
        return RIBON_KEY_POLICY_STATUS_MODE_USAGE_MISMATCH;
    }
    if (!ribon_key_policy_find_record(
            store,
            request->key_id,
            request->key_id_size,
            &record_index)) {
        return RIBON_KEY_POLICY_STATUS_KEY_UNKNOWN;
    }
    record = &store->records[record_index];
    if (!ribon_key_policy_bytes_equal(
            record->product_digest,
            request->product_digest,
            RIBON_KEY_POLICY_DIGEST_BYTES)) {
        return RIBON_KEY_POLICY_STATUS_IDENTITY_MISMATCH;
    }
    if (!ribon_key_policy_record_has_domain(
            record,
            request->rollback_domain_digest)) {
        return RIBON_KEY_POLICY_STATUS_DOMAIN_MISMATCH;
    }
    cursor = record;
    for (;;) {
        uint32_t issuer_index;

        if (!ribon_key_policy_record_allows_request(cursor, request)) {
            return RIBON_KEY_POLICY_STATUS_KEY_POLICY;
        }
        if ((cursor->flags & RIBON_KEY_POLICY_RECORD_ROOT) != 0u) {
            break;
        }
        if (depth >= RIBON_KEY_POLICY_MAX_DELEGATION_EDGES ||
            !ribon_key_policy_find_record(
                store,
                cursor->issuer_key_id,
                cursor->issuer_key_id_size,
                &issuer_index)) {
            return RIBON_KEY_POLICY_STATUS_INVALID_STORE;
        }
        cursor = &store->records[issuer_index];
        ++depth;
    }
    decision->record_index = record_index;
    decision->delegation_depth = depth;
    decision->trust_store_generation = store->generation;
    for (size_t index = 0u; index < RIBON_KEY_POLICY_DIGEST_BYTES; ++index) {
        decision->key_identity_digest[index] =
            record->key_identity_digest[index];
        decision->trust_store_digest[index] = store->canonical_digest[index];
    }
    return RIBON_KEY_POLICY_STATUS_OK;
}

/** @brief Signature verification request의 pointer와 reserved contract를 검사한다. */
static int
ribon_key_policy_verification_is_valid(
    const struct RibonKeyPolicySignatureVerification *verification)
{
    return verification != NULL &&
        verification->size == sizeof(*verification) &&
        verification->abi_version == RIBON_KEY_POLICY_ABI_VERSION &&
        verification->flags == 0u && verification->reserved0 == 0u &&
        verification->policy != NULL && verification->provider != NULL &&
        (verification->message != NULL || verification->message_size == 0u) &&
        verification->signature != NULL &&
        ribon_key_policy_bytes_are_zero(
            (const uint8_t *)verification->reserved,
            sizeof(verification->reserved));
}

/**
 * @brief Key policy를 먼저 승인한 뒤 exact selected key로 signature를 검증한다.
 *
 * 실패한 key tuple은 cryptographic callback에 도달하지 않으며 persistent state를
 * 변경하지 않는다.
 */
int
ribon_key_policy_verify(
    const struct RibonKeyPolicyStore *store,
    const struct RibonKeyPolicySignatureVerification *verification,
    struct RibonKeyPolicyDecision *decision)
{
    const struct RibonKeyPolicyRecord *record;
    struct RibonSignatureVerification signature;
    int status;

    if (!ribon_key_policy_verification_is_valid(verification)) {
        return RIBON_KEY_POLICY_STATUS_INVALID_ARGUMENT;
    }
    status = ribon_key_policy_authorize(store, verification->policy, decision);
    if (status != RIBON_KEY_POLICY_STATUS_OK) {
        return status;
    }
    record = &store->records[decision->record_index];
    if (verification->provider->algorithm != record->algorithm) {
        return RIBON_KEY_POLICY_STATUS_UNSUPPORTED_ALGORITHM;
    }
    signature = (struct RibonSignatureVerification){
        .size = sizeof(signature),
        .abi_version = RIBON_SIGNATURE_PROVIDER_ABI_VERSION,
        .algorithm = record->algorithm,
        .public_key = record->public_key,
        .public_key_size = sizeof(record->public_key),
        .message = verification->message,
        .message_size = verification->message_size,
        .signature = verification->signature,
        .signature_size = verification->signature_size,
        .workspace = verification->workspace,
        .workspace_size = verification->workspace_size,
    };
    status = ribon_signature_verify(verification->provider, &signature);
    if (status == RIBON_SIGNATURE_STATUS_UNSUPPORTED_ALGORITHM) {
        return RIBON_KEY_POLICY_STATUS_UNSUPPORTED_ALGORITHM;
    }
    return status == RIBON_SIGNATURE_STATUS_OK ?
        RIBON_KEY_POLICY_STATUS_OK :
        RIBON_KEY_POLICY_STATUS_SIGNATURE_INVALID;
}
