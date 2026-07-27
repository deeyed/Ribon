#ifndef RIBON_ENVIRONMENTS_UEFI_APP_H
#define RIBON_ENVIRONMENTS_UEFI_APP_H

#include <Ribon/boot/image.h>
#include <Ribon/firmware/environment.h>
#include <Ribon/service/directory.h>
#include <Ribon/plugin/descriptor.h>

#include <Uefi.h>

/** @brief UEFI final memory-map transaction의 bounded attempt 수다. */
#define RIBON_UEFI_EXIT_ATTEMPTS 3u

/** @brief UEFI application consumer가 caller-owned storage에 유지하는 native state다. */
struct RibonUefiAppContext {
    EFI_HANDLE image_handle; /**< UEFI가 전달한 image handle이다. */
    EFI_SYSTEM_TABLE *system_table; /**< Boot Services lifetime의 borrowed table이다. */
    EFI_BOOT_SERVICES *boot_services; /**< ExitBootServices 전까지만 유효하다. */
    const void *payload; /**< Memory boot source의 borrowed bytes다. */
    uint64_t payload_size; /**< Memory boot source byte 수다. */
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
    EFI_SYSTEM_TABLE *system_table,
    const void *payload,
    uint64_t payload_size);

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
