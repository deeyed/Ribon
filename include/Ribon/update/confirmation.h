#ifndef RIBON_UPDATE_CONFIRMATION_H
#define RIBON_UPDATE_CONFIRMATION_H

#include <stddef.h>
#include <stdint.h>

#include <Ribon/protocol/protocol.h>
#include <Ribon/security/key_policy.h>
#include <Ribon/security/protected_state.h>
#include <Ribon/update/transaction.h>

/** @brief OS-neutral boot confirmation envelope와 engine ABI다. */
#define RIBON_BOOT_CONFIRMATION_ABI_VERSION 1u

/** @brief Canonical envelope의 fixed little-endian header byte 수다. */
#define RIBON_BOOT_CONFIRMATION_HEADER_BYTES 256u

/** @brief 한 confirmation envelope의 hard input 상한이다. */
#define RIBON_BOOT_CONFIRMATION_MAX_WIRE_BYTES 2048u

/** @brief Product stable ID의 canonical byte 상한이다. */
#define RIBON_BOOT_CONFIRMATION_MAX_PRODUCT_ID_BYTES 128u

/** @brief Protocol stable ID의 canonical byte 상한이다. */
#define RIBON_BOOT_CONFIRMATION_MAX_PROTOCOL_ID_BYTES 64u

/** @brief Key ID의 canonical byte 상한이다. */
#define RIBON_BOOT_CONFIRMATION_MAX_KEY_ID_BYTES 64u

/** @brief Protocol-owned health payload의 canonical byte 상한이다. */
#define RIBON_BOOT_CONFIRMATION_MAX_HEALTH_BYTES 1024u

/** @brief Boot-attempt nonce의 fixed byte 수다. */
#define RIBON_BOOT_CONFIRMATION_NONCE_BYTES 32u

/** @brief Confirmation engine의 stable fail-closed 결과다. */
enum RibonBootConfirmationStatus {
    RIBON_BOOT_CONFIRMATION_STATUS_OK = 0,
    RIBON_BOOT_CONFIRMATION_STATUS_DUPLICATE = 1,
    RIBON_BOOT_CONFIRMATION_STATUS_INVALID_ARGUMENT = -1,
    RIBON_BOOT_CONFIRMATION_STATUS_MALFORMED = -2,
    RIBON_BOOT_CONFIRMATION_STATUS_IDENTITY = -3,
    RIBON_BOOT_CONFIRMATION_STATUS_STALE = -4,
    RIBON_BOOT_CONFIRMATION_STATUS_AUTHENTICATOR = -5,
    RIBON_BOOT_CONFIRMATION_STATUS_HEALTH_REJECTED = -6,
    RIBON_BOOT_CONFIRMATION_STATUS_PROTECTED_STATE = -7,
    RIBON_BOOT_CONFIRMATION_STATUS_UPDATE_STATE = -8,
    RIBON_BOOT_CONFIRMATION_STATUS_ATTEMPTS_EXHAUSTED = -9,
    RIBON_BOOT_CONFIRMATION_STATUS_OVERFLOW = -10,
};

/** @brief Encoder가 소비하는 complete signed envelope source다. */
struct RibonBootConfirmationEnvelopeSource {
    uint32_t size;
    uint32_t abi_version;
    uint32_t flags;
    uint32_t slot_id;
    uint32_t protocol_major;
    uint32_t protocol_minor;
    uint32_t policy_version;
    uint32_t reserved0;
    uint64_t image_generation;
    uint64_t manifest_sequence;
    uint64_t attempt_sequence;
    const uint8_t *product_id;
    size_t product_id_size;
    const uint8_t *protocol_id;
    size_t protocol_id_size;
    const uint8_t *key_id;
    size_t key_id_size;
    const uint8_t *health_payload;
    size_t health_payload_size;
    uint8_t manifest_digest[32];
    uint8_t nonce[RIBON_BOOT_CONFIRMATION_NONCE_BYTES];
    uint8_t signature[RIBON_ED25519_SIGNATURE_BYTES];
    uint64_t reserved[4];
};

/** @brief Independent reader가 검증한 borrowed envelope view다. */
struct RibonBootConfirmationEnvelopeView {
    uint32_t size;
    uint32_t abi_version;
    uint32_t slot_id;
    uint32_t protocol_major;
    uint32_t protocol_minor;
    uint32_t policy_version;
    uint64_t image_generation;
    uint64_t manifest_sequence;
    uint64_t attempt_sequence;
    const uint8_t *product_id;
    size_t product_id_size;
    const uint8_t *protocol_id;
    size_t protocol_id_size;
    const uint8_t *key_id;
    size_t key_id_size;
    const uint8_t *health_payload;
    size_t health_payload_size;
    const uint8_t *signature;
    size_t signature_size;
    const uint8_t *authenticated_message;
    size_t authenticated_message_size;
    uint8_t manifest_digest[32];
    uint8_t nonce[RIBON_BOOT_CONFIRMATION_NONCE_BYTES];
    uint8_t health_payload_digest[32];
};

/** @brief Product graph와 pending metadata가 정하는 exact attempt identity다. */
struct RibonBootAttemptIdentity {
    uint32_t size;
    uint32_t abi_version;
    enum RibonKeyPolicyMode mode;
    uint32_t slot_id;
    uint32_t protocol_major;
    uint32_t protocol_minor;
    uint32_t policy_version;
    uint32_t flags;
    uint64_t image_generation;
    uint64_t manifest_sequence;
    const uint8_t *product_id;
    size_t product_id_size;
    const uint8_t *protocol_id;
    size_t protocol_id_size;
    uint8_t manifest_digest[32];
    uint8_t product_digest[32];
    uint8_t rollback_domain_digest[32];
    uint64_t reserved[4];
};

/** @brief Product-selected nonce source가 caller-owned bytes를 채우는 callback이다. */
typedef int (*RibonBootAttemptNonceFillFn)(
    void *context,
    uint8_t nonce[RIBON_BOOT_CONFIRMATION_NONCE_BYTES]);

/** @brief Entropy 품질 claim을 environment/product에 남기는 nonce provider다. */
struct RibonBootAttemptNonceSource {
    uint32_t size;
    uint32_t abi_version;
    uint32_t flags;
    uint32_t reserved0;
    void *context;
    RibonBootAttemptNonceFillFn fill;
    uint64_t reserved[4];
};

/** @brief Pending transfer 전에 durable하게 열린 exact boot attempt다. */
struct RibonBootAttempt {
    uint32_t size;
    uint32_t abi_version;
    uint32_t slot_id;
    uint32_t attempts_remaining;
    uint64_t image_generation;
    uint64_t manifest_sequence;
    uint64_t attempt_sequence;
    uint64_t protected_generation;
    uint8_t manifest_digest[32];
    uint8_t nonce[RIBON_BOOT_CONFIRMATION_NONCE_BYTES];
    uint8_t binding_digest[32];
    uint64_t reserved[4];
};

/** @brief Pending metadata, protected state와 nonce authority를 묶는 request다. */
struct RibonBootAttemptBeginRequest {
    uint32_t size;
    uint32_t abi_version;
    uint32_t flags;
    uint32_t maximum_attempts;
    const struct RibonBootAttemptIdentity *identity;
    const struct RibonUpdateTransactionJournal *update_journal;
    const struct RibonProtectedStateJournal *protected_journal;
    const struct RibonBootAttemptNonceSource *nonce_source;
    uint64_t reserved[4];
};

/** @brief Authenticator, protocol와 두 durable journal을 묶는 accept request다. */
struct RibonBootConfirmationAcceptRequest {
    uint32_t size;
    uint32_t abi_version;
    uint32_t flags;
    uint32_t reserved0;
    const uint8_t *envelope;
    size_t envelope_size;
    const struct RibonBootAttemptIdentity *identity;
    const struct RibonBootProtocol *protocol;
    const struct RibonKeyPolicyStore *key_policy;
    const struct RibonSignatureProvider *signature_provider;
    void *signature_workspace;
    size_t signature_workspace_size;
    const struct RibonUpdateTransactionJournal *update_journal;
    const struct RibonProtectedStateJournal *protected_journal;
    struct RibonUpdateTransactionObserver *observer;
    uint64_t reserved[4];
};

/** @brief Protected confirmation과 update commit을 함께 보고하는 receipt다. */
struct RibonBootConfirmationReceipt {
    uint32_t size;
    uint32_t abi_version;
    enum RibonBootConfirmationStatus status;
    uint32_t duplicate;
    uint64_t attempt_sequence;
    uint64_t protected_generation;
    uint64_t update_generation;
    uint32_t active_slot;
    uint32_t reserved0;
    uint64_t reserved[4];
};

/** @brief Source를 canonical little-endian signed envelope로 직렬화한다. */
int ribon_boot_confirmation_envelope_encode(
    const struct RibonBootConfirmationEnvelopeSource *source,
    uint8_t *output,
    size_t capacity,
    size_t *written);

/** @brief Untrusted canonical envelope를 bounds와 digest까지 독립 검증한다. */
int ribon_boot_confirmation_envelope_open(
    const uint8_t *bytes,
    size_t size,
    struct RibonBootConfirmationEnvelopeView *view);

/** @brief Exact attempt identity와 nonce의 canonical protected binding을 계산한다. */
int ribon_boot_confirmation_binding_digest(
    const struct RibonBootAttemptIdentity *identity,
    const uint8_t nonce[RIBON_BOOT_CONFIRMATION_NONCE_BYTES],
    uint64_t attempt_sequence,
    uint8_t digest[32]);

/** @brief 새 nonce를 만들고 protected attempt를 연 뒤 transfer attempt를 선차감한다. */
int ribon_boot_confirmation_begin_attempt(
    const struct RibonBootAttemptBeginRequest *request,
    struct RibonBootAttempt *attempt);

/** @brief Authenticated health payload를 검증하고 exact pending을 confirmed로 승격한다. */
int ribon_boot_confirmation_accept(
    const struct RibonBootConfirmationAcceptRequest *request,
    struct RibonBootConfirmationReceipt *receipt);

/** @brief Stable confirmation status 이름을 반환한다. */
const char *ribon_boot_confirmation_status_name(
    enum RibonBootConfirmationStatus status);

#endif
