#include <Ribon/core/context.h>
#include <Ribon/firmware/personality.h>

#include "../../../src/firmware/bios-compatible/reference.h"

#include <stdio.h>

/** @brief BIOS-compatible provider graph와 bounded unsupported 결과를 검증한다. */
int main(void) {
    const struct RibonProductDescriptor *product =
        ribon_generated_product_descriptor();
    const struct RibonPluginRegistry *registry =
        ribon_generated_plugin_registry();
    const struct RibonPluginDescriptor *plugin;
    const struct RibonFirmwarePersonalityOperations *operations;
    const struct RibonFirmwareServiceDescriptor *e820_service;
    const struct RibonBiosReferenceE820Operations *e820_operations;
    const struct RibonFirmwareServiceDescriptor *service_storage[4];
    struct RibonBiosReferenceE820Entry range;
    struct RibonBiosReferenceContext provider_context;
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
    status = ribon_context_initialize(
            &context,
            product,
            registry,
            &mode,
            &arena);
    if (status != RIBON_CORE_STATUS_OK) {
        fprintf(
            stderr,
            "bios_provider_reference: product graph rejected status=%d\n",
            status);
        return 1;
    }
    plugin = ribon_plugin_registry_find(
        registry,
        RIBON_PLUGIN_KIND_FIRMWARE_PERSONALITY,
        "personality.bios-compatible");
    if (!ribon_firmware_personality_plugin_operations_are_valid(plugin)) {
        fputs("bios_provider_reference: personality rejected\n", stderr);
        return 1;
    }
    operations = plugin->operations;
    ribon_bios_reference_context_init(&provider_context);
    if (ribon_firmware_personality_publish(
            operations->personality,
            RIBON_PLUGIN_PHASE_BOOT,
            &provider_context,
            &directory) != RIBON_SERVICE_STATUS_OK ||
        ribon_firmware_service_directory_require(
            &directory,
            RIBON_FIRMWARE_SERVICE_E820) != RIBON_SERVICE_STATUS_OK ||
        ribon_firmware_service_directory_require(
            &directory,
            RIBON_FIRMWARE_SERVICE_EDD) !=
                RIBON_SERVICE_STATUS_UNSUPPORTED) {
        fputs("bios_provider_reference: service publication failed closed\n", stderr);
        return 1;
    }
    e820_service = ribon_firmware_service_directory_find(
        &directory,
        RIBON_FIRMWARE_SERVICE_E820);
    if (e820_service == 0 ||
        e820_service->operations_size != sizeof(*e820_operations)) {
        fputs("bios_provider_reference: E820 service not found\n", stderr);
        return 1;
    }
    e820_operations = e820_service->operations;
    if (e820_operations->append(
            &provider_context,
            0x1000u,
            0x1000u,
            1u) != RIBON_SERVICE_STATUS_OK ||
        e820_operations->append(
            &provider_context,
            0x3000u,
            0x1000u,
            1u) != RIBON_SERVICE_STATUS_OK ||
        e820_operations->read(&provider_context, 0u, &range) !=
            RIBON_SERVICE_STATUS_OK ||
        range.base != 0x1000u ||
        range.length != 0x1000u ||
        range.type != 1u ||
        e820_operations->append(
            &provider_context,
            0x1800u,
            0x100u,
            1u) != RIBON_SERVICE_STATUS_BAD_ARGUMENT ||
        e820_operations->append(
            &provider_context,
            0x5000u,
            0u,
            1u) != RIBON_SERVICE_STATUS_BAD_ARGUMENT ||
        e820_operations->append(
            &provider_context,
            UINT64_MAX - 1u,
            2u,
            1u) != RIBON_SERVICE_STATUS_BAD_ARGUMENT) {
        fputs("bios_provider_reference: E820 range contract failed\n", stderr);
        return 1;
    }
    for (uint32_t index = 2u;
         index < RIBON_BIOS_REFERENCE_E820_LIMIT;
         ++index) {
        const uint64_t base = 0x5000u + (uint64_t)(index - 2u) * 0x2000u;
        if (e820_operations->append(
                &provider_context,
                base,
                0x1000u,
                1u) != RIBON_SERVICE_STATUS_OK) {
            fputs("bios_provider_reference: E820 capacity fill failed\n", stderr);
            return 1;
        }
    }
    if (e820_operations->append(
            &provider_context,
            0x100000u,
            0x1000u,
            1u) != RIBON_SERVICE_STATUS_OUT_OF_RANGE) {
        fputs("bios_provider_reference: E820 capacity accepted\n", stderr);
        return 1;
    }
    {
        struct RibonFirmwareServiceDirectory forged = directory;
        forged.published_services |= RIBON_FIRMWARE_SERVICE_EDD;
        if (ribon_firmware_service_directory_require(
                &forged,
                RIBON_FIRMWARE_SERVICE_EDD) !=
                    RIBON_SERVICE_STATUS_BAD_ARGUMENT) {
            fputs("bios_provider_reference: forged directory accepted\n", stderr);
            return 1;
        }
    }
    puts("RIBON-R5-BIOS-FIRMWARE-PROVIDER-COMPILE-REFERENCE-OK");
    return 0;
}
