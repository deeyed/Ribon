#ifndef RIBON_FIRMWARE_UEFI_COMPATIBLE_REFERENCE_H
#define RIBON_FIRMWARE_UEFI_COMPATIBLE_REFERENCE_H

#include <stdint.h>

#define RIBON_UEFI_REFERENCE_HANDLE_LIMIT 8u

/** @brief Minimal reference handle database의 caller-owned 한 entry다. */
struct RibonUefiReferenceHandleEntry {
    uint64_t handle;
    uint64_t protocol;
    const void *interface;
};

/** @brief UEFI-compatible personality가 소유하는 bounded handle database다. */
struct RibonUefiReferenceContext {
    struct RibonUefiReferenceHandleEntry entries[RIBON_UEFI_REFERENCE_HANDLE_LIMIT];
    uint32_t entry_count;
};

/** @brief Reference handle database service의 personality-private operation table이다. */
struct RibonUefiReferenceHandleDatabaseOperations {
    uint32_t size;
    int (*install)(void *, uint64_t, uint64_t, const void *);
    const void *(*locate)(const void *, uint64_t, uint64_t);
};

/** @brief Caller-owned UEFI reference context를 빈 handle database로 초기화한다. */
void ribon_uefi_reference_context_init(
    struct RibonUefiReferenceContext *context);

#endif
