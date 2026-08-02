#include "../../src/environments/raw-fdt/raw_fdt.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BEGIN_NODE 1u
#define END_NODE 2u
#define PROP 3u
#define END 9u
#define TEST_PAGE_SIZE 4096u
#define TEST_MEMORY_PAGES 24u

static _Alignas(TEST_PAGE_SIZE) unsigned char
    physical_memory[TEST_PAGE_SIZE * TEST_MEMORY_PAGES];

/** @brief 이 테스트는 environment plugin validation이 아니라 capture만 검사한다. */
int ribon_environment_plugin_operations_are_valid(
    const struct RibonPluginDescriptor *descriptor) {
    (void)descriptor;
    return 1;
}

static void put_be32(unsigned char *bytes, uint32_t offset, uint32_t value) {
    bytes[offset] = (unsigned char)(value >> 24u);
    bytes[offset + 1u] = (unsigned char)(value >> 16u);
    bytes[offset + 2u] = (unsigned char)(value >> 8u);
    bytes[offset + 3u] = (unsigned char)value;
}

static void put_be64(unsigned char *bytes, uint32_t offset, uint64_t value) {
    put_be32(bytes, offset, (uint32_t)(value >> 32u));
    put_be32(bytes, offset + 4u, (uint32_t)value);
}

static void append_u32(unsigned char *bytes, uint32_t *cursor, uint32_t value) {
    put_be32(bytes, *cursor, value);
    *cursor += 4u;
}

static void append_node(
    unsigned char *bytes,
    uint32_t *cursor,
    const char *name) {
    const uint32_t size = (uint32_t)strlen(name) + 1u;
    append_u32(bytes, cursor, BEGIN_NODE);
    memcpy(bytes + *cursor, name, size);
    *cursor += (size + 3u) & ~3u;
}

static void append_property(
    unsigned char *bytes,
    uint32_t *cursor,
    uint32_t name_offset,
    const void *value,
    uint32_t size) {
    append_u32(bytes, cursor, PROP);
    append_u32(bytes, cursor, size);
    append_u32(bytes, cursor, name_offset);
    memcpy(bytes + *cursor, value, size);
    *cursor += (size + 3u) & ~3u;
}

/** @brief Host pointer를 담는 two-cell memory FDT를 만든다. */
static uint32_t make_fdt(unsigned char *bytes, uint64_t base, uint64_t size) {
    static const char strings[] = "#address-cells\0#size-cells\0reg\0";
    const uint32_t reserve_offset = 40u;
    const uint32_t structure_offset =
        reserve_offset + (RIBON_RAW_FDT_MAX_FIRMWARE_RESERVATIONS + 1u) * 16u;
    unsigned char two[4] = {0u, 0u, 0u, 2u};
    unsigned char cells[16];
    uint32_t cursor = structure_offset;
    uint32_t structure_size;
    uint32_t strings_offset;
    uint32_t total_size;

    memset(bytes, 0, TEST_PAGE_SIZE);
    for (uint32_t index = 0u;
         index < RIBON_RAW_FDT_MAX_FIRMWARE_RESERVATIONS;
         ++index) {
        const uint32_t offset = reserve_offset + index * 16u;
        put_be64(
            bytes,
            offset,
            base + (uint64_t)(2u + 2u * index) * TEST_PAGE_SIZE);
        put_be64(bytes, offset + 8u, 128u);
    }
    append_node(bytes, &cursor, "");
    append_property(bytes, &cursor, 0u, two, sizeof(two));
    append_property(bytes, &cursor, 15u, two, sizeof(two));
    append_node(bytes, &cursor, "memory");
    put_be32(cells, 0u, (uint32_t)(base >> 32u));
    put_be32(cells, 4u, (uint32_t)base);
    put_be32(cells, 8u, (uint32_t)(size >> 32u));
    put_be32(cells, 12u, (uint32_t)size);
    append_property(bytes, &cursor, 27u, cells, sizeof(cells));
    append_u32(bytes, &cursor, END_NODE);
    append_u32(bytes, &cursor, END_NODE);
    append_u32(bytes, &cursor, END);
    structure_size = cursor - structure_offset;
    strings_offset = cursor;
    memcpy(bytes + strings_offset, strings, sizeof(strings));
    total_size = strings_offset + (uint32_t)sizeof(strings);

    put_be32(bytes, 0u, 0xd00dfeedu);
    put_be32(bytes, 4u, total_size);
    put_be32(bytes, 8u, structure_offset);
    put_be32(bytes, 12u, strings_offset);
    put_be32(bytes, 16u, reserve_offset);
    put_be32(bytes, 20u, 17u);
    put_be32(bytes, 24u, 16u);
    put_be32(bytes, 28u, 0u);
    put_be32(bytes, 32u, (uint32_t)sizeof(strings));
    put_be32(bytes, 36u, structure_size);
    return total_size;
}

/** @brief target 10개, FDT reserve 8개와 blob이 정확히 39 regions를 만든다. */
int main(void) {
    const uint64_t memory_base = (uint64_t)(uintptr_t)physical_memory;
    const uint64_t memory_size = sizeof(physical_memory);
    unsigned char *const fdt = physical_memory + TEST_PAGE_SIZE;
    const unsigned char payload = 0xa5u;
    const struct RibonArchOps arch_ops = {0};
    struct RibonRawFdtReservation
        reservations[RIBON_RAW_FDT_MAX_TARGET_RESERVATIONS];
    struct RibonMemoryRegion exact_regions[RIBON_RAW_FDT_MAX_MEMORY_REGIONS];
    struct RibonMemoryRegion short_regions[RIBON_RAW_FDT_MAX_MEMORY_REGIONS - 1u];
    struct RibonBootEnvironment environment;
    struct RibonRawFdtEntry entry;
    uint32_t boot_modules = 0u;
    uint32_t firmware = 0u;
    uint32_t usable = 0u;
    const uint32_t fdt_size = make_fdt(fdt, memory_base, memory_size);

    for (uint32_t index = 0u;
         index < RIBON_RAW_FDT_MAX_TARGET_RESERVATIONS;
         ++index) {
        reservations[index] = (struct RibonRawFdtReservation){
            .base = memory_base + (uint64_t)(3u + 2u * index) * TEST_PAGE_SIZE,
            .size = 128u,
            .kind = index == 0u ? RIBON_MEMORY_REGION_BOOTLOADER :
                index == 1u ? RIBON_MEMORY_REGION_KERNEL_IMAGE :
                RIBON_MEMORY_REGION_BOOT_MODULE,
        };
    }
    entry = (struct RibonRawFdtEntry){
        .fdt = fdt,
        .fdt_capacity = fdt_size,
        .architecture = RIBON_ARCHITECTURE_AARCH64,
        .arch_ops = &arch_ops,
        .timer_frequency_hz = 1000000u,
        .payload = &payload,
        .payload_size = sizeof(payload),
        .payload_name = "fixture",
        .reservations = reservations,
        .reservation_count = RIBON_RAW_FDT_MAX_TARGET_RESERVATIONS,
        .memory_regions = exact_regions,
        .memory_region_capacity = RIBON_RAW_FDT_MAX_MEMORY_REGIONS,
    };
    if (ribon_raw_fdt_environment_capture(&entry, &environment) !=
            RIBON_RAW_FDT_STATUS_OK ||
        environment.memory_map.region_count != RIBON_RAW_FDT_MAX_MEMORY_REGIONS) {
        fputs("raw_fdt_capacity_tests: exact 23-region closure failed\n", stderr);
        return 1;
    }
    for (uint32_t index = 0u;
         index < environment.memory_map.region_count;
         ++index) {
        usable += environment.memory_map.regions[index].kind ==
            RIBON_MEMORY_REGION_USABLE ? 1u : 0u;
        boot_modules += environment.memory_map.regions[index].kind ==
            RIBON_MEMORY_REGION_BOOT_MODULE ? 1u : 0u;
        firmware += environment.memory_map.regions[index].kind ==
            RIBON_MEMORY_REGION_FIRMWARE ? 1u : 0u;
    }
    if (usable != 20u || boot_modules != RIBON_BOOT_MODULE_CAPACITY ||
        firmware != RIBON_RAW_FDT_MAX_FIRMWARE_RESERVATIONS + 1u) {
        fputs("raw_fdt_capacity_tests: worst-case kinds are not exact\n", stderr);
        return 1;
    }

    entry.memory_regions = short_regions;
    entry.memory_region_capacity = RIBON_RAW_FDT_MAX_MEMORY_REGIONS - 1u;
    if (ribon_raw_fdt_environment_capture(&entry, &environment) !=
            RIBON_RAW_FDT_STATUS_OUT_OF_CAPACITY) {
        fputs("raw_fdt_capacity_tests: 22-region storage was accepted\n", stderr);
        return 1;
    }
    entry.memory_regions = exact_regions;
    entry.memory_region_capacity = RIBON_RAW_FDT_MAX_MEMORY_REGIONS;
    entry.reservation_count = RIBON_RAW_FDT_MAX_TARGET_RESERVATIONS + 1u;
    if (ribon_raw_fdt_environment_capture(&entry, &environment) !=
            RIBON_RAW_FDT_STATUS_BAD_RESERVATION) {
        fputs("raw_fdt_capacity_tests: eleventh target reservation was accepted\n", stderr);
        return 1;
    }
    puts("RIBON-RAW-FDT-CAPACITY-OK regions=39 modules=8 firmware=9");
    return 0;
}
