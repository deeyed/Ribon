#ifndef RIBON_ENVIRONMENTS_UEFI_UPDATE_STORAGE_H
#define RIBON_ENVIRONMENTS_UEFI_UPDATE_STORAGE_H

#include <Uefi.h>
#include <Protocol/BlockIo.h>

#include <stddef.h>
#include <stdint.h>

#include <Ribon/service/directory.h>
#include <Ribon/update/storage.h>

/** @brief QEMU reference media의 fixed discovery anchor byte 수다. */
#define RIBON_UEFI_UPDATE_MEDIA_ANCHOR_BYTES 1024u

/** @brief GPT table과 겹치지 않는 canonical anchor byte offset이다. */
#define RIBON_UEFI_UPDATE_MEDIA_ANCHOR_OFFSET UINT64_C(65536)

/** @brief Recovery adapter가 조사하는 Block I/O handle 수 상한이다. */
#define RIBON_UEFI_UPDATE_HANDLE_CAPACITY 16u

/** @brief Reference UEFI adapter가 허용하는 exact logical block 크기다. */
#define RIBON_UEFI_UPDATE_BLOCK_BYTES 512u

/** @brief UEFI Block I/O adapter의 stable fail-closed status다. */
enum RibonUefiUpdateStorageStatus {
    RIBON_UEFI_UPDATE_STORAGE_OK = 0,
    RIBON_UEFI_UPDATE_STORAGE_BAD_ARGUMENT = -1,
    RIBON_UEFI_UPDATE_STORAGE_FIRMWARE_ERROR = -2,
    RIBON_UEFI_UPDATE_STORAGE_NOT_FOUND = -3,
    RIBON_UEFI_UPDATE_STORAGE_AMBIGUOUS = -4,
    RIBON_UEFI_UPDATE_STORAGE_MEDIA_MISMATCH = -5,
    RIBON_UEFI_UPDATE_STORAGE_IO = -6,
    RIBON_UEFI_UPDATE_STORAGE_METADATA = -7,
};

/** @brief Recovery product가 caller-owned storage에 유지하는 UEFI adapter state다. */
struct RibonUefiUpdateStorageContext {
    EFI_BOOT_SERVICES *boot_services;
    EFI_BLOCK_IO_PROTOCOL *block_io;
    const struct RibonUpdateStorageProductBinding *binding;
    struct RibonUpdateStorageProvider provider;
    struct RibonUpdateLayout layout;
    _Alignas(4096) uint8_t anchor[RIBON_UEFI_UPDATE_MEDIA_ANCHOR_BYTES];
    _Alignas(4096) uint8_t zero_block[RIBON_UEFI_UPDATE_BLOCK_BYTES];
};

/** @brief Bounded handle scan으로 exact anchored media 하나를 발견하고 연다. */
int ribon_uefi_update_storage_discover(
    struct RibonUefiUpdateStorageContext *context,
    EFI_BOOT_SERVICES *boot_services,
    const struct RibonUpdateStorageProductBinding *binding);

/** @brief Redundant metadata 두 copy가 exact 일치할 때 native snapshot으로 연다. */
int ribon_uefi_update_storage_read_metadata(
    struct RibonUefiUpdateStorageContext *context,
    struct RibonUpdateSlotMetadata *metadata);

/** @brief Metadata 두 copy를 exact write하고 firmware flush로 durably 닫는다. */
int ribon_uefi_update_storage_write_metadata(
    struct RibonUefiUpdateStorageContext *context,
    const struct RibonUpdateSlotMetadata *metadata);

extern const struct RibonServiceDescriptor
    ribon_uefi_update_inactive_slot_storage_service_descriptor;
extern const struct RibonServiceDescriptor
    ribon_uefi_update_persistent_metadata_service_descriptor;
extern const struct RibonServiceDescriptor
    ribon_uefi_update_storage_flush_service_descriptor;

#endif
