#include <Ribon/protocols/os/linux/fdt.h>

#include <stddef.h>

#define LINUX_FDT_MAGIC 0xd00dfeedu
#define LINUX_FDT_HEADER_SIZE 40u
#define LINUX_FDT_BEGIN_NODE 1u
#define LINUX_FDT_END_NODE 2u
#define LINUX_FDT_PROP 3u
#define LINUX_FDT_NOP 4u
#define LINUX_FDT_END 9u
#define LINUX_FDT_MAX_DEPTH 32u
#define LINUX_FDT_MIN_VERSION 17u
#define LINUX_FDT_INITRD_START_NAME "linux,initrd-start"
#define LINUX_FDT_INITRD_END_NAME "linux,initrd-end"

/** @brief Unaligned big-endian 32-bit FDT field를 읽는다. */
static uint32_t linux_fdt_read_be32(const unsigned char *bytes) {
    return ((uint32_t)bytes[0] << 24u) |
           ((uint32_t)bytes[1] << 16u) |
           ((uint32_t)bytes[2] << 8u) |
           (uint32_t)bytes[3];
}

/** @brief Unaligned big-endian 32-bit FDT field를 쓴다. */
static void linux_fdt_write_be32(unsigned char *bytes, uint32_t value) {
    bytes[0] = (unsigned char)(value >> 24u);
    bytes[1] = (unsigned char)(value >> 16u);
    bytes[2] = (unsigned char)(value >> 8u);
    bytes[3] = (unsigned char)value;
}

/** @brief Bounded NUL-terminated string 길이를 반환한다. */
static int linux_fdt_string_size(
    const unsigned char *bytes,
    uint32_t capacity,
    uint32_t *size) {
    if (bytes == 0 || size == 0) {
        return 0;
    }
    for (uint32_t index = 0u; index < capacity; ++index) {
        if (bytes[index] == 0u) {
            *size = index;
            return 1;
        }
    }
    return 0;
}

/** @brief Bounded field가 literal과 같은지 검사한다. */
static int linux_fdt_streq(
    const unsigned char *field,
    uint32_t field_size,
    const char *literal) {
    uint32_t index = 0u;
    while (literal[index] != '\0') {
        if (index >= field_size || field[index] != (unsigned char)literal[index]) {
            return 0;
        }
        ++index;
    }
    return index == field_size;
}

/** @brief 4-byte FDT alignment를 overflow 없이 계산한다. */
static int linux_fdt_align4(uint32_t value, uint32_t *aligned) {
    if (aligned == 0 || value > UINT32_MAX - 3u) {
        return 0;
    }
    *aligned = (value + 3u) & ~3u;
    return 1;
}

/** @brief 두 host range가 겹치는지 overflow 없이 검사한다. */
static int linux_fdt_ranges_overlap(
    const void *lhs,
    uint64_t lhs_size,
    const void *rhs,
    uint64_t rhs_size) {
    const uintptr_t lhs_base = (uintptr_t)lhs;
    const uintptr_t rhs_base = (uintptr_t)rhs;
    if (lhs_size > UINTPTR_MAX - lhs_base || rhs_size > UINTPTR_MAX - rhs_base) {
        return 1;
    }
    return lhs_base < rhs_base + (uintptr_t)rhs_size &&
           rhs_base < lhs_base + (uintptr_t)lhs_size;
}

/** @brief 한 initrd 주소를 root address-cell 폭으로 쓴다. */
static void linux_fdt_write_address(
    unsigned char *destination,
    uint32_t address_cells,
    uint64_t value) {
    if (address_cells == 2u) {
        linux_fdt_write_be32(destination, (uint32_t)(value >> 32u));
        destination += 4u;
    }
    linux_fdt_write_be32(destination, (uint32_t)value);
}

/** @brief 한 FDT property를 caller-owned structure block에 쓴다. */
static uint32_t linux_fdt_write_property(
    unsigned char *destination,
    uint32_t name_offset,
    uint32_t address_cells,
    uint64_t value) {
    const uint32_t value_size = address_cells * 4u;
    linux_fdt_write_be32(destination, LINUX_FDT_PROP);
    linux_fdt_write_be32(destination + 4u, value_size);
    linux_fdt_write_be32(destination + 8u, name_offset);
    linux_fdt_write_address(destination + 12u, address_cells, value);
    return 12u + value_size;
}

int ribon_linux_fdt_build(
    const void *source,
    uint64_t source_capacity,
    uint64_t initrd_start,
    uint64_t initrd_size,
    void *destination,
    uint64_t destination_capacity,
    uint64_t *output_size) {
    const unsigned char *input = source;
    unsigned char *output = destination;
    uint32_t total_size;
    uint32_t structure_offset;
    uint32_t strings_offset;
    uint32_t reserve_offset;
    uint32_t structure_size;
    uint32_t strings_size;
    uint32_t reserve_size;
    uint32_t cursor;
    uint32_t depth = 0u;
    uint32_t chosen_depth = UINT32_MAX;
    uint32_t chosen_insert = UINT32_MAX;
    uint32_t address_cells = 2u;
    uint32_t structure_end;
    uint32_t initrd_property_size = 0u;
    uint32_t new_structure_size;
    uint32_t new_strings_size;
    uint32_t new_reserve_offset = LINUX_FDT_HEADER_SIZE;
    uint32_t new_structure_offset;
    uint32_t new_strings_offset;
    uint32_t new_total_size;
    uint64_t initrd_end = 0u;
    int saw_end = 0;
    int saw_initrd_property = 0;

    if (source == 0 || destination == 0 || output_size == 0 ||
        source_capacity < LINUX_FDT_HEADER_SIZE ||
        destination_capacity < LINUX_FDT_HEADER_SIZE) {
        return 0;
    }
    *output_size = 0u;
    total_size = linux_fdt_read_be32(input + 4u);
    structure_offset = linux_fdt_read_be32(input + 8u);
    strings_offset = linux_fdt_read_be32(input + 12u);
    reserve_offset = linux_fdt_read_be32(input + 16u);
    strings_size = linux_fdt_read_be32(input + 32u);
    structure_size = linux_fdt_read_be32(input + 36u);
    if (linux_fdt_read_be32(input) != LINUX_FDT_MAGIC ||
        total_size < LINUX_FDT_HEADER_SIZE || total_size > source_capacity ||
        linux_fdt_read_be32(input + 20u) < LINUX_FDT_MIN_VERSION ||
        linux_fdt_read_be32(input + 24u) > LINUX_FDT_MIN_VERSION ||
        reserve_offset < LINUX_FDT_HEADER_SIZE || reserve_offset > total_size ||
        structure_offset > total_size || structure_size > total_size - structure_offset ||
        strings_offset > total_size || strings_size > total_size - strings_offset ||
        linux_fdt_ranges_overlap(source, total_size, destination, destination_capacity)) {
        return 0;
    }
    if (initrd_size != 0u) {
        if (initrd_start == 0u || initrd_start > UINT64_MAX - initrd_size) {
            return 0;
        }
        initrd_end = initrd_start + initrd_size;
    }

    /* Reservation block는 16-byte terminator pair까지 exact scan한다. */
    cursor = reserve_offset;
    for (;;) {
        uint64_t address;
        uint64_t size;
        if (cursor > total_size || total_size - cursor < 16u) {
            return 0;
        }
        address = ((uint64_t)linux_fdt_read_be32(input + cursor) << 32u) |
                  linux_fdt_read_be32(input + cursor + 4u);
        size = ((uint64_t)linux_fdt_read_be32(input + cursor + 8u) << 32u) |
               linux_fdt_read_be32(input + cursor + 12u);
        cursor += 16u;
        if (address == 0u && size == 0u) {
            break;
        }
    }
    reserve_size = cursor - reserve_offset;

    structure_end = structure_offset + structure_size;
    cursor = structure_offset;
    while (cursor + 4u <= structure_end) {
        const uint32_t token_offset = cursor;
        const uint32_t token = linux_fdt_read_be32(input + cursor);
        cursor += 4u;
        if (token == LINUX_FDT_BEGIN_NODE) {
            uint32_t name_size;
            uint32_t aligned;
            if (depth >= LINUX_FDT_MAX_DEPTH ||
                !linux_fdt_string_size(input + cursor, structure_end - cursor, &name_size) ||
                !linux_fdt_align4(name_size + 1u, &aligned) ||
                aligned > structure_end - cursor) {
                return 0;
            }
            ++depth;
            if (depth == 2u && linux_fdt_streq(input + cursor, name_size, "chosen")) {
                chosen_depth = depth;
            }
            cursor += aligned;
            continue;
        }
        if (token == LINUX_FDT_END_NODE) {
            if (depth == 0u) {
                return 0;
            }
            if (depth == chosen_depth) {
                chosen_insert = token_offset - structure_offset;
                chosen_depth = UINT32_MAX;
            }
            --depth;
            continue;
        }
        if (token == LINUX_FDT_PROP) {
            uint32_t value_size;
            uint32_t name_offset;
            uint32_t name_size;
            uint32_t aligned;
            if (cursor + 8u > structure_end) {
                return 0;
            }
            value_size = linux_fdt_read_be32(input + cursor);
            name_offset = linux_fdt_read_be32(input + cursor + 4u);
            cursor += 8u;
            if (name_offset >= strings_size ||
                !linux_fdt_string_size(
                    input + strings_offset + name_offset,
                    strings_size - name_offset,
                    &name_size) ||
                !linux_fdt_align4(value_size, &aligned) || aligned > structure_end - cursor) {
                return 0;
            }
            if (depth == 1u && value_size == 4u &&
                linux_fdt_streq(
                    input + strings_offset + name_offset,
                    name_size,
                    "#address-cells")) {
                address_cells = linux_fdt_read_be32(input + cursor);
            }
            if (depth == chosen_depth &&
                (linux_fdt_streq(
                     input + strings_offset + name_offset,
                     name_size,
                     LINUX_FDT_INITRD_START_NAME) ||
                 linux_fdt_streq(
                     input + strings_offset + name_offset,
                     name_size,
                     LINUX_FDT_INITRD_END_NAME))) {
                saw_initrd_property = 1;
            }
            cursor += aligned;
            continue;
        }
        if (token == LINUX_FDT_NOP) {
            continue;
        }
        if (token == LINUX_FDT_END) {
            saw_end = 1;
            break;
        }
        return 0;
    }
    if (!saw_end || depth != 0u || chosen_insert == UINT32_MAX ||
        address_cells < RIBON_LINUX_FDT_MIN_ADDRESS_CELLS ||
        address_cells > RIBON_LINUX_FDT_MAX_ADDRESS_CELLS ||
        (address_cells == 1u && initrd_size != 0u &&
         (initrd_start > UINT32_MAX || initrd_end > UINT32_MAX)) ||
        (initrd_size != 0u && saw_initrd_property)) {
        return 0;
    }
    if (initrd_size != 0u) {
        initrd_property_size = 2u * (12u + address_cells * 4u);
    }
    if (structure_size > UINT32_MAX - initrd_property_size ||
        strings_size > UINT32_MAX -
            (sizeof(LINUX_FDT_INITRD_START_NAME) + sizeof(LINUX_FDT_INITRD_END_NAME))) {
        return 0;
    }
    new_structure_size = structure_size + initrd_property_size;
    new_strings_size = strings_size + (initrd_size != 0u ?
        (uint32_t)(sizeof(LINUX_FDT_INITRD_START_NAME) +
                   sizeof(LINUX_FDT_INITRD_END_NAME)) : 0u);
    new_structure_offset = new_reserve_offset + reserve_size;
    if (new_structure_offset > UINT32_MAX - new_structure_size) {
        return 0;
    }
    new_strings_offset = new_structure_offset + new_structure_size;
    if (new_strings_offset > UINT32_MAX - new_strings_size) {
        return 0;
    }
    new_total_size = new_strings_offset + new_strings_size;
    if (new_total_size > destination_capacity) {
        return 0;
    }

    for (uint32_t index = 0u; index < LINUX_FDT_HEADER_SIZE; ++index) {
        output[index] = input[index];
    }
    linux_fdt_write_be32(output + 4u, new_total_size);
    linux_fdt_write_be32(output + 8u, new_structure_offset);
    linux_fdt_write_be32(output + 12u, new_strings_offset);
    linux_fdt_write_be32(output + 16u, new_reserve_offset);
    linux_fdt_write_be32(output + 32u, new_strings_size);
    linux_fdt_write_be32(output + 36u, new_structure_size);
    for (uint32_t index = 0u; index < reserve_size; ++index) {
        output[new_reserve_offset + index] = input[reserve_offset + index];
    }
    for (uint32_t index = 0u; index < chosen_insert; ++index) {
        output[new_structure_offset + index] = input[structure_offset + index];
    }
    cursor = new_structure_offset + chosen_insert;
    if (initrd_size != 0u) {
        cursor += linux_fdt_write_property(
            output + cursor, strings_size, address_cells, initrd_start);
        cursor += linux_fdt_write_property(
            output + cursor,
            strings_size + (uint32_t)sizeof(LINUX_FDT_INITRD_START_NAME),
            address_cells,
            initrd_end);
    }
    for (uint32_t index = chosen_insert; index < structure_size; ++index) {
        output[cursor++] = input[structure_offset + index];
    }
    for (uint32_t index = 0u; index < strings_size; ++index) {
        output[new_strings_offset + index] = input[strings_offset + index];
    }
    cursor = new_strings_offset + strings_size;
    if (initrd_size != 0u) {
        for (uint32_t index = 0u; index < sizeof(LINUX_FDT_INITRD_START_NAME); ++index) {
            output[cursor++] = (unsigned char)LINUX_FDT_INITRD_START_NAME[index];
        }
        for (uint32_t index = 0u; index < sizeof(LINUX_FDT_INITRD_END_NAME); ++index) {
            output[cursor++] = (unsigned char)LINUX_FDT_INITRD_END_NAME[index];
        }
    }
    *output_size = new_total_size;
    return 1;
}
