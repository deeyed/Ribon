#ifndef RIBON_COMMON_SYS_FDT_H
#define RIBON_COMMON_SYS_FDT_H

#include <stdint.h>

/** @brief allocation 없이 보존하는 FDT reservation fact 상한이다. */
#define RIBON_FDT_RESERVATION_CAPACITY 8u

/** @brief FDT reserve map 또는 /reserved-memory에서 검증한 physical range다. */
struct RibonFdtReservation {
    uint64_t base;
    uint64_t size;
};

/** @brief Bounded FDT parser가 반환하는 platform-neutral fact다. */
struct RibonFdtFacts {
    uint32_t total_size; /**< 검증된 FDT blob byte 수다. */
    uint64_t memory_base; /**< 첫 usable memory range 시작이다. */
    uint64_t memory_size; /**< 첫 usable memory range 크기다. */
    const char *boot_arguments; /**< FDT가 소유하는 borrowed bootargs다. */
    uint32_t boot_arguments_size; /**< NUL을 제외한 bootargs byte 수다. */
    struct RibonFdtReservation
        reservations[RIBON_FDT_RESERVATION_CAPACITY];
    uint32_t reservation_count; /**< 검증한 nonzero reserved range 수다. */
};

/** @brief FDT parser 결과다. */
enum RibonFdtStatus {
    RIBON_FDT_STATUS_OK = 0,
    RIBON_FDT_STATUS_BAD_ARGUMENT = -1,
    RIBON_FDT_STATUS_BAD_HEADER = -2,
    RIBON_FDT_STATUS_TRUNCATED = -3,
    RIBON_FDT_STATUS_BAD_STRUCTURE = -4,
    RIBON_FDT_STATUS_MISSING_MEMORY = -5,
};

/**
 * @brief Borrowed FDT blob을 bounds-check하고 memory와 chosen fact를 추출한다.
 *
 * Parser는 allocation과 MMIO를 수행하지 않으며 blob lifetime을 연장하지 않는다.
 */
int ribon_fdt_parse(const void *blob, uint64_t capacity, struct RibonFdtFacts *out);

#endif
