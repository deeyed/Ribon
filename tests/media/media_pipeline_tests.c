#include <Ribon/config/boot_config.h>
#include <Ribon/filesystem/fat32.h>
#include <Ribon/storage/block.h>

#include <stdio.h>

#define TEST_BLOCK_BYTES 512u
#define TEST_GPT_BLOCKS 128u
#define TEST_FAT_BLOCKS 66100u

static unsigned char gpt_disk[TEST_GPT_BLOCKS * TEST_BLOCK_BYTES];
static unsigned char fat_disk[TEST_FAT_BLOCKS * TEST_BLOCK_BYTES];
static unsigned char fat_large_output[600];

/** @brief One failed invariant을 stable test diagnostic으로 기록한다. */
static int expect(int condition, const char *message) {
    if (!condition) {
        fputs("media_pipeline_tests: ", stderr);
        fputs(message, stderr);
        fputc('\n', stderr);
        return 0;
    }
    return 1;
}

/** @brief Test image byte range를 zero로 초기화한다. */
static void zero_bytes(unsigned char *bytes, uint64_t count) {
    for (uint64_t index = 0u; index < count; ++index) {
        bytes[index] = 0u;
    }
}

/** @brief Little-endian 16-bit scalar를 test image에 byte-wise 기록한다. */
static void write_le16(unsigned char *bytes, uint32_t offset, uint16_t value) {
    bytes[offset] = (unsigned char)value;
    bytes[offset + 1u] = (unsigned char)(value >> 8u);
}

/** @brief Little-endian 32-bit scalar를 test image에 byte-wise 기록한다. */
static void write_le32(unsigned char *bytes, uint32_t offset, uint32_t value) {
    for (uint32_t index = 0u; index < 4u; ++index) {
        bytes[offset + index] = (unsigned char)(value >> (index * 8u));
    }
}

/** @brief Little-endian 64-bit scalar를 test image에 byte-wise 기록한다. */
static void write_le64(unsigned char *bytes, uint32_t offset, uint64_t value) {
    for (uint32_t index = 0u; index < 8u; ++index) {
        bytes[offset + index] = (unsigned char)(value >> (index * 8u));
    }
}

/** @brief IEEE GPT CRC32를 test fixture byte range에서 계산한다. */
static uint32_t crc32(const unsigned char *bytes, uint64_t count) {
    uint32_t crc = 0xffffffffu;
    for (uint64_t index = 0u; index < count; ++index) {
        crc ^= bytes[index];
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 1u) != 0u ? (crc >> 1u) ^ 0xedb88320u : crc >> 1u;
        }
    }
    return ~crc;
}

/** @brief GPT header CRC field를 zero로 보고 deterministic test CRC를 계산한다. */
static uint32_t header_crc32(const unsigned char *bytes, uint32_t count) {
    unsigned char copy[92];
    for (uint32_t index = 0u; index < count; ++index) {
        copy[index] = bytes[index];
    }
    for (uint32_t index = 16u; index < 20u; ++index) {
        copy[index] = 0u;
    }
    return crc32(copy, count);
}

/** @brief Valid primary GPT/MBR fixture를 two non-overlapping entries로 만든다. */
static void build_gpt_fixture(int overlap) {
    unsigned char *mbr = gpt_disk;
    unsigned char *header = gpt_disk + TEST_BLOCK_BYTES;
    unsigned char *entries = gpt_disk + TEST_BLOCK_BYTES * 2u;
    zero_bytes(gpt_disk, sizeof(gpt_disk));
    mbr[446u + 4u] = 0xeeu;
    write_le32(mbr, 446u + 8u, 1u);
    write_le32(mbr, 446u + 12u, TEST_GPT_BLOCKS - 1u);
    write_le16(mbr, 510u, 0xaa55u);
    header[0] = 'E'; header[1] = 'F'; header[2] = 'I'; header[3] = ' ';
    header[4] = 'P'; header[5] = 'A'; header[6] = 'R'; header[7] = 'T';
    write_le32(header, 8u, 0x00010000u);
    write_le32(header, 12u, 92u);
    write_le64(header, 24u, 1u);
    write_le64(header, 32u, TEST_GPT_BLOCKS - 1u);
    write_le64(header, 40u, 34u);
    write_le64(header, 48u, 100u);
    write_le64(header, 72u, 2u);
    write_le32(header, 80u, 4u);
    write_le32(header, 84u, 128u);
    entries[0] = 0x28u;
    entries[16u] = 0x11u;
    write_le64(entries, 32u, 34u);
    write_le64(entries, 40u, 50u);
    entries[128u] = 0x29u;
    entries[128u + 16u] = 0x12u;
    write_le64(entries, 128u + 32u, overlap ? 40u : 60u);
    write_le64(entries, 128u + 40u, 70u);
    write_le32(header, 88u, crc32(entries, 4u * 128u));
    write_le32(header, 16u, header_crc32(header, 92u));
}

/** @brief GPT success, CRC, overlap와 protective-MBR failure corpus를 확인한다. */
static int test_gpt(void) {
    struct RibonGptPartition partitions[4];
    struct RibonGptTable table = {
        .partitions = partitions,
        .partition_capacity = 4u,
    };
    build_gpt_fixture(0);
    if (!expect(
            ribon_gpt_parse_bytes(gpt_disk, sizeof(gpt_disk), TEST_BLOCK_BYTES, &table) ==
                RIBON_GPT_STATUS_OK && table.partition_count == 2u &&
                table.partitions[1].first_lba == 60u,
            "valid GPT fixture rejected")) {
        return 0;
    }
    gpt_disk[TEST_BLOCK_BYTES + 56u] ^= 1u;
    if (!expect(
            ribon_gpt_parse_bytes(gpt_disk, sizeof(gpt_disk), TEST_BLOCK_BYTES, &table) ==
                RIBON_GPT_STATUS_BAD_HEADER_CRC,
            "corrupt GPT header CRC accepted")) {
        return 0;
    }
    build_gpt_fixture(1);
    if (!expect(
            ribon_gpt_parse_bytes(gpt_disk, sizeof(gpt_disk), TEST_BLOCK_BYTES, &table) ==
                RIBON_GPT_STATUS_OVERLAP,
            "overlapping GPT entries accepted")) {
        return 0;
    }
    build_gpt_fixture(0);
    gpt_disk[446u + 4u] = 0x83u;
    if (!expect(
            ribon_gpt_parse_bytes(gpt_disk, sizeof(gpt_disk), TEST_BLOCK_BYTES, &table) ==
                RIBON_GPT_STATUS_BAD_PROTECTIVE_MBR,
            "non-protective MBR accepted")) {
        return 0;
    }
    return 1;
}

/** @brief In-memory test disk의 bounded block callback이다. */
static int test_block_read(
    void *context,
    uint64_t first_block,
    void *buffer,
    uint32_t block_count,
    uint64_t deadline_ticks) {
    unsigned char *disk = context;
    uint64_t offset;
    (void)deadline_ticks;
    if (disk == 0 || buffer == 0 || block_count == 0u || first_block >= TEST_FAT_BLOCKS ||
        block_count > TEST_FAT_BLOCKS - first_block) {
        return RIBON_BLOCK_STATUS_OUT_OF_RANGE;
    }
    offset = first_block * TEST_BLOCK_BYTES;
    for (uint64_t index = 0u; index < (uint64_t)block_count * TEST_BLOCK_BYTES; ++index) {
        ((unsigned char *)buffer)[index] = disk[offset + index];
    }
    return RIBON_BLOCK_STATUS_OK;
}

/** @brief Minimal valid FAT32 8.3 directory/file fixture를 caller-owned memory에 만든다. */
static void build_fat32_fixture(uint32_t file_size) {
    unsigned char *bpb = fat_disk;
    unsigned char *fat = fat_disk + 32u * TEST_BLOCK_BYTES;
    unsigned char *root = fat_disk + 552u * TEST_BLOCK_BYTES;
    unsigned char *directory = fat_disk + 553u * TEST_BLOCK_BYTES;
    unsigned char *file = fat_disk + 554u * TEST_BLOCK_BYTES;
    zero_bytes(fat_disk, sizeof(fat_disk));
    write_le16(bpb, 11u, TEST_BLOCK_BYTES);
    bpb[13u] = 1u;
    write_le16(bpb, 14u, 32u);
    bpb[16u] = 1u;
    write_le16(bpb, 17u, 0u);
    write_le32(bpb, 32u, TEST_FAT_BLOCKS);
    write_le32(bpb, 36u, 520u);
    write_le32(bpb, 44u, 2u);
    write_le16(bpb, 510u, 0xaa55u);
    write_le32(fat, 0u, 0x0ffffff8u);
    write_le32(fat, 4u, 0x0fffffffu);
    write_le32(fat, 8u, 0x0fffffffu);
    write_le32(fat, 12u, 0x0fffffffu);
    write_le32(fat, 16u, 0x0fffffffu);
    root[0u] = 'R'; root[1u] = 'I'; root[2u] = 'B'; root[3u] = 'O'; root[4u] = 'N';
    root[5u] = ' '; root[6u] = ' '; root[7u] = ' '; root[8u] = ' '; root[9u] = ' '; root[10u] = ' ';
    root[11u] = 0x10u;
    write_le16(root, 26u, 3u);
    directory[0u] = 'B'; directory[1u] = 'O'; directory[2u] = 'O'; directory[3u] = 'T';
    directory[4u] = ' '; directory[5u] = ' '; directory[6u] = ' '; directory[7u] = ' ';
    directory[8u] = 'C'; directory[9u] = 'F'; directory[10u] = 'G';
    directory[11u] = 0x20u;
    write_le16(directory, 26u, 4u);
    write_le32(directory, 28u, file_size);
    file[0u] = 'h'; file[1u] = 'e'; file[2u] = 'l'; file[3u] = 'l'; file[4u] = 'o'; file[5u] = '\n';
}

/** @brief FAT32 mount, path lookup, exact read와 short-chain failure를 확인한다. */
static int test_fat32(void) {
    unsigned char sector[TEST_BLOCK_BYTES];
    unsigned char output[8];
    struct RibonReadOnlyBlockDevice device = {
        .size = sizeof(device),
        .abi_version = RIBON_READ_ONLY_BLOCK_DEVICE_ABI_VERSION,
        .logical_block_bytes = TEST_BLOCK_BYTES,
        .max_read_blocks = 1u,
        .block_count = TEST_FAT_BLOCKS,
        .context = fat_disk,
        .read = test_block_read,
    };
    struct RibonFat32Scratch scratch = {
        .sector_bytes = sector,
        .sector_capacity = sizeof(sector),
    };
    struct RibonFat32Volume volume;
    struct RibonFat32File file;
    build_fat32_fixture(6u);
    if (!expect(
            ribon_fat32_mount(&volume, &device, 0u, TEST_FAT_BLOCKS, &scratch) ==
                RIBON_FAT32_STATUS_OK &&
                ribon_fat32_open(&volume, "/RIBON/BOOT.CFG", &scratch, &file) ==
                    RIBON_FAT32_STATUS_OK && file.size == 6u &&
                ribon_fat32_read(&volume, &file, 0u, output, 6u, &scratch) ==
                    RIBON_FAT32_STATUS_OK && output[0] == 'h' && output[5] == '\n',
            "valid FAT32 fixture rejected")) {
        return 0;
    }
    if (!expect(
            ribon_fat32_open(&volume, "/RIBON/../BOOT.CFG", &scratch, &file) ==
                RIBON_FAT32_STATUS_BAD_PATH,
            "FAT32 traversal path accepted")) {
        return 0;
    }
    build_fat32_fixture(600u);
    if (!expect(
            ribon_fat32_mount(&volume, &device, 0u, TEST_FAT_BLOCKS, &scratch) ==
                RIBON_FAT32_STATUS_OK &&
                ribon_fat32_open(&volume, "/RIBON/BOOT.CFG", &scratch, &file) ==
                    RIBON_FAT32_STATUS_OK &&
                ribon_fat32_read(&volume, &file, 0u, output, sizeof(output), &scratch) ==
                    RIBON_FAT32_STATUS_OK &&
                ribon_fat32_read(&volume, &file, 0u, fat_large_output, sizeof(fat_large_output), &scratch) ==
                    RIBON_FAT32_STATUS_CORRUPT_CHAIN,
            "FAT32 short chain accepted")) {
        return 0;
    }
    return 1;
}

/** @brief Configuration positive, traversal, unknown-key와 priority-tie corpus를 확인한다. */
static int test_config(void) {
    static const unsigned char valid[] =
        "version=1\n"
        "entry=backup\npriority=10\nprotocol=parus\nimage=elf64\n"
        "kernel=/RIBON/BACKUP.ELF\nend\n"
        "entry=primary\npriority=100\nprotocol=parus\nimage=elf64\n"
        "kernel=/RIBON/PAYLOAD.ELF\ninit_image=/RIBON/INIT.IMG\n"
        "module=/RIBON/EXTRA.IMG\ncmdline=console=ttyS0\nend\n";
    static const unsigned char duplicate_init[] =
        "version=1\nentry=bad\npriority=1\nprotocol=parus\nimage=elf64\n"
        "kernel=/RIBON/PAYLOAD.ELF\ninit_image=/RIBON/A.IMG\n"
        "init_image=/RIBON/B.IMG\nend\n";
    static const unsigned char traversal[] =
        "version=1\nentry=bad\npriority=1\nprotocol=parus\nimage=elf64\n"
        "kernel=/RIBON/../PAYLOAD.ELF\nend\n";
    static const unsigned char unknown[] =
        "version=1\nentry=bad\npriority=1\nprotocol=parus\nimage=elf64\n"
        "kernel=/RIBON/PAYLOAD.ELF\nrequired-unknown=x\nend\n";
    static const unsigned char tied[] =
        "version=1\nentry=a\npriority=1\nprotocol=parus\nimage=elf64\nkernel=/A\nend\n"
        "entry=b\npriority=1\nprotocol=parus\nimage=elf64\nkernel=/B\nend\n";
    struct RibonBootConfiguration configuration;
    const struct RibonBootConfigEntry *selected = 0;
    const int valid_parse = ribon_boot_configuration_parse(valid, sizeof(valid) - 1u, &configuration);
    const int valid_select = ribon_boot_configuration_select(&configuration, &selected);
    if (!expect(
            valid_parse == RIBON_BOOT_CONFIG_STATUS_OK &&
                valid_select == RIBON_BOOT_CONFIG_STATUS_OK && selected != 0 &&
                selected->priority == 100u && selected->module_count == 1u &&
                selected->has_init_image == 1u,
            "valid boot configuration rejected")) {
        fprintf(stderr, "media_pipeline_tests: config parse=%d select=%d\n", valid_parse, valid_select);
        return 0;
    }
    if (!expect(
            ribon_boot_configuration_parse(
                duplicate_init,
                sizeof(duplicate_init) - 1u,
                &configuration) == RIBON_BOOT_CONFIG_STATUS_DUPLICATE_KEY,
            "duplicate initial image accepted")) {
        return 0;
    }
    if (!expect(
            ribon_boot_configuration_parse(traversal, sizeof(traversal) - 1u, &configuration) ==
            RIBON_BOOT_CONFIG_STATUS_BAD_PATH,
            "configuration traversal accepted")) {
        return 0;
    }
    if (!expect(
            ribon_boot_configuration_parse(unknown, sizeof(unknown) - 1u, &configuration) ==
                RIBON_BOOT_CONFIG_STATUS_UNKNOWN_KEY,
            "unknown configuration key accepted")) {
        return 0;
    }
    if (!expect(
            ribon_boot_configuration_parse(tied, sizeof(tied) - 1u, &configuration) ==
                RIBON_BOOT_CONFIG_STATUS_OK &&
                ribon_boot_configuration_select(&configuration, &selected) ==
                    RIBON_BOOT_CONFIG_STATUS_AMBIGUOUS,
            "priority tie accepted")) {
        return 0;
    }
    return 1;
}

/** @brief Deterministic one-byte mutation corpus가 parser crash 없이 terminal status를 내는지 확인한다. */
static int test_fuzz_smoke(void) {
    struct RibonGptPartition partitions[4];
    struct RibonGptTable table = {
        .partitions = partitions,
        .partition_capacity = 4u,
    };
    static const unsigned char config[] =
        "version=1\nentry=primary\npriority=1\nprotocol=parus\nimage=elf64\n"
        "kernel=/RIBON/PAYLOAD.ELF\nend\n";
    for (uint32_t index = 0u; index < 64u; ++index) {
        build_gpt_fixture(0);
        gpt_disk[(uint64_t)index * 17u % sizeof(gpt_disk)] ^= (unsigned char)(1u << (index % 8u));
        (void)ribon_gpt_parse_bytes(gpt_disk, sizeof(gpt_disk), TEST_BLOCK_BYTES, &table);
        (void)ribon_boot_configuration_parse(config, index % sizeof(config), &(struct RibonBootConfiguration){0});
    }
    return 1;
}

/** @brief Media parser unit과 deterministic fuzz smoke entrypoint를 실행한다. */
int main(int argc, char **argv) {
    const int fuzz_smoke = argc == 2 && argv[1][0] == '-' && argv[1][1] == '-' &&
        argv[1][2] == 'f' && argv[1][3] == 'u' && argv[1][4] == 'z' && argv[1][5] == 'z' &&
        argv[1][6] == '-' && argv[1][7] == 's' && argv[1][8] == 'm' && argv[1][9] == 'o' &&
        argv[1][10] == 'k' && argv[1][11] == 'e' && argv[1][12] == '\0';
    if (!test_gpt() || !test_fat32() || !test_config() || (fuzz_smoke && !test_fuzz_smoke())) {
        return 1;
    }
    puts(fuzz_smoke ? "RIBON-R8-MEDIA-FUZZ-REPLAY-OK" : "RIBON-R8-MEDIA-PIPELINE-OK");
    return 0;
}
