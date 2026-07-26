#include <Ribon/memory.h>

static int ribon_u64_add_overflows(uint64_t lhs, uint64_t rhs) {
    return lhs > UINT64_MAX - rhs;
}

uint64_t ribon_align_down(uint64_t value, uint64_t alignment) {
    if (alignment == 0u) {
        return value;
    }
    return value - (value % alignment);
}

int ribon_align_up(uint64_t value, uint64_t alignment, uint64_t *out) {
    uint64_t remainder;
    uint64_t delta;
    if (out == 0 || alignment == 0u) {
        return RIBON_MEMORY_STATUS_BAD_ARGUMENT;
    }
    remainder = value % alignment;
    if (remainder == 0u) {
        *out = value;
        return RIBON_MEMORY_STATUS_OK;
    }
    delta = alignment - remainder;
    if (ribon_u64_add_overflows(value, delta)) {
        return RIBON_MEMORY_STATUS_OVERFLOW;
    }
    *out = value + delta;
    return RIBON_MEMORY_STATUS_OK;
}

int ribon_memory_region_end(const struct RibonMemoryRegion *region, uint64_t *out) {
    if (region == 0 || out == 0 || region->length == 0u) {
        return RIBON_MEMORY_STATUS_BAD_ARGUMENT;
    }
    if (ribon_u64_add_overflows(region->base, region->length)) {
        return RIBON_MEMORY_STATUS_OVERFLOW;
    }
    *out = region->base + region->length;
    return RIBON_MEMORY_STATUS_OK;
}

int ribon_memory_region_is_valid(const struct RibonMemoryRegion *region) {
    uint64_t end = 0;
    if (region == 0 || region->length == 0u) {
        return 0;
    }
    return ribon_memory_region_end(region, &end) == RIBON_MEMORY_STATUS_OK;
}

const char *ribon_memory_region_kind_name(enum RibonMemoryRegionKind kind) {
    switch (kind) {
    case RIBON_MEMORY_REGION_UNKNOWN:
        return "unknown";
    case RIBON_MEMORY_REGION_USABLE:
        return "usable";
    case RIBON_MEMORY_REGION_RESERVED:
        return "reserved";
    case RIBON_MEMORY_REGION_ACPI:
        return "acpi";
    case RIBON_MEMORY_REGION_MMIO:
        return "mmio";
    case RIBON_MEMORY_REGION_FRAMEBUFFER:
        return "framebuffer";
    case RIBON_MEMORY_REGION_FIRMWARE:
        return "firmware";
    case RIBON_MEMORY_REGION_BOOTLOADER:
        return "bootloader";
    case RIBON_MEMORY_REGION_KERNEL_IMAGE:
        return "kernel-image";
    case RIBON_MEMORY_REGION_BOOT_MODULE:
        return "boot-module";
    default:
        return "unknown";
    }
}

static int ribon_memory_region_same_class(
    const struct RibonMemoryRegion *lhs,
    const struct RibonMemoryRegion *rhs) {
    return lhs->kind == rhs->kind && lhs->attributes == rhs->attributes;
}

static int ribon_memory_region_insert_sorted(
    struct RibonMutableMemoryMap *destination,
    const struct RibonMemoryRegion *region) {
    uint32_t index = 0;
    uint32_t cursor;
    if (destination->region_count >= destination->capacity) {
        return RIBON_MEMORY_STATUS_OUT_OF_CAPACITY;
    }
    while (index < destination->region_count && destination->regions[index].base <= region->base) {
        ++index;
    }
    cursor = destination->region_count;
    while (cursor > index) {
        destination->regions[cursor] = destination->regions[cursor - 1u];
        --cursor;
    }
    destination->regions[index] = *region;
    ++destination->region_count;
    return RIBON_MEMORY_STATUS_OK;
}

static int ribon_memory_map_merge_adjacent(struct RibonMutableMemoryMap *destination) {
    uint32_t read_index;
    uint32_t write_index = 0;
    for (read_index = 0; read_index < destination->region_count; ++read_index) {
        struct RibonMemoryRegion current = destination->regions[read_index];
        uint64_t previous_end = 0;
        uint64_t current_end = 0;
        if (ribon_memory_region_end(&current, &current_end) != RIBON_MEMORY_STATUS_OK) {
            return RIBON_MEMORY_STATUS_OVERFLOW;
        }
        if (write_index > 0u &&
            ribon_memory_region_end(&destination->regions[write_index - 1u], &previous_end) !=
                RIBON_MEMORY_STATUS_OK) {
            return RIBON_MEMORY_STATUS_OVERFLOW;
        }
        if (write_index > 0u && current.base < previous_end) {
            return RIBON_MEMORY_STATUS_OVERLAP;
        }
        if (write_index > 0u &&
            current.base == previous_end &&
            ribon_memory_region_same_class(&destination->regions[write_index - 1u], &current)) {
            destination->regions[write_index - 1u].length += current.length;
        } else {
            destination->regions[write_index] = current;
            ++write_index;
        }
    }
    destination->region_count = write_index;
    return RIBON_MEMORY_STATUS_OK;
}

int ribon_memory_map_normalize(
    const struct RibonMemoryMap *source,
    struct RibonMutableMemoryMap *destination) {
    uint32_t index;
    if (source == 0 || destination == 0 || destination->regions == 0) {
        return RIBON_MEMORY_STATUS_BAD_ARGUMENT;
    }
    destination->region_count = 0;
    for (index = 0; index < source->region_count; ++index) {
        const struct RibonMemoryRegion *region = &source->regions[index];
        int status;
        if (!ribon_memory_region_is_valid(region)) {
            return RIBON_MEMORY_STATUS_BAD_ARGUMENT;
        }
        status = ribon_memory_region_insert_sorted(destination, region);
        if (status != RIBON_MEMORY_STATUS_OK) {
            return status;
        }
    }
    return ribon_memory_map_merge_adjacent(destination);
}

uint64_t ribon_memory_map_usable_bytes(const struct RibonMemoryMap *memory_map) {
    uint32_t index;
    uint64_t total = 0;
    if (memory_map == 0 || memory_map->regions == 0) {
        return 0;
    }
    for (index = 0; index < memory_map->region_count; ++index) {
        const struct RibonMemoryRegion *region = &memory_map->regions[index];
        if (region->kind != RIBON_MEMORY_REGION_USABLE) {
            continue;
        }
        if (ribon_u64_add_overflows(total, region->length)) {
            return UINT64_MAX;
        }
        total += region->length;
    }
    return total;
}
