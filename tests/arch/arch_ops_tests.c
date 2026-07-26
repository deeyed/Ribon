#include <Ribon/arch.h>
#include <Ribon/loader.h>

#include <stdio.h>
#include <string.h>

static int expected_arch(
    const char *name,
    uint16_t *machine_out,
    uint64_t *capabilities_out) {
    *capabilities_out =
        RIBON_ARCH_CAP_VALIDATE_PAYLOAD |
        RIBON_ARCH_CAP_CACHE_SYNC |
        RIBON_ARCH_CAP_HALT;
    if (strcmp(name, "x86_64") == 0) {
        *machine_out = 62u;
        *capabilities_out |=
            RIBON_ARCH_CAP_DIRECT_HIGH_ENTRY |
            RIBON_ARCH_CAP_ENTRY_BRIDGE;
        return 1;
    }
    if (strcmp(name, "aarch64") == 0) {
        *machine_out = 183u;
        *capabilities_out |=
            RIBON_ARCH_CAP_DIRECT_HIGH_ENTRY |
            RIBON_ARCH_CAP_ENTRY_BRIDGE;
        return 1;
    }
    if (strcmp(name, "riscv64") == 0) {
        *machine_out = 243u;
        return 1;
    }
    return 0;
}

int main(void) {
    const struct RibonArchOps *ops = ribon_arch_selected_ops();
    uint16_t expected_machine = 0u;
    uint64_t expected_capabilities = 0u;
    struct RibonLoadSegment segment = {
        .file_size = 0x1000u,
        .memory_size = 0x1000u,
        .virtual_address = 0x200000u,
        .flags = RIBON_LOAD_SEGMENT_EXECUTE,
    };
    struct RibonLoadedPayload payload = {
        .format = RIBON_EXECUTABLE_FORMAT_ELF64,
        .segment_count = 1u,
        .entry_point = 0x200078u,
        .segments = &segment,
        .segment_capacity = 1u,
    };

    if (ops == 0 ||
        !ribon_arch_ops_are_valid(ops) ||
        ops->descriptor != ribon_arch_selected() ||
        !expected_arch(
            ops->descriptor->canonical_name,
            &expected_machine,
            &expected_capabilities) ||
        ops->capabilities != expected_capabilities) {
        fputs("arch_ops_tests: operation table mismatch\n", stderr);
        return 1;
    }
    payload.machine = expected_machine;
    if (ops->validate_payload(ops->descriptor, &payload) !=
        RIBON_ARCH_OPERATION_OK) {
        fputs("arch_ops_tests: valid payload rejected\n", stderr);
        return 1;
    }
    ++payload.machine;
    if (ops->validate_payload(ops->descriptor, &payload) !=
        RIBON_ARCH_OPERATION_INVALID_PAYLOAD) {
        fputs("arch_ops_tests: wrong machine accepted\n", stderr);
        return 1;
    }
    printf("RIBON-ARCH-OPS-OK %s\n", ops->descriptor->canonical_name);
    return 0;
}
