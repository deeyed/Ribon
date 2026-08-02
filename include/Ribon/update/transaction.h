#ifndef RIBON_UPDATE_TRANSACTION_H
#define RIBON_UPDATE_TRANSACTION_H

#include <stddef.h>
#include <stdint.h>

#include <Ribon/update/installer.h>

/** @brief Crash-consistent update transaction journal ABI다. */
#define RIBON_UPDATE_TRANSACTION_ABI_VERSION 1u

/** @brief Transaction journal의 redundant record slot 수다. */
#define RIBON_UPDATE_TRANSACTION_RECORD_SLOTS 2u

/** @brief Transaction journal의 redundant selector slot 수다. */
#define RIBON_UPDATE_TRANSACTION_SELECTOR_SLOTS 2u

/** @brief 한 complete transaction record의 canonical LE byte 수다. */
#define RIBON_UPDATE_TRANSACTION_RECORD_BYTES 1024u

/** @brief 한 commit selector의 canonical LE byte 수다. */
#define RIBON_UPDATE_TRANSACTION_SELECTOR_BYTES 512u

/** @brief Journal record와 selector가 요구하는 전체 최소 byte 수다. */
#define RIBON_UPDATE_TRANSACTION_MINIMUM_JOURNAL_BYTES \
    (RIBON_UPDATE_TRANSACTION_RECORD_SLOTS * \
         RIBON_UPDATE_TRANSACTION_RECORD_BYTES + \
     RIBON_UPDATE_TRANSACTION_SELECTOR_SLOTS * \
         RIBON_UPDATE_TRANSACTION_SELECTOR_BYTES)

/** @brief Stable operation boundary의 before/after 구분이다. */
enum RibonUpdateTransactionBoundary {
    RIBON_UPDATE_TRANSACTION_BOUNDARY_INVALID = 0,
    RIBON_UPDATE_TRANSACTION_BOUNDARY_BEFORE = 1,
    RIBON_UPDATE_TRANSACTION_BOUNDARY_AFTER = 2,
};

/** @brief Fault runner와 evidence가 공유하는 semantic storage operation ID다. */
enum RibonUpdateTransactionOperation {
    RIBON_UPDATE_TRANSACTION_OPERATION_INVALID = 0,
    RIBON_UPDATE_TRANSACTION_OPERATION_JOURNAL_RECORD_WRITE = 1,
    RIBON_UPDATE_TRANSACTION_OPERATION_JOURNAL_RECORD_FLUSH = 2,
    RIBON_UPDATE_TRANSACTION_OPERATION_JOURNAL_RECORD_READBACK = 3,
    RIBON_UPDATE_TRANSACTION_OPERATION_JOURNAL_SELECTOR_WRITE = 4,
    RIBON_UPDATE_TRANSACTION_OPERATION_JOURNAL_SELECTOR_FLUSH = 5,
    RIBON_UPDATE_TRANSACTION_OPERATION_JOURNAL_REOPEN = 6,
    RIBON_UPDATE_TRANSACTION_OPERATION_BUNDLE_READ = 7,
    RIBON_UPDATE_TRANSACTION_OPERATION_SLOT_ERASE = 8,
    RIBON_UPDATE_TRANSACTION_OPERATION_SLOT_WRITE = 9,
    RIBON_UPDATE_TRANSACTION_OPERATION_SLOT_READBACK = 10,
    RIBON_UPDATE_TRANSACTION_OPERATION_PAYLOAD_FLUSH = 11,
};

/** @brief 한 operation boundary의 pointer-free stable event다. */
struct RibonUpdateTransactionEvent {
    uint32_t size;
    uint32_t abi_version;
    enum RibonUpdateTransactionOperation operation;
    enum RibonUpdateTransactionBoundary boundary;
    enum RibonUpdateSlotState durable_state;
    uint32_t component_index;
    uint32_t sequence;
    uint32_t flags;
    uint64_t journal_generation;
    uint64_t reserved[4];
};

/** @brief Event를 기록하거나 deterministic fail-stop을 요청하는 callback이다. */
typedef int (*RibonUpdateTransactionObserveFn)(
    void *context,
    const struct RibonUpdateTransactionEvent *event);

/**
 * @brief Product 또는 fixture가 제공하는 bounded transaction observer다.
 *
 * Callback failure는 operation 전에는 해당 operation을 막고, operation 후에는 결과를
 * ambiguous failure로 닫는다. Observer는 storage authority를 얻지 않는다.
 */
struct RibonUpdateTransactionObserver {
    uint32_t size;
    uint32_t abi_version;
    uint32_t flags;
    uint32_t next_sequence;
    void *context;
    RibonUpdateTransactionObserveFn observe;
    uint64_t reserved[4];
};

/** @brief Journal codec, ordering과 replay scanner의 stable 결과다. */
enum RibonUpdateTransactionStatus {
    RIBON_UPDATE_TRANSACTION_STATUS_OK = 0,
    RIBON_UPDATE_TRANSACTION_STATUS_INVALID_ARGUMENT = -1,
    RIBON_UPDATE_TRANSACTION_STATUS_UNINITIALIZED = -2,
    RIBON_UPDATE_TRANSACTION_STATUS_IO = -3,
    RIBON_UPDATE_TRANSACTION_STATUS_SHORT_IO = -4,
    RIBON_UPDATE_TRANSACTION_STATUS_CORRUPT = -5,
    RIBON_UPDATE_TRANSACTION_STATUS_CONFLICT = -6,
    RIBON_UPDATE_TRANSACTION_STATUS_REPLAY = -7,
    RIBON_UPDATE_TRANSACTION_STATUS_OVERFLOW = -8,
    RIBON_UPDATE_TRANSACTION_STATUS_STATE = -9,
    RIBON_UPDATE_TRANSACTION_STATUS_IDENTITY = -10,
    RIBON_UPDATE_TRANSACTION_STATUS_AUTHORIZATION = -11,
    RIBON_UPDATE_TRANSACTION_STATUS_INSTALL = -12,
    RIBON_UPDATE_TRANSACTION_STATUS_INTERRUPTED = -13,
};

/** @brief Product-selected provider와 canonical update-journal range를 묶는다. */
struct RibonUpdateTransactionJournal {
    uint32_t size;
    uint32_t abi_version;
    uint32_t flags;
    uint32_t reserved0;
    const struct RibonUpdateStorageProvider *provider;
    const struct RibonUpdateLayout *layout;
    uint64_t minimum_generation;
    uint64_t deadline_ticks;
    uint64_t reserved[4];
};

/** @brief Newest complete selector가 승인한 durable update state다. */
struct RibonUpdateTransactionSnapshot {
    uint32_t size;
    uint32_t abi_version;
    uint32_t selected_record_slot;
    uint32_t selected_selector_slot;
    uint32_t target_slot;
    enum RibonUpdateSlotState target_state;
    uint64_t journal_generation;
    uint64_t predecessor_generation;
    uint8_t record_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t predecessor_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    struct RibonUpdateSlotMetadata metadata;
    uint64_t reserved[4];
};

/** @brief Signed install과 journal, pending attempt policy를 결속하는 request다. */
struct RibonUpdateTransactionalInstallRequest {
    uint32_t size;
    uint32_t abi_version;
    uint32_t flags;
    uint32_t pending_attempts;
    const struct RibonUpdateInstallRequest *install;
    const struct RibonUpdateTransactionJournal *journal;
    struct RibonUpdateTransactionObserver *observer;
    uint64_t reserved[4];
};

/** @brief Idempotent transactional install이 반환하는 durable receipt다. */
struct RibonUpdateTransactionalInstallResult {
    uint32_t size;
    uint32_t abi_version;
    enum RibonUpdateSlotState resumed_from;
    uint32_t target_slot;
    uint32_t component_count;
    uint32_t flags;
    uint64_t installed_exact_bytes;
    uint64_t installed_backing_bytes;
    struct RibonUpdateTransactionSnapshot snapshot;
    uint64_t reserved[4];
};

/** @brief Exact pending identity를 confirmed로 올리는 request다. */
struct RibonUpdateConfirmPendingRequest {
    uint32_t size;
    uint32_t abi_version;
    uint32_t flags;
    uint32_t target_slot;
    uint64_t image_generation;
    uint8_t manifest_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    const struct RibonUpdateTransactionJournal *journal;
    struct RibonUpdateTransactionObserver *observer;
    uint64_t reserved[4];
};

/** @brief Confirmation commit 또는 idempotent reopen 결과다. */
struct RibonUpdateConfirmPendingResult {
    uint32_t size;
    uint32_t abi_version;
    uint32_t duplicate;
    uint32_t reserved0;
    struct RibonUpdateTransactionSnapshot snapshot;
    uint64_t reserved[4];
};

/** @brief Observer descriptor의 shape와 reserved bytes를 검사한다. */
int ribon_update_transaction_observer_is_valid(
    const struct RibonUpdateTransactionObserver *observer);

/** @brief 한 stable operation boundary를 observer에 전달한다. */
int ribon_update_transaction_observe(
    struct RibonUpdateTransactionObserver *observer,
    enum RibonUpdateTransactionOperation operation,
    enum RibonUpdateTransactionBoundary boundary,
    enum RibonUpdateSlotState durable_state,
    uint32_t component_index,
    uint64_t journal_generation);

/** @brief Newest complete record를 독립 검증하고 replay floor를 적용한다. */
int ribon_update_transaction_open(
    const struct RibonUpdateTransactionJournal *journal,
    struct RibonUpdateTransactionSnapshot *snapshot);

/** @brief 빈 journal을 generation 1의 confirmed metadata로 provision한다. */
int ribon_update_transaction_initialize(
    const struct RibonUpdateTransactionJournal *journal,
    const struct RibonUpdateSlotMetadata *initial_metadata,
    struct RibonUpdateTransactionObserver *observer,
    struct RibonUpdateTransactionSnapshot *snapshot);

/**
 * @brief Signed bundle을 STAGING, VERIFIED, PENDING journal commit으로 설치한다.
 *
 * `install->current_metadata`는 NULL이어야 한다. Durable current metadata는 journal에서만
 * 가져오며 retry는 동일 identity의 STAGING, VERIFIED 또는 PENDING에서 idempotent하게
 * 재개한다.
 */
int ribon_update_install_transactionally(
    const struct RibonUpdateTransactionalInstallRequest *request,
    struct RibonUpdateTransactionalInstallResult *result);

/**
 * @brief Exact current pending slot을 selector-committed CONFIRMED로 승격한다.
 *
 * 이미 같은 slot, image generation과 manifest digest가 active confirmed이면
 * `duplicate=1`로 성공한다. 다른 pending 또는 confirmed identity는 바꾸지 않는다.
 */
int ribon_update_transaction_confirm_pending(
    const struct RibonUpdateConfirmPendingRequest *request,
    struct RibonUpdateConfirmPendingResult *result);

/** @brief Transaction status의 stable diagnostic name을 반환한다. */
const char *ribon_update_transaction_status_name(
    enum RibonUpdateTransactionStatus status);

#endif
