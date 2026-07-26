#include <Ribon/firmware.h>
#include <Ribon/memory.h>
#include <Ribon/profiles/parus/rph1.h>
#include <Ribon/ribon.h>

#include <stdio.h>
#include <string.h>

static int failures;

static uint16_t read_u16(const unsigned char *bytes, uint64_t offset) {
    return (uint16_t)bytes[offset] | ((uint16_t)bytes[offset + 1u] << 8u);
}

static uint32_t read_u32(const unsigned char *bytes, uint64_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

static uint64_t read_u64(const unsigned char *bytes, uint64_t offset) {
    uint64_t value = 0u;
    for (uint32_t index = 0; index < 8u; ++index) {
        value |= (uint64_t)bytes[offset + index] << (index * 8u);
    }
    return value;
}

static void write_u32(unsigned char *bytes, uint64_t offset, uint32_t value) {
    bytes[offset] = (unsigned char)(value & 0xffu);
    bytes[offset + 1u] = (unsigned char)((value >> 8u) & 0xffu);
    bytes[offset + 2u] = (unsigned char)((value >> 16u) & 0xffu);
    bytes[offset + 3u] = (unsigned char)((value >> 24u) & 0xffu);
}

static void write_u64(unsigned char *bytes, uint64_t offset, uint64_t value) {
    for (uint32_t index = 0; index < 8u; ++index) {
        bytes[offset + index] = (unsigned char)((value >> (index * 8u)) & 0xffu);
    }
}

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "RPH1-TEST-FAIL: %s\n", message);
        ++failures;
    }
}

static void refresh_checksum(unsigned char *bytes) {
    uint32_t total_size = read_u32(bytes, RIBON_PARUS_RPH1_HEADER_TOTAL_SIZE_OFFSET);
    write_u32(bytes, RIBON_PARUS_RPH1_HEADER_CRC32C_OFFSET, 0u);
    write_u32(
        bytes,
        RIBON_PARUS_RPH1_HEADER_CRC32C_OFFSET,
        ribon_parus_rph1_crc32c(bytes, total_size));
}

static int build_fixture(unsigned char *buffer, struct RibonHandoffArtifact *artifact) {
    static const struct RibonArchDescriptor arch = {
        .canonical_name = "x86_64",
    };
    static const struct RibonLoadSegment segments[] = {
        {
            .file_offset = 0x100u,
            .file_size = 0x800u,
            .memory_size = 0x1000u,
            .virtual_address = 0xffffffff80000000ull,
            .linked_physical_address = 0x200000u,
            .physical_address = 0x200000u,
            .load_address = 0x200000u,
            .runtime_address = 0x200000u,
            .alignment = 0x1000u,
            .flags = RIBON_LOAD_SEGMENT_READ | RIBON_LOAD_SEGMENT_EXECUTE,
        },
    };
    static const struct RibonMemoryRegion regions[] = {
        {
            .base = 0x100000u,
            .length = 0x100000u,
            .kind = RIBON_MEMORY_REGION_USABLE,
            .attributes = RIBON_MEMORY_ATTR_READ | RIBON_MEMORY_ATTR_WRITE,
        },
        {
            .base = 0x200000u,
            .length = 0x1000u,
            .kind = RIBON_MEMORY_REGION_KERNEL_IMAGE,
            .attributes = RIBON_MEMORY_ATTR_READ | RIBON_MEMORY_ATTR_EXECUTE,
        },
    };
    struct RibonMutableMemoryMap map = {
        .regions = (struct RibonMemoryRegion *)regions,
        .region_count = 2u,
        .capacity = 2u,
    };
    struct RibonBootEnvironment environment = {
        .firmware = RIBON_FIRMWARE_UEFI,
        .arch = &arch,
        .boot_media = {
            .kind = RIBON_BOOT_MEDIA_FILE,
            .size = 0x900u,
        },
        .command_line = {
            .text = "profile=parus",
            .length = 13u,
        },
        .flags = RIBON_BOOT_ENV_HAS_MEMORY_MAP |
                 RIBON_BOOT_ENV_HAS_BOOT_MEDIA |
                 RIBON_BOOT_ENV_HAS_COMMAND_LINE,
    };
    struct RibonBootPlan plan = {
        .firmware = RIBON_FIRMWARE_UEFI,
        .arch = &arch,
        .kernel_load_segment_count = 1u,
        .kernel_entry_point = 0xffffffff80000080ull,
        .kernel_entry_load_address = 0x200080u,
        .kernel_runtime_entry_address = 0x200080u,
        .kernel_load_base = 0x200000u,
        .kernel_load_end = 0x201000u,
        .kernel_runtime_load_base = 0x200000u,
        .kernel_runtime_load_end = 0x201000u,
        .kernel_memory_size = 0x1000u,
        .kernel_linked_virtual_base = 0xffffffff80000000ull,
        .kernel_linked_virtual_end = 0xffffffff80001000ull,
        .kernel_linked_physical_base = 0x200000u,
        .kernel_linked_physical_end = 0x201000u,
        .kernel_high_entry_virtual_address = 0xffffffff80000000ull,
        .kernel_high_entry_load_address = 0x200000u,
        .kernel_load_segments = segments,
        .kernel_load_plan_flags = RIBON_LOAD_PLAN_ENTRY_LOAD_VALID |
                                  RIBON_LOAD_PLAN_RUNTIME_ENTRY_VALID |
                                  RIBON_LOAD_PLAN_USES_PADDR |
                                  RIBON_LOAD_PLAN_HAS_HIGHER_HALF,
    };
    memset(buffer, 0xa5, RIBON_PARUS_RPH1_MAX_TOTAL_SIZE);
    return ribon_parus_build_rph1(
        &plan,
        &environment,
        &map,
        buffer,
        RIBON_PARUS_RPH1_MAX_TOTAL_SIZE,
        artifact);
}

int main(void) {
    unsigned char valid[RIBON_PARUS_RPH1_MAX_TOTAL_SIZE];
    unsigned char mutated[RIBON_PARUS_RPH1_MAX_TOTAL_SIZE];
    struct RibonHandoffArtifact artifact = {0};
    struct RibonParusRph1View view = {0};
    uint32_t total_size;
    uint32_t table_offset;
    uint64_t first_payload;

    expect(RIBON_KERNEL_ENTRY_FLAG_RPH1 == 0x1u, "RPH1 entry flag is bit 0");
    expect(RIBON_KERNEL_ENTRY_FLAG_DIRECT_DTB == 0x2u, "direct DTB entry flag is bit 1");
    expect(RIBON_KERNEL_ENTRY_FLAG_ENTERED_HIGH == 0x4u, "entered-high entry flag is bit 2");
    expect(RIBON_KERNEL_ENTRY_FLAG_DIRECT_HIGH == 0x8u, "direct-high entry flag is bit 3");
    expect(build_fixture(valid, &artifact) == RIBON_PROFILE_HANDOFF_STATUS_OK, "builder accepts fixture");
    total_size = read_u32(valid, RIBON_PARUS_RPH1_HEADER_TOTAL_SIZE_OFFSET);
    table_offset = read_u32(valid, RIBON_PARUS_RPH1_HEADER_SECTION_TABLE_OFFSET);
    first_payload = read_u64(
        valid + table_offset,
        RIBON_PARUS_RPH1_SECTION_PAYLOAD_OFFSET);

    expect(valid[0] == 'R' && valid[1] == 'P' && valid[2] == 'H' && valid[3] == '1',
           "wire magic is ASCII RPH1");
    expect(read_u16(valid, RIBON_PARUS_RPH1_HEADER_VERSION_MAJOR_OFFSET) == 1u,
           "major version is 1");
    expect(read_u16(valid, RIBON_PARUS_RPH1_HEADER_SIZE_OFFSET) == 64u,
           "header size is 64");
    expect(read_u16(valid, RIBON_PARUS_RPH1_HEADER_SECTION_ENTRY_SIZE_OFFSET) == 32u,
           "section entry size is 32");
    expect(artifact.size == total_size && artifact.version_major == 1u,
           "artifact metadata reflects wire header");
    expect(strcmp(artifact.format, "rph1") == 0, "artifact format is rph1");
    expect(read_u32(valid, RIBON_PARUS_RPH1_HEADER_CRC32C_OFFSET) ==
               ribon_parus_rph1_crc32c(valid, total_size),
           "stored CRC32C covers the complete artifact");
    expect(ribon_parus_parse_rph1(valid, artifact.size, &view) ==
               RIBON_PARUS_RPH1_PARSE_OK,
           "parser accepts builder output");
    expect(view.total_size == artifact.size && view.section_count == artifact.section_count,
           "parser publishes bounded view metadata");

    memcpy(mutated, valid, total_size);
    mutated[first_payload] ^= 0x1u;
    expect(ribon_parus_parse_rph1(mutated, total_size, &view) ==
               RIBON_PARUS_RPH1_PARSE_BAD_CHECKSUM,
           "parser rejects payload corruption");

    memcpy(mutated, valid, total_size);
    mutated[RIBON_PARUS_RPH1_HEADER_VERSION_MAJOR_OFFSET] = 2u;
    refresh_checksum(mutated);
    expect(ribon_parus_parse_rph1(mutated, total_size, &view) ==
               RIBON_PARUS_RPH1_PARSE_UNSUPPORTED_VERSION,
           "parser rejects unsupported major version");

    memcpy(mutated, valid, total_size);
    write_u32(
        mutated + table_offset,
        RIBON_PARUS_RPH1_SECTION_TYPE_OFFSET,
        0x80000000u);
    refresh_checksum(mutated);
    expect(ribon_parus_parse_rph1(mutated, total_size, &view) ==
               RIBON_PARUS_RPH1_PARSE_UNKNOWN_REQUIRED_SECTION,
           "parser rejects unknown required section");

    memcpy(mutated, valid, total_size);
    write_u32(
        mutated + table_offset + RIBON_PARUS_RPH1_SECTION_ENTRY_SIZE,
        RIBON_PARUS_RPH1_SECTION_TYPE_OFFSET,
        RIBON_PARUS_RPH1_SECTION_MEMORY_MAP);
    refresh_checksum(mutated);
    expect(ribon_parus_parse_rph1(mutated, total_size, &view) ==
               RIBON_PARUS_RPH1_PARSE_DUPLICATE_SECTION,
           "parser rejects singleton duplication");

    memcpy(mutated, valid, total_size);
    write_u64(
        mutated + table_offset + RIBON_PARUS_RPH1_SECTION_ENTRY_SIZE,
        RIBON_PARUS_RPH1_SECTION_PAYLOAD_OFFSET,
        first_payload);
    refresh_checksum(mutated);
    expect(ribon_parus_parse_rph1(mutated, total_size, &view) ==
               RIBON_PARUS_RPH1_PARSE_BAD_SECTION,
           "parser rejects overlapping payloads");

    memcpy(mutated, valid, total_size);
    mutated[RIBON_PARUS_RPH1_HEADER_RESERVED0_OFFSET] = 1u;
    expect(ribon_parus_parse_rph1(mutated, total_size, &view) ==
               RIBON_PARUS_RPH1_PARSE_BAD_HEADER,
           "parser rejects nonzero reserved header fields");

    memcpy(mutated, valid, total_size);
    write_u32(
        mutated,
        RIBON_PARUS_RPH1_HEADER_SECTION_TABLE_OFFSET,
        table_offset + 1u);
    refresh_checksum(mutated);
    expect(ribon_parus_parse_rph1(mutated, total_size, &view) ==
               RIBON_PARUS_RPH1_PARSE_BAD_HEADER,
           "parser rejects misaligned section table");

    if (failures != 0) {
        return 1;
    }
    puts("RPH1-TEST-OK");
    return 0;
}
