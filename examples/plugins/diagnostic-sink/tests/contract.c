#include <Ribon/core/context.h>
#include <Ribon/sdk/host.h>
#include <RibonExamples/diagnostic_sink.h>

#include <stdio.h>

extern const struct RibonPluginDescriptor ribon_example_fixture_arch_plugin;
extern const struct RibonPluginDescriptor ribon_example_fixture_environment_plugin;
extern const struct RibonPluginDescriptor ribon_example_fixture_platform_plugin;

/** @brief Generated external graph, package ABI와 negative descriptor를 검증한다. */
int main(void) {
    const struct RibonProductDescriptor *product =
        ribon_generated_product_descriptor();
    const struct RibonPluginRegistry *registry =
        ribon_generated_plugin_registry();
    struct RibonModeDescriptor mode = {
        .size = sizeof(mode),
        .abi_version = RIBON_CORE_ABI_VERSION,
        .mode = RIBON_MODE_DIAGNOSTIC,
        .name = "diagnostic",
        .required_capabilities =
            RIBON_CAP_ARCHITECTURE |
            RIBON_CAP_DIAGNOSTIC_SINK |
            RIBON_CAP_PLATFORM_FACTS |
            RIBON_CAP_SDK_CONTRACT,
        .limits = product->limits,
    };
    unsigned char arena_storage[32u * 1024u];
    struct RibonArena arena;
    struct RibonCoreContext context;
    struct RibonPluginDescriptor invalid_plugin =
        ribon_example_diagnostic_sink_plugin_descriptor;
    struct RibonSdkPluginPackage invalid_package =
        ribon_example_diagnostic_sink_package;
    struct RibonExampleDiagnosticSink sink = {
        .byte_limit = 8u,
    };
    const struct RibonExampleDiagnosticSinkOperations *operations =
        ribon_example_diagnostic_sink_plugin_descriptor.operations;
    const char receipt[] = "ribon";

    ribon_arena_init(&arena, arena_storage, sizeof(arena_storage));
    if (ribon_context_initialize(
            &context,
            product,
            registry,
            ribon_generated_service_directory(),
            &mode,
            &arena) != RIBON_CORE_STATUS_OK ||
        ribon_sdk_host_validate_package(
            &ribon_example_diagnostic_sink_package,
            RIBON_PLUGIN_KIND_SERVICE,
            RIBON_CAP_DIAGNOSTIC_SINK | RIBON_CAP_SDK_CONTRACT,
            RIBON_CAP_NETWORK_TRANSPORT) != RIBON_CORE_STATUS_OK ||
        operations->write(&sink, receipt, sizeof(receipt)) !=
            RIBON_CORE_STATUS_OK ||
        sink.bytes_written != sizeof(receipt)) {
        fputs("external_plugin_contract: valid package rejected\n", stderr);
        return 1;
    }

    invalid_plugin.operations_abi += 1u;
    invalid_package.plugin = &invalid_plugin;
    if (ribon_sdk_plugin_package_is_valid(&invalid_package)) {
        fputs("external_plugin_contract: bad operation ABI accepted\n", stderr);
        return 1;
    }

    invalid_plugin = ribon_example_diagnostic_sink_plugin_descriptor;
    invalid_plugin.phase = RIBON_PLUGIN_PHASE_RUNTIME;
    {
        const struct RibonPluginDescriptor *plugins[] = {
            &ribon_example_fixture_arch_plugin,
            &ribon_example_fixture_environment_plugin,
            &ribon_example_fixture_platform_plugin,
            &invalid_plugin,
        };
        const struct RibonPluginRegistry invalid_registry = {
            .size = sizeof(invalid_registry),
            .abi_version = RIBON_CORE_ABI_VERSION,
            .plugins = plugins,
            .plugin_count = 4u,
        };
        if (ribon_plugin_registry_validate(
                &invalid_registry,
                product,
                RIBON_MODE_DIAGNOSTIC) !=
                    RIBON_CORE_STATUS_INVALID_DESCRIPTOR) {
            fputs("external_plugin_contract: library runtime plugin accepted\n", stderr);
            return 1;
        }
    }
    puts("RIBON-R6-EXTERNAL-TYPED-SERVICE-PACKAGE-OK");
    return 0;
}
