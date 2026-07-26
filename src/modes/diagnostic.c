#include <Ribon/arch.h>
#include <Ribon/core.h>
#include <Ribon/platform.h>

static const struct RibonModeDescriptor diagnostic_mode = {
    .abi_version = RIBON_CORE_ABI_VERSION,
    .mode = RIBON_MODE_DIAGNOSTIC,
    .name = "diagnostic",
    .required_platform_capabilities =
        RIBON_PLATFORM_CAP_MONOTONIC_TIMER |
        RIBON_PLATFORM_CAP_DIAGNOSTIC_SINK,
    .forbidden_platform_capabilities =
        RIBON_PLATFORM_CAP_INACTIVE_SLOT_WRITE |
        RIBON_PLATFORM_CAP_INACTIVE_SLOT_ERASE |
        RIBON_PLATFORM_CAP_PERSISTENT_METADATA,
    .required_arch_capabilities =
        RIBON_ARCH_CAP_VALIDATE_PAYLOAD |
        RIBON_ARCH_CAP_HALT,
    .forbidden_arch_capabilities = 0u,
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
