#ifndef RIBON_SERVICE_DIRECTORY_H
#define RIBON_SERVICE_DIRECTORY_H

#include <stdint.h>

#include <Ribon/boot/source.h>
#include <Ribon/core/capability.h>
#include <Ribon/plugin/descriptor.h>

struct RibonProductDescriptor;

/** @brief Typed service descriptor를 식별하는 magic이다. */
#define RIBON_SERVICE_DESCRIPTOR_MAGIC 0x52425356u

/** @brief Service descriptor와 operation table ABI다. */
#define RIBON_SERVICE_ABI_VERSION 2u

/** @brief Caller-owned immutable service directory ABI다. */
#define RIBON_SERVICE_DIRECTORY_ABI_VERSION 1u

/** @brief 한 immutable service directory의 최대 descriptor 수다. */
#define RIBON_SERVICE_DIRECTORY_LIMIT 64u

/** @brief Pending slot이 없음을 나타내는 slot index다. */
#define RIBON_SLOT_NONE UINT32_MAX

/** @brief Typed service operation의 공통 결과다. */
enum RibonServiceStatus {
    RIBON_SERVICE_STATUS_OK = 0,
    RIBON_SERVICE_STATUS_BAD_ARGUMENT = -1,
    RIBON_SERVICE_STATUS_UNSUPPORTED = -2,
    RIBON_SERVICE_STATUS_OUT_OF_RANGE = -3,
    RIBON_SERVICE_STATUS_TIMEOUT = -4,
    RIBON_SERVICE_STATUS_IO = -5,
};

/** @brief Service ABI가 구분하는 stable service role이다. */
enum RibonServiceKind {
    RIBON_SERVICE_KIND_BOOT_SOURCE = 0,
    RIBON_SERVICE_KIND_INACTIVE_SLOT_STORAGE = 1,
    RIBON_SERVICE_KIND_STORAGE_FLUSH = 2,
    RIBON_SERVICE_KIND_MONOTONIC_TIMER = 3,
    RIBON_SERVICE_KIND_WATCHDOG = 4,
    RIBON_SERVICE_KIND_RESET = 5,
    RIBON_SERVICE_KIND_PERSISTENT_METADATA = 6,
    RIBON_SERVICE_KIND_NETWORK_TRANSPORT = 7,
    RIBON_SERVICE_KIND_RANDOM_NONCE = 8,
    RIBON_SERVICE_KIND_DIAGNOSTIC_SINK = 9,
    RIBON_SERVICE_KIND_ENVIRONMENT_QUIESCE = 10,
};

/** @brief Service directory 안에서 같은 role이 갖는 provider cardinality다. */
enum RibonServiceCardinality {
    RIBON_SERVICE_CARDINALITY_AUTHORITY = 0,
    RIBON_SERVICE_CARDINALITY_COLLECTION = 1,
};

/** @brief Native handle이 유효한 최대 lifecycle boundary다. */
enum RibonServiceLifetime {
    RIBON_SERVICE_LIFETIME_BOOT = 0,
    RIBON_SERVICE_LIFETIME_QUIESCE = 1,
    RIBON_SERVICE_LIFETIME_PERSISTENT = 2,
};

/** @brief 검증된 slot metadata snapshot이다. */
struct RibonSlotSet {
    uint64_t generation; /**< Metadata generation이다. */
    uint32_t slot_count; /**< Slot descriptor 수다. */
    uint32_t active_slot; /**< Active slot index다. */
    uint32_t pending_slot; /**< Pending slot 또는 `RIBON_SLOT_NONE`이다. */
};

typedef int (*RibonServiceBootSourceReadFn)(
    void *, const struct RibonBootSource *, uint64_t, void *, uint64_t, uint64_t);
typedef int (*RibonServiceSlotWriteFn)(
    void *, uint32_t, uint64_t, const void *, uint64_t, uint64_t);
typedef int (*RibonServiceSlotEraseFn)(void *, uint32_t, uint64_t, uint64_t, uint64_t);
typedef int (*RibonServiceStorageFlushFn)(void *, uint32_t, uint64_t);
typedef int (*RibonServiceTimerNowFn)(void *, uint64_t *);
typedef int (*RibonServiceWatchdogArmFn)(void *, uint64_t);
typedef int (*RibonServiceResetFn)(void *, uint32_t);
typedef int (*RibonServiceMetadataReadFn)(void *, uint64_t, void *, uint64_t);
typedef int (*RibonServiceMetadataWriteFn)(void *, uint64_t, const void *, uint64_t);
typedef int (*RibonServiceNetworkFetchFn)(
    void *, const struct RibonBootSource *, uint64_t, void *, uint64_t, uint64_t *, uint64_t);
typedef int (*RibonServiceRandomFillFn)(void *, void *, uint64_t);
typedef int (*RibonServiceDiagnosticWriteFn)(void *, const void *, uint64_t);
typedef int (*RibonServiceEnvironmentQuiesceFn)(void *);

/** @brief Boot-source role의 typed operation table이다. */
struct RibonBootSourceServiceOperations {
    uint32_t size; /**< Operation table byte 크기다. */
    uint32_t abi_version; /**< `RIBON_SERVICE_ABI_VERSION`이다. */
    void *context; /**< Environment-owned borrowed context다. */
    RibonServiceBootSourceReadFn read; /**< Bounded immutable source read다. */
};

/** @brief Monotonic-timer role의 typed operation table이다. */
struct RibonMonotonicTimerServiceOperations {
    uint32_t size; /**< Operation table byte 크기다. */
    uint32_t abi_version; /**< `RIBON_SERVICE_ABI_VERSION`이다. */
    void *context; /**< Environment-owned borrowed context다. */
    uint64_t frequency_hz; /**< Non-zero monotonic tick frequency다. */
    RibonServiceTimerNowFn now; /**< Monotonic counter snapshot callback이다. */
};

/** @brief Persistent attempt metadata role의 typed operation table이다. */
struct RibonPersistentMetadataServiceOperations {
    uint32_t size; /**< Operation table byte 크기다. */
    uint32_t abi_version; /**< `RIBON_SERVICE_ABI_VERSION`이다. */
    void *context; /**< Environment-owned borrowed context다. */
    RibonServiceMetadataReadFn read; /**< Bounded durable metadata read다. */
    RibonServiceMetadataWriteFn write; /**< Bounded durable metadata write다. */
};

/** @brief Persistent metadata flush role의 typed operation table이다. */
struct RibonStorageFlushServiceOperations {
    uint32_t size; /**< Operation table byte 크기다. */
    uint32_t abi_version; /**< `RIBON_SERVICE_ABI_VERSION`이다. */
    void *context; /**< Environment-owned borrowed context다. */
    RibonServiceStorageFlushFn flush; /**< Bounded durability barrier다. */
};

/** @brief Environment native service closure role의 typed operation table이다. */
struct RibonEnvironmentQuiesceServiceOperations {
    uint32_t size; /**< Operation table byte 크기다. */
    uint32_t abi_version; /**< `RIBON_SERVICE_ABI_VERSION`이다. */
    void *context; /**< Environment-owned borrowed context다. */
    RibonServiceEnvironmentQuiesceFn quiesce; /**< Native service closure callback이다. */
};

struct RibonServiceDescriptor;

/** @brief Kind-specific operation table의 service ABI 검증 callback이다. */
typedef int (*RibonServiceValidateOperationsFn)(
    const struct RibonServiceDescriptor *descriptor);

/** @brief QStar와 provider source가 공유하는 immutable typed service ABI다. */
struct RibonServiceDescriptor {
    uint32_t magic; /**< `RIBON_SERVICE_DESCRIPTOR_MAGIC`이어야 한다. */
    uint32_t size; /**< 이 descriptor의 byte 크기다. */
    uint32_t abi_version; /**< `RIBON_SERVICE_ABI_VERSION`과 일치해야 한다. */
    enum RibonServiceKind kind; /**< Stable typed role이다. */
    enum RibonServiceCardinality cardinality; /**< Authority 또는 collection이다. */
    enum RibonServiceLifetime lifetime; /**< Native handle lifetime boundary다. */
    enum RibonPluginPhase phase; /**< 최초 소비 가능한 phase다. */
    const char *id; /**< Directory 안에서 유일한 stable service ID다. */
    uint64_t provides; /**< 이 role이 제공하는 정확한 capability bitset이다. */
    uint32_t architecture_mask; /**< 허용 architecture bit다. */
    uint32_t environment_mask; /**< 허용 environment bit다. */
    uint32_t personality_mask; /**< 허용 firmware personality bit다. */
    uint32_t mode_mask; /**< 허용 mode bitset이다. */
    uint64_t arena_budget; /**< Core arena 소비 상한이다. */
    uint64_t input_budget; /**< 한 operation input byte 상한이다. */
    uint64_t output_budget; /**< 한 operation output byte 상한이다. */
    uint64_t deadline_ms; /**< 한 operation duration 상한이다. */
    const void *operations; /**< Kind-specific immutable operation table이다. */
    uint32_t operations_size; /**< Operation table byte 크기다. */
    uint32_t operations_abi; /**< Kind-specific operation ABI다. */
    RibonServiceValidateOperationsFn validate_operations; /**< Typed validator다. */
};

/** @brief Product graph가 build time에 고정한 collection owner selector다. */
struct RibonServiceSelection {
    enum RibonServiceKind kind; /**< 선택할 collection role이다. */
    const char *id; /**< 선택한 service descriptor의 stable ID다. */
};

/** @brief Caller-owned, allocation-free immutable service descriptor directory다. */
struct RibonServiceDirectory {
    uint32_t size; /**< 이 directory descriptor의 byte 크기다. */
    uint32_t abi_version; /**< `RIBON_SERVICE_DIRECTORY_ABI_VERSION`이다. */
    const struct RibonServiceDescriptor *const *services; /**< Stable ID 순 pointer array다. */
    uint32_t service_count; /**< Service pointer element 수다. */
};

/** @brief Service role의 안정적인 이름을 반환한다. */
const char *ribon_service_kind_name(enum RibonServiceKind kind);

/** @brief Service descriptor의 generic ABI와 typed operation table을 검사한다. */
int ribon_service_descriptor_is_valid(const struct RibonServiceDescriptor *descriptor);

/** @brief Product tuple, budget, authority/collection selection을 fail-closed로 검사한다. */
int ribon_service_directory_validate(
    const struct RibonServiceDirectory *directory,
    const struct RibonProductDescriptor *product,
    enum RibonMode mode);

/** @brief Stable ID와 role이 정확히 일치하는 descriptor를 반환한다. */
const struct RibonServiceDescriptor *ribon_service_directory_find_exact(
    const struct RibonServiceDirectory *directory,
    enum RibonServiceKind kind,
    const char *id);

/** @brief Product가 collection owner로 선택한 descriptor를 반환한다. */
const struct RibonServiceDescriptor *ribon_service_directory_find_selected(
    const struct RibonServiceDirectory *directory,
    const struct RibonProductDescriptor *product,
    enum RibonServiceKind kind);

/** @brief Environment plugin operation이 local typed directory인지 검사한다. */
int ribon_environment_plugin_operations_are_valid(
    const struct RibonPluginDescriptor *descriptor);

/** @brief QStar가 생성한 product service directory를 반환한다. */
const struct RibonServiceDirectory *ribon_generated_service_directory(void);

#endif
