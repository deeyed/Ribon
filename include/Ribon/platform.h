#ifndef RIBON_PLATFORM_H
#define RIBON_PLATFORM_H

#include <stdint.h>

#include <Ribon/firmware.h>

/** @brief Platform operation table ABI의 첫 번째 안정 버전이다. */
#define RIBON_PLATFORM_OPS_ABI_VERSION 1u

/** @brief Pending slot이 없음을 나타내는 고정 slot index다. */
#define RIBON_SLOT_NONE UINT32_MAX

/** @brief Platform operation table이 분류하는 capability bit다. */
enum RibonPlatformCapability {
    RIBON_PLATFORM_CAP_BOOT_SOURCE_READ = 1ull << 0,
    RIBON_PLATFORM_CAP_INACTIVE_SLOT_WRITE = 1ull << 1,
    RIBON_PLATFORM_CAP_INACTIVE_SLOT_ERASE = 1ull << 2,
    RIBON_PLATFORM_CAP_STORAGE_FLUSH = 1ull << 3,
    RIBON_PLATFORM_CAP_MONOTONIC_TIMER = 1ull << 4,
    RIBON_PLATFORM_CAP_WATCHDOG = 1ull << 5,
    RIBON_PLATFORM_CAP_RESET = 1ull << 6,
    RIBON_PLATFORM_CAP_PERSISTENT_METADATA = 1ull << 7,
    RIBON_PLATFORM_CAP_NETWORK_TRANSPORT = 1ull << 8,
    RIBON_PLATFORM_CAP_RANDOM_NONCE = 1ull << 9,
    RIBON_PLATFORM_CAP_DIAGNOSTIC_SINK = 1ull << 10,
};

/** @brief R2가 정의하는 Platform capability 전체다. */
#define RIBON_PLATFORM_CAP_ALL ((1ull << 11) - 1ull)

/** @brief Platform operation의 공통 결과다. */
enum RibonPlatformStatus {
    RIBON_PLATFORM_STATUS_OK = 0,
    RIBON_PLATFORM_STATUS_BAD_ARGUMENT = -1,
    RIBON_PLATFORM_STATUS_UNSUPPORTED = -2,
    RIBON_PLATFORM_STATUS_OUT_OF_RANGE = -3,
    RIBON_PLATFORM_STATUS_TIMEOUT = -4,
    RIBON_PLATFORM_STATUS_IO = -5,
};

/** @brief Boot source를 native handle 없이 식별하는 byte-range descriptor다. */
struct RibonBootSource {
    enum RibonBootMediaKind kind; /**< Source transport 종류다. */
    uint32_t source_id; /**< Adapter 내부 source table의 검증된 식별자다. */
    uint64_t size; /**< 읽을 수 있는 전체 byte 수다. */
    uint32_t block_size; /**< Block source의 atomic block 크기며 그 외에는 0이다. */
};

/** @brief 검증된 slot metadata snapshot의 Core-facing 부분이다. */
struct RibonSlotSet {
    uint64_t generation; /**< Metadata snapshot generation이다. */
    uint32_t slot_count; /**< 검증된 slot descriptor 수다. */
    uint32_t active_slot; /**< 정상 boot 후보 slot index다. */
    uint32_t pending_slot; /**< Pending slot index며 없으면 `UINT32_MAX`다. */
};

/** @brief Adapter가 Core에 제공하는 immutable platform 사실이다. */
struct RibonPlatformFacts {
    enum RibonFirmwareKind firmware; /**< Adapter를 제공한 firmware 종류다. */
    const char *platform_name; /**< Build가 소유하는 고정 platform 이름이다. */
    uint64_t timer_frequency_hz; /**< Monotonic timer tick frequency다. */
    uint32_t reset_reason; /**< Adapter가 정규화한 reset reason 값이다. */
    uint64_t capabilities; /**< Supported capability bitset의 복제본이다. */
};

/** @brief 지원 bit와 지원 불가 bit를 동시에 명시하는 capability descriptor다. */
struct RibonPlatformCapabilitySet {
    uint64_t supported; /**< Callback이 native operation으로 교체된 bit다. */
    uint64_t unsupported; /**< Callback이 unsupported stub인 bit다. */
};

/** @brief Boot source byte-range read operation이다. */
typedef int (*RibonPlatformBootSourceReadFn)(
    void *context,
    const struct RibonBootSource *source,
    uint64_t offset,
    void *buffer,
    uint64_t size,
    uint64_t deadline_ticks);

/** @brief Inactive slot byte-range write operation이다. */
typedef int (*RibonPlatformSlotWriteFn)(
    void *context,
    uint32_t slot,
    uint64_t offset,
    const void *buffer,
    uint64_t size,
    uint64_t deadline_ticks);

/** @brief Inactive slot byte-range erase operation이다. */
typedef int (*RibonPlatformSlotEraseFn)(
    void *context,
    uint32_t slot,
    uint64_t offset,
    uint64_t size,
    uint64_t deadline_ticks);

/** @brief Durable storage flush operation이다. */
typedef int (*RibonPlatformStorageFlushFn)(
    void *context,
    uint32_t slot,
    uint64_t deadline_ticks);

/** @brief Monotonic timer 값을 읽는 operation이다. */
typedef int (*RibonPlatformTimerNowFn)(void *context, uint64_t *ticks_out);

/** @brief Watchdog deadline을 arm하는 operation이다. */
typedef int (*RibonPlatformWatchdogArmFn)(void *context, uint64_t deadline_ticks);

/** @brief Platform reset을 요청하는 operation이다. */
typedef int (*RibonPlatformResetFn)(void *context, uint32_t reason);

/** @brief Persistent metadata byte-range read operation이다. */
typedef int (*RibonPlatformMetadataReadFn)(
    void *context,
    uint64_t offset,
    void *buffer,
    uint64_t size);

/** @brief Persistent metadata byte-range write operation이다. */
typedef int (*RibonPlatformMetadataWriteFn)(
    void *context,
    uint64_t offset,
    const void *buffer,
    uint64_t size);

/** @brief Bounded network source fetch operation이다. */
typedef int (*RibonPlatformNetworkFetchFn)(
    void *context,
    const struct RibonBootSource *source,
    uint64_t offset,
    void *buffer,
    uint64_t capacity,
    uint64_t *received_out,
    uint64_t deadline_ticks);

/** @brief Boot nonce를 채우는 operation이다. */
typedef int (*RibonPlatformRandomFillFn)(void *context, void *buffer, uint64_t size);

/** @brief Bounded diagnostic byte sequence를 내보내는 operation이다. */
typedef int (*RibonPlatformDiagnosticWriteFn)(
    void *context,
    const void *buffer,
    uint64_t size);

/**
 * @brief Core가 호출할 수 있는 Platform operation table이다.
 *
 * 모든 function pointer는 non-null이어야 한다. 지원하지 않는 operation은
 * `ribon_platform_ops_init_unsupported`가 설치한 명시적 unsupported stub을 유지한다.
 */
struct RibonPlatformOps {
    uint32_t abi_version; /**< `RIBON_PLATFORM_OPS_ABI_VERSION`과 일치해야 한다. */
    struct RibonPlatformFacts facts; /**< Core가 소비하는 immutable platform 사실이다. */
    struct RibonPlatformCapabilitySet capabilities; /**< 모든 알려진 bit의 완전 분류다. */
    void *context; /**< 모든 callback에 전달하는 adapter-owned context다. */
    RibonPlatformBootSourceReadFn boot_source_read; /**< Boot source read callback이다. */
    RibonPlatformSlotWriteFn inactive_slot_write; /**< Inactive slot write callback이다. */
    RibonPlatformSlotEraseFn inactive_slot_erase; /**< Inactive slot erase callback이다. */
    RibonPlatformStorageFlushFn storage_flush; /**< Durable flush callback이다. */
    RibonPlatformTimerNowFn timer_now; /**< Monotonic timer read callback이다. */
    RibonPlatformWatchdogArmFn watchdog_arm; /**< Watchdog arm callback이다. */
    RibonPlatformResetFn reset; /**< Platform reset callback이다. */
    RibonPlatformMetadataReadFn metadata_read; /**< Persistent metadata read callback이다. */
    RibonPlatformMetadataWriteFn metadata_write; /**< Persistent metadata write callback이다. */
    RibonPlatformNetworkFetchFn network_fetch; /**< Bounded network fetch callback이다. */
    RibonPlatformRandomFillFn random_fill; /**< Nonce byte fill callback이다. */
    RibonPlatformDiagnosticWriteFn diagnostic_write; /**< Diagnostic sink write callback이다. */
};

/**
 * @brief 모든 capability가 명시적으로 unsupported인 완전한 operation table을 만든다.
 *
 * Adapter는 지원 callback을 교체한 뒤 해당 bit를 `unsupported`에서 `supported`로
 * 옮겨야 한다.
 */
void ribon_platform_ops_init_unsupported(
    struct RibonPlatformOps *ops,
    enum RibonFirmwareKind firmware,
    const char *platform_name,
    void *context);

/** @brief Platform operation table의 ABI, bit 분류, callback 완전성을 검사한다. */
int ribon_platform_ops_are_valid(const struct RibonPlatformOps *ops);

/** @brief 요청한 capability를 모두 지원하는지 검사한다. */
int ribon_platform_ops_supports(
    const struct RibonPlatformOps *ops,
    uint64_t capabilities);

#endif
