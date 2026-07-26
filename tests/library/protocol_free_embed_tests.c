#include <Ribon/core/context.h>

#include <stdio.h>

static int policy_operations;

static int validate_policy(const struct RibonPluginDescriptor *descriptor) {
    return descriptor != 0 &&
           descriptor->kind == RIBON_PLUGIN_KIND_POLICY &&
           descriptor->operations == &policy_operations;
}

int main(void) {
    const struct RibonPluginDescriptor policy = {
        .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
        .size = sizeof(policy),
        .abi_major = RIBON_PLUGIN_ABI_MAJOR,
        .abi_minor = RIBON_PLUGIN_ABI_MINOR,
        .kind = RIBON_PLUGIN_KIND_POLICY,
        .phase = RIBON_PLUGIN_PHASE_FOUNDATION,
        .id = "policy.embed",
        .provides = RIBON_CAP_DIAGNOSTIC_SINK,
        .architecture_mask = RIBON_ARCH_MASK_X86_64,
        .environment_mask = RIBON_ENV_MASK_HOST,
        .mode_mask = RIBON_MODE_MASK(RIBON_MODE_DIAGNOSTIC),
        .arena_budget = 1024u,
        .input_budget = 1024u,
        .output_budget = 1024u,
        .deadline_ms = 1000u,
        .operations = &policy_operations,
        .operations_size = sizeof(policy_operations),
        .operations_abi = 1u,
        .validate_operations = validate_policy,
    };
    const struct RibonPluginDescriptor *plugins[] = {&policy};
    const struct RibonPluginRegistry registry = {
        .size = sizeof(registry),
        .abi_version = RIBON_CORE_ABI_VERSION,
        .plugins = plugins,
        .plugin_count = 1u,
    };
    const struct RibonResourceLimits limits = {
        .max_memory_regions = 1u,
        .max_load_segments = 1u,
        .max_components = 1u,
        .max_retries = 1u,
        .max_input_bytes = 4096u,
        .max_handoff_bytes = 4096u,
        .arena_bytes = 64u * 1024u,
        .operation_deadline_ms = 1000u,
    };
    const struct RibonProductDescriptor product = {
        .magic = RIBON_PRODUCT_DESCRIPTOR_MAGIC,
        .size = sizeof(product),
        .abi_version = RIBON_CORE_ABI_VERSION,
        .id = "protocol-free-embed",
        .architecture_mask = RIBON_ARCH_MASK_X86_64,
        .environment_mask = RIBON_ENV_MASK_HOST,
        .mode_mask = RIBON_MODE_MASK(RIBON_MODE_DIAGNOSTIC),
        .max_plugins = 1u,
        .required_capabilities = RIBON_CAP_DIAGNOSTIC_SINK,
        .allowed_capabilities = RIBON_CAP_DIAGNOSTIC_SINK,
        .limits = limits,
    };
    const struct RibonModeDescriptor mode = {
        .size = sizeof(mode),
        .abi_version = RIBON_CORE_ABI_VERSION,
        .mode = RIBON_MODE_DIAGNOSTIC,
        .name = "diagnostic",
        .required_capabilities = RIBON_CAP_DIAGNOSTIC_SINK,
        .limits = limits,
    };
    unsigned char storage[64u * 1024u];
    struct RibonArena arena;
    struct RibonCoreContext context;

    ribon_arena_init(&arena, storage, sizeof(storage));
    if (ribon_context_initialize(
            &context,
            &product,
            &registry,
            &mode,
            &arena) != RIBON_CORE_STATUS_OK) {
        fputs("protocol_free_embed_tests: context initialization failed\n", stderr);
        return 1;
    }
    puts("RIBON-R3-PROTOCOL-FREE-EMBED-OK");
    return 0;
}
