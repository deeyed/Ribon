#ifndef RIBON_FIRMWARE_SERVICES_H
#define RIBON_FIRMWARE_SERVICES_H

#include <stdint.h>

#include <Ribon/boot/source.h>
#include <Ribon/core/capability.h>

struct RibonPluginDescriptor;

/** @brief Firmware-neutral service operation table ABI다. */
#define RIBON_SERVICE_TABLE_ABI_VERSION 1u

/** @brief Pending slot이 없음을 나타내는 slot index다. */
#define RIBON_SLOT_NONE UINT32_MAX

/** @brief Service operation의 공통 결과다. */
enum RibonServiceStatus {
    RIBON_SERVICE_STATUS_OK = 0,
    RIBON_SERVICE_STATUS_BAD_ARGUMENT = -1,
    RIBON_SERVICE_STATUS_UNSUPPORTED = -2,
    RIBON_SERVICE_STATUS_OUT_OF_RANGE = -3,
    RIBON_SERVICE_STATUS_TIMEOUT = -4,
    RIBON_SERVICE_STATUS_IO = -5,
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

/**
 * @brief Boot Library가 소비하는 complete firmware-neutral service table이다.
 *
 * 지원하지 않는 operation도 explicit unsupported callback을 가진다.
 */
struct RibonServiceTable {
    uint32_t size; /**< Operation table byte 크기다. */
    uint32_t abi_version; /**< `RIBON_SERVICE_TABLE_ABI_VERSION`과 일치해야 한다. */
    uint64_t capabilities; /**< Native callback으로 제공하는 service bit다. */
    uint64_t timer_frequency_hz; /**< Monotonic timer tick frequency다. */
    void *context; /**< Environment-private borrowed context다. */
    RibonServiceBootSourceReadFn boot_source_read;
    RibonServiceSlotWriteFn inactive_slot_write;
    RibonServiceSlotEraseFn inactive_slot_erase;
    RibonServiceStorageFlushFn storage_flush;
    RibonServiceTimerNowFn timer_now;
    RibonServiceWatchdogArmFn watchdog_arm;
    RibonServiceResetFn reset;
    RibonServiceMetadataReadFn metadata_read;
    RibonServiceMetadataWriteFn metadata_write;
    RibonServiceNetworkFetchFn network_fetch;
    RibonServiceRandomFillFn random_fill;
    RibonServiceDiagnosticWriteFn diagnostic_write;
};

/** @brief 모든 operation을 explicit unsupported callback으로 초기화한다. */
void ribon_service_table_init_unsupported(
    struct RibonServiceTable *services,
    void *context);

/** @brief Service capability와 callback 존재 여부를 정확히 대조한다. */
int ribon_service_table_is_valid(const struct RibonServiceTable *services);

/** @brief 요청 service capability를 모두 제공하는지 검사한다. */
int ribon_service_table_supports(
    const struct RibonServiceTable *services,
    uint64_t capabilities);

/** @brief Environment plugin descriptor와 service table을 함께 검사한다. */
int ribon_environment_plugin_operations_are_valid(
    const struct RibonPluginDescriptor *descriptor);

#endif
