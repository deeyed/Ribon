#ifndef RIBON_FIRMWARE_PERSONALITY_H
#define RIBON_FIRMWARE_PERSONALITY_H

#include <stdint.h>

#include <Ribon/service/directory.h>
#include <Ribon/plugin/descriptor.h>

/** @brief Firmware personality descriptor를 식별하는 magic이다. */
#define RIBON_FIRMWARE_PERSONALITY_MAGIC 0x52424650u

/** @brief Firmware service descriptor를 식별하는 magic이다. */
#define RIBON_FIRMWARE_SERVICE_MAGIC 0x52424653u

/** @brief Firmware provider personality operation table ABI다. */
#define RIBON_FIRMWARE_PERSONALITY_ABI_VERSION 1u

/** @brief 한 personality가 publish할 수 있는 service 수 상한이다. */
#define RIBON_FIRMWARE_SERVICE_LIMIT 32u

/** @brief Consumer environment와 반대 방향인 firmware provider 종류다. */
enum RibonFirmwarePersonalityKind {
    RIBON_FIRMWARE_PERSONALITY_UEFI_COMPATIBLE = 0,
    RIBON_FIRMWARE_PERSONALITY_BIOS_COMPATIBLE = 1,
};

/** @brief Personality-private directory가 publish하는 firmware service bit다. */
enum RibonFirmwareService {
    RIBON_FIRMWARE_SERVICE_MEMORY = 1ull << 0,
    RIBON_FIRMWARE_SERVICE_EVENT = 1ull << 1,
    RIBON_FIRMWARE_SERVICE_HANDLE_DATABASE = 1ull << 2,
    RIBON_FIRMWARE_SERVICE_IMAGE = 1ull << 3,
    RIBON_FIRMWARE_SERVICE_CONFIGURATION_TABLE = 1ull << 4,
    RIBON_FIRMWARE_SERVICE_VARIABLE = 1ull << 5,
    RIBON_FIRMWARE_SERVICE_TIME = 1ull << 6,
    RIBON_FIRMWARE_SERVICE_RESET = 1ull << 7,
    RIBON_FIRMWARE_SERVICE_CONSOLE = 1ull << 8,
    RIBON_FIRMWARE_SERVICE_BLOCK = 1ull << 9,
    RIBON_FIRMWARE_SERVICE_NETWORK = 1ull << 10,
    RIBON_FIRMWARE_SERVICE_E820 = 1ull << 16,
    RIBON_FIRMWARE_SERVICE_EDD = 1ull << 17,
    RIBON_FIRMWARE_SERVICE_VIDEO = 1ull << 18,
    RIBON_FIRMWARE_SERVICE_ACPI = 1ull << 19,
    RIBON_FIRMWARE_SERVICE_SMBIOS = 1ull << 20,
    RIBON_FIRMWARE_SERVICE_MODE_TRANSITION = 1ull << 21,
};

/** @brief 알려진 firmware service bit 전체다. */
#define RIBON_FIRMWARE_SERVICE_ALL \
    ((1ull << 11) - 1ull | (((1ull << 6) - 1ull) << 16))

/** @brief Service가 boot 이후에도 유지되는지 나타낸다. */
enum RibonFirmwareServiceLifetime {
    RIBON_FIRMWARE_SERVICE_LIFETIME_BOOT = 0,
    RIBON_FIRMWARE_SERVICE_LIFETIME_RUNTIME = 1,
};

struct RibonFirmwareServiceDescriptor;

/** @brief Service-private operation table의 계약 validator다. */
typedef int (*RibonFirmwareServiceValidateOperationsFn)(
    const struct RibonFirmwareServiceDescriptor *descriptor);

/** @brief Personality 내부에서만 검색되는 한 firmware service descriptor다. */
struct RibonFirmwareServiceDescriptor {
    uint32_t magic; /**< `RIBON_FIRMWARE_SERVICE_MAGIC`이어야 한다. */
    uint32_t size; /**< Descriptor byte 크기다. */
    uint32_t abi_version; /**< Service-specific operation ABI다. */
    const char *id; /**< Personality 안에서 정렬된 stable service ID다. */
    uint64_t service; /**< 정확히 한 `RibonFirmwareService` bit다. */
    enum RibonPluginPhase phase; /**< Service publication이 허용되는 최초 phase다. */
    enum RibonFirmwareServiceLifetime lifetime; /**< Boot 또는 runtime lifetime이다. */
    const void *operations; /**< Personality-private immutable operation table이다. */
    uint32_t operations_size; /**< Operation table byte 크기다. */
    RibonFirmwareServiceValidateOperationsFn validate_operations; /**< Typed validator다. */
};

/** @brief Firmware provider product가 publish할 personality descriptor다. */
struct RibonFirmwarePersonality {
    uint32_t magic; /**< `RIBON_FIRMWARE_PERSONALITY_MAGIC`이어야 한다. */
    uint32_t size; /**< Descriptor byte 크기다. */
    uint32_t abi_version; /**< `RIBON_FIRMWARE_PERSONALITY_ABI_VERSION`이다. */
    enum RibonFirmwarePersonalityKind kind; /**< Provider ABI 종류다. */
    const char *id; /**< Stable personality ID다. */
    uint64_t published_services; /**< 외부 firmware ABI로 publish할 service bit다. */
    uint64_t runtime_services; /**< OS entry 뒤 보존할 service bit다. */
    const struct RibonFirmwareServiceDescriptor *const *services; /**< Stable ID 순 service다. */
    uint32_t service_count; /**< Service pointer 수다. */
};

/**
 * @brief Caller-owned storage를 사용하는 personality-private service directory다.
 *
 * Generic Core registry가 아니며 firmware provider product lifetime 안에서만 유효하다.
 */
struct RibonFirmwareServiceDirectory {
    uint32_t size; /**< Directory descriptor byte 크기다. */
    uint32_t abi_version; /**< Personality ABI version이다. */
    const struct RibonFirmwarePersonality *personality; /**< Borrowed immutable personality다. */
    const struct RibonFirmwareServiceDescriptor **services; /**< Caller-owned pointer storage다. */
    uint32_t service_capacity; /**< Pointer storage element 수다. */
    uint32_t service_count; /**< Publish된 pointer 수다. */
    uint64_t published_services; /**< Publish된 service bitset이다. */
    void *context; /**< Personality-owned caller context다. */
};

/** @brief Firmware personality plugin의 typed operation table이다. */
struct RibonFirmwarePersonalityOperations {
    uint32_t size; /**< Operation table byte 크기다. */
    uint32_t abi_version; /**< Personality ABI version이다. */
    const struct RibonFirmwarePersonality *personality; /**< Publish할 immutable descriptor다. */
};

/** @brief Personality와 service descriptor 전체를 fail-closed로 검사한다. */
int ribon_firmware_personality_is_valid(
    const struct RibonFirmwarePersonality *personality);

/**
 * @brief 허용 phase까지의 service를 caller-owned directory에 publish한다.
 *
 * 실패하면 directory를 빈 상태로 남기며 allocation이나 전역 등록을 수행하지 않는다.
 */
int ribon_firmware_personality_publish(
    const struct RibonFirmwarePersonality *personality,
    enum RibonPluginPhase phase,
    void *context,
    struct RibonFirmwareServiceDirectory *directory);

/** @brief Directory에서 정확히 한 service bit의 descriptor를 찾는다. */
const struct RibonFirmwareServiceDescriptor *ribon_firmware_service_directory_find(
    const struct RibonFirmwareServiceDirectory *directory,
    uint64_t service);

/** @brief Directory가 요청 service를 모두 publish했는지 fail-closed로 검사한다. */
int ribon_firmware_service_directory_require(
    const struct RibonFirmwareServiceDirectory *directory,
    uint64_t services);

/** @brief Firmware personality plugin operation table과 descriptor를 함께 검사한다. */
int ribon_firmware_personality_plugin_operations_are_valid(
    const struct RibonPluginDescriptor *descriptor);

#endif
