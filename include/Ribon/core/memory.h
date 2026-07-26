#ifndef RIBON_CORE_MEMORY_H
#define RIBON_CORE_MEMORY_H

#include <stdint.h>

#include <Ribon/core/status.h>

/** @brief Memory region이 허용하는 access와 lifetime bit다. */
enum RibonMemoryAttribute {
    RIBON_MEMORY_ATTR_READ = 1ull << 0,
    RIBON_MEMORY_ATTR_WRITE = 1ull << 1,
    RIBON_MEMORY_ATTR_EXECUTE = 1ull << 2,
    RIBON_MEMORY_ATTR_DEVICE = 1ull << 3,
    RIBON_MEMORY_ATTR_FIRMWARE_RUNTIME = 1ull << 4,
    RIBON_MEMORY_ATTR_BOOT_RECLAIMABLE = 1ull << 5,
};

/** @brief Normalized memory region의 ownership 종류다. */
enum RibonMemoryRegionKind {
    RIBON_MEMORY_REGION_UNKNOWN = 0,
    RIBON_MEMORY_REGION_USABLE = 1,
    RIBON_MEMORY_REGION_RESERVED = 2,
    RIBON_MEMORY_REGION_ACPI = 3,
    RIBON_MEMORY_REGION_MMIO = 4,
    RIBON_MEMORY_REGION_FRAMEBUFFER = 5,
    RIBON_MEMORY_REGION_FIRMWARE = 6,
    RIBON_MEMORY_REGION_BOOTLOADER = 7,
    RIBON_MEMORY_REGION_KERNEL_IMAGE = 8,
    RIBON_MEMORY_REGION_BOOT_MODULE = 9,
};

/** @brief Memory normalization과 범위 연산의 결과다. */
enum RibonMemoryStatus {
    RIBON_MEMORY_STATUS_OK = 0,
    RIBON_MEMORY_STATUS_BAD_ARGUMENT = -1,
    RIBON_MEMORY_STATUS_OVERFLOW = -2,
    RIBON_MEMORY_STATUS_OUT_OF_CAPACITY = -3,
    RIBON_MEMORY_STATUS_OVERLAP = -4,
};

/** @brief Half-open physical memory range와 ownership을 나타낸다. */
struct RibonMemoryRegion {
    uint64_t base; /**< 첫 physical byte 주소다. */
    uint64_t length; /**< Range byte 수다. */
    enum RibonMemoryRegionKind kind; /**< Range ownership 종류다. */
    uint64_t attributes; /**< `RibonMemoryAttribute` bitset이다. */
};

/** @brief Caller가 소유하는 immutable memory map view다. */
struct RibonMemoryMap {
    const struct RibonMemoryRegion *regions; /**< Borrowed region array다. */
    uint32_t region_count; /**< Region element 수다. */
};

/** @brief Caller-owned storage에 정규화 결과를 받는 memory map이다. */
struct RibonMutableMemoryMap {
    struct RibonMemoryRegion *regions; /**< Caller-owned output array다. */
    uint32_t region_count; /**< 생성된 region element 수다. */
    uint32_t capacity; /**< Output array element capacity다. */
};

/**
 * @brief Caller-owned 고정 용량 bump arena다.
 *
 * Core는 storage를 해제하지 않으며 allocation rewind를 제공하지 않는다.
 */
struct RibonArena {
    unsigned char *base; /**< Caller-owned storage 시작 주소다. */
    uint64_t capacity; /**< 사용할 수 있는 전체 byte 수다. */
    uint64_t used; /**< 정렬 padding을 포함한 소비 byte 수다. */
    uint64_t high_watermark; /**< Arena 수명의 최대 used 값이다. */
};

/** @brief Caller-owned storage로 빈 arena를 초기화한다. */
void ribon_arena_init(struct RibonArena *arena, void *storage, uint64_t capacity);

/** @brief 정렬된 고정 크기 영역을 arena에서 단방향 할당한다. */
int ribon_arena_allocate(
    struct RibonArena *arena,
    uint64_t size,
    uint64_t alignment,
    void **out);

/** @brief Arena의 남은 byte 수를 반환하며 잘못된 arena에는 0을 반환한다. */
uint64_t ribon_arena_remaining(const struct RibonArena *arena);

/** @brief 값을 alignment 경계 아래로 내린다. */
uint64_t ribon_align_down(uint64_t value, uint64_t alignment);

/** @brief 값을 alignment 경계 위로 올리고 overflow를 검사한다. */
int ribon_align_up(uint64_t value, uint64_t alignment, uint64_t *out);

/** @brief Region의 exclusive end를 계산하고 overflow를 검사한다. */
int ribon_memory_region_end(const struct RibonMemoryRegion *region, uint64_t *out);

/** @brief Region 길이와 범위 overflow를 검사한다. */
int ribon_memory_region_is_valid(const struct RibonMemoryRegion *region);

/** @brief Memory region kind의 안정적인 진단 문자열을 반환한다. */
const char *ribon_memory_region_kind_name(enum RibonMemoryRegionKind kind);

/** @brief 겹치는 입력 map을 caller-owned output에 결정론적으로 정규화한다. */
int ribon_memory_map_normalize(
    const struct RibonMemoryMap *source,
    struct RibonMutableMemoryMap *destination);

/** @brief Validated memory map의 usable byte 합을 overflow 없이 계산한다. */
uint64_t ribon_memory_map_usable_bytes(const struct RibonMemoryMap *memory_map);

#endif
