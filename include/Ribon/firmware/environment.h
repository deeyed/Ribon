#ifndef RIBON_FIRMWARE_ENVIRONMENT_H
#define RIBON_FIRMWARE_ENVIRONMENT_H

#include <stdint.h>

#include <Ribon/arch/ops.h>
#include <Ribon/boot/source.h>
#include <Ribon/core/capability.h>
#include <Ribon/core/memory.h>

/** @brief 기존 execution environment를 소비하는 frontend 종류다. */
enum RibonEnvironmentKind {
    RIBON_ENVIRONMENT_HOST = 0,
    RIBON_ENVIRONMENT_UEFI = 1,
    RIBON_ENVIRONMENT_BIOS = 2,
    RIBON_ENVIRONMENT_RAW_FDT = 3,
    RIBON_ENVIRONMENT_SBI = 4,
};

/** @brief Boot environment input fact bit다. */
enum RibonBootEnvironmentFlags {
    RIBON_BOOT_ENV_HAS_MEMORY_MAP = 1u << 0,
    RIBON_BOOT_ENV_HAS_DEVICE_TREE = 1u << 1,
    RIBON_BOOT_ENV_HAS_FRAMEBUFFER = 1u << 2,
    RIBON_BOOT_ENV_HAS_BOOT_MEDIA = 1u << 3,
    RIBON_BOOT_ENV_HAS_COMMAND_LINE = 1u << 4,
    RIBON_BOOT_ENV_HAS_BOOT_MODULES = 1u << 5,
    RIBON_BOOT_ENV_HAS_RAW_MEMORY_MAP = 1u << 6,
    RIBON_BOOT_ENV_HAS_ACPI = 1u << 7,
    RIBON_BOOT_ENV_HAS_BOOT_CPU_ID = 1u << 8,
};

/** @brief Borrowed device-tree descriptor다. */
struct RibonDeviceTree {
    uint64_t physical_address;
    uint64_t size;
    const void *data;
};

/** @brief Framebuffer provider 종류다. */
enum RibonFramebufferBackend {
    RIBON_FRAMEBUFFER_BACKEND_UNKNOWN = 0,
    RIBON_FRAMEBUFFER_BACKEND_UEFI_GOP = 1,
    RIBON_FRAMEBUFFER_BACKEND_VBE = 2,
    RIBON_FRAMEBUFFER_BACKEND_VGA_TEXT = 3,
};

/** @brief Framebuffer RGB channel layout이다. */
struct RibonFramebufferRgbInfo {
    uint8_t red_position;
    uint8_t red_mask_size;
    uint8_t green_position;
    uint8_t green_mask_size;
    uint8_t blue_position;
    uint8_t blue_mask_size;
};

/** @brief Validated framebuffer descriptor다. */
struct RibonFramebuffer {
    uint64_t physical_address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bits_per_pixel;
    enum RibonFramebufferBackend backend;
    struct RibonFramebufferRgbInfo rgb;
};

/** @brief Environment-native memory map의 borrowed opaque view다. */
struct RibonRawMemoryMap {
    const void *data;
    uint64_t size;
    uint32_t descriptor_size;
    uint32_t descriptor_version;
};

/** @brief Validated ACPI RSDP view다. */
struct RibonAcpiRsdp {
    uint64_t physical_address;
    const void *data;
    uint32_t size;
    uint32_t revision;
};

/** @brief 선택한 boot media의 semantic descriptor다. */
struct RibonBootMedia {
    enum RibonBootMediaKind kind;
    const char *path;
    uint64_t physical_address;
    uint64_t size;
    uint32_t block_size;
};

/** @brief Borrowed command line view다. */
struct RibonCommandLine {
    const char *text;
    uint32_t length;
};

/** @brief Boot module의 semantic role이다. */
enum RibonBootModuleRole {
    RIBON_BOOT_MODULE_ROLE_INVALID = 0,
    RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE = 1,
    RIBON_BOOT_MODULE_ROLE_AUXILIARY = 2,
};

/** @brief 한 boot module의 physical range다. */
struct RibonBootModule {
    const char *name;
    uint64_t physical_address;
    uint64_t size;
    enum RibonBootModuleRole role;
};

/** @brief Borrowed boot module array다. */
struct RibonBootModuleList {
    const struct RibonBootModule *modules;
    uint32_t module_count;
};

/**
 * @brief Firmware memory-map 재캡처 뒤에도 유지할 target 선택 결과다.
 *
 * 모든 pointer는 handoff 준비가 끝날 때까지 caller가 소유하며 immutable해야 한다.
 */
struct RibonBootEnvironmentPersistentInputs {
    struct RibonBootMedia boot_media;
    struct RibonBootModuleList boot_modules;
    struct RibonCommandLine command_line;
};

/**
 * @brief Environment consumer가 수집한 firmware-neutral immutable input 묶음이다.
 *
 * Native firmware handle은 이 구조에 들어가지 않는다.
 */
struct RibonBootEnvironment {
    uint32_t size;
    uint32_t abi_version;
    enum RibonEnvironmentKind kind;
    enum RibonArchitectureId architecture;
    uint64_t boot_cpu_id; /**< Firmware가 선택한 bootstrap CPU의 stable numeric ID다. */
    struct RibonMemoryMap memory_map;
    struct RibonRawMemoryMap raw_memory_map;
    struct RibonDeviceTree device_tree;
    struct RibonFramebuffer framebuffer;
    struct RibonAcpiRsdp acpi_rsdp;
    struct RibonBootMedia boot_media;
    struct RibonBootModuleList boot_modules;
    struct RibonCommandLine command_line;
    uint32_t flags;
};

/** @brief Environment kind의 안정적인 이름을 반환한다. */
const char *ribon_environment_name(enum RibonEnvironmentKind environment);

/** @brief Caller-owned environment를 비어 있는 valid ABI 상태로 초기화한다. */
void ribon_boot_environment_init(
    struct RibonBootEnvironment *environment,
    enum RibonEnvironmentKind kind,
    enum RibonArchitectureId architecture);

/** @brief Environment field와 flag/pointer 일관성을 검사한다. */
int ribon_boot_environment_is_valid(const struct RibonBootEnvironment *environment);

/**
 * @brief Capture된 environment에 immutable target 선택 결과를 다시 적용한다.
 *
 * 최초 capture와 모든 final-map recapture 뒤 같은 값을 적용해야 한다.
 */
int ribon_boot_environment_apply_persistent_inputs(
    struct RibonBootEnvironment *environment,
    const struct RibonBootEnvironmentPersistentInputs *inputs);

#endif
