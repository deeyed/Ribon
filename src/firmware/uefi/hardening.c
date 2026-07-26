#include <Ribon/uefi_hardening.h>

static int uefi_u64_add_overflows(uint64_t lhs, uint64_t rhs) {
    return lhs > UINT64_MAX - rhs;
}

static int uefi_align_up(uint64_t value, uint64_t alignment, uint64_t *out) {
    uint64_t remainder;
    uint64_t delta;
    if (out == 0 || alignment == 0u) {
        return RIBON_UEFI_HARDENING_BAD_ARGUMENT;
    }
    remainder = value % alignment;
    if (remainder == 0u) {
        *out = value;
        return RIBON_UEFI_HARDENING_OK;
    }
    delta = alignment - remainder;
    if (uefi_u64_add_overflows(value, delta)) {
        return RIBON_UEFI_HARDENING_OVERFLOW;
    }
    *out = value + delta;
    return RIBON_UEFI_HARDENING_OK;
}

const char *ribon_uefi_diagnostic_stage_name(enum RibonUefiDiagnosticStage stage) {
    switch (stage) {
    case RIBON_UEFI_DIAG_BOOT_VOLUME:
        return "boot-volume";
    case RIBON_UEFI_DIAG_KERNEL_READ:
        return "kernel-read";
    case RIBON_UEFI_DIAG_INITIAL_MEMORY_MAP:
        return "initial-memory-map";
    case RIBON_UEFI_DIAG_PLATFORM_TABLES:
        return "platform-tables";
    case RIBON_UEFI_DIAG_GOP:
        return "gop";
    case RIBON_UEFI_DIAG_KERNEL_LOAD:
        return "kernel-load";
    case RIBON_UEFI_DIAG_DIRECT_HIGH:
        return "direct-high";
    case RIBON_UEFI_DIAG_FINAL_MEMORY_MAP:
        return "final-memory-map";
    case RIBON_UEFI_DIAG_RPH1_REBUILD:
        return "rph1-rebuild";
    case RIBON_UEFI_DIAG_EXIT_BOOT_SERVICES:
        return "exit-boot-services";
    case RIBON_UEFI_DIAG_JUMP:
        return "jump";
    default:
        return "unknown";
    }
}

int ribon_uefi_memory_map_capacity(
    uint64_t probed_map_size,
    uint64_t descriptor_size,
    uint64_t *out_capacity) {
    uint64_t descriptor_slack;
    uint64_t slack;
    uint64_t capacity;
    if (out_capacity == 0 || probed_map_size == 0u || descriptor_size == 0u) {
        return RIBON_UEFI_HARDENING_BAD_ARGUMENT;
    }
    if (descriptor_size > UINT64_MAX / RIBON_UEFI_MEMORY_MAP_SLACK_DESCRIPTORS) {
        return RIBON_UEFI_HARDENING_OVERFLOW;
    }
    descriptor_slack = descriptor_size * RIBON_UEFI_MEMORY_MAP_SLACK_DESCRIPTORS;
    slack = descriptor_slack > RIBON_UEFI_MEMORY_MAP_MIN_SLACK_BYTES ?
                descriptor_slack :
                RIBON_UEFI_MEMORY_MAP_MIN_SLACK_BYTES;
    if (uefi_u64_add_overflows(probed_map_size, slack)) {
        return RIBON_UEFI_HARDENING_OVERFLOW;
    }
    if (uefi_align_up(probed_map_size + slack, descriptor_size, &capacity) !=
        RIBON_UEFI_HARDENING_OK) {
        return RIBON_UEFI_HARDENING_OVERFLOW;
    }
    *out_capacity = capacity;
    return RIBON_UEFI_HARDENING_OK;
}

uint64_t ribon_uefi_descriptor_capacity(uint64_t buffer_capacity, uint64_t descriptor_size) {
    if (descriptor_size == 0u) {
        return 0;
    }
    return buffer_capacity / descriptor_size;
}

int ribon_uefi_memory_map_refresh_fits(
    uint64_t map_size,
    uint64_t descriptor_size,
    uint64_t raw_capacity,
    uint64_t region_capacity) {
    uint64_t descriptor_count;
    if (map_size == 0u || descriptor_size == 0u || raw_capacity == 0u) {
        return RIBON_UEFI_HARDENING_BAD_ARGUMENT;
    }
    if (map_size > raw_capacity) {
        return RIBON_UEFI_HARDENING_OUT_OF_CAPACITY;
    }
    if (descriptor_size > 1u && map_size > UINT64_MAX - (descriptor_size - 1u)) {
        return RIBON_UEFI_HARDENING_OVERFLOW;
    }
    descriptor_count = (map_size + descriptor_size - 1u) / descriptor_size;
    if (descriptor_count > region_capacity) {
        return RIBON_UEFI_HARDENING_OUT_OF_CAPACITY;
    }
    return RIBON_UEFI_HARDENING_OK;
}
