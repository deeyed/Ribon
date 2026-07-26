#ifndef RIBON_LOADER_H
#define RIBON_LOADER_H

#include <stdint.h>

struct RibonArchDescriptor;

enum RibonExecutableFormat {
    RIBON_EXECUTABLE_FORMAT_UNKNOWN = 0,
    RIBON_EXECUTABLE_FORMAT_ELF64 = 1,
};

enum RibonLoaderStatus {
    RIBON_LOADER_STATUS_OK = 0,
    RIBON_LOADER_STATUS_BAD_ARGUMENT = -1,
    RIBON_LOADER_STATUS_BAD_FORMAT = -2,
    RIBON_LOADER_STATUS_UNSUPPORTED = -3,
    RIBON_LOADER_STATUS_TRUNCATED = -4,
    RIBON_LOADER_STATUS_OVERFLOW = -5,
    RIBON_LOADER_STATUS_OUT_OF_CAPACITY = -6,
    RIBON_LOADER_STATUS_MISALIGNED = -7,
    RIBON_LOADER_STATUS_NO_LOAD_SEGMENTS = -8,
    RIBON_LOADER_STATUS_NON_CANONICAL = -9,
    RIBON_LOADER_STATUS_OVERLAPPING_SEGMENTS = -10,
};

enum RibonLoadSegmentFlags {
    RIBON_LOAD_SEGMENT_READ = 1u << 0,
    RIBON_LOAD_SEGMENT_WRITE = 1u << 1,
    RIBON_LOAD_SEGMENT_EXECUTE = 1u << 2,
};

enum RibonLoadPlanFlags {
    RIBON_LOAD_PLAN_ENTRY_LOAD_VALID = 1u << 0,
    RIBON_LOAD_PLAN_RUNTIME_ENTRY_VALID = 1u << 1,
    RIBON_LOAD_PLAN_USES_PADDR = 1u << 2,
    RIBON_LOAD_PLAN_HAS_HIGHER_HALF = 1u << 3,
    RIBON_LOAD_PLAN_DIRECT_HIGH_ENTRY_CANDIDATE = 1u << 4,
    RIBON_LOAD_PLAN_HAS_LINKED_PHYSICAL_RANGE = 1u << 5,
    RIBON_LOAD_PLAN_SEGMENTS_PLACED = 1u << 6,
    RIBON_LOAD_PLAN_FALLBACK_ALLOCATION = 1u << 7,
};

struct RibonPayloadImage {
    const void *data;
    uint64_t size;
    const char *source_name;
};

struct RibonLoadSegment {
    uint64_t file_offset;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t virtual_address;
    uint64_t linked_physical_address;
    uint64_t physical_address;
    uint64_t load_address;
    uint64_t runtime_address;
    uint64_t alignment;
    uint32_t flags;
};

struct RibonLoadedPayload {
    enum RibonExecutableFormat format;
    uint16_t machine;
    uint32_t segment_count;
    uint32_t load_plan_flags;
    uint64_t entry_point;
    uint64_t entry_load_address;
    uint64_t runtime_entry_address;
    uint64_t load_base;
    uint64_t load_end;
    uint64_t runtime_load_base;
    uint64_t runtime_load_end;
    uint64_t memory_size;
    uint64_t linked_virtual_base;
    uint64_t linked_virtual_end;
    uint64_t linked_physical_base;
    uint64_t linked_physical_end;
    uint64_t high_entry_virtual_address;
    uint64_t high_entry_load_address;
    struct RibonLoadSegment *segments;
    uint32_t segment_capacity;
};

const char *ribon_executable_format_name(enum RibonExecutableFormat format);
const char *ribon_loader_status_name(enum RibonLoaderStatus status);
int ribon_loader_analyze(
    const struct RibonPayloadImage *image,
    const struct RibonArchDescriptor *arch,
    struct RibonLoadedPayload *out);
int ribon_loader_analyze_elf64(
    const struct RibonPayloadImage *image,
    const struct RibonArchDescriptor *arch,
    struct RibonLoadedPayload *out);

#endif
