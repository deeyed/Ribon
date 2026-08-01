#include "reference_storage.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/** @brief Reference provider range가 backing capacity 안인지 검사한다. */
static int
range_valid(const struct RibonTestStorage *storage, uint64_t offset, uint64_t size)
{
    return storage != NULL && size != 0u && offset <= storage->capacity &&
        size <= storage->capacity - offset && offset <= (uint64_t)LONG_MAX;
}

/** @brief File backend의 absolute position을 standard-C seek로 이동한다. */
static int
seek_file(struct RibonTestStorage *storage, uint64_t offset)
{
    return storage->file != NULL && offset <= (uint64_t)LONG_MAX &&
        fseek(storage->file, (long)offset, SEEK_SET) == 0;
}

/** @brief Memory 또는 file backend에서 exact byte range를 읽는다. */
static int
reference_read(
    void *context,
    uint64_t offset,
    void *buffer,
    uint64_t size,
    uint64_t *transferred,
    uint64_t deadline_ticks)
{
    struct RibonTestStorage *storage = context;
    size_t count;

    (void)deadline_ticks;
    if (transferred != NULL) {
        *transferred = 0u;
    }
    if (storage == NULL || buffer == NULL || transferred == NULL ||
        !range_valid(storage, offset, size) || size > (uint64_t)SIZE_MAX) {
        return -1;
    }
    ++storage->read_count;
    if (storage->fault == RIBON_TEST_STORAGE_FAULT_READ_IO) {
        return -1;
    }
    count = (size_t)size;
    if (storage->fault == RIBON_TEST_STORAGE_FAULT_READ_SHORT) {
        count = count > 1u ? count - 1u : 0u;
    }
    if (storage->memory != NULL) {
        memcpy(buffer, storage->memory + (size_t)offset, count);
    } else if (!seek_file(storage, offset) ||
               fread(buffer, 1u, count, storage->file) != count) {
        return -1;
    }
    *transferred = count;
    return 0;
}

/** @brief Memory 또는 file backend에 exact byte range를 쓴다. */
static int
reference_write(
    void *context,
    uint64_t offset,
    const void *buffer,
    uint64_t size,
    uint64_t *transferred,
    uint64_t deadline_ticks)
{
    struct RibonTestStorage *storage = context;
    size_t count;

    (void)deadline_ticks;
    if (transferred != NULL) {
        *transferred = 0u;
    }
    if (storage == NULL || buffer == NULL || transferred == NULL ||
        !range_valid(storage, offset, size) || size > (uint64_t)SIZE_MAX) {
        return -1;
    }
    ++storage->write_count;
    if (storage->fault == RIBON_TEST_STORAGE_FAULT_WRITE_IO) {
        return -1;
    }
    count = (size_t)size;
    if (storage->fault == RIBON_TEST_STORAGE_FAULT_WRITE_SHORT) {
        count = count > 1u ? count - 1u : 0u;
    }
    if (storage->memory != NULL) {
        memcpy(storage->memory + (size_t)offset, buffer, count);
    } else if (!seek_file(storage, offset) ||
               fwrite(buffer, 1u, count, storage->file) != count) {
        return -1;
    }
    *transferred = count;
    return 0;
}

/** @brief Memory 또는 file backend의 byte range를 zero erase한다. */
static int
reference_erase(
    void *context,
    uint64_t offset,
    uint64_t size,
    uint64_t deadline_ticks)
{
    struct RibonTestStorage *storage = context;
    uint8_t zeroes[256] = {0};
    uint64_t cursor = 0u;

    (void)deadline_ticks;
    if (storage == NULL || !range_valid(storage, offset, size)) {
        return -1;
    }
    ++storage->erase_count;
    if (storage->fault == RIBON_TEST_STORAGE_FAULT_ERASE_IO) {
        return -1;
    }
    if (storage->memory != NULL) {
        memset(storage->memory + (size_t)offset, 0, (size_t)size);
        return 0;
    }
    if (!seek_file(storage, offset)) {
        return -1;
    }
    while (cursor < size) {
        const uint64_t remaining = size - cursor;
        const size_t chunk = remaining < sizeof(zeroes) ?
            (size_t)remaining : sizeof(zeroes);
        if (fwrite(zeroes, 1u, chunk, storage->file) != chunk) {
            return -1;
        }
        cursor += chunk;
    }
    return 0;
}

/** @brief Memory 또는 file backend의 durability barrier를 모사한다. */
static int
reference_flush(void *context, uint64_t deadline_ticks)
{
    struct RibonTestStorage *storage = context;

    (void)deadline_ticks;
    if (storage == NULL) {
        return -1;
    }
    ++storage->flush_count;
    if (storage->fault == RIBON_TEST_STORAGE_FAULT_FLUSH_IO) {
        return -1;
    }
    return storage->file == NULL || fflush(storage->file) == 0 ? 0 : -1;
}

/** @brief Common reference provider fields를 deterministic하게 초기화한다. */
static int
open_common(
    struct RibonTestStorage *storage,
    uint64_t capacity,
    uint64_t alignment,
    uint64_t maximum_transfer)
{
    static const uint8_t identity[32] = {
        0x52u, 0x49u, 0x42u, 0x4fu, 0x4eu, 0x2du, 0x44u, 0x30u,
        0x32u, 0x2du, 0x52u, 0x45u, 0x46u, 0x45u, 0x52u, 0x45u,
        0x4eu, 0x43u, 0x45u, 0x2du, 0x4du, 0x45u, 0x44u, 0x49u,
        0x41u, 0x2du, 0x56u, 0x31u, 0x00u, 0x00u, 0x00u, 0x01u,
    };

    storage->capacity = capacity;
    storage->provider.size = sizeof(storage->provider);
    storage->provider.abi_version = RIBON_UPDATE_STORAGE_ABI_VERSION;
    storage->provider.capabilities = RIBON_UPDATE_STORAGE_CAP_ALL;
    storage->provider.capacity_bytes = capacity;
    storage->provider.read_alignment = alignment;
    storage->provider.write_alignment = alignment;
    storage->provider.erase_alignment = alignment;
    storage->provider.maximum_transfer_bytes = maximum_transfer;
    memcpy(storage->provider.media_identity_digest, identity, sizeof(identity));
    storage->provider.context = storage;
    storage->provider.read = reference_read;
    storage->provider.write = reference_write;
    storage->provider.erase = reference_erase;
    storage->provider.flush = reference_flush;
    return ribon_update_storage_provider_is_valid(&storage->provider);
}

/** @brief Caller-owned byte array를 bounded reference provider로 연다. */
int
ribon_test_storage_open_memory(
    struct RibonTestStorage *storage,
    uint8_t *bytes,
    size_t size,
    uint64_t alignment,
    uint64_t maximum_transfer)
{
    if (storage == NULL || bytes == NULL || size == 0u) {
        return 0;
    }
    memset(storage, 0, sizeof(*storage));
    storage->memory = bytes;
    return open_common(storage, size, alignment, maximum_transfer);
}

/** @brief Caller-owned seekable file을 bounded reference provider로 연다. */
int
ribon_test_storage_open_file(
    struct RibonTestStorage *storage,
    FILE *file,
    uint64_t capacity,
    uint64_t alignment,
    uint64_t maximum_transfer)
{
    if (storage == NULL || file == NULL || capacity == 0u) {
        return 0;
    }
    memset(storage, 0, sizeof(*storage));
    storage->file = file;
    return open_common(storage, capacity, alignment, maximum_transfer);
}
