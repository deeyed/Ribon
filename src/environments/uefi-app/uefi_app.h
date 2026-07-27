#ifndef RIBON_ENVIRONMENTS_UEFI_APP_H
#define RIBON_ENVIRONMENTS_UEFI_APP_H

#include <Ribon/boot/image.h>
#include <Ribon/boot/source.h>
#include <Ribon/firmware/environment.h>
#include <Ribon/service/directory.h>
#include <Ribon/plugin/descriptor.h>
#include <Ribon/storage/block.h>

#include <Uefi.h>
#include <Protocol/BlockIo.h>
#include <Protocol/SimpleFileSystem.h>

/** @brief UEFI final memory-map transaction의 bounded attempt 수다. */
#define RIBON_UEFI_EXIT_ATTEMPTS 3u

/** @brief UEFI consumer가 동시에 유지할 read-only file source 수 상한이다. */
#define RIBON_UEFI_FILE_SOURCE_CAPACITY 4u

/** @brief UEFI native path conversion의 NUL 포함 UTF-16 code unit 상한이다. */
#define RIBON_UEFI_PATH_CAPACITY 128u

/** @brief Environment-private UEFI file handle과 validated byte size다. */
struct RibonUefiFileSource {
    EFI_FILE_PROTOCOL *handle; /**< ExitBootServices 전까지만 유효한 native handle이다. */
    uint64_t size; /**< SetPosition/GetPosition으로 확인한 immutable read bound다. */
};

/** @brief UEFI application consumer가 caller-owned storage에 유지하는 native state다. */
struct RibonUefiAppContext {
    EFI_HANDLE image_handle; /**< UEFI가 전달한 image handle이다. */
    EFI_SYSTEM_TABLE *system_table; /**< Boot Services lifetime의 borrowed table이다. */
    EFI_BOOT_SERVICES *boot_services; /**< ExitBootServices 전까지만 유효하다. */
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *file_system; /**< Loaded-image device의 borrowed file system이다. */
    EFI_FILE_PROTOCOL *root; /**< Loaded-image volume의 borrowed root handle이다. */
    EFI_BLOCK_IO_PROTOCOL *block_io; /**< 선택적으로 capture한 borrowed block adapter다. */
    struct RibonUefiFileSource files[RIBON_UEFI_FILE_SOURCE_CAPACITY]; /**< Bounded file source slots다. */
    void *raw_memory_map; /**< Caller-owned descriptor buffer다. */
    uint64_t raw_memory_map_capacity; /**< Raw buffer byte 수다. */
    struct RibonMemoryRegion *regions; /**< Caller-owned converted region storage다. */
    uint32_t region_capacity; /**< Converted region element 수 상한이다. */
    uint32_t region_count; /**< 마지막 capture의 region 수다. */
    UINTN map_key; /**< 마지막 GetMemoryMap key다. */
    UINTN descriptor_size; /**< Native descriptor stride다. */
    UINT32 descriptor_version; /**< Native descriptor version이다. */
};

/** @brief UEFI consumer operation의 결과다. */
enum RibonUefiAppStatus {
    RIBON_UEFI_APP_STATUS_OK = 0,
    RIBON_UEFI_APP_STATUS_BAD_ARGUMENT = -1,
    RIBON_UEFI_APP_STATUS_OUT_OF_CAPACITY = -2,
    RIBON_UEFI_APP_STATUS_FIRMWARE_ERROR = -3,
    RIBON_UEFI_APP_STATUS_PAYLOAD_ERROR = -4,
    RIBON_UEFI_APP_STATUS_RETRY_EXHAUSTED = -5,
};

/** @brief Final map capture 뒤 handoff를 재생성하는 target-owned callback이다. */
typedef int (*RibonUefiRefreshPlanFn)(
    void *context,
    struct RibonBootEnvironment *environment);

/** @brief UEFI native state와 caller-owned buffer를 consumer context에 결합한다. */
int ribon_uefi_app_initialize(
    struct RibonUefiAppContext *context,
    EFI_HANDLE image_handle,
    EFI_SYSTEM_TABLE *system_table);

/** @brief UEFI Simple File System에서 bounded canonical path의 exact file bytes를 읽는다. */
int ribon_uefi_app_read_file(
    struct RibonUefiAppContext *context,
    const char *path,
    void *buffer,
    uint64_t buffer_capacity,
    uint64_t *size_out);

/** @brief UEFI file을 native handle 없이 `RibonBootSource` slot으로 동결한다. */
int ribon_uefi_app_open_boot_source(
    struct RibonUefiAppContext *context,
    const char *path,
    struct RibonBootSource *out);

/** @brief UEFI Block I/O를 native type 없는 bounded read-only block provider로 변환한다. */
int ribon_uefi_app_read_only_block_device(
    struct RibonUefiAppContext *context,
    struct RibonReadOnlyBlockDevice *out);

/** @brief UEFI memory map을 firmware-neutral environment로 capture한다. */
int ribon_uefi_app_capture_environment(
    struct RibonUefiAppContext *context,
    struct RibonBootEnvironment *out);

/** @brief Analyzed payload segment를 UEFI-owned page allocation에 배치한다. */
int ribon_uefi_app_place_payload(
    struct RibonUefiAppContext *context,
    const struct RibonPayloadImage *payload,
    struct RibonLoadedPayload *layout);

/**
 * @brief Final map, plan refresh, ExitBootServices를 bounded transaction으로 수행한다.
 *
 * `EFI_INVALID_PARAMETER`이면 새 map을 capture하고 callback으로 handoff를 다시 만든다.
 */
int ribon_uefi_app_exit_boot_services(
    struct RibonUefiAppContext *context,
    struct RibonBootEnvironment *environment,
    RibonUefiRefreshPlanFn refresh,
    void *refresh_context);

/** @brief 초기화된 UEFI application typed service directory를 반환한다. */
const struct RibonServiceDirectory *ribon_uefi_app_service_directory(void);

/** @brief UEFI application environment consumer plugin descriptor다. */
extern const struct RibonPluginDescriptor ribon_uefi_app_environment_plugin_descriptor;

#endif
