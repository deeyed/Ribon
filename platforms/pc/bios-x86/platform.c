#include <Ribon/platform/facts.h>
#include <Ribon/plugin/descriptor.h>

static const struct RibonPlatformFacts selected_platform = {
    .size = sizeof(selected_platform),
    .abi_version = RIBON_PLATFORM_FACTS_ABI_VERSION,
    .id = "pc-bios-x86",
    .architecture = RIBON_ARCHITECTURE_X86_64,
    .environment = RIBON_ENVIRONMENT_BIOS,
    .diagnostic_uart_base = 0x3f8u,
    .diagnostic_poll_limit = 1000000u,
    .timer_frequency_hz = 18u,
    .native_input_capacity = 4096u,
    .payload_load_base = 0x00200000ull,
    .payload_load_size = 16ull * 1024ull * 1024ull,
};

/** @brief 선택된 legacy client platform fact를 반환한다. */
const struct RibonPlatformFacts *ribon_platform_selected(void) {
    return &selected_platform;
}

/** @brief 선택된 legacy client platform plugin descriptor다. */
const struct RibonPluginDescriptor ribon_platform_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_platform_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_PLATFORM,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION,
    .id = "platform.pc-bios-x86",
    .provides = RIBON_CAP_PLATFORM_FACTS,
    .requires = RIBON_CAP_ARCHITECTURE,
    .architecture_mask = RIBON_ARCH_MASK_X86_64,
    .environment_mask = RIBON_ENV_MASK_BIOS,
    .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 4096u,
    .input_budget = 4096u,
    .output_budget = 4096u,
    .deadline_ms = 30000u,
    .operations = &selected_platform,
    .operations_size = sizeof(selected_platform),
    .operations_abi = RIBON_PLATFORM_FACTS_ABI_VERSION,
    .validate_operations = ribon_platform_plugin_operations_are_valid,
};
