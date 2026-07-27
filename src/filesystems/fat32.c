#include <Ribon/filesystem/fat32.h>

#include <stdint.h>

#define RIBON_FAT32_DIRECTORY_ENTRY_BYTES 32u
#define RIBON_FAT32_ENTRY_DELETED 0xe5u
#define RIBON_FAT32_ENTRY_END 0x00u
#define RIBON_FAT32_ATTRIBUTE_DIRECTORY 0x10u
#define RIBON_FAT32_ATTRIBUTE_VOLUME_ID 0x08u
#define RIBON_FAT32_ATTRIBUTE_LONG_NAME 0x0fu
#define RIBON_FAT32_EOC_MINIMUM 0x0ffffff8u
#define RIBON_FAT32_BAD_CLUSTER 0x0ffffff7u

/** @brief Little-endian 16-bit scalar를 unaligned sector byte range에서 읽는다. */
static uint16_t fat32_read_le16(const unsigned char *bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8u);
}

/** @brief Little-endian 32-bit scalar를 unaligned sector byte range에서 읽는다. */
static uint32_t fat32_read_le32(const unsigned char *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}

/** @brief Unsigned 32-bit power-of-two geometry를 검사한다. */
static int fat32_is_power_of_two(uint32_t value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}

/** @brief One logical sector를 environment-private block provider에서 exact read한다. */
static int fat32_read_sector(
    const struct RibonFat32Volume *volume,
    uint64_t block,
    struct RibonFat32Scratch *scratch) {
    int status;
    if (volume == 0 || scratch == 0 || scratch->sector_bytes == 0 ||
        scratch->sector_capacity < volume->device->logical_block_bytes ||
        block < volume->partition_first_block ||
        block - volume->partition_first_block >= volume->partition_block_count) {
        return RIBON_FAT32_STATUS_OUT_OF_RANGE;
    }
    status = volume->device->read(
        volume->device->context,
        block,
        scratch->sector_bytes,
        1u,
        0u);
    return status == RIBON_BLOCK_STATUS_OK ? RIBON_FAT32_STATUS_OK : RIBON_FAT32_STATUS_IO;
}

/** @brief Valid data cluster를 first data block에서 absolute block address로 변환한다. */
static int fat32_cluster_first_block(
    const struct RibonFat32Volume *volume,
    uint32_t cluster,
    uint64_t *out) {
    uint64_t relative;
    if (volume == 0 || out == 0 || cluster < 2u || cluster > volume->max_cluster ||
        (uint64_t)(cluster - 2u) > UINT64_MAX / volume->sectors_per_cluster) {
        return RIBON_FAT32_STATUS_CORRUPT_CHAIN;
    }
    relative = (uint64_t)(cluster - 2u) * volume->sectors_per_cluster;
    if (relative > UINT64_MAX - volume->data_first_block ||
        volume->data_first_block + relative > volume->partition_first_block + volume->partition_block_count ||
        volume->sectors_per_cluster >
            volume->partition_first_block + volume->partition_block_count -
                (volume->data_first_block + relative)) {
        return RIBON_FAT32_STATUS_CORRUPT_CHAIN;
    }
    *out = volume->data_first_block + relative;
    return RIBON_FAT32_STATUS_OK;
}

/** @brief FAT table에서 next cluster를 읽고 EOC 여부를 반환한다. */
static int fat32_next_cluster(
    const struct RibonFat32Volume *volume,
    uint32_t cluster,
    struct RibonFat32Scratch *scratch,
    uint32_t *next,
    int *end_of_chain) {
    uint64_t offset;
    uint64_t sector;
    uint32_t within;
    int status;
    if (volume == 0 || scratch == 0 || next == 0 || end_of_chain == 0 || cluster < 2u ||
        cluster > volume->max_cluster) {
        return RIBON_FAT32_STATUS_CORRUPT_CHAIN;
    }
    offset = (uint64_t)cluster * 4u;
    sector = volume->fat_first_block + offset / volume->device->logical_block_bytes;
    within = (uint32_t)(offset % volume->device->logical_block_bytes);
    if (within > volume->device->logical_block_bytes - 4u) {
        return RIBON_FAT32_STATUS_CORRUPT_CHAIN;
    }
    status = fat32_read_sector(volume, sector, scratch);
    if (status != RIBON_FAT32_STATUS_OK) {
        return status;
    }
    *next = fat32_read_le32((const unsigned char *)scratch->sector_bytes + within) & 0x0fffffffu;
    *end_of_chain = *next >= RIBON_FAT32_EOC_MINIMUM;
    if (!*end_of_chain && (*next < 2u || *next > volume->max_cluster ||
                           *next == RIBON_FAT32_BAD_CLUSTER)) {
        return RIBON_FAT32_STATUS_CORRUPT_CHAIN;
    }
    return RIBON_FAT32_STATUS_OK;
}

/** @brief ASCII 8.3 component를 on-media padded upper-case short name으로 변환한다. */
static int fat32_make_short_name(
    const char *component,
    uint32_t component_size,
    unsigned char out[11]) {
    uint32_t dot = component_size;
    uint32_t base_bytes;
    uint32_t extension_bytes = 0u;
    if (component == 0 || component_size == 0u || component_size > RIBON_FAT32_MAX_PATH_COMPONENT_BYTES) {
        return 0;
    }
    for (uint32_t index = 0u; index < 11u; ++index) {
        out[index] = ' ';
    }
    for (uint32_t index = 0u; index < component_size; ++index) {
        if (component[index] == '.') {
            if (dot != component_size) {
                return 0;
            }
            dot = index;
        }
    }
    base_bytes = dot;
    if (dot != component_size) {
        extension_bytes = component_size - dot - 1u;
    }
    if (base_bytes == 0u || base_bytes > 8u || extension_bytes > 3u) {
        return 0;
    }
    for (uint32_t index = 0u; index < component_size; ++index) {
        unsigned char byte;
        uint32_t destination;
        if (index == dot) {
            continue;
        }
        byte = (unsigned char)component[index];
        if ((byte >= 'a' && byte <= 'z')) {
            byte = (unsigned char)(byte - ('a' - 'A'));
        }
        if (!((byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
              byte == '_' || byte == '-' || byte == '$' || byte == '~' || byte == '!' ||
              byte == '#' || byte == '%' || byte == '&' || byte == '\'' || byte == '(' ||
              byte == ')' || byte == '@' || byte == '^')) {
            return 0;
        }
        destination = dot == component_size || index < dot ? index : 8u + index - dot - 1u;
        out[destination] = byte;
    }
    return 1;
}

/** @brief Directory cluster chain에서 one 8.3 name entry를 bounded scan한다. */
static int fat32_find_directory_entry(
    const struct RibonFat32Volume *volume,
    uint32_t first_cluster,
    const unsigned char wanted[11],
    struct RibonFat32Scratch *scratch,
    unsigned char out_entry[RIBON_FAT32_DIRECTORY_ENTRY_BYTES]) {
    uint32_t cluster = first_cluster;
    for (uint32_t chain_steps = 0u; chain_steps < volume->cluster_count; ++chain_steps) {
        uint64_t first_block;
        int status = fat32_cluster_first_block(volume, cluster, &first_block);
        if (status != RIBON_FAT32_STATUS_OK) {
            return status;
        }
        for (uint32_t sector_index = 0u; sector_index < volume->sectors_per_cluster; ++sector_index) {
            const unsigned char *bytes;
            status = fat32_read_sector(volume, first_block + sector_index, scratch);
            if (status != RIBON_FAT32_STATUS_OK) {
                return status;
            }
            bytes = scratch->sector_bytes;
            for (uint32_t offset = 0u;
                 offset + RIBON_FAT32_DIRECTORY_ENTRY_BYTES <= volume->device->logical_block_bytes;
                 offset += RIBON_FAT32_DIRECTORY_ENTRY_BYTES) {
                const unsigned char *entry = bytes + offset;
                unsigned char mismatch = 0u;
                if (entry[0] == RIBON_FAT32_ENTRY_END) {
                    return RIBON_FAT32_STATUS_NOT_FOUND;
                }
                if (entry[0] == RIBON_FAT32_ENTRY_DELETED ||
                    entry[11] == RIBON_FAT32_ATTRIBUTE_LONG_NAME ||
                    (entry[11] & RIBON_FAT32_ATTRIBUTE_VOLUME_ID) != 0u) {
                    continue;
                }
                for (uint32_t character = 0u; character < 11u; ++character) {
                    mismatch |= (unsigned char)(entry[character] ^ wanted[character]);
                }
                if (mismatch == 0u) {
                    for (uint32_t character = 0u; character < RIBON_FAT32_DIRECTORY_ENTRY_BYTES; ++character) {
                        out_entry[character] = entry[character];
                    }
                    return RIBON_FAT32_STATUS_OK;
                }
            }
        }
        {
            uint32_t next;
            int end_of_chain;
            status = fat32_next_cluster(volume, cluster, scratch, &next, &end_of_chain);
            if (status != RIBON_FAT32_STATUS_OK) {
                return status;
            }
            if (end_of_chain) {
                return RIBON_FAT32_STATUS_NOT_FOUND;
            }
            cluster = next;
        }
    }
    return RIBON_FAT32_STATUS_CHAIN_CYCLE;
}

/** @brief FAT32 parser status를 stable diagnostic name으로 변환한다. */
const char *ribon_fat32_status_name(enum RibonFat32Status status) {
    switch (status) {
    case RIBON_FAT32_STATUS_OK:
        return "ok";
    case RIBON_FAT32_STATUS_BAD_ARGUMENT:
        return "bad-argument";
    case RIBON_FAT32_STATUS_BAD_BPB:
        return "bad-bpb";
    case RIBON_FAT32_STATUS_OUT_OF_RANGE:
        return "out-of-range";
    case RIBON_FAT32_STATUS_IO:
        return "io";
    case RIBON_FAT32_STATUS_BAD_PATH:
        return "bad-path";
    case RIBON_FAT32_STATUS_NOT_FOUND:
        return "not-found";
    case RIBON_FAT32_STATUS_NOT_REGULAR:
        return "not-regular";
    case RIBON_FAT32_STATUS_OUT_OF_CAPACITY:
        return "out-of-capacity";
    case RIBON_FAT32_STATUS_CHAIN_CYCLE:
        return "chain-cycle";
    case RIBON_FAT32_STATUS_CORRUPT_CHAIN:
        return "corrupt-chain";
    }
    return "unknown";
}

/** @brief FAT32 BPB, FAT area와 data cluster geometry를 fail-closed mount한다. */
int ribon_fat32_mount(
    struct RibonFat32Volume *out,
    const struct RibonReadOnlyBlockDevice *device,
    uint64_t partition_first_block,
    uint64_t partition_block_count,
    struct RibonFat32Scratch *scratch) {
    const unsigned char *bpb;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t reserved_sectors;
    uint32_t fat_count;
    uint32_t sectors_per_fat;
    uint32_t root_cluster;
    uint32_t total_sectors;
    uint64_t first_data_relative;
    uint64_t data_sectors;
    int status;
    if (out != 0) {
        *out = (struct RibonFat32Volume){0};
    }
    if (out == 0 || !ribon_read_only_block_device_is_valid(device) || scratch == 0 ||
        scratch->sector_bytes == 0 || scratch->sector_capacity < device->logical_block_bytes ||
        partition_block_count == 0u || partition_first_block >= device->block_count ||
        partition_block_count > device->block_count - partition_first_block) {
        return RIBON_FAT32_STATUS_BAD_ARGUMENT;
    }
    {
        struct RibonFat32Volume temporary = {
            .device = device,
            .partition_first_block = partition_first_block,
            .partition_block_count = partition_block_count,
        };
        status = fat32_read_sector(&temporary, partition_first_block, scratch);
    }
    if (status != RIBON_FAT32_STATUS_OK) {
        return status;
    }
    bpb = scratch->sector_bytes;
    bytes_per_sector = fat32_read_le16(bpb + 11u);
    sectors_per_cluster = bpb[13];
    reserved_sectors = fat32_read_le16(bpb + 14u);
    fat_count = bpb[16];
    total_sectors = fat32_read_le16(bpb + 19u);
    if (total_sectors == 0u) {
        total_sectors = fat32_read_le32(bpb + 32u);
    }
    sectors_per_fat = fat32_read_le32(bpb + 36u);
    root_cluster = fat32_read_le32(bpb + 44u);
    if (fat32_read_le16(bpb + 510u) != 0xaa55u || bytes_per_sector != device->logical_block_bytes ||
        !fat32_is_power_of_two(bytes_per_sector) || !fat32_is_power_of_two(sectors_per_cluster) ||
        sectors_per_cluster > 128u || reserved_sectors == 0u || (fat_count != 1u && fat_count != 2u) ||
        fat32_read_le16(bpb + 17u) != 0u || sectors_per_fat == 0u || total_sectors == 0u ||
        total_sectors > partition_block_count || root_cluster < 2u) {
        return RIBON_FAT32_STATUS_BAD_BPB;
    }
    first_data_relative = (uint64_t)reserved_sectors + (uint64_t)fat_count * sectors_per_fat;
    if (first_data_relative >= total_sectors) {
        return RIBON_FAT32_STATUS_BAD_BPB;
    }
    data_sectors = (uint64_t)total_sectors - first_data_relative;
    if (data_sectors / sectors_per_cluster < 65525u ||
        data_sectors / sectors_per_cluster > 0x0fffffedu ||
        (uint64_t)sectors_per_fat * bytes_per_sector / 4u <
            data_sectors / sectors_per_cluster + 2u) {
        return RIBON_FAT32_STATUS_BAD_BPB;
    }
    out->device = device;
    out->partition_first_block = partition_first_block;
    out->partition_block_count = total_sectors;
    out->fat_first_block = partition_first_block + reserved_sectors;
    out->data_first_block = partition_first_block + first_data_relative;
    out->sectors_per_cluster = sectors_per_cluster;
    out->root_cluster = root_cluster;
    out->cluster_count = (uint32_t)(data_sectors / sectors_per_cluster);
    out->max_cluster = out->cluster_count + 1u;
    if (root_cluster > out->max_cluster) {
        *out = (struct RibonFat32Volume){0};
        return RIBON_FAT32_STATUS_BAD_BPB;
    }
    return RIBON_FAT32_STATUS_OK;
}

/** @brief Canonical absolute 8.3 path를 bounded directory chain으로 resolve한다. */
int ribon_fat32_open(
    const struct RibonFat32Volume *volume,
    const char *path,
    struct RibonFat32Scratch *scratch,
    struct RibonFat32File *out) {
    uint32_t cluster;
    uint32_t component_count = 0u;
    uint32_t start = 1u;
    if (out != 0) {
        *out = (struct RibonFat32File){0};
    }
    if (volume == 0 || volume->device == 0 || path == 0 || path[0] != '/' || path[1] == '\0' ||
        scratch == 0 || scratch->sector_bytes == 0 || out == 0) {
        return RIBON_FAT32_STATUS_BAD_ARGUMENT;
    }
    cluster = volume->root_cluster;
    for (uint32_t index = 1u;; ++index) {
        const char terminal = path[index];
        if (terminal != '/' && terminal != '\0') {
            if ((unsigned char)terminal < 0x21u || (unsigned char)terminal > 0x7eu) {
                return RIBON_FAT32_STATUS_BAD_PATH;
            }
            continue;
        }
        {
            unsigned char wanted[11];
            unsigned char entry[RIBON_FAT32_DIRECTORY_ENTRY_BYTES];
            uint32_t component_bytes = index - start;
            uint32_t first_cluster;
            int status;
            if (component_bytes == 0u || ++component_count > RIBON_FAT32_MAX_PATH_COMPONENTS ||
                !fat32_make_short_name(path + start, component_bytes, wanted)) {
                return RIBON_FAT32_STATUS_BAD_PATH;
            }
            status = fat32_find_directory_entry(volume, cluster, wanted, scratch, entry);
            if (status != RIBON_FAT32_STATUS_OK) {
                return status;
            }
            first_cluster = ((uint32_t)fat32_read_le16(entry + 20u) << 16u) |
                            fat32_read_le16(entry + 26u);
            if (terminal == '\0') {
                if ((entry[11] & RIBON_FAT32_ATTRIBUTE_DIRECTORY) != 0u) {
                    return RIBON_FAT32_STATUS_NOT_REGULAR;
                }
                out->first_cluster = first_cluster;
                out->size = fat32_read_le32(entry + 28u);
                if (out->size != 0u && (first_cluster < 2u || first_cluster > volume->max_cluster)) {
                    *out = (struct RibonFat32File){0};
                    return RIBON_FAT32_STATUS_CORRUPT_CHAIN;
                }
                return RIBON_FAT32_STATUS_OK;
            }
            if ((entry[11] & RIBON_FAT32_ATTRIBUTE_DIRECTORY) == 0u || first_cluster < 2u ||
                first_cluster > volume->max_cluster) {
                return RIBON_FAT32_STATUS_NOT_FOUND;
            }
            cluster = first_cluster;
            start = index + 1u;
        }
    }
}

/** @brief FAT32 regular file byte range를 cluster chain cycle 상한 안에서 exact copy한다. */
int ribon_fat32_read(
    const struct RibonFat32Volume *volume,
    const struct RibonFat32File *file,
    uint64_t offset,
    void *buffer,
    uint64_t size,
    struct RibonFat32Scratch *scratch) {
    uint64_t cluster_bytes;
    uint64_t remaining;
    uint64_t skip_clusters;
    uint32_t cluster;
    uint64_t written = 0u;
    if (volume == 0 || file == 0 || scratch == 0 || scratch->sector_bytes == 0 ||
        (size != 0u && buffer == 0) || offset > file->size || size > file->size - offset) {
        return RIBON_FAT32_STATUS_BAD_ARGUMENT;
    }
    if (size == 0u) {
        return RIBON_FAT32_STATUS_OK;
    }
    cluster_bytes = (uint64_t)volume->sectors_per_cluster * volume->device->logical_block_bytes;
    if (cluster_bytes == 0u || file->first_cluster < 2u || file->first_cluster > volume->max_cluster) {
        return RIBON_FAT32_STATUS_CORRUPT_CHAIN;
    }
    cluster = file->first_cluster;
    skip_clusters = offset / cluster_bytes;
    remaining = offset % cluster_bytes;
    for (uint64_t skipped = 0u; skipped < skip_clusters; ++skipped) {
        uint32_t next;
        int end_of_chain;
        int status = fat32_next_cluster(volume, cluster, scratch, &next, &end_of_chain);
        if (status != RIBON_FAT32_STATUS_OK) {
            return status;
        }
        if (end_of_chain) {
            return RIBON_FAT32_STATUS_CORRUPT_CHAIN;
        }
        cluster = next;
    }
    for (uint32_t chain_steps = 0u; written < size && chain_steps < volume->cluster_count; ++chain_steps) {
        uint64_t first_block;
        int status = fat32_cluster_first_block(volume, cluster, &first_block);
        if (status != RIBON_FAT32_STATUS_OK) {
            return status;
        }
        for (uint32_t sector = 0u; sector < volume->sectors_per_cluster && written < size; ++sector) {
            const unsigned char *sector_bytes;
            uint64_t sector_offset = remaining;
            uint64_t available;
            uint64_t take;
            status = fat32_read_sector(volume, first_block + sector, scratch);
            if (status != RIBON_FAT32_STATUS_OK) {
                return status;
            }
            sector_bytes = scratch->sector_bytes;
            if (sector_offset >= volume->device->logical_block_bytes) {
                remaining -= volume->device->logical_block_bytes;
                continue;
            }
            available = volume->device->logical_block_bytes - sector_offset;
            take = size - written < available ? size - written : available;
            for (uint64_t index = 0u; index < take; ++index) {
                ((unsigned char *)buffer)[written + index] = sector_bytes[sector_offset + index];
            }
            written += take;
            remaining = 0u;
        }
        if (written == size) {
            return RIBON_FAT32_STATUS_OK;
        }
        {
            uint32_t next;
            int end_of_chain;
            status = fat32_next_cluster(volume, cluster, scratch, &next, &end_of_chain);
            if (status != RIBON_FAT32_STATUS_OK) {
                return status;
            }
            if (end_of_chain) {
                return RIBON_FAT32_STATUS_CORRUPT_CHAIN;
            }
            cluster = next;
        }
    }
    return RIBON_FAT32_STATUS_CHAIN_CYCLE;
}
