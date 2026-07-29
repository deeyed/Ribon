#ifndef RIBON_BOOT_IMAGE_H
#define RIBON_BOOT_IMAGE_H

#include <stdint.h>

struct RibonPluginDescriptor;

/** @brief Image-format operation table ABI다. */
#define RIBON_IMAGE_FORMAT_OPS_ABI_VERSION 2u

/** @brief Ribon image format의 stable ID다. */
enum RibonExecutableFormat {
    RIBON_EXECUTABLE_FORMAT_UNKNOWN = 0,
    RIBON_EXECUTABLE_FORMAT_ELF64 = 1,
    RIBON_EXECUTABLE_FORMAT_PE_COFF = 2,
};

/** @brief Image format을 protocol allowlist bit로 변환한다. */
#define RIBON_IMAGE_FORMAT_MASK(format) (1ull << (uint32_t)(format))

/** @brief Image parser와 load-plan 생성의 결과다. */
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

/** @brief Load segment permission bit다. */
enum RibonLoadSegmentFlags {
    RIBON_LOAD_SEGMENT_READ = 1u << 0,
    RIBON_LOAD_SEGMENT_WRITE = 1u << 1,
    RIBON_LOAD_SEGMENT_EXECUTE = 1u << 2,
};

/** @brief Validated load plan fact bit다. */
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

/** @brief Caller가 이미 읽은 immutable image bytes다. */
struct RibonPayloadImage {
    const void *data; /**< Borrowed image byte 시작이다. */
    uint64_t size; /**< Image byte 수다. */
    const char *source_name; /**< 진단용 stable source 이름이다. */
};

/** @brief 한 validated load segment다. */
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

/** @brief Image-format plugin이 caller-owned segment array에 만든 load plan이다. */
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

/** @brief Image bytes를 architecture-neutral load plan으로 분석한다. */
typedef int (*RibonImageAnalyzeFn)(
    const struct RibonPayloadImage *image,
    struct RibonLoadedPayload *out);

/** @brief 한 image-format plugin의 typed operation table이다. */
struct RibonImageFormatOps {
    uint32_t size; /**< Operation table byte 크기다. */
    uint32_t abi_version; /**< `RIBON_IMAGE_FORMAT_OPS_ABI_VERSION`과 일치해야 한다. */
    enum RibonExecutableFormat format; /**< 이 parser가 소유하는 format이다. */
    RibonImageAnalyzeFn analyze; /**< Bounds-checked analyzer다. */
};

/** @brief Executable format의 안정적인 이름을 반환한다. */
const char *ribon_executable_format_name(enum RibonExecutableFormat format);

/** @brief Loader status의 안정적인 이름을 반환한다. */
const char *ribon_loader_status_name(enum RibonLoaderStatus status);

/** @brief Image-format operation table의 ABI와 callback을 검사한다. */
int ribon_image_format_ops_are_valid(const struct RibonImageFormatOps *ops);

/** @brief Image-format plugin descriptor와 operation table을 함께 검사한다. */
int ribon_image_plugin_operations_are_valid(
    const struct RibonPluginDescriptor *descriptor);

/** @brief ELF64 image-format plugin descriptor다. */
extern const struct RibonPluginDescriptor ribon_elf64_image_plugin_descriptor;

/** @brief PE32+ image-format plugin descriptor다. */
extern const struct RibonPluginDescriptor ribon_pe_coff_image_plugin_descriptor;

#endif
