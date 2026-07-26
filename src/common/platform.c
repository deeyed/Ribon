#include <Ribon/platform/facts.h>
#include <Ribon/plugin/descriptor.h>

/** @brief Platform fact descriptor의 tuple과 bounded resource를 검사한다. */
int ribon_platform_facts_are_valid(const struct RibonPlatformFacts *facts) {
    return facts != 0 &&
           facts->size == sizeof(*facts) &&
           facts->abi_version == RIBON_PLATFORM_FACTS_ABI_VERSION &&
           facts->id != 0 &&
           facts->id[0] != '\0' &&
           facts->architecture >= RIBON_ARCHITECTURE_X86_64 &&
           facts->architecture <= RIBON_ARCHITECTURE_RISCV64 &&
           facts->environment >= RIBON_ENVIRONMENT_HOST &&
           facts->environment <= RIBON_ENVIRONMENT_SBI &&
           facts->timer_frequency_hz != 0u &&
           facts->native_input_capacity != 0u &&
           facts->payload_load_base != 0u &&
           facts->payload_load_size != 0u &&
           facts->payload_load_base <= UINT64_MAX - facts->payload_load_size &&
           ((facts->diagnostic_uart_base == 0u &&
             facts->diagnostic_poll_limit == 0u) ||
            (facts->diagnostic_uart_base != 0u &&
             facts->diagnostic_poll_limit != 0u));
}

/** @brief Platform plugin descriptor와 operation table을 함께 검사한다. */
int ribon_platform_plugin_operations_are_valid(
    const struct RibonPluginDescriptor *descriptor) {
    const struct RibonPlatformFacts *facts;
    if (descriptor == 0 ||
        descriptor->kind != RIBON_PLUGIN_KIND_PLATFORM ||
        descriptor->provides != RIBON_CAP_PLATFORM_FACTS ||
        descriptor->operations_size != sizeof(struct RibonPlatformFacts) ||
        descriptor->operations_abi != RIBON_PLATFORM_FACTS_ABI_VERSION) {
        return 0;
    }
    facts = (const struct RibonPlatformFacts *)descriptor->operations;
    return ribon_platform_facts_are_valid(facts) &&
           descriptor->architecture_mask ==
               ribon_architecture_mask(facts->architecture) &&
           (descriptor->environment_mask &
            (1u << (uint32_t)facts->environment)) != 0u;
}
