#include <Ribon/plugin/registry.h>

#include <stdio.h>

static int dummy_operations;

static int accept_policy_operations(
    const struct RibonPluginDescriptor *descriptor) {
    return descriptor != 0 &&
           descriptor->kind == RIBON_PLUGIN_KIND_POLICY &&
           descriptor->operations == &dummy_operations;
}

static struct RibonPluginDescriptor policy_descriptor(
    const char *id,
    uint64_t provides,
    uint64_t requires) {
    return (struct RibonPluginDescriptor){
        .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
        .size = sizeof(struct RibonPluginDescriptor),
        .abi_major = RIBON_PLUGIN_ABI_MAJOR,
        .abi_minor = RIBON_PLUGIN_ABI_MINOR,
        .kind = RIBON_PLUGIN_KIND_POLICY,
        .phase = RIBON_PLUGIN_PHASE_BOOT,
        .id = id,
        .provides = provides,
        .requires = requires,
        .architecture_mask = RIBON_ARCH_MASK_X86_64,
        .environment_mask = RIBON_ENV_MASK_HOST,
        .mode_mask = RIBON_MODE_MASK(RIBON_MODE_NORMAL),
        .arena_budget = 1024u,
        .input_budget = 1024u,
        .output_budget = 1024u,
        .deadline_ms = 1000u,
        .operations = &dummy_operations,
        .operations_size = sizeof(dummy_operations),
        .operations_abi = 1u,
        .validate_operations = accept_policy_operations,
    };
}

static struct RibonProductDescriptor policy_product(uint64_t capabilities) {
    return (struct RibonProductDescriptor){
        .magic = RIBON_PRODUCT_DESCRIPTOR_MAGIC,
        .size = sizeof(struct RibonProductDescriptor),
        .abi_version = RIBON_CORE_ABI_VERSION,
        .id = "policy-test",
        .architecture_mask = RIBON_ARCH_MASK_X86_64,
        .environment_mask = RIBON_ENV_MASK_HOST,
        .mode_mask = RIBON_MODE_MASK(RIBON_MODE_NORMAL),
        .max_plugins = 4u,
        .required_capabilities = capabilities,
        .allowed_capabilities = capabilities,
        .limits = {
            .max_memory_regions = 1u,
            .max_load_segments = 1u,
            .max_components = 1u,
            .max_retries = 1u,
            .max_input_bytes = 4096u,
            .max_handoff_bytes = 4096u,
            .arena_bytes = 4096u,
            .operation_deadline_ms = 1000u,
        },
    };
}

int main(void) {
    struct RibonPluginDescriptor first = policy_descriptor(
        "policy.a",
        RIBON_CAP_DIAGNOSTIC_SINK,
        RIBON_CAP_RANDOM_NONCE);
    struct RibonPluginDescriptor second = policy_descriptor(
        "policy.b",
        RIBON_CAP_RANDOM_NONCE,
        RIBON_CAP_DIAGNOSTIC_SINK);
    const struct RibonPluginDescriptor *plugins[] = {&first, &second};
    struct RibonPluginRegistry registry = {
        .size = sizeof(registry),
        .abi_version = RIBON_CORE_ABI_VERSION,
        .plugins = plugins,
        .plugin_count = 2u,
    };
    struct RibonProductDescriptor product = policy_product(
        RIBON_CAP_DIAGNOSTIC_SINK | RIBON_CAP_RANDOM_NONCE);

    if (!ribon_plugin_descriptor_is_valid(&first) ||
        ribon_plugin_registry_validate(
            &registry,
            &product,
            RIBON_MODE_NORMAL) != RIBON_CORE_STATUS_DEPENDENCY_CYCLE) {
        fputs("descriptor_tests: dependency cycle was not rejected\n", stderr);
        return 1;
    }

    first.requires = 0u;
    second.requires = 0u;
    second.provides = RIBON_CAP_DIAGNOSTIC_SINK;
    if (ribon_plugin_registry_validate(
            &registry,
            &product,
            RIBON_MODE_NORMAL) != RIBON_CORE_STATUS_DUPLICATE_PROVIDER) {
        fputs("descriptor_tests: duplicate provider was not rejected\n", stderr);
        return 1;
    }

    second.provides = RIBON_CAP_RANDOM_NONCE;
    product.required_capabilities |= RIBON_CAP_NETWORK_TRANSPORT;
    product.allowed_capabilities |= RIBON_CAP_NETWORK_TRANSPORT;
    if (ribon_plugin_registry_validate(
            &registry,
            &product,
            RIBON_MODE_NORMAL) != RIBON_CORE_STATUS_MISSING_CAPABILITY) {
        fputs("descriptor_tests: missing capability was not rejected\n", stderr);
        return 1;
    }

    puts("RIBON-R3-PLUGIN-DESCRIPTOR-NEGATIVE-OK");
    return 0;
}
