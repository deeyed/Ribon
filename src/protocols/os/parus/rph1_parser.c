#include <Ribon/protocols/os/parus/rph1.h>

#include <stddef.h>

/** @brief Little-endian 16-bit field를 읽는다. */
static uint16_t rph1_read_u16(const unsigned char *bytes, uint64_t offset) {
    return (uint16_t)bytes[offset] | ((uint16_t)bytes[offset + 1u] << 8u);
}

/** @brief Little-endian 32-bit field를 읽는다. */
static uint32_t rph1_read_u32(const unsigned char *bytes, uint64_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

/** @brief Little-endian 64-bit field를 읽는다. */
static uint64_t rph1_read_u64(const unsigned char *bytes, uint64_t offset) {
    uint64_t value = 0u;
    for (uint32_t index = 0; index < 8u; ++index) {
        value |= (uint64_t)bytes[offset + index] << (index * 8u);
    }
    return value;
}

/** @brief 값이 0이 아닌 2의 거듭제곱인지 반환한다. */
static int rph1_is_power_of_two(uint32_t value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}

/** @brief Section type이 RPH1 v1 registry에 속하는지 반환한다. */
static int rph1_is_known_section(uint32_t type) {
    return type >= RIBON_PARUS_RPH1_SECTION_MEMORY_MAP &&
           type <= RIBON_PARUS_RPH1_SECTION_OVERSEER;
}

static int rph1_payload_shape_is_valid(
    uint32_t type,
    const unsigned char *payload,
    uint64_t length) {
    uint32_t count;
    uint32_t entry_size;
    uint64_t expected;
    switch (type) {
    case RIBON_PARUS_RPH1_SECTION_MEMORY_MAP:
    case RIBON_PARUS_RPH1_SECTION_RESERVED_RANGES:
        if (length < 8u) {
            return 0;
        }
        count = rph1_read_u32(payload, 0u);
        entry_size = rph1_read_u32(payload, 4u);
        if (entry_size != RIBON_PARUS_RPH1_REGION_ENTRY_SIZE ||
            count > (RIBON_PARUS_RPH1_MAX_TOTAL_SIZE - 8u) / entry_size) {
            return 0;
        }
        expected = 8u + ((uint64_t)count * entry_size);
        return length == expected;
    case RIBON_PARUS_RPH1_SECTION_KERNEL_LAYOUT:
        if (length < RIBON_PARUS_RPH1_KERNEL_LAYOUT_SIZE ||
            rph1_read_u32(payload, 0u) != 1u) {
            return 0;
        }
        count = rph1_read_u32(payload, 4u);
        if (count > RIBON_PARUS_RPH1_MAX_KERNEL_SEGMENTS) {
            return 0;
        }
        expected = RIBON_PARUS_RPH1_KERNEL_LAYOUT_SIZE +
                   ((uint64_t)count * RIBON_PARUS_RPH1_KERNEL_SEGMENT_SIZE);
        return length == expected && rph1_read_u32(payload, 124u) == 0u;
    case RIBON_PARUS_RPH1_SECTION_DEVICE_TREE:
    case RIBON_PARUS_RPH1_SECTION_ACPI:
        return length == RIBON_PARUS_RPH1_RANGE_DESCRIPTOR_SIZE;
    case RIBON_PARUS_RPH1_SECTION_COMMAND_LINE:
        return length != 0u &&
               length <= RIBON_PARUS_RPH1_COMMAND_LINE_MAX_SIZE &&
               payload[length - 1u] == '\0';
    case RIBON_PARUS_RPH1_SECTION_FRAMEBUFFER:
        return length == RIBON_PARUS_RPH1_FRAMEBUFFER_SIZE;
    case RIBON_PARUS_RPH1_SECTION_MODULES:
        if (length < 8u) {
            return 0;
        }
        count = rph1_read_u32(payload, 0u);
        entry_size = rph1_read_u32(payload, 4u);
        if (entry_size != RIBON_PARUS_RPH1_MODULE_ENTRY_SIZE ||
            count == 0u || count > RIBON_PARUS_RPH1_MAX_MODULES) {
            return 0;
        }
        if (length != 8u + ((uint64_t)count * entry_size)) {
            return 0;
        }
        {
            uint32_t initial_images = 0u;
            for (uint32_t index = 0u; index < count; ++index) {
                const unsigned char *entry =
                    payload + 8u + ((uint64_t)index * entry_size);
                const uint64_t address = rph1_read_u64(entry, 0u);
                const uint64_t size = rph1_read_u64(entry, 8u);
                const uint32_t flags = rph1_read_u32(entry, 16u);
                if (address == 0u || size == 0u ||
                    address > UINT64_MAX - size ||
                    (flags & ~RIBON_PARUS_RPH1_MODULE_FLAG_ALL) != 0u ||
                    rph1_read_u32(entry, 20u) != 0u ||
                    rph1_read_u64(entry, 24u) != 0u) {
                    return 0;
                }
                if ((flags & RIBON_PARUS_RPH1_MODULE_FLAG_INITIAL_IMAGE) != 0u &&
                    ++initial_images > 1u) {
                    return 0;
                }
                for (uint32_t previous = 0u; previous < index; ++previous) {
                    const unsigned char *other =
                        payload + 8u + ((uint64_t)previous * entry_size);
                    const uint64_t other_address = rph1_read_u64(other, 0u);
                    const uint64_t other_size = rph1_read_u64(other, 8u);
                    if (address < other_address + other_size &&
                        other_address < address + size) {
                        return 0;
                    }
                }
            }
        }
        return 1;
    case RIBON_PARUS_RPH1_SECTION_BOOT_MEDIA:
        return length == RIBON_PARUS_RPH1_BOOT_MEDIA_SIZE;
    case RIBON_PARUS_RPH1_SECTION_PROVENANCE:
        return length == RIBON_PARUS_RPH1_PROVENANCE_SIZE;
    case RIBON_PARUS_RPH1_SECTION_OVERSEER:
        return length != 0u;
    default:
        return 1;
    }
}

int ribon_parus_parse_rph1(
    const void *data,
    uint64_t available_size,
    struct RibonParusRph1View *out) {
    const unsigned char *bytes = (const unsigned char *)data;
    uint64_t offsets[RIBON_PARUS_RPH1_MAX_SECTIONS];
    uint64_t lengths[RIBON_PARUS_RPH1_MAX_SECTIONS];
    uint32_t seen_types = 0u;
    uint32_t required_types = 0u;
    uint32_t total_size;
    uint32_t section_table_offset;
    uint16_t section_count;
    uint64_t table_end;
    const unsigned char *kernel_layout = 0;
    const unsigned char *modules = 0;
    if (bytes == 0 || out == 0) {
        return RIBON_PARUS_RPH1_PARSE_BAD_ARGUMENT;
    }
    *out = (struct RibonParusRph1View){0};
    if (available_size < RIBON_PARUS_RPH1_HEADER_SIZE) {
        return RIBON_PARUS_RPH1_PARSE_TRUNCATED;
    }
    if (rph1_read_u32(bytes, RIBON_PARUS_RPH1_HEADER_MAGIC_OFFSET) !=
        RIBON_PARUS_RPH1_MAGIC) {
        return RIBON_PARUS_RPH1_PARSE_BAD_MAGIC;
    }
    if (rph1_read_u16(bytes, RIBON_PARUS_RPH1_HEADER_VERSION_MAJOR_OFFSET) !=
            RIBON_PARUS_RPH1_VERSION_MAJOR ||
        rph1_read_u16(bytes, RIBON_PARUS_RPH1_HEADER_VERSION_MINOR_OFFSET) >
            RIBON_PARUS_RPH1_VERSION_MINOR) {
        return RIBON_PARUS_RPH1_PARSE_UNSUPPORTED_VERSION;
    }
    section_count = rph1_read_u16(bytes, RIBON_PARUS_RPH1_HEADER_SECTION_COUNT_OFFSET);
    total_size = rph1_read_u32(bytes, RIBON_PARUS_RPH1_HEADER_TOTAL_SIZE_OFFSET);
    section_table_offset =
        rph1_read_u32(bytes, RIBON_PARUS_RPH1_HEADER_SECTION_TABLE_OFFSET);
    if (rph1_read_u16(bytes, RIBON_PARUS_RPH1_HEADER_SIZE_OFFSET) !=
            RIBON_PARUS_RPH1_HEADER_SIZE ||
        rph1_read_u16(bytes, RIBON_PARUS_RPH1_HEADER_SECTION_ENTRY_SIZE_OFFSET) !=
            RIBON_PARUS_RPH1_SECTION_ENTRY_SIZE ||
        section_count == 0u || section_count > RIBON_PARUS_RPH1_MAX_SECTIONS ||
        total_size < RIBON_PARUS_RPH1_HEADER_SIZE ||
        total_size > RIBON_PARUS_RPH1_MAX_TOTAL_SIZE || total_size > available_size ||
        section_table_offset < RIBON_PARUS_RPH1_HEADER_SIZE ||
        (section_table_offset % RIBON_PARUS_RPH1_PAYLOAD_ALIGNMENT) != 0u ||
        rph1_read_u16(bytes, RIBON_PARUS_RPH1_HEADER_RESERVED0_OFFSET) != 0u ||
        rph1_read_u32(bytes, RIBON_PARUS_RPH1_HEADER_RESERVED1_OFFSET) != 0u ||
        rph1_read_u64(bytes, RIBON_PARUS_RPH1_HEADER_RESERVED2_OFFSET) != 0u) {
        return RIBON_PARUS_RPH1_PARSE_BAD_HEADER;
    }
    table_end = (uint64_t)section_table_offset +
                ((uint64_t)section_count * RIBON_PARUS_RPH1_SECTION_ENTRY_SIZE);
    if (table_end > total_size) {
        return RIBON_PARUS_RPH1_PARSE_BAD_HEADER;
    }
    if (rph1_read_u32(bytes, RIBON_PARUS_RPH1_HEADER_CRC32C_OFFSET) !=
        ribon_parus_rph1_crc32c(bytes, total_size)) {
        return RIBON_PARUS_RPH1_PARSE_BAD_CHECKSUM;
    }

    for (uint16_t index = 0; index < section_count; ++index) {
        const unsigned char *section =
            bytes + section_table_offset +
            ((uint64_t)index * RIBON_PARUS_RPH1_SECTION_ENTRY_SIZE);
        uint32_t type = rph1_read_u32(section, RIBON_PARUS_RPH1_SECTION_TYPE_OFFSET);
        uint32_t flags = rph1_read_u32(section, RIBON_PARUS_RPH1_SECTION_FLAGS_OFFSET);
        uint32_t alignment =
            rph1_read_u32(section, RIBON_PARUS_RPH1_SECTION_ALIGNMENT_OFFSET);
        uint64_t offset =
            rph1_read_u64(section, RIBON_PARUS_RPH1_SECTION_PAYLOAD_OFFSET);
        uint64_t length = rph1_read_u64(section, RIBON_PARUS_RPH1_SECTION_LENGTH_OFFSET);
        uint32_t type_bit;
        if ((flags & ~(RIBON_PARUS_RPH1_SECTION_REQUIRED_TO_UNDERSTAND |
                       RIBON_PARUS_RPH1_SECTION_BORROWED_RANGE_DESCRIPTOR)) != 0u ||
            rph1_read_u32(section, RIBON_PARUS_RPH1_SECTION_RESERVED_OFFSET) != 0u ||
            alignment < RIBON_PARUS_RPH1_PAYLOAD_ALIGNMENT ||
            !rph1_is_power_of_two(alignment) || offset < table_end ||
            (offset % alignment) != 0u || length == 0u || offset > total_size ||
            length > (uint64_t)total_size - offset) {
            return RIBON_PARUS_RPH1_PARSE_BAD_SECTION;
        }
        if (!rph1_is_known_section(type)) {
            if ((flags & RIBON_PARUS_RPH1_SECTION_REQUIRED_TO_UNDERSTAND) != 0u) {
                return RIBON_PARUS_RPH1_PARSE_UNKNOWN_REQUIRED_SECTION;
            }
        } else {
            type_bit = 1u << (type - 1u);
            if ((seen_types & type_bit) != 0u) {
                return RIBON_PARUS_RPH1_PARSE_DUPLICATE_SECTION;
            }
            seen_types |= type_bit;
            if (!rph1_payload_shape_is_valid(type, bytes + offset, length)) {
                return RIBON_PARUS_RPH1_PARSE_BAD_SECTION;
            }
            if (type == RIBON_PARUS_RPH1_SECTION_KERNEL_LAYOUT) {
                kernel_layout = bytes + offset;
            } else if (type == RIBON_PARUS_RPH1_SECTION_MODULES) {
                if (flags !=
                    (RIBON_PARUS_RPH1_SECTION_REQUIRED_TO_UNDERSTAND |
                     RIBON_PARUS_RPH1_SECTION_BORROWED_RANGE_DESCRIPTOR)) {
                    return RIBON_PARUS_RPH1_PARSE_BAD_SECTION;
                }
                modules = bytes + offset;
            }
        }
        for (uint16_t previous = 0; previous < index; ++previous) {
            if (offset < offsets[previous] + lengths[previous] &&
                offsets[previous] < offset + length) {
                return RIBON_PARUS_RPH1_PARSE_BAD_SECTION;
            }
        }
        offsets[index] = offset;
        lengths[index] = length;
    }
    required_types =
        (1u << (RIBON_PARUS_RPH1_SECTION_MEMORY_MAP - 1u)) |
        (1u << (RIBON_PARUS_RPH1_SECTION_RESERVED_RANGES - 1u)) |
        (1u << (RIBON_PARUS_RPH1_SECTION_KERNEL_LAYOUT - 1u)) |
        (1u << (RIBON_PARUS_RPH1_SECTION_PROVENANCE - 1u));
    if ((seen_types & required_types) != required_types) {
        return RIBON_PARUS_RPH1_PARSE_MISSING_REQUIRED_SECTION;
    }
    if (modules != 0) {
        const uint64_t kernel_base = rph1_read_u64(kernel_layout, 32u);
        const uint64_t kernel_end = rph1_read_u64(kernel_layout, 40u);
        const uint32_t module_count = rph1_read_u32(modules, 0u);
        if (kernel_base == 0u || kernel_end <= kernel_base) {
            return RIBON_PARUS_RPH1_PARSE_BAD_SECTION;
        }
        for (uint32_t index = 0u; index < module_count; ++index) {
            const unsigned char *entry =
                modules + 8u +
                ((uint64_t)index * RIBON_PARUS_RPH1_MODULE_ENTRY_SIZE);
            const uint64_t address = rph1_read_u64(entry, 0u);
            const uint64_t end = address + rph1_read_u64(entry, 8u);
            if (address < kernel_end && kernel_base < end) {
                return RIBON_PARUS_RPH1_PARSE_BAD_SECTION;
            }
        }
    }
    out->bytes = bytes;
    out->total_size = total_size;
    out->section_table_offset = section_table_offset;
    out->section_count = section_count;
    out->flags = rph1_read_u64(bytes, RIBON_PARUS_RPH1_HEADER_FLAGS_OFFSET);
    out->boot_generation =
        rph1_read_u64(bytes, RIBON_PARUS_RPH1_HEADER_BOOT_GENERATION_OFFSET);
    out->manifest_sequence =
        rph1_read_u64(bytes, RIBON_PARUS_RPH1_HEADER_MANIFEST_SEQUENCE_OFFSET);
    return RIBON_PARUS_RPH1_PARSE_OK;
}
