#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PE_BYTE_LIMIT (16u * 1024u * 1024u)

static uint16_t read_u16(const unsigned char *bytes, size_t offset) {
    return (uint16_t)bytes[offset] | ((uint16_t)bytes[offset + 1u] << 8u);
}

static uint32_t read_u32(const unsigned char *bytes, size_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

int main(int argc, char **argv) {
    FILE *stream;
    unsigned char *bytes;
    long length;
    size_t pe;
    size_t optional;
    size_t sections;
    uint16_t section_count;
    uint16_t optional_size;
    int text_seen = 0;
    int reloc_seen = 0;
    if (argc != 2) {
        fprintf(stderr, "usage: %s BOOTAA64.EFI\n", argv[0]);
        return 2;
    }
    stream = fopen(argv[1], "rb");
    if (stream == NULL || fseek(stream, 0, SEEK_END) != 0 ||
        (length = ftell(stream)) < 0 || (unsigned long)length > PE_BYTE_LIMIT ||
        fseek(stream, 0, SEEK_SET) != 0) {
        return 1;
    }
    bytes = malloc((size_t)length);
    if (bytes == NULL || fread(bytes, 1u, (size_t)length, stream) != (size_t)length ||
        fclose(stream) != 0 || length < 128) {
        free(bytes);
        return 1;
    }
    pe = read_u32(bytes, 0x3cu);
    if (pe > (size_t)length - 24u || memcmp(bytes + pe, "PE\0\0", 4u) != 0 ||
        read_u16(bytes, pe + 4u) != 0xaa64u) {
        free(bytes);
        return 1;
    }
    section_count = read_u16(bytes, pe + 6u);
    optional_size = read_u16(bytes, pe + 20u);
    optional = pe + 24u;
    sections = optional + optional_size;
    if (section_count < 2u || section_count > 16u || optional_size < 70u ||
        sections > (size_t)length ||
        (size_t)section_count > ((size_t)length - sections) / 40u ||
        read_u16(bytes, optional) != 0x020bu ||
        read_u32(bytes, optional + 16u) == 0u ||
        read_u16(bytes, optional + 68u) != 10u) {
        free(bytes);
        return 1;
    }
    for (uint16_t index = 0u; index < section_count; ++index) {
        const unsigned char *name = bytes + sections + (size_t)index * 40u;
        text_seen |= memcmp(name, ".text\0\0\0", 8u) == 0;
        reloc_seen |= memcmp(name, ".reloc\0\0", 8u) == 0;
    }
    free(bytes);
    if (!text_seen || !reloc_seen) {
        return 1;
    }
    puts("RIBON-AARCH64-UEFI-PE-OK machine=0xaa64 subsystem=efi_application reloc=1");
    return 0;
}
