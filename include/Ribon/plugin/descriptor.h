#ifndef RIBON_PLUGIN_DESCRIPTOR_H
#define RIBON_PLUGIN_DESCRIPTOR_H

#include <stdint.h>

#include <Ribon/core/capability.h>
#include <Ribon/plugin/phases.h>

/** @brief Plugin descriptor를 byte order와 무관하게 식별하는 magic이다. */
#define RIBON_PLUGIN_DESCRIPTOR_MAGIC 0x5242504cu

/** @brief Plugin ABI major와 minor다. */
#define RIBON_PLUGIN_ABI_MAJOR 2u
#define RIBON_PLUGIN_ABI_MINOR 0u

/** @brief Architecture compatibility mask다. */
enum RibonArchitectureMask {
    RIBON_ARCH_MASK_X86_64 = 1u << 0,
    RIBON_ARCH_MASK_AARCH64 = 1u << 1,
    RIBON_ARCH_MASK_RISCV64 = 1u << 2,
};

/** @brief 알려진 architecture compatibility bit 전체다. */
#define RIBON_ARCH_MASK_ALL ((1u << 3) - 1u)

/** @brief Environment compatibility mask다. */
enum RibonEnvironmentMask {
    RIBON_ENV_MASK_HOST = 1u << 0,
    RIBON_ENV_MASK_UEFI = 1u << 1,
    RIBON_ENV_MASK_BIOS = 1u << 2,
    RIBON_ENV_MASK_RAW_FDT = 1u << 3,
    RIBON_ENV_MASK_SBI = 1u << 4,
};

/** @brief 알려진 environment compatibility bit 전체다. */
#define RIBON_ENV_MASK_ALL ((1u << 5) - 1u)

/** @brief Firmware provider personality compatibility mask다. */
enum RibonFirmwarePersonalityMask {
    RIBON_PERSONALITY_MASK_UEFI_COMPATIBLE = 1u << 0,
    RIBON_PERSONALITY_MASK_BIOS_COMPATIBLE = 1u << 1,
};

/** @brief 알려진 firmware personality compatibility bit 전체다. */
#define RIBON_PERSONALITY_MASK_ALL ((1u << 2) - 1u)

/** @brief Static product graph가 조합하는 plugin 종류다. */
enum RibonPluginKind {
    RIBON_PLUGIN_KIND_ARCHITECTURE = 0,
    RIBON_PLUGIN_KIND_ENVIRONMENT = 1,
    RIBON_PLUGIN_KIND_IMAGE_FORMAT = 2,
    RIBON_PLUGIN_KIND_BOOT_PROTOCOL = 3,
    RIBON_PLUGIN_KIND_POLICY = 4,
    RIBON_PLUGIN_KIND_FIRMWARE_PERSONALITY = 5,
    RIBON_PLUGIN_KIND_PLATFORM = 6,
    RIBON_PLUGIN_KIND_SERVICE = 7,
};

struct RibonPluginDescriptor;

/** @brief Kind-specific operation table의 capability 일관성 검증 callback이다. */
typedef int (*RibonPluginValidateOperationsFn)(
    const struct RibonPluginDescriptor *descriptor);

/** @brief Build-time registry에 들어가는 immutable plugin descriptor다. */
struct RibonPluginDescriptor {
    uint32_t magic; /**< `RIBON_PLUGIN_DESCRIPTOR_MAGIC`이어야 한다. */
    uint32_t size; /**< 이 descriptor의 byte 크기다. */
    uint16_t abi_major; /**< `RIBON_PLUGIN_ABI_MAJOR`와 일치해야 한다. */
    uint16_t abi_minor; /**< Descriptor size로 협상하는 minor다. */
    enum RibonPluginKind kind; /**< Operation table의 typed kind다. */
    enum RibonPluginPhase phase; /**< Plugin이 실행 가능한 최초 phase다. */
    const char *id; /**< Product 안에서 유일한 stable ID다. */
    uint64_t provides; /**< 이 plugin이 제공하는 capability다. */
    uint64_t requires; /**< 더 늦지 않은 phase provider에 요구하는 capability다. */
    uint32_t architecture_mask; /**< 허용 architecture bit다. */
    uint32_t environment_mask; /**< 허용 environment bit다. */
    uint32_t personality_mask; /**< 허용 firmware personality bit다. */
    uint32_t mode_mask; /**< 허용 `RibonMode` bitset이다. */
    uint32_t reserved; /**< 반드시 0이어야 한다. */
    uint64_t arena_budget; /**< Core arena에서 소비할 수 있는 byte 상한이다. */
    uint64_t input_budget; /**< 한 input artifact byte 상한이다. */
    uint64_t output_budget; /**< 한 output artifact byte 상한이다. */
    uint64_t deadline_ms; /**< 한 operation duration 상한이다. */
    const void *operations; /**< Kind-specific immutable operation table이다. */
    uint32_t operations_size; /**< Operation table byte 크기다. */
    uint32_t operations_abi; /**< Kind-specific operation ABI다. */
    RibonPluginValidateOperationsFn validate_operations; /**< Typed validator다. */
};

/** @brief Plugin kind의 안정적인 이름을 반환한다. */
const char *ribon_plugin_kind_name(enum RibonPluginKind kind);

/** @brief Plugin descriptor의 독립 field와 typed operation table을 검사한다. */
int ribon_plugin_descriptor_is_valid(const struct RibonPluginDescriptor *descriptor);

#endif
