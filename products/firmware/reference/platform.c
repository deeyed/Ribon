#include <Ribon/platform/facts.h>
#include <Ribon/plugin/descriptor.h>

static const struct RibonPlatformFacts firmware_reference_platform = {
    .size = sizeof(firmware_reference_platform),
    .abi_version = RIBON_PLATFORM_FACTS_ABI_VERSION,
    .id = "firmware-reference",
    .architecture = RIBON_ARCHITECTURE_X86_64,
    .environment = RIBON_ENVIRONMENT_HOST,
    .timer_frequency_hz = 1000000u,
    .native_input_capacity = 4096u,
    .payload_load_base = 0x00200000u,
    .payload_load_size = 16ull * 1024ull * 1024ull,
};

/** @brief Firmware provider compile reference의 platform fact를 반환한다. */
const struct RibonPlatformFacts *ribon_firmware_reference_platform(void) {
    return &firmware_reference_platform;
}

/** @brief Firmware provider compile reference의 platform plugin descriptor다. */
const struct RibonPluginDescriptor
    ribon_firmware_reference_platform_plugin_descriptor = {
        .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
        .size =
            sizeof(ribon_firmware_reference_platform_plugin_descriptor),
        .abi_major = RIBON_PLUGIN_ABI_MAJOR,
        .abi_minor = RIBON_PLUGIN_ABI_MINOR,
        .kind = RIBON_PLUGIN_KIND_PLATFORM,
        .phase = RIBON_PLUGIN_PHASE_FOUNDATION,
        .id = "platform.firmware-reference",
        .provides = RIBON_CAP_PLATFORM_FACTS,
        .requires = RIBON_CAP_ARCHITECTURE,
        .architecture_mask = RIBON_ARCH_MASK_X86_64,
        .environment_mask = RIBON_ENV_MASK_HOST,
        .personality_mask = RIBON_PERSONALITY_MASK_ALL,
        .mode_mask = RIBON_MODE_MASK_ALL,
        .arena_budget = 2048u,
        .input_budget = 4096u,
        .output_budget = 4096u,
        .deadline_ms = 1000u,
        .operations = &firmware_reference_platform,
        .operations_size = sizeof(firmware_reference_platform),
        .operations_abi = RIBON_PLATFORM_FACTS_ABI_VERSION,
        .validate_operations = ribon_platform_plugin_operations_are_valid,
    };
