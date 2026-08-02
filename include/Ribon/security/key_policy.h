#ifndef RIBON_SECURITY_KEY_POLICY_H
#define RIBON_SECURITY_KEY_POLICY_H

#include <stddef.h>
#include <stdint.h>

#include <Ribon/security/signature.h>

/** @brief Product key-policy store descriptor를 식별하는 magic이다. */
#define RIBON_KEY_POLICY_STORE_MAGIC 0x52424b50u

/** @brief Product key-policy native ABI v1의 version이다. */
#define RIBON_KEY_POLICY_ABI_VERSION 1u

/** @brief Product key-policy identity에 사용하는 SHA-256 길이다. */
#define RIBON_KEY_POLICY_DIGEST_BYTES 32u

/** @brief 한 product trust store가 가질 수 있는 최대 key record 수다. */
#define RIBON_KEY_POLICY_MAX_RECORDS 32u

/** @brief 한 key record가 승인할 수 있는 rollback domain 최대 수다. */
#define RIBON_KEY_POLICY_MAX_DOMAINS 4u

/** @brief Root 아래에서 허용하는 최대 delegation edge 수다. */
#define RIBON_KEY_POLICY_MAX_DELEGATION_EDGES 2u

/** @brief Artifact가 가질 수 있는 opaque key ID 최대 길이다. */
#define RIBON_KEY_POLICY_MAX_KEY_ID_BYTES 64u

/** @brief Issuer가 없는 root key record를 나타내는 flag다. */
#define RIBON_KEY_POLICY_RECORD_ROOT (UINT32_C(1) << 0)

/** @brief Canonical trust message와 공유하는 stable execution mode registry다. */
enum RibonKeyPolicyMode {
    RIBON_KEY_POLICY_MODE_INVALID = 0,
    RIBON_KEY_POLICY_MODE_NORMAL = 1,
    RIBON_KEY_POLICY_MODE_RECOVERY = 2,
    RIBON_KEY_POLICY_MODE_PROVISIONING = 3,
    RIBON_KEY_POLICY_MODE_DIAGNOSTIC = 4,
};

/** @brief 한 signed object가 요청하는 단일 key usage registry다. */
enum RibonKeyPolicyUsage {
    RIBON_KEY_POLICY_USAGE_INVALID = 0,
    RIBON_KEY_POLICY_USAGE_POLICY_NORMAL = 1,
    RIBON_KEY_POLICY_USAGE_POLICY_RECOVERY = 2,
    RIBON_KEY_POLICY_USAGE_POLICY_PROVISIONING = 3,
    RIBON_KEY_POLICY_USAGE_POLICY_DIAGNOSTIC = 4,
    RIBON_KEY_POLICY_USAGE_UPDATE_MANIFEST = 5,
    RIBON_KEY_POLICY_USAGE_BOOT_IMAGE = 6,
    RIBON_KEY_POLICY_USAGE_BOOT_CONFIRMATION = 7,
};

/** @brief Wall clock 없이 sequence 범위로 해석하는 key lifecycle이다. */
enum RibonKeyPolicyLifecycle {
    RIBON_KEY_POLICY_LIFECYCLE_INVALID = 0,
    RIBON_KEY_POLICY_LIFECYCLE_ACTIVE = 1,
    RIBON_KEY_POLICY_LIFECYCLE_RETIRING = 2,
    RIBON_KEY_POLICY_LIFECYCLE_REVOKED = 3,
};

/** @brief Product-bound key authorization의 stable fail-closed 결과다. */
enum RibonKeyPolicyStatus {
    RIBON_KEY_POLICY_STATUS_OK = 0,
    RIBON_KEY_POLICY_STATUS_INVALID_ARGUMENT = -1,
    RIBON_KEY_POLICY_STATUS_INVALID_STORE = -2,
    RIBON_KEY_POLICY_STATUS_IDENTITY_MISMATCH = -3,
    RIBON_KEY_POLICY_STATUS_MODE_USAGE_MISMATCH = -4,
    RIBON_KEY_POLICY_STATUS_DOMAIN_MISMATCH = -5,
    RIBON_KEY_POLICY_STATUS_KEY_UNKNOWN = -6,
    RIBON_KEY_POLICY_STATUS_KEY_POLICY = -7,
    RIBON_KEY_POLICY_STATUS_UNSUPPORTED_ALGORITHM = -8,
    RIBON_KEY_POLICY_STATUS_SIGNATURE_INVALID = -9,
};

/**
 * @brief Generated immutable trust store의 한 bounded key record다.
 *
 * 모든 pointer는 product image와 같은 lifetime의 read-only storage를 가리킨다.
 * Domain table은 canonical digest 오름차순이고 issuer chain은 최대 두 edge다.
 */
struct RibonKeyPolicyRecord {
    uint32_t size;
    uint32_t abi_version;
    uint32_t flags;
    enum RibonSignatureAlgorithm algorithm;
    enum RibonKeyPolicyLifecycle lifecycle;
    uint32_t mode_mask;
    uint64_t usage_mask;
    const uint8_t *key_id;
    size_t key_id_size;
    uint8_t public_key[RIBON_ED25519_PUBLIC_KEY_BYTES];
    uint8_t key_identity_digest[RIBON_KEY_POLICY_DIGEST_BYTES];
    uint8_t product_digest[RIBON_KEY_POLICY_DIGEST_BYTES];
    const uint8_t (*rollback_domain_digests)[RIBON_KEY_POLICY_DIGEST_BYTES];
    uint32_t rollback_domain_count;
    uint32_t delegation_depth;
    const uint8_t *issuer_key_id;
    size_t issuer_key_id_size;
    uint64_t minimum_sequence;
    uint64_t maximum_sequence;
    uint64_t reserved[4];
};

/**
 * @brief Product manifest에서 생성되는 immutable bounded trust store다.
 *
 * Record는 key ID byte 순서로 정렬된다. Canonical digest는 pointer와 C layout을
 * 제외한 versioned little-endian serialization을 봉인한다.
 */
struct RibonKeyPolicyStore {
    uint32_t magic;
    uint32_t size;
    uint32_t abi_version;
    uint32_t flags;
    const uint8_t *id;
    size_t id_size;
    uint64_t generation;
    const struct RibonKeyPolicyRecord *records;
    uint32_t record_count;
    uint32_t reserved0;
    uint8_t canonical_digest[RIBON_KEY_POLICY_DIGEST_BYTES];
    uint64_t reserved[4];
};

/** @brief 한 signed object가 immutable product key authority에 요청하는 tuple이다. */
struct RibonKeyPolicyRequest {
    uint32_t size;
    uint32_t abi_version;
    enum RibonKeyPolicyMode mode;
    enum RibonKeyPolicyUsage usage;
    uint32_t flags;
    uint32_t reserved0;
    const uint8_t *key_id;
    size_t key_id_size;
    uint8_t product_digest[RIBON_KEY_POLICY_DIGEST_BYTES];
    uint8_t rollback_domain_digest[RIBON_KEY_POLICY_DIGEST_BYTES];
    uint64_t sequence;
    uint64_t reserved[4];
};

/**
 * @brief Key policy 승인 결과를 native authorizer에 전달하는 pointer-free handle이다.
 *
 * Public key와 trust-store pointer는 포함하지 않으므로 Ribos receipt로 복사할 수
 * 있는 값은 identity digest와 generation뿐이다.
 */
struct RibonKeyPolicyDecision {
    uint32_t size;
    uint32_t abi_version;
    uint32_t record_index;
    uint32_t delegation_depth;
    uint64_t trust_store_generation;
    uint8_t key_identity_digest[RIBON_KEY_POLICY_DIGEST_BYTES];
    uint8_t trust_store_digest[RIBON_KEY_POLICY_DIGEST_BYTES];
    uint64_t reserved[4];
};

/** @brief Key policy 승인 뒤 selected provider로 검증할 immutable signature view다. */
struct RibonKeyPolicySignatureVerification {
    uint32_t size;
    uint32_t abi_version;
    uint32_t flags;
    uint32_t reserved0;
    const struct RibonKeyPolicyRequest *policy;
    const struct RibonSignatureProvider *provider;
    const uint8_t *message;
    size_t message_size;
    const uint8_t *signature;
    size_t signature_size;
    void *workspace;
    size_t workspace_size;
    uint64_t reserved[4];
};

/** @brief Store의 정렬, digest, delegation과 authority containment를 재검산한다. */
int ribon_key_policy_store_validate(const struct RibonKeyPolicyStore *store);

/**
 * @brief Struct layout과 pointer를 제외한 canonical store digest를 계산한다.
 *
 * Caller는 32-byte output을 소유한다. Stored digest 값 자체는 입력 serialization에
 * 포함되지 않으며 이 함수는 생성기와 runtime validator의 공통 byte 계약을 검사한다.
 */
int ribon_key_policy_store_canonical_digest(
    const struct RibonKeyPolicyStore *store,
    uint8_t digest[RIBON_KEY_POLICY_DIGEST_BYTES]);

/**
 * @brief Product, mode, usage, domain, sequence와 issuer chain을 승인한다.
 *
 * 함수는 allocation, network, wall clock과 persistent write를 사용하지 않는다.
 * 성공 시 public key가 아닌 pointer-free decision만 반환한다.
 */
int ribon_key_policy_authorize(
    const struct RibonKeyPolicyStore *store,
    const struct RibonKeyPolicyRequest *request,
    struct RibonKeyPolicyDecision *decision);

/**
 * @brief Key policy를 먼저 승인한 뒤 exact selected key로 signature를 검증한다.
 *
 * Key-policy 실패 시 provider callback을 호출하지 않는다. 이 함수는 rollback
 * protected state나 bytecode verifier를 실행하지 않는다.
 */
int ribon_key_policy_verify(
    const struct RibonKeyPolicyStore *store,
    const struct RibonKeyPolicySignatureVerification *verification,
    struct RibonKeyPolicyDecision *decision);

#endif
