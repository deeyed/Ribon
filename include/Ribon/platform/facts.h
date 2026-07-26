#ifndef RIBON_PLATFORM_FACTS_H
#define RIBON_PLATFORM_FACTS_H

#include <stdint.h>

#include <Ribon/arch/ops.h>
#include <Ribon/firmware/environment.h>

struct RibonPluginDescriptor;

/** @brief Platform fact operation table ABI다. */
#define RIBON_PLATFORM_FACTS_ABI_VERSION 1u

/**
 * @brief Target가 build-time에 선택한 immutable platform resource와 load window다.
 *
 * Runtime-discovered FDT 또는 firmware fact가 이 값과 충돌하면 environment가 거부한다.
 */
struct RibonPlatformFacts {
    uint32_t size; /**< Descriptor byte 크기다. */
    uint32_t abi_version; /**< `RIBON_PLATFORM_FACTS_ABI_VERSION`이다. */
    const char *id; /**< Product tuple의 stable platform ID다. */
    enum RibonArchitectureId architecture; /**< 허용 architecture다. */
    enum RibonEnvironmentKind environment; /**< 허용 entry environment다. */
    uint64_t diagnostic_uart_base; /**< 0이면 target이 MMIO UART를 제공하지 않는다. */
    uint32_t diagnostic_poll_limit; /**< 한 byte polling 상한이다. */
    uint64_t timer_frequency_hz; /**< Architecture counter frequency다. */
    uint64_t native_input_capacity; /**< FDT 등 native input byte 상한이다. */
    uint64_t payload_load_base; /**< Boot payload reserved window 시작이다. */
    uint64_t payload_load_size; /**< Boot payload reserved window 크기다. */
};

/** @brief Platform fact descriptor의 tuple과 bounded resource를 검사한다. */
int ribon_platform_facts_are_valid(const struct RibonPlatformFacts *facts);

/** @brief Platform plugin descriptor와 operation table을 함께 검사한다. */
int ribon_platform_plugin_operations_are_valid(
    const struct RibonPluginDescriptor *descriptor);

/** @brief Target image가 선택한 platform facts를 반환한다. */
const struct RibonPlatformFacts *ribon_platform_selected(void);

/** @brief Target image가 registry에 제공하는 platform plugin descriptor다. */
extern const struct RibonPluginDescriptor ribon_platform_plugin_descriptor;

#endif
