#ifndef RIBON_UPDATE_MANIFEST_H
#define RIBON_UPDATE_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

#include <Ribon/security/key_policy.h>

/** @brief Update manifest native API v1의 version이다. */
#define RIBON_UPDATE_MANIFEST_ABI_VERSION 1u

/** @brief Update manifest identity와 content hash의 SHA-256 길이다. */
#define RIBON_UPDATE_MANIFEST_DIGEST_BYTES 32u

/** @brief Canonical update manifest header의 exact byte 길이다. */
#define RIBON_UPDATE_MANIFEST_HEADER_BYTES 256u

/** @brief Canonical product/target binding section의 exact byte 길이다. */
#define RIBON_UPDATE_MANIFEST_BINDING_BYTES 256u

/** @brief Canonical component table entry의 exact byte 길이다. */
#define RIBON_UPDATE_MANIFEST_COMPONENT_BYTES 192u

/** @brief 한 update manifest가 기술할 수 있는 최대 component 수다. */
#define RIBON_UPDATE_MANIFEST_MAX_COMPONENTS 16u

/** @brief Update manifest가 항상 갖는 canonical section 수다. */
#define RIBON_UPDATE_MANIFEST_SECTION_COUNT 2u

/** @brief Update manifest signature가 승인하는 canonical message 길이다. */
#define RIBON_UPDATE_SIGNED_MESSAGE_V1_BYTES 256u

/** @brief Detached signature envelope의 fixed header 길이다. */
#define RIBON_UPDATE_SIGNATURE_ENVELOPE_HEADER_BYTES 160u

/** @brief Detached signature envelope가 허용하는 opaque key ID 최대 길이다. */
#define RIBON_UPDATE_SIGNATURE_KEY_ID_MAX_BYTES 64u

/** @brief Component가 설치에 반드시 필요함을 나타내는 canonical flag다. */
#define RIBON_UPDATE_COMPONENT_REQUIRED UINT16_C(1)

/** @brief Update manifest의 stable section registry다. */
enum RibonUpdateManifestSectionType {
    RIBON_UPDATE_SECTION_INVALID = 0,
    RIBON_UPDATE_SECTION_BINDING = 1,
    RIBON_UPDATE_SECTION_COMPONENTS = 2,
};

/** @brief OS 이름과 독립적인 update component role registry다. */
enum RibonUpdateComponentRole {
    RIBON_UPDATE_COMPONENT_ROLE_INVALID = 0,
    RIBON_UPDATE_COMPONENT_ROLE_KERNEL = 1,
    RIBON_UPDATE_COMPONENT_ROLE_BOOT_MODULE = 2,
    RIBON_UPDATE_COMPONENT_ROLE_POLICY = 3,
    RIBON_UPDATE_COMPONENT_ROLE_FIRMWARE = 4,
    RIBON_UPDATE_COMPONENT_ROLE_RECOVERY_IMAGE = 5,
};

/** @brief Storage address를 노출하지 않는 semantic destination registry다. */
enum RibonUpdateDestinationClass {
    RIBON_UPDATE_DESTINATION_INVALID = 0,
    RIBON_UPDATE_DESTINATION_KERNEL_SLOT = 1,
    RIBON_UPDATE_DESTINATION_MODULE_SLOT = 2,
    RIBON_UPDATE_DESTINATION_POLICY_SLOT = 3,
    RIBON_UPDATE_DESTINATION_FIRMWARE_SLOT = 4,
    RIBON_UPDATE_DESTINATION_RECOVERY_SLOT = 5,
};

/** @brief Component payload의 bounded image-format registry다. */
enum RibonUpdateImageFormat {
    RIBON_UPDATE_IMAGE_FORMAT_INVALID = 0,
    RIBON_UPDATE_IMAGE_FORMAT_OPAQUE = 1,
    RIBON_UPDATE_IMAGE_FORMAT_ELF64 = 2,
    RIBON_UPDATE_IMAGE_FORMAT_PE_COFF = 3,
    RIBON_UPDATE_IMAGE_FORMAT_LINUX_IMAGE = 4,
    RIBON_UPDATE_IMAGE_FORMAT_RAW = 5,
};

/** @brief Update manifest와 authorization의 stable fail-closed 결과다. */
enum RibonUpdateManifestStatus {
    RIBON_UPDATE_MANIFEST_STATUS_OK = 0,
    RIBON_UPDATE_MANIFEST_STATUS_INVALID_ARGUMENT = -1,
    RIBON_UPDATE_MANIFEST_STATUS_CAPACITY = -2,
    RIBON_UPDATE_MANIFEST_STATUS_UNSUPPORTED_VERSION = -3,
    RIBON_UPDATE_MANIFEST_STATUS_UNSUPPORTED_ALGORITHM = -4,
    RIBON_UPDATE_MANIFEST_STATUS_MALFORMED = -5,
    RIBON_UPDATE_MANIFEST_STATUS_IDENTITY_MISMATCH = -6,
    RIBON_UPDATE_MANIFEST_STATUS_MODE_USAGE_MISMATCH = -7,
    RIBON_UPDATE_MANIFEST_STATUS_DOMAIN_MISMATCH = -8,
    RIBON_UPDATE_MANIFEST_STATUS_DIGEST_MISMATCH = -9,
    RIBON_UPDATE_MANIFEST_STATUS_KEY_POLICY = -10,
    RIBON_UPDATE_MANIFEST_STATUS_SIGNATURE_INVALID = -11,
};

/**
 * @brief Host composer가 canonical table로 내리는 한 component 입력이다.
 *
 * Pointer나 source path는 포함하지 않는다. Digest와 bounded bundle byte range만
 * target codec에 전달하며 `install_order`는 table index와 같아야 한다.
 */
struct RibonUpdateComponent {
    uint32_t size;
    uint32_t abi_version;
    uint32_t flags;
    uint32_t install_order;
    enum RibonUpdateComponentRole role;
    enum RibonUpdateDestinationClass destination_class;
    enum RibonUpdateImageFormat image_format;
    uint32_t reserved0;
    uint64_t bundle_offset;
    uint64_t exact_size;
    uint64_t maximum_size;
    uint8_t logical_id_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t content_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t destination_id_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t entry_contract_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint64_t reserved[4];
};

/** @brief Canonical manifest 생성에 필요한 product와 target binding 입력이다. */
struct RibonUpdateManifestInput {
    uint32_t size;
    uint32_t abi_version;
    uint32_t flags;
    enum RibonKeyPolicyMode mode;
    uint64_t bundle_generation;
    uint64_t predecessor_generation;
    uint64_t rollback_sequence;
    uint64_t creation_policy_version;
    uint16_t protocol_major;
    uint16_t protocol_minor;
    uint32_t minimum_hardware_revision;
    uint32_t maximum_hardware_revision;
    uint8_t schema_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t product_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t architecture_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t platform_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t environment_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t protocol_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t rollback_domain_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    const struct RibonUpdateComponent *components;
    uint32_t component_count;
    uint32_t reserved0;
    uint64_t reserved[4];
};

/** @brief Independent reader가 검증한 immutable manifest byte view다. */
struct RibonUpdateManifestView {
    const uint8_t *bytes;
    size_t byte_size;
    uint64_t bundle_generation;
    uint64_t predecessor_generation;
    uint64_t rollback_sequence;
    uint64_t creation_policy_version;
    enum RibonKeyPolicyMode mode;
    uint16_t protocol_major;
    uint16_t protocol_minor;
    uint32_t minimum_hardware_revision;
    uint32_t maximum_hardware_revision;
    uint32_t component_count;
    uint32_t reserved0;
    size_t components_offset;
    uint8_t schema_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t product_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t architecture_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t platform_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t environment_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t protocol_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t rollback_domain_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
};

/** @brief 검증된 component table row를 native scalar와 digest로 복사한 view다. */
struct RibonUpdateComponentView {
    uint32_t install_order;
    enum RibonUpdateComponentRole role;
    enum RibonUpdateDestinationClass destination_class;
    enum RibonUpdateImageFormat image_format;
    uint32_t flags;
    uint64_t bundle_offset;
    uint64_t exact_size;
    uint64_t maximum_size;
    uint8_t logical_id_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t content_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t destination_id_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t entry_contract_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
};

/** @brief Detached Ed25519 signature envelope 생성 입력이다. */
struct RibonUpdateSignatureEnvelopeInput {
    uint32_t size;
    uint32_t abi_version;
    uint32_t flags;
    uint32_t reserved0;
    const uint8_t *manifest;
    size_t manifest_size;
    const uint8_t *key_id;
    size_t key_id_size;
    const uint8_t *signature;
    size_t signature_size;
    uint64_t reserved[4];
};

/** @brief Independent reader가 검증한 detached signature와 key-ID view다. */
struct RibonUpdateSignatureEnvelopeView {
    const uint8_t *bytes;
    size_t byte_size;
    const uint8_t *key_id;
    size_t key_id_size;
    const uint8_t *signature;
    size_t signature_size;
    size_t manifest_size;
    enum RibonKeyPolicyMode mode;
    uint8_t manifest_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t signed_message_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
};

/** @brief Selected product graph가 요구하는 exact update binding이다. */
struct RibonUpdateManifestExpectation {
    uint32_t size;
    uint32_t abi_version;
    uint32_t flags;
    enum RibonKeyPolicyMode mode;
    uint16_t protocol_major;
    uint16_t protocol_minor;
    uint32_t hardware_revision;
    uint8_t schema_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t product_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t architecture_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t platform_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t environment_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t protocol_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t rollback_domain_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint64_t reserved[4];
};

/** @brief Manifest, detached envelope와 selected security authority의 승인 요청이다. */
struct RibonUpdateManifestAuthorization {
    uint32_t size;
    uint32_t abi_version;
    uint32_t flags;
    uint32_t reserved0;
    const uint8_t *manifest;
    size_t manifest_size;
    const uint8_t *signature_envelope;
    size_t signature_envelope_size;
    const struct RibonUpdateManifestExpectation *expectation;
    const struct RibonKeyPolicyStore *key_policy;
    const struct RibonSignatureProvider *signature_provider;
    void *workspace;
    size_t workspace_size;
    uint64_t reserved[4];
};

/**
 * @brief Caller-owned buffer에 canonical manifest v1을 직렬화한다.
 *
 * Heap과 storage I/O를 사용하지 않는다. 실패 시 `written`은 0이고 output은
 * authorization input으로 사용할 수 없다.
 */
int ribon_update_manifest_encode(
    const struct RibonUpdateManifestInput *input,
    uint8_t *output,
    size_t capacity,
    size_t *written);

/**
 * @brief Untrusted manifest bytes를 independent하게 열어 모든 section bound를 검증한다.
 *
 * 성공한 view는 입력 byte lifetime 동안만 유효하며 reader는 compiler metadata를
 * 신뢰하지 않는다.
 */
int ribon_update_manifest_open(
    const uint8_t *bytes,
    size_t size,
    struct RibonUpdateManifestView *view);

/** @brief 검증된 manifest의 한 component row를 caller-owned view로 복사한다. */
int ribon_update_manifest_component_at(
    const struct RibonUpdateManifestView *view,
    uint32_t index,
    struct RibonUpdateComponentView *component);

/**
 * @brief Update-manifest usage 전용 canonical 256-byte signed message를 만든다.
 *
 * Key ID는 1..64 non-NUL opaque byte다. Signature와 key lookup은 수행하지 않는다.
 */
int ribon_update_manifest_signed_message_v1(
    const struct RibonUpdateManifestView *view,
    const uint8_t *key_id,
    size_t key_id_size,
    uint8_t message[RIBON_UPDATE_SIGNED_MESSAGE_V1_BYTES]);

/** @brief Detached signature envelope를 explicit little-endian bytes로 직렬화한다. */
int ribon_update_signature_envelope_encode(
    const struct RibonUpdateSignatureEnvelopeInput *input,
    uint8_t *output,
    size_t capacity,
    size_t *written);

/** @brief Untrusted detached signature envelope의 shape와 digest binding을 검증한다. */
int ribon_update_signature_envelope_open(
    const uint8_t *bytes,
    size_t size,
    struct RibonUpdateSignatureEnvelopeView *view);

/**
 * @brief Product binding, update key usage와 Ed25519 signature를 순서대로 승인한다.
 *
 * 이 함수는 protected rollback floor를 읽거나 쓰지 않는다. 성공한 manifest view와
 * pointer-free key decision만 반환하며 storage와 network authority를 갖지 않는다.
 */
int ribon_update_manifest_authorize(
    const struct RibonUpdateManifestAuthorization *authorization,
    struct RibonUpdateManifestView *view,
    struct RibonKeyPolicyDecision *decision);

#endif
