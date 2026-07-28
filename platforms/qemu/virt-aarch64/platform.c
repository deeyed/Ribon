#include <Ribon/platform/facts.h>
#include <Ribon/plugin/descriptor.h>

static const struct RibonPlatformFacts selected_platform = {
    .size = sizeof(selected_platform),
    .abi_version = RIBON_PLATFORM_FACTS_ABI_VERSION,
    .id = "virt-aarch64",
    .architecture = RIBON_ARCHITECTURE_AARCH64,
    .environment = RIBON_ENVIRONMENT_RAW_FDT,
    .diagnostic_uart_base = 0x09000000ull,
    .diagnostic_poll_limit = 1000000u,
    .timer_frequency_hz = 62500000u,
    .native_input_capacity = 2ull * 1024ull * 1024ull,
    .payload_load_base = 0x41000000ull,
    .payload_load_size = 16ull * 1024ull * 1024ull,
};

/** @brief 선택된 virtual-machine platform fact를 반환한다. */
const struct RibonPlatformFacts *ribon_platform_selected(void) {
    return &selected_platform;
}

/** @brief 선택된 virtual-machine platform plugin descriptor다. */
const struct RibonPluginDescriptor ribon_platform_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_platform_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_PLATFORM,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION,
    .id = "platform.virt-aarch64",
    .provides = RIBON_CAP_PLATFORM_FACTS,
    .requires = RIBON_CAP_ARCHITECTURE,
    .architecture_mask = RIBON_ARCH_MASK_AARCH64,
    .environment_mask = RIBON_ENV_MASK_RAW_FDT,
    .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 4096u,
    .input_budget = 2ull * 1024ull * 1024ull,
    .output_budget = 4096u,
    .deadline_ms = 30000u,
    .operations = &selected_platform,
    .operations_size = sizeof(selected_platform),
    .operations_abi = RIBON_PLATFORM_FACTS_ABI_VERSION,
    .validate_operations = ribon_platform_plugin_operations_are_valid,
};
