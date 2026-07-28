#ifndef RIBON_PORT_PORT_H
#define RIBON_PORT_PORT_H

#include <stdint.h>

#include <Ribon/arch/ops.h>
#include <Ribon/firmware/environment.h>
#include <Ribon/service/directory.h>

/** @brief Target composition이 선택한 machine port descriptor ABI다. */
#define RIBON_PORT_ABI_VERSION 1u

/**
 * @brief Firmware entry recipe가 소비하는 machine wiring aggregate다.
 *
 * Core와 Boot Library는 이 descriptor를 보지 않는다. 각 권한은 별도 typed service
 * descriptor로 service directory에 publish된다.
 */
struct RibonPortDescriptor {
    uint32_t size; /**< Descriptor byte 크기다. */
    uint32_t abi_version; /**< `RIBON_PORT_ABI_VERSION`이다. */
    const char *id; /**< Stable port ID다. */
    enum RibonArchitectureId architecture; /**< Port ISA다. */
    enum RibonEnvironmentKind environment; /**< Native entry environment다. */
    uint64_t timer_frequency_hz; /**< Native counter frequency다. */
    const struct RibonServiceDescriptor *diagnostic_sink; /**< Optional sink authority다. */
    const struct RibonServiceDescriptor *machine_description; /**< Optional native input authority다. */
    const struct RibonServiceDescriptor *payload_placement; /**< Optional payload window authority다. */
};

/** @brief Port tuple와 capability-specific service descriptor를 검사한다. */
int ribon_port_descriptor_is_valid(const struct RibonPortDescriptor *port);

/** @brief Target image가 정적으로 선택한 machine port를 반환한다. */
const struct RibonPortDescriptor *ribon_port_selected(void);

#endif
