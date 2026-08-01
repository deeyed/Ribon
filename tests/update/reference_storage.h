#ifndef RIBON_TEST_UPDATE_REFERENCE_STORAGE_H
#define RIBON_TEST_UPDATE_REFERENCE_STORAGE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <Ribon/update/storage.h>

/** @brief Reference provider에 주입하는 deterministic I/O failure다. */
enum RibonTestStorageFault {
    RIBON_TEST_STORAGE_FAULT_NONE = 0,
    RIBON_TEST_STORAGE_FAULT_READ_IO = 1,
    RIBON_TEST_STORAGE_FAULT_READ_SHORT = 2,
    RIBON_TEST_STORAGE_FAULT_WRITE_IO = 3,
    RIBON_TEST_STORAGE_FAULT_WRITE_SHORT = 4,
    RIBON_TEST_STORAGE_FAULT_ERASE_IO = 5,
    RIBON_TEST_STORAGE_FAULT_FLUSH_IO = 6,
};

/** @brief Test-only memory/file backend의 caller-owned state다. */
struct RibonTestStorage {
    struct RibonUpdateStorageProvider provider;
    uint8_t *memory;
    FILE *file;
    uint64_t capacity;
    enum RibonTestStorageFault fault;
    uint64_t read_count;
    uint64_t write_count;
    uint64_t erase_count;
    uint64_t flush_count;
};

/** @brief Caller-owned byte array를 bounded reference provider로 연다. */
int ribon_test_storage_open_memory(
    struct RibonTestStorage *storage,
    uint8_t *bytes,
    size_t size,
    uint64_t alignment,
    uint64_t maximum_transfer);

/** @brief Caller-owned seekable file을 bounded reference provider로 연다. */
int ribon_test_storage_open_file(
    struct RibonTestStorage *storage,
    FILE *file,
    uint64_t capacity,
    uint64_t alignment,
    uint64_t maximum_transfer);

#endif
