#include <Ribon/boot/image.h>
#include <Ribon/plugin/descriptor.h>

#include <stddef.h>

#define RIBON_LINUX_RISCV64_HEADER_SIZE 64u
#define RIBON_LINUX_RISCV64_MACHINE 243u
#define RIBON_LINUX_RISCV64_TEXT_OFFSET (2ull * 1024ull * 1024ull)
#define RIBON_LINUX_RISCV64_ALIGNMENT (2ull * 1024ull * 1024ull)
#define RIBON_LINUX_RISCV64_MAX_IMAGE_SIZE (64ull * 1024ull * 1024ull)
#define RIBON_LINUX_RISCV64_HEADER_VERSION 2u
#define RIBON_LINUX_RISCV64_MAGIC 0x0000005643534952ull
#define RIBON_LINUX_RISCV64_MAGIC2 0x05435352u
#define RIBON_LINUX_RISCV64_FLAG_BIG_ENDIAN (1ull << 0u)
#define RIBON_LINUX_RISCV64_KNOWN_FLAGS RIBON_LINUX_RISCV64_FLAG_BIG_ENDIAN

/** @brief Unaligned little-endian 32-bit Linux Image field를 읽는다. */
static uint32_t linux_riscv64_read_le32(const unsigned char *bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) |
           ((uint32_t)bytes[3] << 24u);
}

/** @brief Unaligned little-endian 64-bit Linux Image field를 읽는다. */
static uint64_t linux_riscv64_read_le64(const unsigned char *bytes) {
    return (uint64_t)linux_riscv64_read_le32(bytes) |
           ((uint64_t)linux_riscv64_read_le32(bytes + 4u) << 32u);
}

/** @brief Caller-owned direct plan을 fail-closed empty state로 되돌린다. */
static void linux_riscv64_reset(struct RibonDirectLoadPlan *out) {
    struct RibonLoadSegment *segments = out->segments;
    const uint32_t capacity = out->segment_capacity;
    *out = (struct RibonDirectLoadPlan){
        .size = sizeof(*out),
        .abi_version = RIBON_DIRECT_LOAD_PLAN_ABI_VERSION,
        .segments = segments,
        .segment_capacity = capacity,
    };
}

/** @brief Linux RISC-V64 Image header를 2 MiB-aligned relative plan으로 낮춘다. */
static int linux_riscv64_image_analyze(
    const struct RibonPayloadImage *image,
    struct RibonValidatedImage *validated_out,
    struct RibonDirectLoadPlan *out) {
    const unsigned char *bytes;
    uint64_t text_offset;
    uint64_t image_size;
    uint64_t flags;
    uint32_t version;
    uint32_t pe_header_offset;
    struct RibonLoadSegment *segment;

    if (image == 0 || image->data == 0 || validated_out == 0 ||
        !ribon_direct_load_plan_has_storage(out)) {
        return RIBON_LOADER_STATUS_BAD_ARGUMENT;
    }
    *validated_out = (struct RibonValidatedImage){0};
    linux_riscv64_reset(out);
    if (image->size < RIBON_LINUX_RISCV64_HEADER_SIZE) {
        return RIBON_LOADER_STATUS_TRUNCATED;
    }
    bytes = image->data;
    text_offset = linux_riscv64_read_le64(bytes + 8u);
    image_size = linux_riscv64_read_le64(bytes + 16u);
    flags = linux_riscv64_read_le64(bytes + 24u);
    version = linux_riscv64_read_le32(bytes + 32u);
    pe_header_offset = linux_riscv64_read_le32(bytes + 60u);
    if (linux_riscv64_read_le64(bytes + 48u) !=
            RIBON_LINUX_RISCV64_MAGIC ||
        linux_riscv64_read_le32(bytes + 56u) !=
            RIBON_LINUX_RISCV64_MAGIC2) {
        return RIBON_LOADER_STATUS_BAD_FORMAT;
    }
    if (text_offset != RIBON_LINUX_RISCV64_TEXT_OFFSET ||
        image_size == 0u || image_size < image->size ||
        image_size > RIBON_LINUX_RISCV64_MAX_IMAGE_SIZE ||
        version != RIBON_LINUX_RISCV64_HEADER_VERSION ||
        (flags & RIBON_LINUX_RISCV64_FLAG_BIG_ENDIAN) != 0u ||
        (flags & ~RIBON_LINUX_RISCV64_KNOWN_FLAGS) != 0u ||
        linux_riscv64_read_le32(bytes + 36u) != 0u ||
        linux_riscv64_read_le64(bytes + 40u) != 0u ||
        (pe_header_offset != 0u &&
         (pe_header_offset < RIBON_LINUX_RISCV64_HEADER_SIZE ||
          pe_header_offset > image->size - 4u ||
          linux_riscv64_read_le32(bytes + pe_header_offset) != 0x00004550u))) {
        return RIBON_LOADER_STATUS_UNSUPPORTED;
    }

    segment = &out->segments[0];
    *segment = (struct RibonLoadSegment){
        .file_offset = 0u,
        .file_size = image->size,
        .memory_size = image_size,
        .alignment = RIBON_LINUX_RISCV64_ALIGNMENT,
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
    out->load_end = image_size;
    out->runtime_load_end = image_size;
    out->memory_size = image_size;
    out->linked_virtual_end = image_size;
    out->linked_physical_end = image_size;
    *validated_out = (struct RibonValidatedImage){
        .size = sizeof(*validated_out),
        .abi_version = RIBON_VALIDATED_IMAGE_ABI_VERSION,
        .format = RIBON_EXECUTABLE_FORMAT_LINUX_RISCV64,
        .machine = RIBON_LINUX_RISCV64_MACHINE,
        .execution_support = RIBON_IMAGE_EXECUTION_DIRECT_ENTRY,
        .image_size = image->size,
        .validation_receipt =
            ((uint64_t)version << 32u) | (uint64_t)pe_header_offset,
    };
    return RIBON_LOADER_STATUS_OK;
}

static const struct RibonImageFormatOps linux_riscv64_image_ops = {
    .size = sizeof(linux_riscv64_image_ops),
    .abi_version = RIBON_IMAGE_FORMAT_OPS_ABI_VERSION,
    .format = RIBON_EXECUTABLE_FORMAT_LINUX_RISCV64,
    .execution_support = RIBON_IMAGE_EXECUTION_DIRECT_ENTRY,
    .analyze = linux_riscv64_image_analyze,
};

/** @brief RISC-V64 Linux raw Image plugin descriptor다. */
const struct RibonPluginDescriptor ribon_linux_riscv64_image_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_linux_riscv64_image_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_IMAGE_FORMAT,
    .phase = RIBON_PLUGIN_PHASE_BOOT,
    .id = "image.linux-riscv64",
    .provides = RIBON_CAP_IMAGE_LINUX_RISCV64,
    .requires = 0u,
    .architecture_mask = RIBON_ARCH_MASK_RISCV64,
    .environment_mask = RIBON_ENV_MASK_RAW_FDT,
    .mode_mask =
        RIBON_MODE_MASK(RIBON_MODE_NORMAL) |
        RIBON_MODE_MASK(RIBON_MODE_DIAGNOSTIC),
    .arena_budget = 4096u,
    .input_budget = 64ull * 1024ull * 1024ull,
    .output_budget = 4096u,
    .deadline_ms = 30000u,
    .operations = &linux_riscv64_image_ops,
    .operations_size = sizeof(linux_riscv64_image_ops),
    .operations_abi = RIBON_IMAGE_FORMAT_OPS_ABI_VERSION,
    .validate_operations = ribon_image_plugin_operations_are_valid,
};
