#ifndef RIBON_FIRMWARE_BIOS_COMPATIBLE_REFERENCE_H
#define RIBON_FIRMWARE_BIOS_COMPATIBLE_REFERENCE_H

#include <stdint.h>

#define RIBON_BIOS_REFERENCE_E820_LIMIT 16u

/** @brief BIOS-compatible E820 reference의 한 immutable range다. */
struct RibonBiosReferenceE820Entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
};

/** @brief BIOS-compatible personality가 소유하는 bounded E820 table이다. */
struct RibonBiosReferenceContext {
    struct RibonBiosReferenceE820Entry entries[RIBON_BIOS_REFERENCE_E820_LIMIT];
    uint32_t entry_count;
};

/** @brief Reference E820 service의 personality-private operation table이다. */
struct RibonBiosReferenceE820Operations {
    uint32_t size;
    int (*append)(void *, uint64_t, uint64_t, uint32_t);
    int (*read)(const void *, uint32_t, struct RibonBiosReferenceE820Entry *);
};

/** @brief Caller-owned BIOS reference context를 빈 E820 table로 초기화한다. */
void ribon_bios_reference_context_init(
    struct RibonBiosReferenceContext *context);

#endif
