#ifndef RIBON_SECURITY_SIGNATURE_H
#define RIBON_SECURITY_SIGNATURE_H

#include <stddef.h>
#include <stdint.h>

/** @brief Signature provider descriptor를 식별하는 magic이다. */
#define RIBON_SIGNATURE_PROVIDER_MAGIC 0x52425350u

/** @brief Signature provider ABI v1의 version이다. */
#define RIBON_SIGNATURE_PROVIDER_ABI_VERSION 1u

/** @brief Ed25519 public key의 canonical byte 길이다. */
#define RIBON_ED25519_PUBLIC_KEY_BYTES 32u

/** @brief Ed25519 signature의 canonical byte 길이다. */
#define RIBON_ED25519_SIGNATURE_BYTES 64u

/** @brief Product graph가 선택할 수 있는 signature algorithm이다. */
enum RibonSignatureAlgorithm {
    RIBON_SIGNATURE_ALGORITHM_INVALID = 0,
    RIBON_SIGNATURE_ALGORITHM_ED25519 = 1,
};

/** @brief Production graph와 test fixture graph를 분리하는 provider class다. */
enum RibonSignatureProviderClass {
    RIBON_SIGNATURE_PROVIDER_CLASS_INVALID = 0,
    RIBON_SIGNATURE_PROVIDER_CLASS_PRODUCTION = 1,
    RIBON_SIGNATURE_PROVIDER_CLASS_FIXTURE = 2,
};

/** @brief Signature verification의 fail-closed 결과 registry다. */
enum RibonSignatureStatus {
    RIBON_SIGNATURE_STATUS_OK = 0,
    RIBON_SIGNATURE_STATUS_INVALID_ARGUMENT = -1,
    RIBON_SIGNATURE_STATUS_INVALID_PROVIDER = -2,
    RIBON_SIGNATURE_STATUS_UNSUPPORTED_ALGORITHM = -3,
    RIBON_SIGNATURE_STATUS_INVALID_ENCODING = -4,
    RIBON_SIGNATURE_STATUS_INVALID_SIGNATURE = -5,
    RIBON_SIGNATURE_STATUS_WORKSPACE_TOO_SMALL = -6,
};

/**
 * @brief Provider가 검사하는 caller-owned immutable byte view다.
 *
 * Message는 길이 0일 때 NULL일 수 있다. Public key와 signature는 algorithm의
 * canonical fixed size여야 한다. Workspace가 필요한 provider는 caller가 storage와
 * lifetime을 소유하며 provider는 반환 뒤 pointer를 보존하지 않는다.
 */
struct RibonSignatureVerification {
    uint32_t size;
    uint32_t abi_version;
    enum RibonSignatureAlgorithm algorithm;
    uint32_t flags;
    const uint8_t *public_key;
    size_t public_key_size;
    const uint8_t *message;
    size_t message_size;
    const uint8_t *signature;
    size_t signature_size;
    void *workspace;
    size_t workspace_size;
    uint64_t reserved[4];
};

struct RibonSignatureProvider;

/** @brief Typed provider가 수행하는 verification-only callback이다. */
typedef int (*RibonSignatureVerifyFn)(
    const struct RibonSignatureProvider *provider,
    const struct RibonSignatureVerification *request);

/**
 * @brief Product graph가 정확히 하나 선택하는 immutable signature provider다.
 *
 * Provider는 signer, private key, allocator 또는 mutable global state를 노출하지
 * 않는다. `workspace_bytes == 0`이면 fixed stack만 사용하며 workspace pointer는
 * NULL이어야 한다.
 */
struct RibonSignatureProvider {
    uint32_t magic;
    uint32_t size;
    uint32_t abi_version;
    enum RibonSignatureProviderClass provider_class;
    enum RibonSignatureAlgorithm algorithm;
    uint32_t flags;
    const char *id;
    size_t public_key_bytes;
    size_t signature_bytes;
    size_t workspace_bytes;
    size_t workspace_alignment;
    RibonSignatureVerifyFn verify;
    uint64_t reserved[4];
};

/** @brief Provider descriptor의 ABI, class, 크기와 callback을 검사한다. */
int ribon_signature_provider_is_valid(
    const struct RibonSignatureProvider *provider);

/**
 * @brief Generic validation 뒤 selected provider로 signature를 검증한다.
 *
 * 반환값은 외부 diagnostic 문자열이 아니라 stable fail-closed class다. Product
 * authorizer는 필요하면 모든 실패를 하나의 authorization failure로 축약한다.
 */
int ribon_signature_verify(
    const struct RibonSignatureProvider *provider,
    const struct RibonSignatureVerification *request);

#endif
