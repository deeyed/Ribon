#ifndef RIBON_ENVIRONMENTS_RAW_FDT_H
#define RIBON_ENVIRONMENTS_RAW_FDT_H

#include <Ribon/arch/ops.h>
#include <Ribon/firmware/environment.h>
#include <Ribon/firmware/services.h>
#include <Ribon/plugin/descriptor.h>

/** @brief Target가 raw-FDT capture에 제공하는 reserved physical range다. */
struct RibonRawFdtReservation {
    uint64_t base; /**< Reserved range 시작이다. */
    uint64_t size; /**< Reserved range byte 수다. */
    enum RibonMemoryRegionKind kind; /**< Ribon memory-map 분류다. */
};

/**
 * @brief Native entry와 target image recipe가 raw-FDT consumer에 제공하는 입력이다.
 *
 * 모든 pointer와 storage는 transfer까지 target image가 소유한다.
 */
struct RibonRawFdtEntry {
    const void *fdt; /**< Firmware가 전달한 borrowed FDT pointer다. */
    uint64_t fdt_capacity; /**< Parser가 읽을 수 있는 byte 상한이다. */
    enum RibonArchitectureId architecture; /**< 선택된 architecture ID다. */
    const struct RibonArchOps *arch_ops; /**< Monotonic counter provider다. */
    uint64_t timer_frequency_hz; /**< Counter frequency다. */
    const void *payload; /**< Memory boot source의 borrowed byte다. */
    uint64_t payload_size; /**< Boot source byte 수다. */
    const char *payload_name; /**< Diagnostic source 이름이다. */
    const struct RibonRawFdtReservation *reservations; /**< Target-owned reserved range다. */
    uint32_t reservation_count; /**< Reservation element 수다. */
    struct RibonMemoryRegion *memory_regions; /**< Caller-owned normalized-input storage다. */
    uint32_t memory_region_capacity; /**< Region storage element 수다. */
};

/** @brief raw-FDT environment consumer의 결과다. */
enum RibonRawFdtStatus {
    RIBON_RAW_FDT_STATUS_OK = 0,
    RIBON_RAW_FDT_STATUS_BAD_ARGUMENT = -1,
    RIBON_RAW_FDT_STATUS_BAD_FDT = -2,
    RIBON_RAW_FDT_STATUS_BAD_RESERVATION = -3,
    RIBON_RAW_FDT_STATUS_OUT_OF_CAPACITY = -4,
};

/** @brief Native FDT와 target reservation을 firmware-neutral environment로 동결한다. */
int ribon_raw_fdt_environment_capture(
    struct RibonRawFdtEntry *entry,
    struct RibonBootEnvironment *out);

/** @brief 초기화된 raw-FDT service table을 반환한다. */
const struct RibonServiceTable *ribon_raw_fdt_services(void);

/** @brief raw-FDT environment consumer plugin descriptor다. */
extern const struct RibonPluginDescriptor ribon_raw_fdt_environment_plugin_descriptor;

#endif
