#include <Ribon/core/context.h>
#include <Ribon/firmware/personality.h>

#include "../../../src/firmware/uefi-compatible/reference.h"

#include <stdio.h>

/** @brief UEFI-compatible provider graph와 bounded unsupported 결과를 검증한다. */
int main(void) {
    const struct RibonProductDescriptor *product =
        ribon_generated_product_descriptor();
    const struct RibonPluginRegistry *registry =
        ribon_generated_plugin_registry();
    const struct RibonPluginDescriptor *plugin;
    const struct RibonFirmwarePersonalityOperations *operations;
    const struct RibonFirmwareServiceDescriptor *handle_service;
    const struct RibonUefiReferenceHandleDatabaseOperations *handle_operations;
    const struct RibonFirmwareServiceDescriptor *service_storage[4];
    const uint32_t interfaces[RIBON_UEFI_REFERENCE_HANDLE_LIMIT + 1u] = {0};
    struct RibonUefiReferenceContext provider_context;
    struct RibonFirmwareServiceDirectory directory = {
        .size = sizeof(directory),
        .abi_version = RIBON_FIRMWARE_PERSONALITY_ABI_VERSION,
        .services = service_storage,
        .service_capacity = 4u,
    };
    struct RibonModeDescriptor mode = {
        .size = sizeof(mode),
        .abi_version = RIBON_CORE_ABI_VERSION,
        .mode = RIBON_MODE_DIAGNOSTIC,
        .name = "diagnostic",
        .required_capabilities =
            RIBON_CAP_ARCHITECTURE |
            RIBON_CAP_PLATFORM_FACTS |
            RIBON_CAP_FIRMWARE_PERSONALITY |
            RIBON_CAP_FIRMWARE_SERVICE_DIRECTORY,
        .limits = product->limits,
    };
    unsigned char arena_storage[64u * 1024u];
    struct RibonArena arena;
    struct RibonCoreContext context;
    int status;

    ribon_arena_init(&arena, arena_storage, sizeof(arena_storage));
    if (!ribon_product_descriptor_is_valid(product)) {
        fputs("uefi_provider_reference: product descriptor rejected\n", stderr);
        return 1;
    }
    for (uint32_t index = 0; index < registry->plugin_count; ++index) {
        if (!ribon_plugin_descriptor_is_valid(registry->plugins[index])) {
            fprintf(
                stderr,
                "uefi_provider_reference: plugin rejected id=%s\n",
                registry->plugins[index]->id);
            return 1;
        }
    }
    status = ribon_context_initialize(
            &context,
            product,
            registry,
            &mode,
            &arena);
    if (status != RIBON_CORE_STATUS_OK) {
        fprintf(
            stderr,
            "uefi_provider_reference: product graph rejected status=%d\n",
            status);
        return 1;
    }
    plugin = ribon_plugin_registry_find(
        registry,
        RIBON_PLUGIN_KIND_FIRMWARE_PERSONALITY,
        "personality.uefi-compatible");
    if (!ribon_firmware_personality_plugin_operations_are_valid(plugin)) {
        fputs("uefi_provider_reference: personality rejected\n", stderr);
        return 1;
    }
    operations = plugin->operations;
    ribon_uefi_reference_context_init(&provider_context);
    if (ribon_firmware_personality_publish(
            operations->personality,
            RIBON_PLUGIN_PHASE_BOOT,
            &provider_context,
            &directory) != RIBON_SERVICE_STATUS_OK ||
        ribon_firmware_service_directory_require(
            &directory,
            RIBON_FIRMWARE_SERVICE_HANDLE_DATABASE) !=
                RIBON_SERVICE_STATUS_OK ||
        ribon_firmware_service_directory_require(
            &directory,
            RIBON_FIRMWARE_SERVICE_VARIABLE) !=
                RIBON_SERVICE_STATUS_UNSUPPORTED) {
        fputs("uefi_provider_reference: service publication failed closed\n", stderr);
        return 1;
    }
    handle_service = ribon_firmware_service_directory_find(
        &directory,
        RIBON_FIRMWARE_SERVICE_HANDLE_DATABASE);
    if (handle_service == 0 ||
        handle_service->operations_size != sizeof(*handle_operations)) {
        fputs("uefi_provider_reference: handle service not found\n", stderr);
        return 1;
    }
    handle_operations = handle_service->operations;
    if (handle_operations->install(
            &provider_context,
            1u,
            1u,
            &interfaces[0]) != RIBON_SERVICE_STATUS_OK ||
        handle_operations->locate(&provider_context, 1u, 1u) !=
            &interfaces[0] ||
        handle_operations->install(
            &provider_context,
            1u,
            1u,
            &interfaces[0]) != RIBON_SERVICE_STATUS_BAD_ARGUMENT) {
        fputs("uefi_provider_reference: handle contract failed\n", stderr);
        return 1;
    }
    for (uint32_t index = 1u;
         index < RIBON_UEFI_REFERENCE_HANDLE_LIMIT;
         ++index) {
        if (handle_operations->install(
                &provider_context,
                index + 1u,
                index + 1u,
                &interfaces[index]) != RIBON_SERVICE_STATUS_OK) {
            fputs("uefi_provider_reference: handle capacity fill failed\n", stderr);
            return 1;
        }
    }
    if (handle_operations->install(
            &provider_context,
            RIBON_UEFI_REFERENCE_HANDLE_LIMIT + 1u,
            RIBON_UEFI_REFERENCE_HANDLE_LIMIT + 1u,
            &interfaces[RIBON_UEFI_REFERENCE_HANDLE_LIMIT]) !=
                RIBON_SERVICE_STATUS_OUT_OF_RANGE) {
        fputs("uefi_provider_reference: handle capacity accepted\n", stderr);
        return 1;
    }
    {
        struct RibonFirmwareServiceDirectory forged = directory;
        forged.published_services |= RIBON_FIRMWARE_SERVICE_VARIABLE;
        if (ribon_firmware_service_directory_require(
                &forged,
                RIBON_FIRMWARE_SERVICE_VARIABLE) !=
                    RIBON_SERVICE_STATUS_BAD_ARGUMENT) {
            fputs("uefi_provider_reference: forged directory accepted\n", stderr);
            return 1;
        }
    }
    puts("RIBON-R5-UEFI-FIRMWARE-PROVIDER-COMPILE-REFERENCE-OK");
    return 0;
}
