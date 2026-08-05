#include "fdt.h"

#include <stddef.h>

#define RIBON_FDT_MAGIC 0xd00dfeedu
#define RIBON_FDT_HEADER_SIZE 40u
#define RIBON_FDT_BEGIN_NODE 1u
#define RIBON_FDT_END_NODE 2u
#define RIBON_FDT_PROP 3u
#define RIBON_FDT_NOP 4u
#define RIBON_FDT_END 9u
#define RIBON_FDT_MAX_DEPTH 32u

/** @brief Big-endian 32-bit FDT field를 읽는다. */
static uint32_t fdt_be32(const unsigned char *bytes) {
    return ((uint32_t)bytes[0] << 24u) |
           ((uint32_t)bytes[1] << 16u) |
           ((uint32_t)bytes[2] << 8u) |
           (uint32_t)bytes[3];
}

/** @brief Big-endian 64-bit FDT reserve-map field를 읽는다. */
static uint64_t fdt_be64(const unsigned char *bytes) {
    return ((uint64_t)fdt_be32(bytes) << 32u) | fdt_be32(bytes + 4u);
}

/** @brief FDT cell 하나 또는 둘을 64-bit 값으로 결합한다. */
static int fdt_cells(
    const unsigned char *bytes,
    uint32_t available,
    uint32_t cells,
    uint64_t *out) {
    if (bytes == 0 || out == 0 || (cells != 1u && cells != 2u) ||
        available < cells * 4u) {
        return 0;
    }
    if (cells == 1u) {
        *out = fdt_be32(bytes);
    } else {
        *out = ((uint64_t)fdt_be32(bytes) << 32u) | fdt_be32(bytes + 4u);
    }
    return 1;
}

/** @brief NUL-terminated field가 bounded range 안에서 끝나는 길이를 반환한다. */
static int fdt_string_length(
    const unsigned char *bytes,
    uint32_t capacity,
    uint32_t *length_out) {
    if (bytes == 0 || length_out == 0) {
        return 0;
    }
    for (uint32_t index = 0u; index < capacity; ++index) {
        if (bytes[index] == 0u) {
            *length_out = index;
            return 1;
        }
    }
    return 0;
}

/** @brief ASCII field와 literal이 정확히 일치하는지 검사한다. */
static int fdt_streq(const char *field, uint32_t field_size, const char *literal) {
    uint32_t index = 0u;
    if (field == 0 || literal == 0) {
        return 0;
    }
    while (literal[index] != '\0') {
        if (index >= field_size || field[index] != literal[index]) {
            return 0;
        }
        ++index;
    }
    return index == field_size;
}

/** @brief Node name이 memory 또는 memory@ unit-address인지 검사한다. */
static int fdt_is_memory_node(const char *name, uint32_t size) {
    static const char prefix[] = "memory";
    if (name == 0 || size < sizeof(prefix) - 1u) {
        return 0;
    }
    for (uint32_t index = 0u; index < sizeof(prefix) - 1u; ++index) {
        if (name[index] != prefix[index]) {
            return 0;
        }
    }
    return size == sizeof(prefix) - 1u || name[sizeof(prefix) - 1u] == '@';
}

/** @brief 4-byte token alignment을 overflow 없이 계산한다. */
static int fdt_align4(uint32_t value, uint32_t *out) {
    if (out == 0 || value > UINT32_MAX - 3u) {
        return 0;
    }
    *out = (value + 3u) & ~3u;
    return 1;
}

/** @brief Borrowed FDT blob에서 bounded platform fact를 추출한다. */
int ribon_fdt_parse(const void *blob, uint64_t capacity, struct RibonFdtFacts *out) {
    const unsigned char *bytes = (const unsigned char *)blob;
    uint32_t total_size;
    uint32_t structure_offset;
    uint32_t strings_offset;
    uint32_t reserve_offset;
    uint32_t strings_size;
    uint32_t structure_size;
    uint32_t cursor;
    uint32_t structure_end;
    uint32_t depth = 0u;
    uint32_t chosen_depth = UINT32_MAX;
    uint32_t memory_depth = UINT32_MAX;
    uint32_t reserved_memory_depth = UINT32_MAX;
    uint32_t reserved_child_depth = UINT32_MAX;
    struct RibonFdtReservation
        reserved_child_reservations[RIBON_FDT_RESERVATION_CAPACITY];
    uint32_t reserved_child_reservation_count = 0u;
    uint32_t address_cells = 2u;
    uint32_t size_cells = 2u;
    uint32_t reserved_address_cells = 2u;
    uint32_t reserved_size_cells = 2u;
    int reserved_child_has_zero_size = 0;
    int reserved_child_disabled = 0;
    int saw_end = 0;

    if (bytes == 0 || out == 0 || capacity < RIBON_FDT_HEADER_SIZE) {
        return RIBON_FDT_STATUS_BAD_ARGUMENT;
    }
    *out = (struct RibonFdtFacts){0};
    total_size = fdt_be32(bytes + 4u);
    structure_offset = fdt_be32(bytes + 8u);
    strings_offset = fdt_be32(bytes + 12u);
    reserve_offset = fdt_be32(bytes + 16u);
    strings_size = fdt_be32(bytes + 32u);
    structure_size = fdt_be32(bytes + 36u);
    if (fdt_be32(bytes) != RIBON_FDT_MAGIC ||
        total_size < RIBON_FDT_HEADER_SIZE ||
        total_size > capacity ||
        fdt_be32(bytes + 20u) < 17u ||
        fdt_be32(bytes + 24u) > 17u ||
        reserve_offset < RIBON_FDT_HEADER_SIZE ||
        reserve_offset > total_size ||
        structure_offset > total_size ||
        structure_size > total_size - structure_offset ||
        strings_offset > total_size ||
        strings_size > total_size - strings_offset) {
        return RIBON_FDT_STATUS_BAD_HEADER;
    }
    cursor = reserve_offset;
    for (;;) {
        uint64_t base;
        uint64_t size;
        if (cursor > total_size || total_size - cursor < 16u) {
            return RIBON_FDT_STATUS_TRUNCATED;
        }
        base = fdt_be64(bytes + cursor);
        size = fdt_be64(bytes + cursor + 8u);
        cursor += 16u;
        if (base == 0u && size == 0u) {
            break;
        }
        if (size == 0u || base > UINT64_MAX - size ||
            out->reservation_count >= RIBON_FDT_RESERVATION_CAPACITY) {
            return RIBON_FDT_STATUS_BAD_STRUCTURE;
        }
        out->reservations[out->reservation_count++] =
            (struct RibonFdtReservation){.base = base, .size = size};
    }
    structure_end = structure_offset + structure_size;
    cursor = structure_offset;
    while (cursor + 4u <= structure_end) {
        const uint32_t token = fdt_be32(bytes + cursor);
        cursor += 4u;
        if (token == RIBON_FDT_BEGIN_NODE) {
            uint32_t name_size;
            uint32_t aligned_name;
            if (depth >= RIBON_FDT_MAX_DEPTH ||
                !fdt_string_length(
                    bytes + cursor,
                    structure_end - cursor,
                    &name_size) ||
                !fdt_align4(name_size + 1u, &aligned_name) ||
                aligned_name > structure_end - cursor) {
                return RIBON_FDT_STATUS_BAD_STRUCTURE;
            }
            ++depth;
            if (depth == 2u &&
                fdt_streq((const char *)bytes + cursor, name_size, "chosen")) {
                chosen_depth = depth;
            }
            if (memory_depth == UINT32_MAX &&
                fdt_is_memory_node((const char *)bytes + cursor, name_size)) {
                memory_depth = depth;
            }
            if (depth == 2u &&
                fdt_streq(
                    (const char *)bytes + cursor,
                    name_size,
                    "reserved-memory")) {
                reserved_memory_depth = depth;
                reserved_address_cells = address_cells;
                reserved_size_cells = size_cells;
            } else if (reserved_memory_depth != UINT32_MAX &&
                       depth == reserved_memory_depth + 1u) {
                reserved_child_depth = depth;
                reserved_child_reservation_count = 0u;
                reserved_child_has_zero_size = 0;
                reserved_child_disabled = 0;
            }
            cursor += aligned_name;
            continue;
        }
        if (token == RIBON_FDT_END_NODE) {
            if (depth == 0u) {
                return RIBON_FDT_STATUS_BAD_STRUCTURE;
            }
            if (chosen_depth == depth) {
                chosen_depth = UINT32_MAX;
            }
            if (memory_depth == depth) {
                memory_depth = UINT32_MAX;
            }
            if (reserved_child_depth == depth) {
                if (!reserved_child_disabled) {
                    if (reserved_child_has_zero_size ||
                        reserved_child_reservation_count >
                            RIBON_FDT_RESERVATION_CAPACITY -
                                out->reservation_count) {
                        return RIBON_FDT_STATUS_BAD_STRUCTURE;
                    }
                    for (uint32_t index = 0u;
                         index < reserved_child_reservation_count;
                         ++index) {
                        out->reservations[out->reservation_count++] =
                            reserved_child_reservations[index];
                    }
                }
                reserved_child_depth = UINT32_MAX;
                reserved_child_reservation_count = 0u;
                reserved_child_has_zero_size = 0;
                reserved_child_disabled = 0;
            }
            if (reserved_memory_depth == depth) {
                reserved_memory_depth = UINT32_MAX;
            }
            --depth;
            continue;
        }
        if (token == RIBON_FDT_PROP) {
            uint32_t value_size;
            uint32_t name_offset;
            uint32_t property_name_size;
            uint32_t aligned_value;
            const char *property_name;
            const unsigned char *value;
            if (cursor + 8u > structure_end) {
                return RIBON_FDT_STATUS_TRUNCATED;
            }
            value_size = fdt_be32(bytes + cursor);
            name_offset = fdt_be32(bytes + cursor + 4u);
            cursor += 8u;
            if (name_offset >= strings_size ||
                !fdt_string_length(
                    bytes + strings_offset + name_offset,
                    strings_size - name_offset,
                    &property_name_size) ||
                !fdt_align4(value_size, &aligned_value) ||
                aligned_value > structure_end - cursor) {
                return RIBON_FDT_STATUS_BAD_STRUCTURE;
            }
            property_name =
                (const char *)bytes + strings_offset + name_offset;
            value = bytes + cursor;
            if (depth == 1u && value_size == 4u &&
                fdt_streq(property_name, property_name_size, "#address-cells")) {
                address_cells = fdt_be32(value);
            } else if (depth == 1u && value_size == 4u &&
                       fdt_streq(property_name, property_name_size, "#size-cells")) {
                size_cells = fdt_be32(value);
            } else if (chosen_depth == depth &&
                       fdt_streq(property_name, property_name_size, "bootargs")) {
                uint32_t text_size;
                if (!fdt_string_length(value, value_size, &text_size)) {
                    return RIBON_FDT_STATUS_BAD_STRUCTURE;
                }
                out->boot_arguments = (const char *)value;
                out->boot_arguments_size = text_size;
            } else if (memory_depth == depth &&
                       fdt_streq(property_name, property_name_size, "reg")) {
                const uint32_t required = (address_cells + size_cells) * 4u;
                if (required > value_size ||
                    !fdt_cells(value, value_size, address_cells, &out->memory_base) ||
                    !fdt_cells(
                        value + address_cells * 4u,
                        value_size - address_cells * 4u,
                        size_cells,
                        &out->memory_size)) {
                    return RIBON_FDT_STATUS_BAD_STRUCTURE;
                }
            } else if (reserved_memory_depth == depth && value_size == 4u &&
                       fdt_streq(
                           property_name,
                           property_name_size,
                           "#address-cells")) {
                reserved_address_cells = fdt_be32(value);
            } else if (reserved_memory_depth == depth && value_size == 4u &&
                       fdt_streq(
                           property_name,
                           property_name_size,
                           "#size-cells")) {
                reserved_size_cells = fdt_be32(value);
            } else if (reserved_child_depth == depth &&
                       fdt_streq(property_name, property_name_size, "reg")) {
                const uint32_t tuple_cells =
                    reserved_address_cells + reserved_size_cells;
                uint32_t tuple_size;
                if ((reserved_address_cells != 1u &&
                     reserved_address_cells != 2u) ||
                    (reserved_size_cells != 1u &&
                     reserved_size_cells != 2u) ||
                    tuple_cells > UINT32_MAX / 4u) {
                    return RIBON_FDT_STATUS_BAD_STRUCTURE;
                }
                tuple_size = tuple_cells * 4u;
                if (value_size == 0u || value_size % tuple_size != 0u) {
                    return RIBON_FDT_STATUS_BAD_STRUCTURE;
                }
                for (uint32_t offset = 0u;
                     offset < value_size;
                     offset += tuple_size) {
                    uint64_t base;
                    uint64_t size;
                    if (!fdt_cells(
                            value + offset,
                            value_size - offset,
                            reserved_address_cells,
                            &base) ||
                        !fdt_cells(
                            value + offset + reserved_address_cells * 4u,
                            value_size - offset - reserved_address_cells * 4u,
                            reserved_size_cells,
                            &size) ||
                        base > UINT64_MAX - size) {
                        return RIBON_FDT_STATUS_BAD_STRUCTURE;
                    }
                    if (size == 0u) {
                        reserved_child_has_zero_size = 1;
                        continue;
                    }
                    if (reserved_child_reservation_count >=
                        RIBON_FDT_RESERVATION_CAPACITY) {
                        return RIBON_FDT_STATUS_BAD_STRUCTURE;
                    }
                    reserved_child_reservations[
                        reserved_child_reservation_count++] =
                        (struct RibonFdtReservation){
                            .base = base,
                            .size = size,
                        };
                }
            } else if (reserved_child_depth == depth &&
                       fdt_streq(property_name, property_name_size, "status")) {
                uint32_t text_size;
                if (!fdt_string_length(value, value_size, &text_size)) {
                    return RIBON_FDT_STATUS_BAD_STRUCTURE;
                }
                reserved_child_disabled =
                    fdt_streq((const char *)value, text_size, "disabled");
            }
            cursor += aligned_value;
            continue;
        }
        if (token == RIBON_FDT_NOP) {
            continue;
        }
        if (token == RIBON_FDT_END) {
            saw_end = 1;
            break;
        }
        return RIBON_FDT_STATUS_BAD_STRUCTURE;
    }
    if (!saw_end || depth != 0u) {
        return RIBON_FDT_STATUS_BAD_STRUCTURE;
    }
    if (out->memory_size == 0u) {
        return RIBON_FDT_STATUS_MISSING_MEMORY;
    }
    out->total_size = total_size;
    return RIBON_FDT_STATUS_OK;
}
