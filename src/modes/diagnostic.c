#include <Ribon/core/capability.h>

static const struct RibonModeDescriptor diagnostic_mode = {
    .size = sizeof(diagnostic_mode),
    .abi_version = RIBON_CORE_ABI_VERSION,
    .mode = RIBON_MODE_DIAGNOSTIC,
    .name = "diagnostic",
    .required_capabilities =
        RIBON_CAP_MONOTONIC_TIMER |
        RIBON_CAP_DIAGNOSTIC_SINK |
        RIBON_CAP_ARCHITECTURE,
    .forbidden_capabilities =
        RIBON_CAP_INACTIVE_SLOT_WRITE |
        RIBON_CAP_INACTIVE_SLOT_ERASE |
        RIBON_CAP_PERSISTENT_METADATA,
    .limits = {
        .max_memory_regions = 512u,
        .max_load_segments = 64u,
        .max_components = 64u,
        .max_retries = 2u,
        .max_input_bytes = 64ull * 1024ull * 1024ull,
        .max_handoff_bytes = 64ull * 1024ull,
        .arena_bytes = 1024ull * 1024ull,
        .operation_deadline_ms = 60000u,
    },
};

/** @brief Diagnostic link object graph가 선택한 mode descriptor를 반환한다. */
const struct RibonModeDescriptor *ribon_mode_selected(void) {
    return &diagnostic_mode;
}
