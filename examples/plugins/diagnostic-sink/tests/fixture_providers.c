#include <Ribon/plugin/descriptor.h>

static int fixture_operations;

/** @brief External harness fixture provider가 자기 operation token을 가리키는지 검사한다. */
static int fixture_operations_are_valid(
    const struct RibonPluginDescriptor *descriptor) {
    return descriptor != 0 &&
           descriptor->operations == &fixture_operations &&
           descriptor->operations_size == sizeof(fixture_operations) &&
           descriptor->operations_abi == 1u;
}

/** @brief Library product contract용 architecture fixture provider다. */
const struct RibonPluginDescriptor ribon_example_fixture_arch_plugin = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_example_fixture_arch_plugin),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_ARCHITECTURE,
    .phase = RIBON_PLUGIN_PHASE_EARLY,
    .id = "arch.fixture",
    .provides = RIBON_CAP_ARCHITECTURE,
    .architecture_mask = RIBON_ARCH_MASK_X86_64,
    .environment_mask = RIBON_ENV_MASK_HOST,
    .mode_mask = RIBON_MODE_MASK(RIBON_MODE_DIAGNOSTIC),
    .arena_budget = 1024u,
    .input_budget = 1024u,
    .output_budget = 1024u,
    .deadline_ms = 1000u,
    .operations = &fixture_operations,
    .operations_size = sizeof(fixture_operations),
    .operations_abi = 1u,
    .validate_operations = fixture_operations_are_valid,
};

/** @brief Library product contract용 environment fixture provider다. */
const struct RibonPluginDescriptor ribon_example_fixture_environment_plugin = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_example_fixture_environment_plugin),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_ENVIRONMENT,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION,
    .id = "environment.fixture",
    .provides = RIBON_CAP_MONOTONIC_TIMER,
    .requires = RIBON_CAP_PLATFORM_FACTS,
    .architecture_mask = RIBON_ARCH_MASK_X86_64,
    .environment_mask = RIBON_ENV_MASK_HOST,
    .mode_mask = RIBON_MODE_MASK(RIBON_MODE_DIAGNOSTIC),
    .arena_budget = 1024u,
    .input_budget = 1024u,
    .output_budget = 1024u,
    .deadline_ms = 1000u,
    .operations = &fixture_operations,
    .operations_size = sizeof(fixture_operations),
    .operations_abi = 1u,
    .validate_operations = fixture_operations_are_valid,
};

/** @brief Library product contract용 platform fixture provider다. */
const struct RibonPluginDescriptor ribon_example_fixture_platform_plugin = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_example_fixture_platform_plugin),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_PLATFORM,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION,
    .id = "platform.fixture",
    .provides = RIBON_CAP_PLATFORM_FACTS,
    .requires = RIBON_CAP_ARCHITECTURE,
    .architecture_mask = RIBON_ARCH_MASK_X86_64,
    .environment_mask = RIBON_ENV_MASK_HOST,
    .mode_mask = RIBON_MODE_MASK(RIBON_MODE_DIAGNOSTIC),
    .arena_budget = 1024u,
    .input_budget = 1024u,
    .output_budget = 1024u,
    .deadline_ms = 1000u,
    .operations = &fixture_operations,
    .operations_size = sizeof(fixture_operations),
    .operations_abi = 1u,
    .validate_operations = fixture_operations_are_valid,
};
