#ifndef RIBON_UEFI_HARDENING_H
#define RIBON_UEFI_HARDENING_H

#include <stdint.h>

#define RIBON_UEFI_MEMORY_MAP_SLACK_DESCRIPTORS 32u
#define RIBON_UEFI_MEMORY_MAP_MIN_SLACK_BYTES 16384ull
#define RIBON_UEFI_MEMORY_MAP_CAPTURE_ATTEMPTS 3u
#define RIBON_UEFI_EXIT_BOOT_SERVICES_MAX_ATTEMPTS 3u

enum RibonUefiHardeningStatus {
    RIBON_UEFI_HARDENING_OK = 0,
    RIBON_UEFI_HARDENING_BAD_ARGUMENT = -1,
    RIBON_UEFI_HARDENING_OVERFLOW = -2,
    RIBON_UEFI_HARDENING_OUT_OF_CAPACITY = -3,
};

enum RibonUefiDiagnosticStage {
    RIBON_UEFI_DIAG_BOOT_VOLUME = 0,
    RIBON_UEFI_DIAG_KERNEL_READ = 1,
    RIBON_UEFI_DIAG_INITIAL_MEMORY_MAP = 2,
    RIBON_UEFI_DIAG_PLATFORM_TABLES = 3,
    RIBON_UEFI_DIAG_GOP = 4,
    RIBON_UEFI_DIAG_KERNEL_LOAD = 5,
    RIBON_UEFI_DIAG_DIRECT_HIGH = 6,
    RIBON_UEFI_DIAG_FINAL_MEMORY_MAP = 7,
    RIBON_UEFI_DIAG_RPH1_REBUILD = 8,
    RIBON_UEFI_DIAG_EXIT_BOOT_SERVICES = 9,
    RIBON_UEFI_DIAG_JUMP = 10,
};

const char *ribon_uefi_diagnostic_stage_name(enum RibonUefiDiagnosticStage stage);
int ribon_uefi_memory_map_capacity(
    uint64_t probed_map_size,
    uint64_t descriptor_size,
    uint64_t *out_capacity);
uint64_t ribon_uefi_descriptor_capacity(uint64_t buffer_capacity, uint64_t descriptor_size);
int ribon_uefi_memory_map_refresh_fits(
    uint64_t map_size,
    uint64_t descriptor_size,
    uint64_t raw_capacity,
    uint64_t region_capacity);

#endif
