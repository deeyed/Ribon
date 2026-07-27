#ifndef RIBON_STORAGE_BLOCK_H
#define RIBON_STORAGE_BLOCK_H

#include <stdint.h>

/** @brief Read-only block device operation table ABI다. */
#define RIBON_READ_ONLY_BLOCK_DEVICE_ABI_VERSION 1u

/** @brief Read-only block operation의 안정적인 결과다. */
enum RibonBlockStatus {
    RIBON_BLOCK_STATUS_OK = 0,
    RIBON_BLOCK_STATUS_BAD_ARGUMENT = -1,
    RIBON_BLOCK_STATUS_OUT_OF_RANGE = -2,
    RIBON_BLOCK_STATUS_IO = -3,
    RIBON_BLOCK_STATUS_SHORT_READ = -4,
    RIBON_BLOCK_STATUS_UNSUPPORTED = -5,
};

/**
 * @brief Native controller를 노출하지 않고 연속 logical block을 읽는다.
 *
 * @param context environment-private controller context다.
 * @param first_block 읽기를 시작할 logical block 번호다.
 * @param buffer 호출자가 소유하는 `block_count * logical_block_bytes` buffer다.
 * @param block_count 한 호출에서 읽는 bounded block 수다.
 * @param deadline_ticks environment가 해석하는 absolute deadline이며 0은 parser-local read다.
 * @return Exact read만 `RIBON_BLOCK_STATUS_OK`를 반환한다.
 */
typedef int (*RibonReadOnlyBlockReadFn)(
    void *context,
    uint64_t first_block,
    void *buffer,
    uint32_t block_count,
    uint64_t deadline_ticks);

/**
 * @brief Caller-owned read-only block provider descriptor다.
 *
 * Native UEFI Block I/O, BIOS EDD와 memory corpus는 이 typed callback 뒤에만 둔다.
 */
struct RibonReadOnlyBlockDevice {
    uint32_t size; /**< `sizeof(struct RibonReadOnlyBlockDevice)`다. */
    uint32_t abi_version; /**< `RIBON_READ_ONLY_BLOCK_DEVICE_ABI_VERSION`과 일치한다. */
    uint32_t logical_block_bytes; /**< Logical block byte 수다. */
    uint32_t max_read_blocks; /**< 한 callback에 허용하는 block 수 상한이다. */
    uint64_t block_count; /**< Addressable logical block 총수다. */
    void *context; /**< Environment-private borrowed context다. */
    RibonReadOnlyBlockReadFn read; /**< Exact read callback이다. */
};

/** @brief Read-only block descriptor의 ABI, geometry와 callback을 fail-closed 검사한다. */
int ribon_read_only_block_device_is_valid(const struct RibonReadOnlyBlockDevice *device);

/** @brief Block status의 안정적인 이름을 반환한다. */
const char *ribon_block_status_name(enum RibonBlockStatus status);

/** @brief GPT parser가 caller-owned partition array에 반환하는 한 entry다. */
struct RibonGptPartition {
    uint8_t type_guid[16]; /**< Little-endian on-media GUID bytes다. */
    uint8_t unique_guid[16]; /**< Little-endian on-media unique GUID bytes다. */
    uint64_t first_lba; /**< Inclusive first LBA다. */
    uint64_t last_lba; /**< Inclusive last LBA다. */
    uint64_t attributes; /**< GPT partition attribute bits다. */
    uint32_t entry_index; /**< On-media partition entry index다. */
};

/** @brief GPT parser의 caller-owned result storage다. */
struct RibonGptTable {
    struct RibonGptPartition *partitions; /**< Caller-owned compact non-empty entry storage다. */
    uint32_t partition_capacity; /**< `partitions` element 상한이다. */
    uint32_t partition_count; /**< 검증된 non-empty partition 수다. */
    uint64_t first_usable_lba; /**< Header가 선언한 first usable LBA다. */
    uint64_t last_usable_lba; /**< Header가 선언한 last usable LBA다. */
    uint64_t disk_last_lba; /**< Input byte range에서 계산한 last LBA다. */
};

/** @brief GPT/MBR parser의 안정적인 결과다. */
enum RibonGptStatus {
    RIBON_GPT_STATUS_OK = 0,
    RIBON_GPT_STATUS_BAD_ARGUMENT = -1,
    RIBON_GPT_STATUS_TRUNCATED = -2,
    RIBON_GPT_STATUS_BAD_PROTECTIVE_MBR = -3,
    RIBON_GPT_STATUS_BAD_HEADER = -4,
    RIBON_GPT_STATUS_BAD_HEADER_CRC = -5,
    RIBON_GPT_STATUS_BAD_ENTRIES_CRC = -6,
    RIBON_GPT_STATUS_OVERFLOW = -7,
    RIBON_GPT_STATUS_OUT_OF_CAPACITY = -8,
    RIBON_GPT_STATUS_OUT_OF_RANGE = -9,
    RIBON_GPT_STATUS_OVERLAP = -10,
};

/**
 * @brief Memory-resident GPT disk view를 byte-wise로 검증하고 non-empty entries를 반환한다.
 *
 * Input은 protective MBR LBA 0과 primary GPT header LBA 1 및 declared entry array를
 * 모두 포함해야 한다. 이 parser는 packed C struct cast나 writable repair를 수행하지 않는다.
 */
int ribon_gpt_parse_bytes(
    const void *disk_bytes,
    uint64_t disk_byte_count,
    uint32_t logical_block_bytes,
    struct RibonGptTable *out);

/** @brief GPT parser status의 안정적인 이름을 반환한다. */
const char *ribon_gpt_status_name(enum RibonGptStatus status);

#endif
