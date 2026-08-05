#include "../../src/common/sys/fdt/fdt.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BEGIN_NODE 1u
#define END_NODE 2u
#define PROP 3u
#define END 9u

static void put_be32(unsigned char *bytes, uint32_t offset, uint32_t value) {
    bytes[offset] = (unsigned char)(value >> 24u);
    bytes[offset + 1u] = (unsigned char)(value >> 16u);
    bytes[offset + 2u] = (unsigned char)(value >> 8u);
    bytes[offset + 3u] = (unsigned char)value;
}

static void append_u32(unsigned char *bytes, uint32_t *cursor, uint32_t value) {
    put_be32(bytes, *cursor, value);
    *cursor += 4u;
}

static void append_node(unsigned char *bytes, uint32_t *cursor, const char *name) {
    uint32_t size = (uint32_t)strlen(name) + 1u;
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

static uint32_t make_fdt(
    unsigned char bytes[512],
    int disabled_zero_reservation) {
    static const char strings[] =
        "#address-cells\0#size-cells\0reg\0bootargs\0status\0";
    const uint32_t address_name = 0u;
    const uint32_t size_name = 15u;
    const uint32_t reg_name = 27u;
    const uint32_t bootargs_name = 31u;
    const uint32_t status_name = 40u;
    const uint32_t structure_offset = 56u;
    uint32_t cursor = structure_offset;
    unsigned char cells[16];
    unsigned char two[4] = {0u, 0u, 0u, 2u};
    static const char arguments[] = "console=ttyAMA0";
    static const char disabled[] = "disabled";
    uint32_t structure_size;
    uint32_t strings_offset;
    uint32_t total_size;

    memset(bytes, 0, 512u);
    append_node(bytes, &cursor, "");
    append_property(bytes, &cursor, address_name, two, sizeof(two));
    append_property(bytes, &cursor, size_name, two, sizeof(two));
    append_node(bytes, &cursor, "memory@40000000");
    put_be32(cells, 0u, 0u);
    put_be32(cells, 4u, 0x40000000u);
    put_be32(cells, 8u, 0u);
    put_be32(cells, 12u, 0x10000000u);
    append_property(bytes, &cursor, reg_name, cells, sizeof(cells));
    append_u32(bytes, &cursor, END_NODE);
    append_node(bytes, &cursor, "reserved-memory");
    append_property(bytes, &cursor, address_name, two, sizeof(two));
    append_property(bytes, &cursor, size_name, two, sizeof(two));
    append_node(bytes, &cursor, "resident@40010000");
    put_be32(cells, 0u, 0u);
    put_be32(cells, 4u, 0x40010000u);
    put_be32(cells, 8u, 0u);
    put_be32(cells, 12u, 0x00010000u);
    append_property(bytes, &cursor, reg_name, cells, sizeof(cells));
    append_u32(bytes, &cursor, END_NODE);
    append_node(bytes, &cursor, "nvram@0");
    memset(cells, 0, sizeof(cells));
    append_property(bytes, &cursor, reg_name, cells, sizeof(cells));
    if (disabled_zero_reservation) {
        append_property(
            bytes,
            &cursor,
            status_name,
            disabled,
            (uint32_t)sizeof(disabled));
    }
    append_u32(bytes, &cursor, END_NODE);
    append_u32(bytes, &cursor, END_NODE);
    append_node(bytes, &cursor, "chosen");
    append_property(
        bytes,
        &cursor,
        bootargs_name,
        arguments,
        (uint32_t)sizeof(arguments));
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
    put_be32(bytes, 16u, 40u);
    put_be32(bytes, 20u, 17u);
    put_be32(bytes, 24u, 16u);
    put_be32(bytes, 28u, 0u);
    put_be32(bytes, 32u, (uint32_t)sizeof(strings));
    put_be32(bytes, 36u, structure_size);
    return total_size;
}

int main(void) {
    unsigned char blob[512];
    struct RibonFdtFacts facts;
    uint32_t size = make_fdt(blob, 1);
    if (ribon_fdt_parse(blob, size, &facts) != RIBON_FDT_STATUS_OK ||
        facts.total_size != size ||
        facts.memory_base != 0x40000000u ||
        facts.memory_size != 0x10000000u ||
        facts.reservation_count != 1u ||
        facts.reservations[0].base != 0x40010000u ||
        facts.reservations[0].size != 0x00010000u ||
        facts.boot_arguments_size != 15u ||
        memcmp(facts.boot_arguments, "console=ttyAMA0", 15u) != 0) {
        fputs("fdt_parser_tests: valid fixture rejected\n", stderr);
        return 1;
    }
    size = make_fdt(blob, 0);
    if (ribon_fdt_parse(blob, size, &facts) !=
        RIBON_FDT_STATUS_BAD_STRUCTURE) {
        fputs("fdt_parser_tests: active zero reservation accepted\n", stderr);
        return 1;
    }
    size = make_fdt(blob, 1);
    put_be32(blob, 4u, size + 1u);
    if (ribon_fdt_parse(blob, size, &facts) != RIBON_FDT_STATUS_BAD_HEADER) {
        fputs("fdt_parser_tests: oversized blob accepted\n", stderr);
        return 1;
    }
    puts("RIBON-FDT-PARSER-TESTS-OK");
    return 0;
}
