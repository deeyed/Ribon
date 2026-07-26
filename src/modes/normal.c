#include <Ribon/arch.h>
#include <Ribon/core.h>
#include <Ribon/platform.h>

static const struct RibonModeDescriptor normal_mode = {
    .abi_version = RIBON_CORE_ABI_VERSION,
    .mode = RIBON_MODE_NORMAL,
    .name = "normal",
    .required_platform_capabilities =
        RIBON_PLATFORM_CAP_BOOT_SOURCE_READ |
        RIBON_PLATFORM_CAP_MONOTONIC_TIMER,
    .forbidden_platform_capabilities =
        RIBON_PLATFORM_CAP_INACTIVE_SLOT_WRITE |
        RIBON_PLATFORM_CAP_INACTIVE_SLOT_ERASE |
        RIBON_PLATFORM_CAP_NETWORK_TRANSPORT,
    .required_arch_capabilities =
        RIBON_ARCH_CAP_VALIDATE_PAYLOAD |
        RIBON_ARCH_CAP_CACHE_SYNC |
        RIBON_ARCH_CAP_ENTRY_BRIDGE |
        RIBON_ARCH_CAP_HALT,
    .forbidden_arch_capabilities = 0u,
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
