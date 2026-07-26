#ifndef RIBON_MEMORY_H
#define RIBON_MEMORY_H

#include <stdint.h>

#define RIBON_MEMORY_ATTR_READ (1ull << 0)
#define RIBON_MEMORY_ATTR_WRITE (1ull << 1)
#define RIBON_MEMORY_ATTR_EXECUTE (1ull << 2)
#define RIBON_MEMORY_ATTR_DEVICE (1ull << 3)
#define RIBON_MEMORY_ATTR_FIRMWARE_RUNTIME (1ull << 4)
#define RIBON_MEMORY_ATTR_BOOT_RECLAIMABLE (1ull << 5)

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

enum RibonMemoryStatus {
    RIBON_MEMORY_STATUS_OK = 0,
    RIBON_MEMORY_STATUS_BAD_ARGUMENT = -1,
    RIBON_MEMORY_STATUS_OVERFLOW = -2,
    RIBON_MEMORY_STATUS_OUT_OF_CAPACITY = -3,
    RIBON_MEMORY_STATUS_OVERLAP = -4,
};

struct RibonMemoryRegion {
    uint64_t base;
    uint64_t length;
    enum RibonMemoryRegionKind kind;
    uint64_t attributes;
};

struct RibonMemoryMap {
    const struct RibonMemoryRegion *regions;
    uint32_t region_count;
};

struct RibonMutableMemoryMap {
    struct RibonMemoryRegion *regions;
    uint32_t region_count;
    uint32_t capacity;
};

uint64_t ribon_align_down(uint64_t value, uint64_t alignment);
int ribon_align_up(uint64_t value, uint64_t alignment, uint64_t *out);
int ribon_memory_region_end(const struct RibonMemoryRegion *region, uint64_t *out);
int ribon_memory_region_is_valid(const struct RibonMemoryRegion *region);
const char *ribon_memory_region_kind_name(enum RibonMemoryRegionKind kind);
int ribon_memory_map_normalize(
    const struct RibonMemoryMap *source,
    struct RibonMutableMemoryMap *destination);
uint64_t ribon_memory_map_usable_bytes(const struct RibonMemoryMap *memory_map);

#endif
