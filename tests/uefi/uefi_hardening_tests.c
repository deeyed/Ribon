#include <Ribon/uefi_hardening.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_failures;

static uint64_t test_align_up(uint64_t value, uint64_t alignment) {
    const uint64_t remainder = value % alignment;
    return remainder == 0u ? value : value + alignment - remainder;
}

static void expect_i32(int actual, int expected, const char *message) {
    if (actual != expected) {
        fprintf(
            stderr,
            "UEFI-HARDENING-TEST-FAIL: %s actual=%d expected=%d\n",
            message,
            actual,
            expected);
        ++g_failures;
    }
}

static void expect_u64(uint64_t actual, uint64_t expected, const char *message) {
    if (actual != expected) {
        fprintf(
            stderr,
            "UEFI-HARDENING-TEST-FAIL: %s actual=0x%llx expected=0x%llx\n",
            message,
            (unsigned long long)actual,
            (unsigned long long)expected);
        ++g_failures;
    }
}

static void expect_string(const char *actual, const char *expected, const char *message) {
    if (actual == 0 || strcmp(actual, expected) != 0) {
        fprintf(
            stderr,
            "UEFI-HARDENING-TEST-FAIL: %s actual=%s expected=%s\n",
            message,
            actual == 0 ? "(null)" : actual,
            expected);
        ++g_failures;
    }
}

static void test_memory_map_capacity_policy(void) {
    uint64_t capacity = 0;
    expect_i32(
        ribon_uefi_memory_map_capacity(4800u, 48u, &capacity),
        RIBON_UEFI_HARDENING_OK,
        "small descriptor capacity status");
    expect_u64(
        capacity,
        test_align_up(4800u + RIBON_UEFI_MEMORY_MAP_MIN_SLACK_BYTES, 48u),
        "small descriptor capacity");

    expect_i32(
        ribon_uefi_memory_map_capacity(65536u, 1024u, &capacity),
        RIBON_UEFI_HARDENING_OK,
        "large descriptor capacity status");
    expect_u64(
        capacity,
        65536u + (1024u * RIBON_UEFI_MEMORY_MAP_SLACK_DESCRIPTORS),
        "large descriptor capacity");

    expect_i32(
        ribon_uefi_memory_map_capacity(UINT64_MAX - 4096u, 4096u, &capacity),
        RIBON_UEFI_HARDENING_OVERFLOW,
        "capacity overflow");
    expect_i32(
        ribon_uefi_memory_map_capacity(4096u, 0u, &capacity),
        RIBON_UEFI_HARDENING_BAD_ARGUMENT,
        "zero descriptor size");
    expect_u64(
        ribon_uefi_descriptor_capacity(24576u, 48u),
        512u,
        "descriptor capacity");
}

static void test_refresh_fit_policy(void) {
    expect_i32(
        ribon_uefi_memory_map_refresh_fits(4096u, 48u, 8192u, 128u),
        RIBON_UEFI_HARDENING_OK,
        "refresh fits");
    expect_i32(
        ribon_uefi_memory_map_refresh_fits(8193u, 48u, 8192u, 256u),
        RIBON_UEFI_HARDENING_OUT_OF_CAPACITY,
        "raw capacity too small");
    expect_i32(
        ribon_uefi_memory_map_refresh_fits(4096u, 48u, 8192u, 8u),
        RIBON_UEFI_HARDENING_OUT_OF_CAPACITY,
        "region capacity too small");
    expect_i32(
        ribon_uefi_memory_map_refresh_fits(0u, 48u, 8192u, 8u),
        RIBON_UEFI_HARDENING_BAD_ARGUMENT,
        "bad refresh argument");
    expect_i32(
        ribon_uefi_memory_map_refresh_fits(UINT64_MAX - 1u, 4u, UINT64_MAX, UINT64_MAX),
        RIBON_UEFI_HARDENING_OVERFLOW,
        "refresh count overflow");
}

static void test_diagnostic_stage_names(void) {
    expect_string(
        ribon_uefi_diagnostic_stage_name(RIBON_UEFI_DIAG_EXIT_BOOT_SERVICES),
        "exit-boot-services",
        "exit stage name");
    expect_string(
        ribon_uefi_diagnostic_stage_name(RIBON_UEFI_DIAG_RPH1_REBUILD),
        "rph1-rebuild",
        "rph1 stage name");
    expect_string(
        ribon_uefi_diagnostic_stage_name((enum RibonUefiDiagnosticStage)99),
        "unknown",
        "unknown stage name");
}

int main(void) {
    test_memory_map_capacity_policy();
    test_refresh_fit_policy();
    test_diagnostic_stage_names();
    if (g_failures != 0) {
        return 1;
    }
    puts("RIBON-UEFI-HARDENING-TEST-OK");
    return 0;
}
