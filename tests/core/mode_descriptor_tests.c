#include <Ribon/arch.h>
#include <Ribon/core.h>
#include <Ribon/platform.h>

#include <stdio.h>
#include <string.h>

static int limits_match(
    const struct RibonResourceLimits *actual,
    const struct RibonResourceLimits *expected) {
    return actual->max_memory_regions == expected->max_memory_regions &&
           actual->max_load_segments == expected->max_load_segments &&
           actual->max_components == expected->max_components &&
           actual->max_retries == expected->max_retries &&
           actual->max_input_bytes == expected->max_input_bytes &&
           actual->max_handoff_bytes == expected->max_handoff_bytes &&
           actual->arena_bytes == expected->arena_bytes &&
           actual->operation_deadline_ms == expected->operation_deadline_ms;
}

static int expected_descriptor(
    enum RibonMode mode,
    const char **name_out,
    uint64_t *required_out,
    uint64_t *forbidden_out,
    uint64_t *required_arch_out,
    struct RibonResourceLimits *limits_out) {
    *required_out = 0u;
    *forbidden_out = 0u;
    *required_arch_out = 0u;
    *limits_out = (struct RibonResourceLimits){0};
    switch (mode) {
    case RIBON_MODE_NORMAL:
        *name_out = "normal";
        *required_out =
            RIBON_PLATFORM_CAP_BOOT_SOURCE_READ |
            RIBON_PLATFORM_CAP_MONOTONIC_TIMER;
        *forbidden_out =
            RIBON_PLATFORM_CAP_INACTIVE_SLOT_WRITE |
            RIBON_PLATFORM_CAP_INACTIVE_SLOT_ERASE |
            RIBON_PLATFORM_CAP_NETWORK_TRANSPORT;
        *required_arch_out =
            RIBON_ARCH_CAP_VALIDATE_PAYLOAD |
            RIBON_ARCH_CAP_CACHE_SYNC |
            RIBON_ARCH_CAP_ENTRY_BRIDGE |
            RIBON_ARCH_CAP_HALT;
        *limits_out = (struct RibonResourceLimits){
            .max_memory_regions = 256u,
            .max_load_segments = 32u,
            .max_components = 32u,
            .max_retries = 2u,
            .max_input_bytes = 64ull * 1024ull * 1024ull,
            .max_handoff_bytes = 64ull * 1024ull,
            .arena_bytes = 256ull * 1024ull,
            .operation_deadline_ms = 30000u,
        };
        return 1;
    case RIBON_MODE_RECOVERY:
        *name_out = "recovery";
        *required_out =
            RIBON_PLATFORM_CAP_BOOT_SOURCE_READ |
            RIBON_PLATFORM_CAP_INACTIVE_SLOT_WRITE |
            RIBON_PLATFORM_CAP_INACTIVE_SLOT_ERASE |
            RIBON_PLATFORM_CAP_STORAGE_FLUSH |
            RIBON_PLATFORM_CAP_MONOTONIC_TIMER |
            RIBON_PLATFORM_CAP_PERSISTENT_METADATA |
            RIBON_PLATFORM_CAP_RANDOM_NONCE;
        *required_arch_out =
            RIBON_ARCH_CAP_VALIDATE_PAYLOAD |
            RIBON_ARCH_CAP_CACHE_SYNC |
            RIBON_ARCH_CAP_ENTRY_BRIDGE |
            RIBON_ARCH_CAP_HALT;
        *limits_out = (struct RibonResourceLimits){
            .max_memory_regions = 256u,
            .max_load_segments = 32u,
            .max_components = 32u,
            .max_retries = 4u,
            .max_input_bytes = 256ull * 1024ull * 1024ull,
            .max_handoff_bytes = 64ull * 1024ull,
            .arena_bytes = 512ull * 1024ull,
            .operation_deadline_ms = 120000u,
        };
        return 1;
    case RIBON_MODE_PROVISIONING:
        *name_out = "provisioning";
        *required_out =
            RIBON_PLATFORM_CAP_INACTIVE_SLOT_WRITE |
            RIBON_PLATFORM_CAP_STORAGE_FLUSH |
            RIBON_PLATFORM_CAP_MONOTONIC_TIMER |
            RIBON_PLATFORM_CAP_PERSISTENT_METADATA |
            RIBON_PLATFORM_CAP_RANDOM_NONCE;
        *required_arch_out =
            RIBON_ARCH_CAP_VALIDATE_PAYLOAD |
            RIBON_ARCH_CAP_HALT;
        *limits_out = (struct RibonResourceLimits){
            .max_memory_regions = 256u,
            .max_load_segments = 32u,
            .max_components = 32u,
            .max_retries = 3u,
            .max_input_bytes = 256ull * 1024ull * 1024ull,
            .max_handoff_bytes = 64ull * 1024ull,
            .arena_bytes = 512ull * 1024ull,
            .operation_deadline_ms = 120000u,
        };
        return 1;
    case RIBON_MODE_DIAGNOSTIC:
        *name_out = "diagnostic";
        *required_out =
            RIBON_PLATFORM_CAP_MONOTONIC_TIMER |
            RIBON_PLATFORM_CAP_DIAGNOSTIC_SINK;
        *forbidden_out =
            RIBON_PLATFORM_CAP_INACTIVE_SLOT_WRITE |
            RIBON_PLATFORM_CAP_INACTIVE_SLOT_ERASE |
            RIBON_PLATFORM_CAP_PERSISTENT_METADATA;
        *required_arch_out =
            RIBON_ARCH_CAP_VALIDATE_PAYLOAD |
            RIBON_ARCH_CAP_HALT;
        *limits_out = (struct RibonResourceLimits){
            .max_memory_regions = 512u,
            .max_load_segments = 64u,
            .max_components = 64u,
            .max_retries = 2u,
            .max_input_bytes = 64ull * 1024ull * 1024ull,
            .max_handoff_bytes = 64ull * 1024ull,
            .arena_bytes = 1024ull * 1024ull,
            .operation_deadline_ms = 60000u,
        };
        return 1;
    default:
        return 0;
    }
}

int main(void) {
    const struct RibonModeDescriptor *mode = ribon_mode_selected();
    const char *expected_name = 0;
    uint64_t expected_required = 0u;
    uint64_t expected_forbidden = 0u;
    uint64_t expected_required_arch = 0u;
    struct RibonResourceLimits expected_limits;

    if (mode == 0 ||
        mode->abi_version != RIBON_CORE_ABI_VERSION ||
        !expected_descriptor(
            mode->mode,
            &expected_name,
            &expected_required,
            &expected_forbidden,
            &expected_required_arch,
            &expected_limits) ||
        strcmp(mode->name, expected_name) != 0 ||
        mode->required_platform_capabilities != expected_required ||
        mode->forbidden_platform_capabilities != expected_forbidden ||
        mode->required_arch_capabilities != expected_required_arch ||
        mode->forbidden_arch_capabilities != 0u ||
        (expected_required & expected_forbidden) != 0u ||
        !limits_match(&mode->limits, &expected_limits)) {
        fputs("mode_descriptor_tests: contract mismatch\n", stderr);
        return 1;
    }
    printf("RIBON-MODE-DESCRIPTOR-OK %s\n", mode->name);
    return 0;
}
