#include <Ribon/core/capability.h>

static const struct RibonModeDescriptor provisioning_mode = {
    .size = sizeof(provisioning_mode),
    .abi_version = RIBON_CORE_ABI_VERSION,
    .mode = RIBON_MODE_PROVISIONING,
    .name = "provisioning",
    .required_capabilities =
        RIBON_CAP_INACTIVE_SLOT_WRITE |
        RIBON_CAP_STORAGE_FLUSH |
        RIBON_CAP_MONOTONIC_TIMER |
        RIBON_CAP_PERSISTENT_METADATA |
        RIBON_CAP_RANDOM_NONCE |
        RIBON_CAP_ARCHITECTURE,
    .forbidden_capabilities = 0u,
    .limits = {
        .max_memory_regions = 256u,
        .max_load_segments = 32u,
        .max_components = 32u,
        .max_retries = 3u,
        .max_input_bytes = 256ull * 1024ull * 1024ull,
        .max_handoff_bytes = 64ull * 1024ull,
        .arena_bytes = 512ull * 1024ull,
        .operation_deadline_ms = 120000u,
    },
};

/** @brief Provisioning link object graph가 선택한 mode descriptor를 반환한다. */
const struct RibonModeDescriptor *ribon_mode_selected(void) {
    return &provisioning_mode;
}
