#include "../../language/ribos/artifact/src/internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct FileBytes {
    unsigned char *data;
    size_t size;
};

static struct FileBytes load(const char *path, size_t limit) {
    FILE *stream = fopen(path, "rb");
    struct FileBytes result = {0};
    long length;
    if (stream == NULL || fseek(stream, 0, SEEK_END) != 0 ||
        (length = ftell(stream)) < 0 || (unsigned long)length > limit ||
        fseek(stream, 0, SEEK_SET) != 0) {
        if (stream != NULL) {
            fclose(stream);
        }
        return result;
    }
    result.data = calloc((size_t)length + 1u, 1u);
    if (result.data == NULL ||
        fread(result.data, 1u, (size_t)length, stream) != (size_t)length ||
        fclose(stream) != 0) {
        free(result.data);
        result.data = NULL;
        return result;
    }
    result.size = (size_t)length;
    return result;
}

static uint16_t read_u16(const unsigned char *bytes, size_t offset) {
    return (uint16_t)bytes[offset] | ((uint16_t)bytes[offset + 1u] << 8u);
}

static uint64_t read_u64(const unsigned char *bytes, size_t offset) {
    uint64_t value = 0u;
    for (size_t index = 0u; index < 8u; ++index) {
        value |= (uint64_t)bytes[offset + index] << (index * 8u);
    }
    return value;
}

static int contains(const struct FileBytes *file, const char *text) {
    const size_t length = strlen(text);
    if (file->data == NULL || length > file->size) {
        return 0;
    }
    for (size_t offset = 0u; offset <= file->size - length; ++offset) {
        if (memcmp(file->data + offset, text, length) == 0) {
            return 1;
        }
    }
    return 0;
}

static void digest_hex(const uint8_t digest[32], char out[65]) {
    static const char digits[] = "0123456789abcdef";
    for (size_t index = 0u; index < 32u; ++index) {
        out[index * 2u] = digits[digest[index] >> 4u];
        out[index * 2u + 1u] = digits[digest[index] & 15u];
    }
    out[64] = '\0';
}

int main(int argc, char **argv) {
    static const char expected_config[] =
        "version=1\nentry=primary\npriority=100\nprotocol=protocol.luca\n"
        "image=image.elf64\nkernel=/RIBON/PAYLOAD.ELF\n"
        "init_image=/RIBON/INIT.IMG\nend\n";
    static const char init_prefix[] = "RIBON-OPAQUE-INITIAL-IMAGE-V1\n";
    struct FileBytes manifest;
    struct FileBytes registry;
    struct FileBytes report;
    struct FileBytes config;
    struct FileBytes fixture;
    struct FileBytes init;
    uint8_t digest[32];
    char hex[65];
    int failed = 0;
    if (argc != 7) {
        fprintf(stderr, "usage: %s MANIFEST REGISTRY REPORT CONFIG FIXTURE INIT\n", argv[0]);
        return 2;
    }
    manifest = load(argv[1], 64u * 1024u);
    registry = load(argv[2], 256u * 1024u);
    report = load(argv[3], 64u * 1024u);
    config = load(argv[4], 4096u);
    fixture = load(argv[5], 64u * 1024u);
    init = load(argv[6], 8192u);
    if (manifest.data == NULL || registry.data == NULL || report.data == NULL ||
        config.data == NULL || fixture.data == NULL || init.data == NULL) {
        failed = 1;
    } else {
        ribos_artifact_sha256(manifest.data, manifest.size, digest);
        digest_hex(digest, hex);
        failed |= !contains(&registry, "RIBON_ARCH_MASK_AARCH64");
        failed |= contains(&registry, "RIBON_ARCH_MASK_X86_64");
        failed |= !contains(&registry, "ribon_uefi_app_environment_plugin_descriptor");
        failed |= !contains(&registry, "bootmgr.aarch64-uefi-parus-fixture");
        failed |= !contains(&report, hex);
        failed |= !contains(&report, "\"target_id\": \"aarch64-uefi-parus-fixture\"");
        failed |= config.size != sizeof(expected_config) - 1u ||
                  memcmp(config.data, expected_config, sizeof(expected_config) - 1u) != 0;
        failed |= fixture.size < 0x1000u + 64u ||
                  memcmp(fixture.data, "\x7f" "ELF", 4u) != 0 ||
                  read_u16(fixture.data, 18u) != 183u ||
                  read_u64(fixture.data, 24u) != 0x41000000ull ||
                  read_u64(fixture.data, 80u) != 0x41000000ull ||
                  !contains(&fixture, "RIBON-FIXTURE-PAYLOAD-V1");
        failed |= init.size != 4096u ||
                  memcmp(init.data, init_prefix, sizeof(init_prefix) - 1u) != 0;
        for (size_t index = sizeof(init_prefix) - 1u; index < init.size; ++index) {
            failed |= init.data[index] != 0u;
        }
    }
    free(manifest.data);
    free(registry.data);
    free(report.data);
    free(config.data);
    free(fixture.data);
    free(init.data);
    if (failed) {
        fputs("RIBON-AARCH64-UEFI-HOST-TOOLS-FAIL\n", stderr);
        return 1;
    }
    puts("RIBON-AARCH64-UEFI-HOST-TOOLS-OK deterministic=1 hostile=1 python=0");
    return 0;
}
