#include <Ribon/boot/image.h>
#include <Ribon/plugin/descriptor.h>

/** @brief Executable format의 stable name을 반환한다. */
const char *ribon_executable_format_name(enum RibonExecutableFormat format) {
    switch (format) {
    case RIBON_EXECUTABLE_FORMAT_ELF64:
        return "elf64";
    case RIBON_EXECUTABLE_FORMAT_PE_COFF:
        return "pe-coff";
    case RIBON_EXECUTABLE_FORMAT_LINUX_AARCH64:
        return "linux-aarch64-image";
    case RIBON_EXECUTABLE_FORMAT_UNKNOWN:
    default:
        return "unknown";
    }
}

/** @brief Loader status의 stable name을 반환한다. */
const char *ribon_loader_status_name(enum RibonLoaderStatus status) {
    switch (status) {
    case RIBON_LOADER_STATUS_OK:
        return "ok";
    case RIBON_LOADER_STATUS_BAD_ARGUMENT:
        return "bad-argument";
    case RIBON_LOADER_STATUS_BAD_FORMAT:
        return "bad-format";
    case RIBON_LOADER_STATUS_UNSUPPORTED:
        return "unsupported";
    case RIBON_LOADER_STATUS_TRUNCATED:
        return "truncated";
    case RIBON_LOADER_STATUS_OVERFLOW:
        return "overflow";
    case RIBON_LOADER_STATUS_OUT_OF_CAPACITY:
        return "out-of-capacity";
    case RIBON_LOADER_STATUS_MISALIGNED:
        return "misaligned";
    case RIBON_LOADER_STATUS_NO_LOAD_SEGMENTS:
        return "no-load-segments";
    case RIBON_LOADER_STATUS_NON_CANONICAL:
        return "non-canonical";
    case RIBON_LOADER_STATUS_OVERLAPPING_SEGMENTS:
        return "overlapping-segments";
    default:
        return "unknown";
    }
}

/** @brief Image-format operation table의 ABI와 callback을 검사한다. */
int ribon_image_format_ops_are_valid(const struct RibonImageFormatOps *ops) {
    return ops != 0 &&
           ops->size == sizeof(*ops) &&
           ops->abi_version == RIBON_IMAGE_FORMAT_OPS_ABI_VERSION &&
           ops->format != RIBON_EXECUTABLE_FORMAT_UNKNOWN &&
           ops->execution_support != 0u &&
           (ops->execution_support & ~RIBON_IMAGE_EXECUTION_ALL) == 0u &&
           ops->analyze != 0;
}

/** @brief Pointer-free validation artifact의 shape와 known bit를 검사한다. */
int ribon_validated_image_is_valid(const struct RibonValidatedImage *image) {
    return image != 0 && image->size == sizeof(*image) &&
           image->abi_version == RIBON_VALIDATED_IMAGE_ABI_VERSION &&
           image->format != RIBON_EXECUTABLE_FORMAT_UNKNOWN && image->machine != 0u &&
           image->reserved == 0u && image->execution_support != 0u &&
           (image->execution_support & ~RIBON_IMAGE_EXECUTION_ALL) == 0u &&
           image->image_size != 0u;
}

/** @brief Direct load plan의 ABI와 caller-owned storage shape를 검사한다. */
int ribon_direct_load_plan_has_storage(const struct RibonDirectLoadPlan *plan) {
    return plan != 0 && plan->segments != 0 && plan->segment_capacity != 0u;
}

/** @brief Image-format descriptor와 capability ownership을 검사한다. */
int ribon_image_plugin_operations_are_valid(
    const struct RibonPluginDescriptor *descriptor) {
    const struct RibonImageFormatOps *ops;
    uint64_t expected_capability;
    if (descriptor == 0 ||
        descriptor->kind != RIBON_PLUGIN_KIND_IMAGE_FORMAT ||
        descriptor->operations_size != sizeof(struct RibonImageFormatOps) ||
        descriptor->operations_abi != RIBON_IMAGE_FORMAT_OPS_ABI_VERSION) {
        return 0;
    }
    ops = (const struct RibonImageFormatOps *)descriptor->operations;
    switch (ops->format) {
    case RIBON_EXECUTABLE_FORMAT_ELF64:
        expected_capability = RIBON_CAP_IMAGE_ELF64;
        break;
    case RIBON_EXECUTABLE_FORMAT_PE_COFF:
        expected_capability = RIBON_CAP_IMAGE_PE_COFF;
        break;
    case RIBON_EXECUTABLE_FORMAT_LINUX_AARCH64:
        expected_capability = RIBON_CAP_IMAGE_LINUX_AARCH64;
        break;
    default:
        return 0;
    }
    return ribon_image_format_ops_are_valid(ops) &&
           descriptor->provides == expected_capability;
}
