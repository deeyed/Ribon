#include <Ribon/storage/block.h>

#include <stdint.h>

#define RIBON_GPT_HEADER_MINIMUM_BYTES 92u
#define RIBON_GPT_ENTRY_MINIMUM_BYTES 128u
#define RIBON_GPT_ENTRY_MAXIMUM_BYTES 4096u
#define RIBON_GPT_ENTRY_MAXIMUM_COUNT 128u

/** @brief Little-endian 16-bit scalar를 unaligned byte range에서 읽는다. */
static uint16_t gpt_read_le16(const unsigned char *bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8u);
}

/** @brief Little-endian 32-bit scalar를 unaligned byte range에서 읽는다. */
static uint32_t gpt_read_le32(const unsigned char *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}

/** @brief Little-endian 64-bit scalar를 unaligned byte range에서 읽는다. */
static uint64_t gpt_read_le64(const unsigned char *bytes) {
    uint64_t value = 0u;
    for (uint32_t index = 0u; index < 8u; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8u);
    }
    return value;
}

/** @brief IEEE GPT CRC32를 byte-wise로 계산한다. */
static uint32_t gpt_crc32(const unsigned char *bytes, uint64_t byte_count) {
    uint32_t crc = 0xffffffffu;
    for (uint64_t index = 0u; index < byte_count; ++index) {
        crc ^= bytes[index];
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 1u) != 0u ? (crc >> 1u) ^ 0xedb88320u : crc >> 1u;
        }
    }
    return ~crc;
}

/** @brief GPT header CRC field를 zero로 간주해 CRC32를 계산한다. */
static uint32_t gpt_header_crc32(const unsigned char *header, uint32_t header_bytes) {
    uint32_t crc = 0xffffffffu;
    for (uint32_t index = 0u; index < header_bytes; ++index) {
        unsigned char value = header[index];
        if (index >= 16u && index < 20u) {
            value = 0u;
        }
        crc ^= value;
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 1u) != 0u ? (crc >> 1u) ^ 0xedb88320u : crc >> 1u;
        }
    }
    return ~crc;
}

/** @brief GUID byte range가 all-zero인지 검사한다. */
static int gpt_guid_is_zero(const unsigned char *guid) {
    unsigned char accumulator = 0u;
    for (uint32_t index = 0u; index < 16u; ++index) {
        accumulator |= guid[index];
    }
    return accumulator == 0u;
}

/** @brief Partition byte range가 inclusive overlap하는지 검사한다. */
static int gpt_ranges_overlap(
    uint64_t first_a,
    uint64_t last_a,
    uint64_t first_b,
    uint64_t last_b) {
    return first_a <= last_b && first_b <= last_a;
}

/** @brief Protective MBR signature와 exactly-one protective entry를 검사한다. */
static int gpt_validate_protective_mbr(const unsigned char *mbr) {
    uint32_t protective_entries = 0u;
    if (gpt_read_le16(mbr + 510u) != 0xaa55u) {
        return 0;
    }
    for (uint32_t index = 0u; index < 4u; ++index) {
        const unsigned char *entry = mbr + 446u + index * 16u;
        if (entry[4] == 0xeeu) {
            if (gpt_read_le32(entry + 8u) != 1u || gpt_read_le32(entry + 12u) == 0u) {
                return 0;
            }
            ++protective_entries;
        } else if (entry[4] != 0u) {
            return 0;
        }
    }
    return protective_entries == 1u;
}

/** @brief One GPT entry의 fixed fields를 caller-owned output으로 byte-wise copy한다. */
static void gpt_copy_partition(
    struct RibonGptPartition *out,
    const unsigned char *entry,
    uint32_t entry_index) {
    for (uint32_t index = 0u; index < 16u; ++index) {
        out->type_guid[index] = entry[index];
        out->unique_guid[index] = entry[16u + index];
    }
    out->first_lba = gpt_read_le64(entry + 32u);
    out->last_lba = gpt_read_le64(entry + 40u);
    out->attributes = gpt_read_le64(entry + 48u);
    out->entry_index = entry_index;
}

/** @brief GPT parser status를 stable diagnostic name으로 변환한다. */
const char *ribon_gpt_status_name(enum RibonGptStatus status) {
    switch (status) {
    case RIBON_GPT_STATUS_OK:
        return "ok";
    case RIBON_GPT_STATUS_BAD_ARGUMENT:
        return "bad-argument";
    case RIBON_GPT_STATUS_TRUNCATED:
        return "truncated";
    case RIBON_GPT_STATUS_BAD_PROTECTIVE_MBR:
        return "bad-protective-mbr";
    case RIBON_GPT_STATUS_BAD_HEADER:
        return "bad-header";
    case RIBON_GPT_STATUS_BAD_HEADER_CRC:
        return "bad-header-crc";
    case RIBON_GPT_STATUS_BAD_ENTRIES_CRC:
        return "bad-entries-crc";
    case RIBON_GPT_STATUS_OVERFLOW:
        return "overflow";
    case RIBON_GPT_STATUS_OUT_OF_CAPACITY:
        return "out-of-capacity";
    case RIBON_GPT_STATUS_OUT_OF_RANGE:
        return "out-of-range";
    case RIBON_GPT_STATUS_OVERLAP:
        return "overlap";
    }
    return "unknown";
}

/**
 * @brief Protective MBR, primary header, entry CRC와 non-overlap partition ranges를 검증한다.
 *
 * `out`은 failure에도 count를 0으로 유지하며 input bytes를 repair하거나 native struct로
 * cast하지 않는다.
 */
int ribon_gpt_parse_bytes(
    const void *disk_bytes,
    uint64_t disk_byte_count,
    uint32_t logical_block_bytes,
    struct RibonGptTable *out) {
    const unsigned char *disk = disk_bytes;
    const unsigned char *header;
    const unsigned char *entries;
    uint64_t disk_block_count;
    uint64_t disk_last_lba;
    uint64_t entry_lba;
    uint64_t entries_bytes;
    uint64_t entries_offset;
    uint64_t first_usable;
    uint64_t last_usable;
    uint64_t backup_lba;
    uint32_t header_bytes;
    uint32_t entry_count;
    uint32_t entry_bytes;

    if (out != 0) {
        out->partition_count = 0u;
    }
    if (disk == 0 || out == 0 || out->partitions == 0 || out->partition_capacity == 0u ||
        logical_block_bytes < 512u || logical_block_bytes > 4096u ||
        (logical_block_bytes & (logical_block_bytes - 1u)) != 0u ||
        disk_byte_count < (uint64_t)logical_block_bytes * 2u ||
        disk_byte_count % logical_block_bytes != 0u) {
        return RIBON_GPT_STATUS_BAD_ARGUMENT;
    }
    disk_block_count = disk_byte_count / logical_block_bytes;
    disk_last_lba = disk_block_count - 1u;
    if (!gpt_validate_protective_mbr(disk)) {
        return RIBON_GPT_STATUS_BAD_PROTECTIVE_MBR;
    }
    header = disk + logical_block_bytes;
    if (header[0] != 'E' || header[1] != 'F' || header[2] != 'I' || header[3] != ' ' ||
        header[4] != 'P' || header[5] != 'A' || header[6] != 'R' || header[7] != 'T' ||
        gpt_read_le32(header + 8u) != 0x00010000u) {
        return RIBON_GPT_STATUS_BAD_HEADER;
    }
    header_bytes = gpt_read_le32(header + 12u);
    if (header_bytes < RIBON_GPT_HEADER_MINIMUM_BYTES || header_bytes > logical_block_bytes ||
        gpt_header_crc32(header, header_bytes) != gpt_read_le32(header + 16u)) {
        return header_bytes < RIBON_GPT_HEADER_MINIMUM_BYTES || header_bytes > logical_block_bytes ?
            RIBON_GPT_STATUS_BAD_HEADER : RIBON_GPT_STATUS_BAD_HEADER_CRC;
    }
    if (gpt_read_le64(header + 24u) != 1u) {
        return RIBON_GPT_STATUS_BAD_HEADER;
    }
    backup_lba = gpt_read_le64(header + 32u);
    first_usable = gpt_read_le64(header + 40u);
    last_usable = gpt_read_le64(header + 48u);
    entry_lba = gpt_read_le64(header + 72u);
    entry_count = gpt_read_le32(header + 80u);
    entry_bytes = gpt_read_le32(header + 84u);
    if (backup_lba == 0u || backup_lba > disk_last_lba || first_usable > last_usable ||
        last_usable > disk_last_lba || entry_lba < 2u || entry_count == 0u ||
        entry_count > RIBON_GPT_ENTRY_MAXIMUM_COUNT ||
        entry_bytes < RIBON_GPT_ENTRY_MINIMUM_BYTES ||
        entry_bytes > RIBON_GPT_ENTRY_MAXIMUM_BYTES || entry_bytes % 8u != 0u ||
        entry_count > UINT64_MAX / entry_bytes) {
        return RIBON_GPT_STATUS_BAD_HEADER;
    }
    entries_bytes = (uint64_t)entry_count * entry_bytes;
    if (entry_lba > UINT64_MAX / logical_block_bytes) {
        return RIBON_GPT_STATUS_OVERFLOW;
    }
    entries_offset = entry_lba * logical_block_bytes;
    if (entries_offset > disk_byte_count || entries_bytes > disk_byte_count - entries_offset ||
        gpt_crc32(disk + entries_offset, entries_bytes) != gpt_read_le32(header + 88u)) {
        return entries_offset > disk_byte_count || entries_bytes > disk_byte_count - entries_offset ?
            RIBON_GPT_STATUS_TRUNCATED : RIBON_GPT_STATUS_BAD_ENTRIES_CRC;
    }
    entries = disk + entries_offset;
    out->first_usable_lba = first_usable;
    out->last_usable_lba = last_usable;
    out->disk_last_lba = disk_last_lba;
    for (uint32_t index = 0u; index < entry_count; ++index) {
        const unsigned char *entry = entries + (uint64_t)index * entry_bytes;
        struct RibonGptPartition candidate;
        if (gpt_guid_is_zero(entry)) {
            continue;
        }
        gpt_copy_partition(&candidate, entry, index);
        if (candidate.first_lba > candidate.last_lba || candidate.first_lba < first_usable ||
            candidate.last_lba > last_usable) {
            out->partition_count = 0u;
            return RIBON_GPT_STATUS_OUT_OF_RANGE;
        }
        for (uint32_t previous = 0u; previous < out->partition_count; ++previous) {
            if (gpt_ranges_overlap(
                    candidate.first_lba,
                    candidate.last_lba,
                    out->partitions[previous].first_lba,
                    out->partitions[previous].last_lba)) {
                out->partition_count = 0u;
                return RIBON_GPT_STATUS_OVERLAP;
            }
        }
        if (out->partition_count == out->partition_capacity) {
            out->partition_count = 0u;
            return RIBON_GPT_STATUS_OUT_OF_CAPACITY;
        }
        out->partitions[out->partition_count++] = candidate;
    }
    return RIBON_GPT_STATUS_OK;
}
