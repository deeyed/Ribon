#include "../../src/environments/uefi-app/update_storage.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MOCK_HANDLE_CAPACITY 20u

struct MockDevice {
    EFI_BLOCK_IO_PROTOCOL protocol;
    EFI_BLOCK_IO_MEDIA media;
    uint8_t *bytes;
    size_t size;
    int read_error;
    int write_error;
    int flush_error;
    uint32_t write_count;
    uint32_t flush_count;
};

static struct MockDevice *mock_handles[MOCK_HANDLE_CAPACITY];
static UINTN mock_handle_count;
static int failures;

/** @brief Unit assertion 실패를 stable 이름과 함께 누적한다. */
static void
expect(int condition, const char *name)
{
    if (!condition) {
        (void)fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

/** @brief Mock Block I/O에서 exact range를 읽는다. */
static EFI_STATUS EFIAPI
mock_read(
    EFI_BLOCK_IO_PROTOCOL *protocol,
    UINT32 media_id,
    EFI_LBA lba,
    UINTN size,
    VOID *buffer)
{
    struct MockDevice *device = (struct MockDevice *)protocol;
    uint64_t offset = lba * device->media.BlockSize;
    if (device->read_error) {
        return EFI_DEVICE_ERROR;
    }
    if (buffer == NULL || media_id != device->media.MediaId || size == 0u ||
        offset > device->size || size > device->size - offset) {
        return EFI_INVALID_PARAMETER;
    }
    memcpy(buffer, device->bytes + (size_t)offset, size);
    return EFI_SUCCESS;
}

/** @brief Mock Block I/O에 exact range를 쓴다. */
static EFI_STATUS EFIAPI
mock_write(
    EFI_BLOCK_IO_PROTOCOL *protocol,
    UINT32 media_id,
    EFI_LBA lba,
    UINTN size,
    VOID *buffer)
{
    struct MockDevice *device = (struct MockDevice *)protocol;
    uint64_t offset = lba * device->media.BlockSize;
    if (device->write_error) {
        return EFI_DEVICE_ERROR;
    }
    if (buffer == NULL || media_id != device->media.MediaId || size == 0u ||
        offset > device->size || size > device->size - offset) {
        return EFI_INVALID_PARAMETER;
    }
    memcpy(device->bytes + (size_t)offset, buffer, size);
    ++device->write_count;
    return EFI_SUCCESS;
}

/** @brief Mock Block I/O durability barrier를 기록한다. */
static EFI_STATUS EFIAPI
mock_flush(EFI_BLOCK_IO_PROTOCOL *protocol)
{
    struct MockDevice *device = (struct MockDevice *)protocol;
    ++device->flush_count;
    return device->flush_error ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

/** @brief Bounded caller buffer에 configured mock handles를 반환한다. */
static EFI_STATUS EFIAPI
mock_locate_handle(
    EFI_LOCATE_SEARCH_TYPE search_type,
    EFI_GUID *protocol,
    VOID *search_key,
    UINTN *size,
    EFI_HANDLE *handles)
{
    UINTN required = mock_handle_count * sizeof(handles[0]);
    UINTN index;
    (void)protocol;
    (void)search_key;
    if (search_type != ByProtocol || size == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    if (*size < required) {
        *size = required;
        return EFI_BUFFER_TOO_SMALL;
    }
    if (required == 0u) {
        return EFI_NOT_FOUND;
    }
    for (index = 0u; index < mock_handle_count; ++index) {
        handles[index] = (EFI_HANDLE)mock_handles[index];
    }
    *size = required;
    return EFI_SUCCESS;
}

/** @brief Mock handle에서 Block I/O interface를 연다. */
static EFI_STATUS EFIAPI
mock_handle_protocol(EFI_HANDLE handle, EFI_GUID *protocol, VOID **interface)
{
    (void)protocol;
    if (handle == NULL || interface == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *interface = &((struct MockDevice *)handle)->protocol;
    return EFI_SUCCESS;
}

/** @brief Exact raw fixture file을 heap buffer로 읽는다. */
static uint8_t *
read_file(const char *path, size_t *size)
{
    FILE *stream = fopen(path, "rb");
    long length;
    uint8_t *bytes;
    if (stream == NULL || fseek(stream, 0L, SEEK_END) != 0 ||
        (length = ftell(stream)) <= 0L || fseek(stream, 0L, SEEK_SET) != 0) {
        if (stream != NULL) {
            (void)fclose(stream);
        }
        return NULL;
    }
    bytes = malloc((size_t)length);
    if (bytes == NULL || fread(bytes, 1u, (size_t)length, stream) !=
            (size_t)length || fgetc(stream) != EOF || fclose(stream) != 0) {
        free(bytes);
        return NULL;
    }
    *size = (size_t)length;
    return bytes;
}

/** @brief One mock device를 canonical writable whole-media handle로 초기화한다. */
static void
device_init(struct MockDevice *device, uint8_t *bytes, size_t size)
{
    memset(device, 0, sizeof(*device));
    device->bytes = bytes;
    device->size = size;
    device->media.MediaId = 1u;
    device->media.MediaPresent = TRUE;
    device->media.BlockSize = RIBON_UEFI_UPDATE_BLOCK_BYTES;
    device->media.IoAlign = 1u;
    device->media.LastBlock = size / RIBON_UEFI_UPDATE_BLOCK_BYTES - 1u;
    device->protocol.Revision = EFI_BLOCK_IO_PROTOCOL_REVISION;
    device->protocol.Media = &device->media;
    device->protocol.ReadBlocks = mock_read;
    device->protocol.WriteBlocks = mock_write;
    device->protocol.FlushBlocks = mock_flush;
}

/** @brief Fixture anchor에서 exact product binding을 구성한다. */
static struct RibonUpdateStorageProductBinding
binding_from_disk(const uint8_t *disk)
{
    const uint8_t *anchor = disk + RIBON_UEFI_UPDATE_MEDIA_ANCHOR_OFFSET;
    struct RibonUpdateStorageProductBinding binding = {
        .size = sizeof(binding),
        .abi_version = RIBON_UPDATE_STORAGE_ABI_VERSION,
        .provider_class = RIBON_UPDATE_STORAGE_PROVIDER_CLASS_FIRMWARE,
        .layout_id = "layout.validation.ab-v1",
        .read_service_id = "service.read",
        .writer_service_id = "service.write",
        .metadata_service_id = "service.metadata",
        .flush_service_id = "service.flush",
    };
    memcpy(binding.media_identity_digest, anchor + 64u, 32u);
    memcpy(binding.layout_digest, anchor + 96u, 32u);
    return binding;
}

/** @brief Discovery filtering, ambiguity, identity와 metadata I/O를 검증한다. */
static void
test_adapter(uint8_t *disk, size_t size)
{
    EFI_BOOT_SERVICES boot_services = {0};
    struct MockDevice valid;
    struct MockDevice readonly;
    struct MockDevice partition;
    struct MockDevice duplicate;
    struct RibonUefiUpdateStorageContext context;
    struct RibonUpdateStorageProductBinding binding = binding_from_disk(disk);
    struct RibonUpdateStorageProductBinding hostile_binding;
    struct RibonUpdateSlotMetadata metadata;
    uint64_t metadata_offset;
    uint8_t saved;
    UINTN index;

    boot_services.LocateHandle = mock_locate_handle;
    boot_services.HandleProtocol = mock_handle_protocol;
    device_init(&valid, disk, size);
    device_init(&readonly, disk, size);
    readonly.media.ReadOnly = TRUE;
    device_init(&partition, disk, size);
    partition.media.LogicalPartition = TRUE;
    mock_handles[0] = &readonly;
    mock_handles[1] = &partition;
    mock_handles[2] = &valid;
    mock_handle_count = 3u;
    expect(ribon_uefi_update_storage_discover(
               &context, &boot_services, &binding) == RIBON_UEFI_UPDATE_STORAGE_OK &&
               context.block_io == &valid.protocol &&
               ribon_update_storage_provider_is_valid(&context.provider),
           "filter candidates and select exact anchored whole media");
    expect(ribon_uefi_update_storage_read_metadata(&context, &metadata) ==
               RIBON_UEFI_UPDATE_STORAGE_OK &&
               metadata.active_slot == 0u &&
               metadata.slots[1].state == RIBON_UPDATE_SLOT_EMPTY,
           "open byte-identical redundant metadata copies");
    expect(ribon_uefi_update_storage_write_metadata(&context, &metadata) ==
               RIBON_UEFI_UPDATE_STORAGE_OK &&
               valid.write_count == 1u && valid.flush_count == 1u,
           "write both metadata copies and flush");

    metadata_offset = context.layout.regions[
        RIBON_UPDATE_REGION_SLOT_METADATA - 1u].offset;
    disk[(size_t)metadata_offset + RIBON_UPDATE_SLOT_METADATA_BYTES] ^= 1u;
    expect(ribon_uefi_update_storage_read_metadata(&context, &metadata) ==
               RIBON_UEFI_UPDATE_STORAGE_METADATA,
           "torn metadata copies fail closed");
    disk[(size_t)metadata_offset + RIBON_UPDATE_SLOT_METADATA_BYTES] ^= 1u;

    device_init(&duplicate, disk, size);
    mock_handles[0] = &valid;
    mock_handles[1] = &duplicate;
    mock_handle_count = 2u;
    expect(ribon_uefi_update_storage_discover(
               &context, &boot_services, &binding) ==
               RIBON_UEFI_UPDATE_STORAGE_AMBIGUOUS,
           "duplicate matching media rejected");
    expect(ribon_uefi_update_storage_read_metadata(&context, &metadata) ==
               RIBON_UEFI_UPDATE_STORAGE_BAD_ARGUMENT,
           "failed rediscovery revokes prior selected context");

    for (index = 0u; index < MOCK_HANDLE_CAPACITY; ++index) {
        mock_handles[index] = &readonly;
    }
    mock_handle_count = RIBON_UEFI_UPDATE_HANDLE_CAPACITY + 1u;
    expect(ribon_uefi_update_storage_discover(
               &context, &boot_services, &binding) ==
               RIBON_UEFI_UPDATE_STORAGE_AMBIGUOUS,
           "handle enumeration capacity fails closed");

    mock_handles[0] = &valid;
    mock_handle_count = 1u;
    hostile_binding = binding;
    hostile_binding.media_identity_digest[0] ^= 1u;
    expect(ribon_uefi_update_storage_discover(
               &context, &boot_services, &hostile_binding) ==
               RIBON_UEFI_UPDATE_STORAGE_NOT_FOUND,
           "wrong product media identity rejected");
    saved = disk[RIBON_UEFI_UPDATE_MEDIA_ANCHOR_OFFSET + 640u];
    disk[RIBON_UEFI_UPDATE_MEDIA_ANCHOR_OFFSET + 640u] ^= 1u;
    expect(ribon_uefi_update_storage_discover(
               &context, &boot_services, &binding) ==
               RIBON_UEFI_UPDATE_STORAGE_NOT_FOUND,
           "corrupted anchor integrity rejected");
    disk[RIBON_UEFI_UPDATE_MEDIA_ANCHOR_OFFSET + 640u] = saved;
    valid.read_error = 1;
    expect(ribon_uefi_update_storage_discover(
               &context, &boot_services, &binding) ==
               RIBON_UEFI_UPDATE_STORAGE_NOT_FOUND,
           "firmware read failure cannot select media");
    valid.read_error = 0;

    expect(ribon_uefi_update_storage_discover(
               &context, &boot_services, &binding) == RIBON_UEFI_UPDATE_STORAGE_OK,
           "restore valid adapter context");
    valid.write_error = 1;
    expect(ribon_uefi_update_storage_write_metadata(&context, &metadata) ==
               RIBON_UEFI_UPDATE_STORAGE_IO,
           "firmware metadata write error fails closed");
    valid.write_error = 0;
    valid.flush_error = 1;
    expect(ribon_uefi_update_storage_write_metadata(&context, &metadata) ==
               RIBON_UEFI_UPDATE_STORAGE_IO,
           "firmware flush error fails closed");
}

int
main(int argc, char **argv)
{
    size_t size = 0u;
    uint8_t *disk;
    if (argc != 2 || (disk = read_file(argv[1], &size)) == NULL) {
        (void)fprintf(stderr, "usage: uefi_storage_tests UPDATE_DISK\n");
        return 2;
    }
    test_adapter(disk, size);
    free(disk);
    if (failures != 0) {
        (void)fprintf(stderr,
            "RIBON-UEFI-UPDATE-STORAGE-FAIL failures=%d\n", failures);
        return 1;
    }
    (void)printf(
        "RIBON-UEFI-UPDATE-STORAGE-OK filtering=1 ambiguity=1 "
        "identity=1 metadata=1 io-faults=3\n");
    return 0;
}
