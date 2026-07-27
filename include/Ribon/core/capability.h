#ifndef RIBON_CORE_CAPABILITY_H
#define RIBON_CORE_CAPABILITY_H

#include <stdint.h>

/** @brief Ribon Core와 plugin descriptor ABI major다. */
#define RIBON_CORE_ABI_VERSION 3u

/** @brief 한 product registry가 허용하는 plugin 수의 고정 상한이다. */
#define RIBON_PLUGIN_REGISTRY_LIMIT 64u

/** @brief Mode와 plugin graph가 교환하는 stable capability bit다. */
enum RibonCapability {
    RIBON_CAP_BOOT_SOURCE_READ = 1ull << 0,
    RIBON_CAP_INACTIVE_SLOT_WRITE = 1ull << 1,
    RIBON_CAP_INACTIVE_SLOT_ERASE = 1ull << 2,
    RIBON_CAP_STORAGE_FLUSH = 1ull << 3,
    RIBON_CAP_MONOTONIC_TIMER = 1ull << 4,
    RIBON_CAP_WATCHDOG = 1ull << 5,
    RIBON_CAP_RESET = 1ull << 6,
    RIBON_CAP_PERSISTENT_METADATA = 1ull << 7,
    RIBON_CAP_NETWORK_TRANSPORT = 1ull << 8,
    RIBON_CAP_RANDOM_NONCE = 1ull << 9,
    RIBON_CAP_DIAGNOSTIC_SINK = 1ull << 10,
    RIBON_CAP_ENVIRONMENT_QUIESCE = 1ull << 11,
    RIBON_CAP_ARCHITECTURE = 1ull << 16,
    RIBON_CAP_IMAGE_ELF64 = 1ull << 17,
    RIBON_CAP_BOOT_PROTOCOL = 1ull << 18,
    RIBON_CAP_HANDOFF = 1ull << 19,
    RIBON_CAP_ENTRY_CONTRACT = 1ull << 20,
    RIBON_CAP_BOOT_CONFIRMATION = 1ull << 21,
    RIBON_CAP_IMAGE_PE_COFF = 1ull << 22,
    RIBON_CAP_PLATFORM_FACTS = 1ull << 23,
    RIBON_CAP_FIRMWARE_PERSONALITY = 1ull << 24,
    RIBON_CAP_FIRMWARE_SERVICE_DIRECTORY = 1ull << 25,
    RIBON_CAP_SDK_CONTRACT = 1ull << 26,
};

/** @brief Public plugin ABI가 정의하는 capability 전체다. */
#define RIBON_CAP_ALL ((1ull << 27) - 1ull)

/** @brief Ribon product의 실행 정책 mode다. */
enum RibonMode {
    RIBON_MODE_NORMAL = 0,
    RIBON_MODE_RECOVERY = 1,
    RIBON_MODE_PROVISIONING = 2,
    RIBON_MODE_DIAGNOSTIC = 3,
};

/** @brief 알려진 mode bit 전체다. */
#define RIBON_MODE_MASK_ALL ((1u << 4) - 1u)

/** @brief Mode 값을 product bitset으로 변환한다. */
#define RIBON_MODE_MASK(mode) (1u << (uint32_t)(mode))

/** @brief Product가 넘을 수 없는 자원 상한이다. */
struct RibonResourceLimits {
    uint32_t max_memory_regions; /**< 정규화 region 수 상한이다. */
    uint32_t max_load_segments; /**< Image load segment 수 상한이다. */
    uint32_t max_components; /**< Protocol component 수 상한이다. */
    uint32_t max_retries; /**< Operation retry 수 상한이다. */
    uint64_t max_input_bytes; /**< 한 input artifact byte 상한이다. */
    uint64_t max_handoff_bytes; /**< Handoff output byte 상한이다. */
    uint64_t arena_bytes; /**< Core arena 최소 byte 수다. */
    uint64_t operation_deadline_ms; /**< Operation duration 상한이다. */
};

/** @brief Link-time mode policy와 capability 경계를 나타낸다. */
struct RibonModeDescriptor {
    uint32_t size; /**< 이 descriptor의 byte 크기다. */
    uint32_t abi_version; /**< `RIBON_CORE_ABI_VERSION`과 일치해야 한다. */
    enum RibonMode mode; /**< 선택한 mode다. */
    const char *name; /**< 안정적인 mode ID다. */
    uint64_t required_capabilities; /**< Registry가 제공해야 하는 bit다. */
    uint64_t forbidden_capabilities; /**< Registry에 있으면 안 되는 bit다. */
    struct RibonResourceLimits limits; /**< Mode 자원 상한이다. */
};

/** @brief Mode 값의 안정적인 이름을 반환한다. */
const char *ribon_mode_name(enum RibonMode mode);

/** @brief Resource limit의 내부 일관성을 검사한다. */
int ribon_resource_limits_are_valid(const struct RibonResourceLimits *limits);

/** @brief Mode descriptor ABI, capability와 limit를 fail-closed로 검사한다. */
int ribon_mode_descriptor_is_valid(const struct RibonModeDescriptor *mode);

/** @brief Product object graph가 선택한 단 하나의 mode descriptor를 반환한다. */
const struct RibonModeDescriptor *ribon_mode_selected(void);

#endif
