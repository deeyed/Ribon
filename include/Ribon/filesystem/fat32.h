#ifndef RIBON_FILESYSTEM_FAT32_H
#define RIBON_FILESYSTEM_FAT32_H

#include <Ribon/storage/block.h>

#include <stdint.h>

/** @brief FAT32 parser가 허용하는 canonical absolute path depth 상한이다. */
#define RIBON_FAT32_MAX_PATH_COMPONENTS 8u

/** @brief FAT32 parser가 허용하는 short-name path component byte 상한이다. */
#define RIBON_FAT32_MAX_PATH_COMPONENT_BYTES 12u

/** @brief Read-only FAT32 parser의 안정적인 결과다. */
enum RibonFat32Status {
    RIBON_FAT32_STATUS_OK = 0,
    RIBON_FAT32_STATUS_BAD_ARGUMENT = -1,
    RIBON_FAT32_STATUS_BAD_BPB = -2,
    RIBON_FAT32_STATUS_OUT_OF_RANGE = -3,
    RIBON_FAT32_STATUS_IO = -4,
    RIBON_FAT32_STATUS_BAD_PATH = -5,
    RIBON_FAT32_STATUS_NOT_FOUND = -6,
    RIBON_FAT32_STATUS_NOT_REGULAR = -7,
    RIBON_FAT32_STATUS_OUT_OF_CAPACITY = -8,
    RIBON_FAT32_STATUS_CHAIN_CYCLE = -9,
    RIBON_FAT32_STATUS_CORRUPT_CHAIN = -10,
};

/** @brief FAT32 mount와 traversal이 재사용하는 caller-owned one-sector scratch다. */
struct RibonFat32Scratch {
    void *sector_bytes; /**< Logical-block-sized caller-owned scratch다. */
    uint32_t sector_capacity; /**< `sector_bytes` byte 상한이다. */
};

/** @brief Mounted FAT32 volume의 immutable geometry다. */
struct RibonFat32Volume {
    const struct RibonReadOnlyBlockDevice *device; /**< Borrowed block provider다. */
    uint64_t partition_first_block; /**< FAT32 partition first logical block다. */
    uint64_t partition_block_count; /**< FAT32 partition block 수다. */
    uint64_t fat_first_block; /**< First FAT logical block다. */
    uint64_t data_first_block; /**< Cluster 2가 시작하는 logical block다. */
    uint32_t sectors_per_cluster; /**< Validated power-of-two sectors-per-cluster다. */
    uint32_t root_cluster; /**< Validated root directory first cluster다. */
    uint32_t cluster_count; /**< Addressable data cluster 수다. */
    uint32_t max_cluster; /**< Largest valid data cluster 번호다. */
};

/** @brief FAT32 directory가 반환한 regular-file locator다. */
struct RibonFat32File {
    uint32_t first_cluster; /**< File data first cluster이며 empty file은 0이다. */
    uint64_t size; /**< Exact regular-file byte 수다. */
};

/**
 * @brief BPB와 FAT32 geometry를 byte-wise로 검증해 immutable mount를 만든다.
 *
 * `scratch`는 device logical block보다 작을 수 없고 mount 뒤에도 read/open 호출자가
 * 같은 caller-owned scratch를 제공해야 한다.
 */
int ribon_fat32_mount(
    struct RibonFat32Volume *out,
    const struct RibonReadOnlyBlockDevice *device,
    uint64_t partition_first_block,
    uint64_t partition_block_count,
    struct RibonFat32Scratch *scratch);

/** @brief Canonical absolute 8.3 path에서 regular file locator를 찾는다. */
int ribon_fat32_open(
    const struct RibonFat32Volume *volume,
    const char *path,
    struct RibonFat32Scratch *scratch,
    struct RibonFat32File *out);

/**
 * @brief Validated FAT32 regular file의 exact byte range를 caller-owned buffer로 읽는다.
 *
 * `offset + size`가 file 범위를 벗어나거나 chain이 EOC 전에 손상되면 partial read 없이
 * 실패한다.
 */
int ribon_fat32_read(
    const struct RibonFat32Volume *volume,
    const struct RibonFat32File *file,
    uint64_t offset,
    void *buffer,
    uint64_t size,
    struct RibonFat32Scratch *scratch);

/** @brief FAT32 parser status의 안정적인 이름을 반환한다. */
const char *ribon_fat32_status_name(enum RibonFat32Status status);

#endif
