#include <Ribon/platform/facts.h>
#include <Ribon/plugin/descriptor.h>

#ifndef RIBON_HOST_PLATFORM_ARCH
#define RIBON_HOST_PLATFORM_ARCH RIBON_ARCHITECTURE_X86_64
#endif

#ifndef RIBON_HOST_PLATFORM_ARCH_MASK
#define RIBON_HOST_PLATFORM_ARCH_MASK RIBON_ARCH_MASK_X86_64
#endif

static const struct RibonPlatformFacts host_platform = {
    .size = sizeof(host_platform),
    .abi_version = RIBON_PLATFORM_FACTS_ABI_VERSION,
    .id = "host",
    .architecture = RIBON_HOST_PLATFORM_ARCH,
    .environment = RIBON_ENVIRONMENT_HOST,
    .timer_frequency_hz = 1000000u,
    .native_input_capacity = 4096u,
    .payload_load_base = 0x00200000u,
    .payload_load_size = 64ull * 1024ull * 1024ull,
};

/** @brief Host reference target의 platform fact를 반환한다. */
const struct RibonPlatformFacts *ribon_platform_selected(void) {
    return &host_platform;
}

/** @brief Host reference target의 platform plugin descriptor다. */
const struct RibonPluginDescriptor ribon_platform_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_platform_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_PLATFORM,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION,
    .id = "platform.host",
    .provides = RIBON_CAP_PLATFORM_FACTS,
    .requires = RIBON_CAP_ARCHITECTURE,
    .architecture_mask = RIBON_HOST_PLATFORM_ARCH_MASK,
    .environment_mask = RIBON_ENV_MASK_HOST,
    .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 4096u,
    .input_budget = 4096u,
    .output_budget = 4096u,
    .deadline_ms = 30000u,
    .operations = &host_platform,
    .operations_size = sizeof(host_platform),
    .operations_abi = RIBON_PLATFORM_FACTS_ABI_VERSION,
    .validate_operations = ribon_platform_plugin_operations_are_valid,
};
