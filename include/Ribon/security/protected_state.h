#ifndef RIBON_SECURITY_PROTECTED_STATE_H
#define RIBON_SECURITY_PROTECTED_STATE_H

#include <stddef.h>
#include <stdint.h>

/** @brief Protected rollback-state provider descriptor magic이다. */
#define RIBON_PROTECTED_STATE_PROVIDER_MAGIC UINT32_C(0x52425053)

/** @brief Protected rollback-state와 journal codec ABI v1이다. */
#define RIBON_PROTECTED_STATE_ABI_VERSION 1u

/** @brief Rollback domain과 record identity에 사용하는 digest 길이다. */
#define RIBON_PROTECTED_STATE_DIGEST_BYTES 32u

/** @brief Redundant journal이 소유하는 record slot 수다. */
#define RIBON_PROTECTED_STATE_RECORD_SLOTS 2u

/** @brief Journal record의 canonical little-endian byte 길이다. */
#define RIBON_PROTECTED_STATE_RECORD_BYTES 128u

/** @brief Commit selector의 canonical little-endian byte 길이다. */
#define RIBON_PROTECTED_STATE_SELECTOR_BYTES 128u

/** @brief 한 product가 승인할 수 있는 rollback domain 최대 수다. */
#define RIBON_PROTECTED_STATE_MAX_DOMAINS 8u

/** @brief 한 trial generation에 허용하는 최대 boot attempt 수다. */
#define RIBON_PROTECTED_STATE_MAX_TRIAL_ATTEMPTS 32u

/** @brief Provider의 실제 anti-replay 성질을 분리하는 class다. */
enum RibonProtectedStateProviderClass {
    RIBON_PROTECTED_STATE_PROVIDER_CLASS_INVALID = 0,
    RIBON_PROTECTED_STATE_PROVIDER_CLASS_HARDWARE = 1,
    RIBON_PROTECTED_STATE_PROVIDER_CLASS_REFERENCE = 2,
    RIBON_PROTECTED_STATE_PROVIDER_CLASS_FIXTURE = 3,
};

/** @brief Provider가 접근할 수 있는 고정 크기 논리 객체다. */
enum RibonProtectedStateObject {
    RIBON_PROTECTED_STATE_OBJECT_INVALID = 0,
    RIBON_PROTECTED_STATE_OBJECT_RECORD = 1,
    RIBON_PROTECTED_STATE_OBJECT_SELECTOR = 2,
};

/** @brief Provider callback의 fail-closed storage 결과다. */
enum RibonProtectedStateProviderStatus {
    RIBON_PROTECTED_STATE_PROVIDER_OK = 0,
    RIBON_PROTECTED_STATE_PROVIDER_UNAVAILABLE = -1,
    RIBON_PROTECTED_STATE_PROVIDER_IO_ERROR = -2,
};

/** @brief Domain별 rollback authority의 논리 상태다. */
enum RibonProtectedStateKind {
    RIBON_PROTECTED_STATE_KIND_INVALID = 0,
    RIBON_PROTECTED_STATE_KIND_CONFIRMED = 1,
    RIBON_PROTECTED_STATE_KIND_TRIAL = 2,
};

/** @brief 한 sequence를 실행할 수 있는 authority 종류다. */
enum RibonProtectedStateAuthority {
    RIBON_PROTECTED_STATE_AUTHORITY_INVALID = 0,
    RIBON_PROTECTED_STATE_AUTHORITY_CONFIRMED = 1,
    RIBON_PROTECTED_STATE_AUTHORITY_TRIAL = 2,
};

/** @brief Protected-state engine의 stable fail-closed 결과다. */
enum RibonProtectedStateStatus {
    RIBON_PROTECTED_STATE_STATUS_OK = 0,
    RIBON_PROTECTED_STATE_STATUS_INVALID_ARGUMENT = -1,
    RIBON_PROTECTED_STATE_STATUS_INVALID_PROVIDER = -2,
    RIBON_PROTECTED_STATE_STATUS_UNAVAILABLE = -3,
    RIBON_PROTECTED_STATE_STATUS_IO_ERROR = -4,
    RIBON_PROTECTED_STATE_STATUS_UNINITIALIZED = -5,
    RIBON_PROTECTED_STATE_STATUS_CORRUPT = -6,
    RIBON_PROTECTED_STATE_STATUS_CONFLICT = -7,
    RIBON_PROTECTED_STATE_STATUS_DOMAIN_MISMATCH = -8,
    RIBON_PROTECTED_STATE_STATUS_ROLLBACK = -9,
    RIBON_PROTECTED_STATE_STATUS_SEQUENCE_GAP = -10,
    RIBON_PROTECTED_STATE_STATUS_ATTEMPTS_EXHAUSTED = -11,
    RIBON_PROTECTED_STATE_STATUS_OVERFLOW = -12,
    RIBON_PROTECTED_STATE_STATUS_READBACK_MISMATCH = -13,
    RIBON_PROTECTED_STATE_STATUS_ALREADY_INITIALIZED = -14,
};

struct RibonProtectedStateProvider;

/** @brief Caller-owned buffer로 exact logical object를 읽는 callback이다. */
typedef int (*RibonProtectedStateReadFn)(
    const struct RibonProtectedStateProvider *provider,
    const uint8_t domain_digest[RIBON_PROTECTED_STATE_DIGEST_BYTES],
    enum RibonProtectedStateObject object,
    uint32_t slot,
    uint8_t *bytes,
    size_t size);

/** @brief Exact logical object 전체를 provider의 pending storage에 쓰는 callback이다. */
typedef int (*RibonProtectedStateWriteFn)(
    const struct RibonProtectedStateProvider *provider,
    const uint8_t domain_digest[RIBON_PROTECTED_STATE_DIGEST_BYTES],
    enum RibonProtectedStateObject object,
    uint32_t slot,
    const uint8_t *bytes,
    size_t size);

/** @brief 선행 write를 durable media에 반영하는 caller-controlled callback이다. */
typedef int (*RibonProtectedStateFlushFn)(
    const struct RibonProtectedStateProvider *provider,
    const uint8_t domain_digest[RIBON_PROTECTED_STATE_DIGEST_BYTES]);

/**
 * @brief Product graph가 선택하는 protected-state storage provider다.
 *
 * Provider는 raw address를 노출하지 않는다. `REFERENCE`와 `FIXTURE` class는 hostile
 * replay 저항성을 주장할 수 없으며 `HARDWARE`의 실제 보장은 product 문서가 소유한다.
 */
struct RibonProtectedStateProvider {
    uint32_t magic;
    uint32_t size;
    uint32_t abi_version;
    enum RibonProtectedStateProviderClass provider_class;
    uint32_t flags;
    uint32_t record_slots;
    uint32_t selector_slots;
    uint32_t reserved0;
    size_t record_bytes;
    size_t selector_bytes;
    const char *id;
    void *context;
    RibonProtectedStateReadFn read;
    RibonProtectedStateWriteFn write;
    RibonProtectedStateFlushFn flush;
    uint64_t reserved[4];
};

/** @brief Product manifest가 provider와 허용 domain을 묶는 immutable binding이다. */
struct RibonProtectedStateProductBinding {
    uint32_t size;
    uint32_t abi_version;
    enum RibonProtectedStateProviderClass provider_class;
    uint32_t flags;
    const struct RibonProtectedStateProvider *provider;
    const uint8_t (*domain_digests)[RIBON_PROTECTED_STATE_DIGEST_BYTES];
    uint32_t domain_count;
    uint32_t reserved0;
    uint64_t reserved[4];
};

/** @brief 한 product binding에서 exact rollback domain을 선택한 journal view다. */
struct RibonProtectedStateJournal {
    uint32_t size;
    uint32_t abi_version;
    const struct RibonProtectedStateProvider *provider;
    uint8_t domain_digest[RIBON_PROTECTED_STATE_DIGEST_BYTES];
    uint64_t reserved[4];
};

/** @brief Selector로 commit된 pointer-free logical rollback state다. */
struct RibonProtectedStateSnapshot {
    uint32_t size;
    uint32_t abi_version;
    enum RibonProtectedStateKind kind;
    uint32_t selected_slot;
    uint64_t confirmed_floor;
    uint64_t pending_sequence;
    uint32_t trial_attempts_remaining;
    uint32_t flags;
    uint64_t generation;
    uint8_t domain_digest[RIBON_PROTECTED_STATE_DIGEST_BYTES];
    uint64_t reserved[4];
};

/** @brief Sequence authorization 결과와 그때 관찰한 generation을 담는다. */
struct RibonProtectedStateDecision {
    uint32_t size;
    uint32_t abi_version;
    enum RibonProtectedStateAuthority authority;
    uint32_t trial_attempts_remaining;
    uint64_t sequence;
    uint64_t generation;
    uint8_t domain_digest[RIBON_PROTECTED_STATE_DIGEST_BYTES];
    uint64_t reserved[4];
};

/** @brief Provider descriptor의 ABI, class, 크기와 callback을 검사한다. */
int ribon_protected_state_provider_validate(
    const struct RibonProtectedStateProvider *provider);

/** @brief Product binding의 provider class와 sorted domain table을 검사한다. */
int ribon_protected_state_binding_validate(
    const struct RibonProtectedStateProductBinding *binding);

/** @brief Product binding에서 exact domain digest의 journal을 만든다. */
int ribon_protected_state_journal_bind(
    const struct RibonProtectedStateProductBinding *binding,
    const uint8_t domain_digest[RIBON_PROTECTED_STATE_DIGEST_BYTES],
    struct RibonProtectedStateJournal *journal);

/** @brief Selector와 두 record를 재검산해 현재 commit state를 연다. */
int ribon_protected_state_open(
    const struct RibonProtectedStateJournal *journal,
    struct RibonProtectedStateSnapshot *snapshot);

/** @brief 비어 있는 domain journal을 generation 1의 confirmed floor로 provision한다. */
int ribon_protected_state_initialize(
    const struct RibonProtectedStateJournal *journal,
    uint64_t confirmed_floor,
    struct RibonProtectedStateSnapshot *snapshot);

/** @brief Confirmed `N`에서 exact `N+1` bounded trial을 durable하게 시작한다. */
int ribon_protected_state_begin_trial(
    const struct RibonProtectedStateJournal *journal,
    uint64_t candidate_sequence,
    uint32_t attempts,
    struct RibonProtectedStateSnapshot *snapshot);

/** @brief 현재 state에서 sequence의 confirmed 또는 pending authority를 판정한다. */
int ribon_protected_state_authorize(
    const struct RibonProtectedStateJournal *journal,
    uint64_t sequence,
    struct RibonProtectedStateDecision *decision);

/** @brief Pending transfer 전에 attempt를 먼저 durable하게 감소시킨다. */
int ribon_protected_state_consume_trial_attempt(
    const struct RibonProtectedStateJournal *journal,
    uint64_t pending_sequence,
    struct RibonProtectedStateSnapshot *snapshot);

/** @brief Exact pending sequence를 새 confirmed floor로 durable하게 승격한다. */
int ribon_protected_state_confirm(
    const struct RibonProtectedStateJournal *journal,
    uint64_t pending_sequence,
    struct RibonProtectedStateSnapshot *snapshot);

/** @brief Pending state를 폐기하고 기존 confirmed floor를 유지한다. */
int ribon_protected_state_fail_trial(
    const struct RibonProtectedStateJournal *journal,
    struct RibonProtectedStateSnapshot *snapshot);

#endif
