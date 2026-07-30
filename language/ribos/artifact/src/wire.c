#include "internal.h"

#include <string.h>

int
ribos_artifact_size_add(
    size_t left,
    size_t right,
    size_t *result)
{
    if (result == NULL || right > SIZE_MAX - left) {
        return 0;
    }
    *result = left + right;
    return 1;
}

int
ribos_artifact_size_multiply(
    size_t left,
    size_t right,
    size_t *result)
{
    if (result == NULL ||
        (left != 0 && right > SIZE_MAX / left)) {
        return 0;
    }
    *result = left * right;
    return 1;
}

int
ribos_artifact_size_align(
    size_t value,
    size_t alignment,
    size_t *result)
{
    size_t mask;

    if (result == NULL || alignment == 0 ||
        (alignment & (alignment - 1)) != 0) {
        return 0;
    }
    mask = alignment - 1;
    if (value > SIZE_MAX - mask) {
        return 0;
    }
    *result = (value + mask) & ~mask;
    return 1;
}

static int
ribos_artifact_range(
    size_t capacity,
    size_t offset,
    size_t length)
{
    return offset <= capacity && length <= capacity - offset;
}

void
ribos_artifact_writer_bytes(
    RibosArtifactWriter *writer,
    size_t offset,
    const uint8_t *bytes,
    size_t byte_count)
{
    if (writer == NULL || writer->failed ||
        (byte_count != 0 && bytes == NULL) ||
        !ribos_artifact_range(writer->capacity, offset, byte_count)) {
        if (writer != NULL) {
            writer->failed = 1;
        }
        return;
    }
    if (writer->output != NULL && byte_count != 0) {
        memcpy(writer->output + offset, bytes, byte_count);
    }
}

void
ribos_artifact_writer_u8(
    RibosArtifactWriter *writer,
    size_t offset,
    uint8_t value)
{
    ribos_artifact_writer_bytes(writer, offset, &value, 1);
}

void
ribos_artifact_writer_u16(
    RibosArtifactWriter *writer,
    size_t offset,
    uint16_t value)
{
    uint8_t bytes[2] = {
        (uint8_t)value,
        (uint8_t)(value >> 8),
    };

    ribos_artifact_writer_bytes(writer, offset, bytes, sizeof(bytes));
}

void
ribos_artifact_writer_u32(
    RibosArtifactWriter *writer,
    size_t offset,
    uint32_t value)
{
    uint8_t bytes[4] = {
        (uint8_t)value,
        (uint8_t)(value >> 8),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 24),
    };

    ribos_artifact_writer_bytes(writer, offset, bytes, sizeof(bytes));
}

void
ribos_artifact_writer_u64(
    RibosArtifactWriter *writer,
    size_t offset,
    uint64_t value)
{
    uint8_t bytes[8] = {
        (uint8_t)value,
        (uint8_t)(value >> 8),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 24),
        (uint8_t)(value >> 32),
        (uint8_t)(value >> 40),
        (uint8_t)(value >> 48),
        (uint8_t)(value >> 56),
    };

    ribos_artifact_writer_bytes(writer, offset, bytes, sizeof(bytes));
}

int
ribos_artifact_reader_bytes(
    const uint8_t *input,
    size_t input_size,
    size_t offset,
    size_t byte_count,
    const uint8_t **bytes)
{
    if (input == NULL || bytes == NULL ||
        !ribos_artifact_range(input_size, offset, byte_count)) {
        return 0;
    }
    *bytes = input + offset;
    return 1;
}

int
ribos_artifact_reader_u16(
    const uint8_t *input,
    size_t input_size,
    size_t offset,
    uint16_t *value)
{
    const uint8_t *bytes;

    if (value == NULL ||
        !ribos_artifact_reader_bytes(
            input, input_size, offset, 2, &bytes)) {
        return 0;
    }
    *value = (uint16_t)bytes[0] |
        ((uint16_t)bytes[1] << 8);
    return 1;
}

int
ribos_artifact_reader_u32(
    const uint8_t *input,
    size_t input_size,
    size_t offset,
    uint32_t *value)
{
    const uint8_t *bytes;

    if (value == NULL ||
        !ribos_artifact_reader_bytes(
            input, input_size, offset, 4, &bytes)) {
        return 0;
    }
    *value = (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
    return 1;
}

int
ribos_artifact_reader_u64(
    const uint8_t *input,
    size_t input_size,
    size_t offset,
    uint64_t *value)
{
    const uint8_t *bytes;

    if (value == NULL ||
        !ribos_artifact_reader_bytes(
            input, input_size, offset, 8, &bytes)) {
        return 0;
    }
    *value = (uint64_t)bytes[0] |
        ((uint64_t)bytes[1] << 8) |
        ((uint64_t)bytes[2] << 16) |
        ((uint64_t)bytes[3] << 24) |
        ((uint64_t)bytes[4] << 32) |
        ((uint64_t)bytes[5] << 40) |
        ((uint64_t)bytes[6] << 48) |
        ((uint64_t)bytes[7] << 56);
    return 1;
}

int
ribos_artifact_bytes_are_zero(
    const uint8_t *input,
    size_t byte_count)
{
    size_t index;

    if (input == NULL && byte_count != 0) {
        return 0;
    }
    for (index = 0; index < byte_count; ++index) {
        if (input[index] != 0) {
            return 0;
        }
    }
    return 1;
}
