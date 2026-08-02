#include <Ribon/boot/image.h>
#include <Ribon/plugin/descriptor.h>

#include <stddef.h>

#define RIBON_LINUX_AARCH64_HEADER_SIZE 64u
#define RIBON_LINUX_AARCH64_MAGIC 0x644d5241u
#define RIBON_LINUX_AARCH64_MACHINE 183u
#define RIBON_LINUX_AARCH64_FLAG_BIG_ENDIAN (1ull << 0u)
#define RIBON_LINUX_AARCH64_KNOWN_FLAGS 0xfull
#define RIBON_LINUX_AARCH64_MIN_ALIGNMENT 4096ull

/** @brief Unaligned little-endian 32-bit Linux Image field를 읽는다. */
static uint32_t linux_image_read_le32(const unsigned char *bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) |
           ((uint32_t)bytes[3] << 24u);
}

/** @brief Unaligned little-endian 64-bit Linux Image field를 읽는다. */
static uint64_t linux_image_read_le64(const unsigned char *bytes) {
    return (uint64_t)linux_image_read_le32(bytes) |
           ((uint64_t)linux_image_read_le32(bytes + 4u) << 32u);
}

/** @brief Caller-owned load plan을 fail-closed empty state로 초기화한다. */
static void linux_image_reset(struct RibonDirectLoadPlan *out) {
    struct RibonLoadSegment *segments = out->segments;
    const uint32_t capacity = out->segment_capacity;
    *out = (struct RibonDirectLoadPlan){
        .size = sizeof(*out),
        .abi_version = RIBON_DIRECT_LOAD_PLAN_ABI_VERSION,
        .segments = segments,
        .segment_capacity = capacity,
    };
}

/**
 * @brief AArch64 Linux Image header를 relative load plan으로 낮춘다.
 *
 * Parser는 absolute board address를 선택하지 않는다. `text_offset`을 product의
 * payload-placement window에 대한 relative offset으로 보존하고 Boot Library가
 * selected placement authority로 이를 한 번 재배치한다.
 */
static int linux_aarch64_image_analyze(
    const struct RibonPayloadImage *image,
    struct RibonValidatedImage *validated_out,
    struct RibonDirectLoadPlan *out) {
    const unsigned char *bytes;
    uint64_t text_offset;
    uint64_t image_size;
    uint64_t flags;
    uint64_t image_end;
    struct RibonLoadSegment *segment;

    if (image == 0 || image->data == 0 || validated_out == 0 ||
        !ribon_direct_load_plan_has_storage(out)) {
        return RIBON_LOADER_STATUS_BAD_ARGUMENT;
    }
    *validated_out = (struct RibonValidatedImage){0};
    linux_image_reset(out);
    if (image->size < RIBON_LINUX_AARCH64_HEADER_SIZE) {
        return RIBON_LOADER_STATUS_TRUNCATED;
    }
    bytes = image->data;
    if (linux_image_read_le32(bytes + 56u) != RIBON_LINUX_AARCH64_MAGIC) {
        return RIBON_LOADER_STATUS_BAD_FORMAT;
    }
    text_offset = linux_image_read_le64(bytes + 8u);
    image_size = linux_image_read_le64(bytes + 16u);
    flags = linux_image_read_le64(bytes + 24u);
    if (image_size == 0u || image_size < image->size ||
        (flags & RIBON_LINUX_AARCH64_FLAG_BIG_ENDIAN) != 0u ||
        (flags & ~RIBON_LINUX_AARCH64_KNOWN_FLAGS) != 0u ||
        (text_offset & (RIBON_LINUX_AARCH64_MIN_ALIGNMENT - 1u)) != 0u ||
        linux_image_read_le64(bytes + 32u) != 0u ||
        linux_image_read_le64(bytes + 40u) != 0u ||
        linux_image_read_le64(bytes + 48u) != 0u) {
        return RIBON_LOADER_STATUS_UNSUPPORTED;
    }
    if (text_offset > UINT64_MAX - image_size) {
        return RIBON_LOADER_STATUS_OVERFLOW;
    }
    image_end = text_offset + image_size;

    segment = &out->segments[0];
    *segment = (struct RibonLoadSegment){
        .file_offset = 0u,
        .file_size = image->size,
        .memory_size = image_size,
        .virtual_address = text_offset,
        .linked_physical_address = text_offset,
        .physical_address = text_offset,
        .load_address = text_offset,
        .runtime_address = text_offset,
        .alignment = RIBON_LINUX_AARCH64_MIN_ALIGNMENT,
        .flags =
            RIBON_LOAD_SEGMENT_READ |
            RIBON_LOAD_SEGMENT_WRITE |
            RIBON_LOAD_SEGMENT_EXECUTE,
    };
    out->segment_count = 1u;
    out->load_plan_flags =
        RIBON_LOAD_PLAN_ENTRY_LOAD_VALID |
        RIBON_LOAD_PLAN_RUNTIME_ENTRY_VALID |
        RIBON_LOAD_PLAN_RELOCATABLE;
    out->entry_point = text_offset;
    out->entry_load_address = text_offset;
    out->runtime_entry_address = text_offset;
    out->load_base = text_offset;
    out->load_end = image_end;
    out->runtime_load_base = text_offset;
    out->runtime_load_end = image_end;
    out->memory_size = image_size;
    out->linked_virtual_base = text_offset;
    out->linked_virtual_end = image_end;
    out->linked_physical_base = text_offset;
    out->linked_physical_end = image_end;
    *validated_out = (struct RibonValidatedImage){
        .size = sizeof(*validated_out),
        .abi_version = RIBON_VALIDATED_IMAGE_ABI_VERSION,
        .format = RIBON_EXECUTABLE_FORMAT_LINUX_AARCH64,
        .machine = RIBON_LINUX_AARCH64_MACHINE,
        .execution_support = RIBON_IMAGE_EXECUTION_DIRECT_ENTRY,
        .image_size = image->size,
        .validation_receipt = flags,
    };
    return RIBON_LOADER_STATUS_OK;
}

static const struct RibonImageFormatOps linux_aarch64_image_ops = {
    .size = sizeof(linux_aarch64_image_ops),
    .abi_version = RIBON_IMAGE_FORMAT_OPS_ABI_VERSION,
    .format = RIBON_EXECUTABLE_FORMAT_LINUX_AARCH64,
    .execution_support = RIBON_IMAGE_EXECUTION_DIRECT_ENTRY,
    .analyze = linux_aarch64_image_analyze,
};

/** @brief AArch64 Linux raw Image plugin descriptor다. */
const struct RibonPluginDescriptor ribon_linux_aarch64_image_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_linux_aarch64_image_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_IMAGE_FORMAT,
    .phase = RIBON_PLUGIN_PHASE_BOOT,
    .id = "image.linux-aarch64",
    .provides = RIBON_CAP_IMAGE_LINUX_AARCH64,
    .requires = 0u,
    .architecture_mask = RIBON_ARCH_MASK_AARCH64,
    .environment_mask = RIBON_ENV_MASK_RAW_FDT,
    .mode_mask =
        RIBON_MODE_MASK(RIBON_MODE_NORMAL) |
        RIBON_MODE_MASK(RIBON_MODE_DIAGNOSTIC),
    .arena_budget = 4096u,
    .input_budget = 64ull * 1024ull * 1024ull,
    .output_budget = 4096u,
    .deadline_ms = 30000u,
    .operations = &linux_aarch64_image_ops,
    .operations_size = sizeof(linux_aarch64_image_ops),
    .operations_abi = RIBON_IMAGE_FORMAT_OPS_ABI_VERSION,
    .validate_operations = ribon_image_plugin_operations_are_valid,
};
