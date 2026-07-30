#ifndef RIBOS_ARTIFACT_INTERNAL_H
#define RIBOS_ARTIFACT_INTERNAL_H

#include "ribos/artifact/format.h"

#include <stddef.h>
#include <stdint.h>

#define RIBOS_ARTIFACT_ENVELOPE_MAGIC "RIBOSA1\0"
#define RIBOS_ARTIFACT_PAYLOAD_MAGIC "RIBBC01\0"
#define RIBOS_ARTIFACT_MAGIC_BYTES 8u

#define RIBOS_ENVELOPE_MAGIC_OFFSET 0u
#define RIBOS_ENVELOPE_MAJOR_OFFSET 8u
#define RIBOS_ENVELOPE_MINOR_OFFSET 10u
#define RIBOS_ENVELOPE_HEADER_BYTES_OFFSET 12u
#define RIBOS_ENVELOPE_FLAGS_OFFSET 16u
#define RIBOS_ENVELOPE_HASH_ALGORITHM_OFFSET 20u
#define RIBOS_ENVELOPE_SIGNATURE_ALGORITHM_OFFSET 22u
#define RIBOS_ENVELOPE_PAYLOAD_OFFSET_OFFSET 24u
#define RIBOS_ENVELOPE_PAYLOAD_LENGTH_OFFSET 32u
#define RIBOS_ENVELOPE_KEY_ID_OFFSET_OFFSET 40u
#define RIBOS_ENVELOPE_KEY_ID_LENGTH_OFFSET 48u
#define RIBOS_ENVELOPE_SIGNATURE_LENGTH_OFFSET 52u
#define RIBOS_ENVELOPE_SIGNATURE_OFFSET_OFFSET 56u
#define RIBOS_ENVELOPE_TOTAL_LENGTH_OFFSET 64u
#define RIBOS_ENVELOPE_HASH_OFFSET 72u
#define RIBOS_ENVELOPE_RESERVED_OFFSET 104u

#define RIBOS_PAYLOAD_MAGIC_OFFSET 0u
#define RIBOS_PAYLOAD_VM_MAJOR_OFFSET 8u
#define RIBOS_PAYLOAD_VM_MINOR_OFFSET 10u
#define RIBOS_PAYLOAD_ISA_MAJOR_OFFSET 12u
#define RIBOS_PAYLOAD_ISA_MINOR_OFFSET 14u
#define RIBOS_PAYLOAD_HEADER_BYTES_OFFSET 16u
#define RIBOS_PAYLOAD_FLAGS_OFFSET 20u
#define RIBOS_PAYLOAD_SECTION_COUNT_OFFSET 24u
#define RIBOS_PAYLOAD_ENTRY_FUNCTION_OFFSET 28u
#define RIBOS_PAYLOAD_REGISTER_COUNT_OFFSET 32u
#define RIBOS_PAYLOAD_SLOT_COUNT_OFFSET 36u
#define RIBOS_PAYLOAD_DECLARED_CAPS_OFFSET 40u
#define RIBOS_PAYLOAD_REQUIRED_CAPS_OFFSET 44u
#define RIBOS_PAYLOAD_INSTRUCTION_BUDGET_OFFSET 48u
#define RIBOS_PAYLOAD_INSTRUCTION_UPPER_OFFSET 56u
#define RIBOS_PAYLOAD_HELPER_BUDGET_OFFSET 64u
#define RIBOS_PAYLOAD_HELPER_UPPER_OFFSET 72u
#define RIBOS_PAYLOAD_STACK_BYTES_OFFSET 80u
#define RIBOS_PAYLOAD_CALL_DEPTH_OFFSET 88u
#define RIBOS_PAYLOAD_SCHEMA_DIGEST_OFFSET 96u
#define RIBOS_PAYLOAD_DIRECTORY_OFFSET_OFFSET 128u
#define RIBOS_PAYLOAD_DIRECTORY_LENGTH_OFFSET 136u
#define RIBOS_PAYLOAD_LENGTH_OFFSET 144u
#define RIBOS_PAYLOAD_RESERVED_OFFSET 152u

#define RIBOS_SECTION_KIND_OFFSET 0u
#define RIBOS_SECTION_FLAGS_OFFSET 2u
#define RIBOS_SECTION_ROW_SIZE_OFFSET 4u
#define RIBOS_SECTION_DATA_OFFSET_OFFSET 8u
#define RIBOS_SECTION_DATA_LENGTH_OFFSET 16u
#define RIBOS_SECTION_COUNT_OFFSET 24u
#define RIBOS_SECTION_RESERVED_OFFSET 28u

typedef struct RibosArtifactWriter {
    uint8_t *output;
    size_t capacity;
    int failed;
} RibosArtifactWriter;

/**
 * Target-safe streaming SHA-256 context shared by artifact and VM internals.
 *
 * This is not a serialized ABI.  Public callers use the one-shot artifact and
 * schema identity APIs instead.
 */
typedef struct RibosArtifactSha256 {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t block[64];
    size_t block_size;
} RibosArtifactSha256;

int ribos_artifact_size_add(
    size_t left,
    size_t right,
    size_t *result);

int ribos_artifact_size_multiply(
    size_t left,
    size_t right,
    size_t *result);

int ribos_artifact_size_align(
    size_t value,
    size_t alignment,
    size_t *result);

void ribos_artifact_writer_bytes(
    RibosArtifactWriter *writer,
    size_t offset,
    const uint8_t *bytes,
    size_t byte_count);

void ribos_artifact_writer_u8(
    RibosArtifactWriter *writer,
    size_t offset,
    uint8_t value);

void ribos_artifact_writer_u16(
    RibosArtifactWriter *writer,
    size_t offset,
    uint16_t value);

void ribos_artifact_writer_u32(
    RibosArtifactWriter *writer,
    size_t offset,
    uint32_t value);

void ribos_artifact_writer_u64(
    RibosArtifactWriter *writer,
    size_t offset,
    uint64_t value);

int ribos_artifact_reader_bytes(
    const uint8_t *input,
    size_t input_size,
    size_t offset,
    size_t byte_count,
    const uint8_t **bytes);

int ribos_artifact_reader_u16(
    const uint8_t *input,
    size_t input_size,
    size_t offset,
    uint16_t *value);

int ribos_artifact_reader_u32(
    const uint8_t *input,
    size_t input_size,
    size_t offset,
    uint32_t *value);

int ribos_artifact_reader_u64(
    const uint8_t *input,
    size_t input_size,
    size_t offset,
    uint64_t *value);

int ribos_artifact_bytes_are_zero(
    const uint8_t *input,
    size_t byte_count);

void ribos_artifact_sha256(
    const uint8_t *input,
    size_t input_size,
    uint8_t digest[RIBOS_SCHEMA_DIGEST_BYTES]);

void ribos_artifact_sha256_initialize(
    RibosArtifactSha256 *context);

void ribos_artifact_sha256_update(
    RibosArtifactSha256 *context,
    const uint8_t *input,
    size_t input_size);

void ribos_artifact_sha256_finish(
    RibosArtifactSha256 *context,
    uint8_t digest[RIBOS_SCHEMA_DIGEST_BYTES]);

#endif
