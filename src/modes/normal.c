#include <Ribon/core/capability.h>

static const struct RibonModeDescriptor normal_mode = {
    .size = sizeof(normal_mode),
    .abi_version = RIBON_CORE_ABI_VERSION,
    .mode = RIBON_MODE_NORMAL,
    .name = "normal",
    .required_capabilities =
        RIBON_CAP_BOOT_SOURCE_READ |
        RIBON_CAP_MONOTONIC_TIMER |
        RIBON_CAP_ARCHITECTURE |
        RIBON_CAP_BOOT_PROTOCOL |
        RIBON_CAP_HANDOFF |
        RIBON_CAP_ENTRY_CONTRACT,
    .forbidden_capabilities =
        RIBON_CAP_INACTIVE_SLOT_WRITE |
        RIBON_CAP_INACTIVE_SLOT_ERASE |
        RIBON_CAP_NETWORK_TRANSPORT,
    .limits = {
        .max_memory_regions = 256u,
        .max_load_segments = 32u,
        .max_components = 32u,
        .max_retries = 2u,
        .max_input_bytes = 64ull * 1024ull * 1024ull,
        .max_handoff_bytes = 64ull * 1024ull,
        .arena_bytes = 256ull * 1024ull,
        .operation_deadline_ms = 30000u,
    },
};

/** @brief Normal link object graph가 선택한 mode descriptor를 반환한다. */
const struct RibonModeDescriptor *ribon_mode_selected(void) {
    return &normal_mode;
}
