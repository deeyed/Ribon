#ifndef RIBON_PROTOCOLS_OS_PARUS_RPH1_H
#define RIBON_PROTOCOLS_OS_PARUS_RPH1_H

#include <stdint.h>

#include <Ribon/protocol/protocol.h>

struct RibonBootEnvironment;
struct RibonBootPlan;
struct RibonMutableMemoryMap;

/*
 * Parus Handoff v1 (RPH1) is a byte-addressed little-endian wire format.
 * These constants are offsets and encoded sizes, not native C layouts.
 */
#define RIBON_PARUS_RPH1_MAGIC 0x31485052u
#define RIBON_PARUS_RPH1_VERSION_MAJOR 1u
#define RIBON_PARUS_RPH1_VERSION_MINOR 0u
#define RIBON_PARUS_RPH1_HEADER_SIZE 64u
#define RIBON_PARUS_RPH1_SECTION_ENTRY_SIZE 32u
#define RIBON_PARUS_RPH1_MAX_SECTIONS 32u
#define RIBON_PARUS_RPH1_MAX_TOTAL_SIZE 65536u
#define RIBON_PARUS_RPH1_PAYLOAD_ALIGNMENT 16u

#define RIBON_PARUS_RPH1_HEADER_MAGIC_OFFSET 0u
#define RIBON_PARUS_RPH1_HEADER_VERSION_MAJOR_OFFSET 4u
#define RIBON_PARUS_RPH1_HEADER_VERSION_MINOR_OFFSET 6u
#define RIBON_PARUS_RPH1_HEADER_SIZE_OFFSET 8u
#define RIBON_PARUS_RPH1_HEADER_SECTION_ENTRY_SIZE_OFFSET 10u
#define RIBON_PARUS_RPH1_HEADER_SECTION_COUNT_OFFSET 12u
#define RIBON_PARUS_RPH1_HEADER_RESERVED0_OFFSET 14u
#define RIBON_PARUS_RPH1_HEADER_TOTAL_SIZE_OFFSET 16u
#define RIBON_PARUS_RPH1_HEADER_SECTION_TABLE_OFFSET 20u
#define RIBON_PARUS_RPH1_HEADER_FLAGS_OFFSET 24u
#define RIBON_PARUS_RPH1_HEADER_CRC32C_OFFSET 32u
#define RIBON_PARUS_RPH1_HEADER_RESERVED1_OFFSET 36u
#define RIBON_PARUS_RPH1_HEADER_BOOT_GENERATION_OFFSET 40u
#define RIBON_PARUS_RPH1_HEADER_MANIFEST_SEQUENCE_OFFSET 48u
#define RIBON_PARUS_RPH1_HEADER_RESERVED2_OFFSET 56u

#define RIBON_PARUS_RPH1_SECTION_TYPE_OFFSET 0u
#define RIBON_PARUS_RPH1_SECTION_FLAGS_OFFSET 4u
#define RIBON_PARUS_RPH1_SECTION_PAYLOAD_OFFSET 8u
#define RIBON_PARUS_RPH1_SECTION_LENGTH_OFFSET 16u
#define RIBON_PARUS_RPH1_SECTION_ALIGNMENT_OFFSET 24u
#define RIBON_PARUS_RPH1_SECTION_RESERVED_OFFSET 28u

#define RIBON_PARUS_RPH1_FLAG_NONE 0ull

#define RIBON_PARUS_RPH1_SECTION_REQUIRED_TO_UNDERSTAND (1u << 0)
#define RIBON_PARUS_RPH1_SECTION_BORROWED_RANGE_DESCRIPTOR (1u << 1)

#define RIBON_PARUS_RPH1_SECTION_MEMORY_MAP 0x00000001u
#define RIBON_PARUS_RPH1_SECTION_RESERVED_RANGES 0x00000002u
#define RIBON_PARUS_RPH1_SECTION_KERNEL_LAYOUT 0x00000003u
#define RIBON_PARUS_RPH1_SECTION_DEVICE_TREE 0x00000004u
#define RIBON_PARUS_RPH1_SECTION_ACPI 0x00000005u
#define RIBON_PARUS_RPH1_SECTION_COMMAND_LINE 0x00000006u
#define RIBON_PARUS_RPH1_SECTION_FRAMEBUFFER 0x00000007u
#define RIBON_PARUS_RPH1_SECTION_MODULES 0x00000008u
#define RIBON_PARUS_RPH1_SECTION_BOOT_MEDIA 0x00000009u
#define RIBON_PARUS_RPH1_SECTION_PROVENANCE 0x0000000au
#define RIBON_PARUS_RPH1_SECTION_OVERSEER 0x0000000bu

#define RIBON_PARUS_RPH1_REGION_ENTRY_SIZE 32u
#define RIBON_PARUS_RPH1_KERNEL_LAYOUT_SIZE 128u
#define RIBON_PARUS_RPH1_KERNEL_SEGMENT_SIZE 64u
#define RIBON_PARUS_RPH1_RANGE_DESCRIPTOR_SIZE 32u
#define RIBON_PARUS_RPH1_FRAMEBUFFER_SIZE 48u
#define RIBON_PARUS_RPH1_MODULE_ENTRY_SIZE 32u
#define RIBON_PARUS_RPH1_BOOT_MEDIA_SIZE 32u
#define RIBON_PARUS_RPH1_PROVENANCE_SIZE 32u
#define RIBON_PARUS_RPH1_MAX_KERNEL_SEGMENTS 16u

enum RibonParusRph1ParseStatus {
    RIBON_PARUS_RPH1_PARSE_OK = 0,
    RIBON_PARUS_RPH1_PARSE_BAD_ARGUMENT = -1,
    RIBON_PARUS_RPH1_PARSE_TRUNCATED = -2,
    RIBON_PARUS_RPH1_PARSE_BAD_MAGIC = -3,
    RIBON_PARUS_RPH1_PARSE_UNSUPPORTED_VERSION = -4,
    RIBON_PARUS_RPH1_PARSE_BAD_HEADER = -5,
    RIBON_PARUS_RPH1_PARSE_BAD_CHECKSUM = -6,
    RIBON_PARUS_RPH1_PARSE_BAD_SECTION = -7,
    RIBON_PARUS_RPH1_PARSE_UNKNOWN_REQUIRED_SECTION = -8,
    RIBON_PARUS_RPH1_PARSE_MISSING_REQUIRED_SECTION = -9,
    RIBON_PARUS_RPH1_PARSE_DUPLICATE_SECTION = -10,
};

struct RibonParusRph1View {
    const unsigned char *bytes;
    uint32_t total_size;
    uint32_t section_table_offset;
    uint16_t section_count;
    uint64_t flags;
    uint64_t boot_generation;
    uint64_t manifest_sequence;
};

/** @brief Parus Boot Protocol plugin descriptor다. */
extern const struct RibonPluginDescriptor ribon_parus_protocol_plugin_descriptor;

/**
 * @brief RPH1 artifact의 CRC32C 값을 계산한다.
 *
 * @param data CRC 대상 byte sequence.
 * @param size byte 수.
 * @return Castagnoli CRC32C.
 */
uint32_t ribon_parus_rph1_crc32c(const void *data, uint64_t size);

/**
 * @brief Caller-owned buffer에 Parus Handoff v1 artifact를 생성한다.
 */
int ribon_parus_build_rph1(
    const struct RibonBootPlan *plan,
    const struct RibonBootEnvironment *environment,
    const struct RibonMutableMemoryMap *normalized_memory_map,
    void *buffer,
    uint64_t capacity,
    struct RibonHandoffArtifact *out);

/**
 * @brief Untrusted RPH1 artifact를 bounds, checksum, section 규칙까지 검증한다.
 */
int ribon_parus_parse_rph1(
    const void *data,
    uint64_t available_size,
    struct RibonParusRph1View *out);

#endif
/** @brief Parus kernel entry가 해석하는 OS-specific flag다. */
enum RibonParusEntryFlag {
    RIBON_PARUS_ENTRY_FLAG_RPH1 = 1ull << 0,
    RIBON_PARUS_ENTRY_FLAG_DIRECT_DTB = 1ull << 1,
    RIBON_PARUS_ENTRY_FLAG_ENTERED_HIGH = 1ull << 2,
    RIBON_PARUS_ENTRY_FLAG_DIRECT_HIGH = 1ull << 3,
};
