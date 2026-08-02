#include "../../src/environments/raw-fdt/raw_fdt.h"

#include <Ribon/arch/entry.h>
#include <Ribon/boot/module_bundle.h>
#include <Ribon/boot/transfer.h>
#include <Ribon/port/port.h>

#include <string.h>

#define RIBON_BOOTMGR_MAX_MEMORY_REGIONS RIBON_RAW_FDT_MAX_MEMORY_REGIONS
#define RIBON_BOOTMGR_MAX_LOAD_SEGMENTS 16u
#define RIBON_BOOTMGR_HANDOFF_CAPACITY 65536u
#define RIBON_BOOTMGR_ARENA_CAPACITY (256u * 1024u)

extern const unsigned char ribon_embedded_payload[];
extern const uint64_t ribon_embedded_payload_size;
extern unsigned char __image_start[];
extern unsigned char __bootloader_runtime_end[];
extern unsigned char __ribon_boot_modules_start[];
extern unsigned char __ribon_boot_modules_end[];
extern unsigned char __image_end[];

#if defined(RIBON_RAW_FDT_HAS_BOOT_MODULE_BUNDLE)
extern const struct RibonServiceDescriptor
    ribon_generated_boot_module_bundle_service_descriptor;
#endif

static struct RibonMemoryRegion environment_regions[RIBON_BOOTMGR_MAX_MEMORY_REGIONS];
static struct RibonMemoryRegion normalized_regions[RIBON_BOOTMGR_MAX_MEMORY_REGIONS];
static struct RibonLoadSegment load_segments[RIBON_BOOTMGR_MAX_LOAD_SEGMENTS];
static struct RibonBootModule boot_modules[RIBON_BOOT_MODULE_CAPACITY];
#if defined(RIBON_RAW_FDT_HAS_BOOT_MODULE_BUNDLE)
static struct RibonBootModuleBackingRange
    boot_module_backings[RIBON_BOOT_MODULE_CAPACITY];
#endif
static _Alignas(4096) unsigned char handoff_buffer[RIBON_BOOTMGR_HANDOFF_CAPACITY];
static _Alignas(16) unsigned char arena_storage[RIBON_BOOTMGR_ARENA_CAPACITY];
static const struct RibonDiagnosticSinkServiceOperations *diagnostic_sink;

/** @brief Early serial에 stable marker를 기록한다. */
static void bootmgr_marker(const char *marker) {
    if (diagnostic_sink != 0) {
        uint64_t length = 0u;
        while (marker[length] != '\0') {
            ++length;
        }
        (void)diagnostic_sink->write(diagnostic_sink->context, marker, length);
        (void)diagnostic_sink->write(diagnostic_sink->context, "\r\n", 2u);
    }
}

/** @brief 64-bit 값을 fixed-width hexadecimal marker로 기록한다. */
static void bootmgr_hex(const char *prefix, uint64_t value) {
    char digits[19];
    digits[0] = '0';
    digits[1] = 'x';
    for (uint32_t index = 0u; index < 16u; ++index) {
        const uint32_t shift = (15u - index) * 4u;
        const uint8_t digit = (uint8_t)((value >> shift) & 0xfu);
        digits[index + 2u] =
            (char)(digit < 10u ? ('0' + digit) : ('a' + digit - 10u));
    }
    digits[18] = '\0';
    if (diagnostic_sink != 0) {
        uint64_t length = 0u;
        while (prefix[length] != '\0') {
            ++length;
        }
        (void)diagnostic_sink->write(diagnostic_sink->context, prefix, length);
    }
    bootmgr_marker(digits);
}

/** @brief First divergence marker를 남기고 architecture halt로 전환한다. */
static _Noreturn void bootmgr_fail(const char *stage, int status) {
    bootmgr_marker("RIBON-R4-RAW-FDT-FAIL");
    bootmgr_marker(stage);
    bootmgr_hex("RIBON-R4-STATUS=", (uint64_t)(int64_t)status);
    ribon_arch_selected_ops()->halt();
    for (;;) {
    }
}

/** @brief Product capability와 generated service authority의 iff 관계를 검사한다. */
static int bootmgr_module_authority_is_exact(
    const struct RibonProductDescriptor *product,
    const struct RibonServiceDirectory *services,
    const struct RibonServiceDescriptor *expected) {
    const int selected = expected != 0;
    uint32_t provider_count = 0u;
    if (!ribon_product_descriptor_is_valid(product) || services == 0 ||
        services->size != sizeof(*services) ||
        services->abi_version != RIBON_SERVICE_DIRECTORY_ABI_VERSION ||
        services->service_count > RIBON_SERVICE_DIRECTORY_LIMIT ||
        (services->service_count != 0u && services->services == 0) ||
        (((product->required_capabilities & RIBON_CAP_BOOT_MODULE_BUNDLE) != 0u) !=
         selected) ||
        (((product->allowed_capabilities & RIBON_CAP_BOOT_MODULE_BUNDLE) != 0u) !=
         selected)) {
        return 0;
    }
    for (uint32_t index = 0u; index < services->service_count; ++index) {
        const struct RibonServiceDescriptor *service = services->services[index];
        if (service != 0 &&
            service->kind == RIBON_SERVICE_KIND_BOOT_MODULE_BUNDLE) {
            if (!selected || service != expected) {
                return 0;
            }
            ++provider_count;
        }
    }
    return provider_count == (selected ? 1u : 0u);
}

/** @brief Analyzed segment를 target payload window 안에 복사한다. */
static int bootmgr_place_payload(
    const struct RibonPayloadPlacementServiceOperations *placement,
    const struct RibonPayloadImage *payload,
    struct RibonLoadedPayload *layout) {
    const uint64_t window_end =
        placement->physical_base + placement->physical_size;
    for (uint32_t index = 0u; index < layout->segment_count; ++index) {
        struct RibonLoadSegment *segment = &layout->segments[index];
        const unsigned char *source =
            (const unsigned char *)payload->data + segment->file_offset;
        unsigned char *destination =
            (unsigned char *)(uintptr_t)segment->load_address;
        uint64_t destination_end;
        if (segment->load_address < placement->physical_base ||
            segment->load_address > UINT64_MAX - segment->memory_size ||
            (destination_end = segment->load_address + segment->memory_size) >
                window_end ||
            segment->file_offset > payload->size ||
            segment->file_size > payload->size - segment->file_offset ||
            segment->file_size > segment->memory_size) {
            return RIBON_BOOT_STATUS_INVALID_PAYLOAD;
        }
        if (source == destination) {
            memset(
                destination + segment->file_size,
                0,
                (size_t)(segment->memory_size - segment->file_size));
        } else {
            const uintptr_t source_base = (uintptr_t)source;
            const uintptr_t destination_base = (uintptr_t)destination;
            if (segment->file_size > UINTPTR_MAX - source_base ||
                segment->memory_size > UINTPTR_MAX - destination_base ||
                (source_base < destination_base + segment->memory_size &&
                 destination_base < source_base + segment->file_size)) {
                return RIBON_BOOT_STATUS_INVALID_PAYLOAD;
            }
            memset(destination, 0, (size_t)segment->memory_size);
            memcpy(destination, source, (size_t)segment->file_size);
        }
        segment->runtime_address = segment->load_address;
        if (ribon_arch_selected_ops()->cache_sync(
                segment->runtime_address,
                segment->memory_size) != RIBON_ARCH_OPERATION_OK) {
            return RIBON_BOOT_STATUS_INVALID_PAYLOAD;
        }
    }
    layout->runtime_entry_address = layout->entry_load_address;
    layout->runtime_load_base = layout->load_base;
    layout->runtime_load_end = layout->load_end;
    layout->load_plan_flags |= RIBON_LOAD_PLAN_SEGMENTS_PLACED;
    return RIBON_BOOT_STATUS_OK;
}

/**
 * @brief raw-FDT native entry를 selected product graph와 boot protocol에 연결한다.
 *
 * 이 함수는 allocation과 interrupt를 사용하지 않으며 성공 시 payload로 terminal transfer한다.
 */
_Noreturn void ribon_raw_fdt_boot_main(
    uint64_t boot_cpu_id,
    uint64_t fdt_address) {
    const struct RibonPortDescriptor *port = ribon_port_selected();
    const struct RibonArchOps *arch = ribon_arch_selected_ops();
    const struct RibonPluginRegistry *registry;
    const struct RibonProductDescriptor *product;
    const struct RibonServiceDirectory *services;
    const struct RibonPluginDescriptor *protocol_plugin;
    const struct RibonPluginDescriptor *image_plugin;
    const struct RibonBootProtocol *protocol;
    const struct RibonImageFormatOps *image_format;
    const struct RibonMachineDescriptionServiceOperations *machine;
    const struct RibonPayloadPlacementServiceOperations *placement;
    struct RibonRawFdtReservation
        reservations[RIBON_RAW_FDT_MAX_TARGET_RESERVATIONS];
    struct RibonRawFdtEntry native_entry;
    struct RibonBootEnvironment environment;
    struct RibonArena arena;
    struct RibonCoreContext core;
    struct RibonBootTransaction transaction;
    struct RibonBootSource source;
    struct RibonLoadedPayload layout;
    struct RibonMutableMemoryMap normalized;
    struct RibonHandoffArtifact handoff;
    struct RibonBootEnvironmentPersistentInputs persistent_inputs;
    const struct RibonBootPlan *plan;
    uint32_t reservation_count = 0u;
    uint32_t boot_module_count = 0u;
    uint32_t initial_image_count = 0u;
    int status;

    if (!ribon_port_descriptor_is_valid(port) ||
        arch == 0 || arch->descriptor == 0 ||
        port->architecture != arch->descriptor->id ||
        port->environment != RIBON_ENVIRONMENT_RAW_FDT ||
        port->diagnostic_sink == 0 ||
        port->machine_description == 0 ||
        port->payload_placement == 0) {
        if (arch != 0 && arch->halt != 0) {
            arch->halt();
        }
        for (;;) {
        }
    }
    diagnostic_sink = port->diagnostic_sink->operations;
    machine = port->machine_description->operations;
    placement = port->payload_placement->operations;
    if (diagnostic_sink->initialize(diagnostic_sink->context) !=
        RIBON_SERVICE_STATUS_OK) {
        arch->halt();
    }
    bootmgr_marker("RIBON-R4-RAW-FDT-ENTRY");
    bootmgr_marker(port->id);

    product = ribon_generated_product_descriptor();
    services = ribon_generated_service_directory();
#if defined(RIBON_RAW_FDT_HAS_BOOT_MODULE_BUNDLE)
    if (!bootmgr_module_authority_is_exact(
            product,
            services,
            &ribon_generated_boot_module_bundle_service_descriptor)) {
        bootmgr_fail("module-authority", RIBON_BOOT_STATUS_BAD_ARGUMENT);
    }
#else
    if (!bootmgr_module_authority_is_exact(product, services, 0)) {
        bootmgr_fail("module-authority", RIBON_BOOT_STATUS_BAD_ARGUMENT);
    }
#endif

    reservations[reservation_count++] = (struct RibonRawFdtReservation){
        .base = (uint64_t)(uintptr_t)__image_start,
        .size = (uint64_t)(__bootloader_runtime_end - __image_start),
        .kind = RIBON_MEMORY_REGION_BOOTLOADER,
    };
    reservations[reservation_count++] = (struct RibonRawFdtReservation){
        .base = placement->physical_base,
        .size = placement->physical_size,
        .kind = RIBON_MEMORY_REGION_KERNEL_IMAGE,
    };
#if defined(RIBON_RAW_FDT_HAS_BOOT_MODULE_BUNDLE)
    const struct RibonBootModuleBundleServiceOperations *module_provider =
        ribon_generated_boot_module_bundle_service_descriptor.operations;
    if (!ribon_service_descriptor_is_valid(
            &ribon_generated_boot_module_bundle_service_descriptor) ||
        module_provider == 0 ||
        module_provider->section_start != __ribon_boot_modules_start ||
        module_provider->section_end != __ribon_boot_modules_end) {
        bootmgr_fail("module-provider", RIBON_BOOT_MODULE_BUNDLE_STATUS_BAD_ABI);
    }
    status = ribon_boot_module_bundle_materialize(
        module_provider->bundle,
        &(const struct RibonBootModuleBundleLayout){
            .section_base =
                (uint64_t)(uintptr_t)__ribon_boot_modules_start,
            .section_size =
                (uint64_t)(__ribon_boot_modules_end -
                           __ribon_boot_modules_start),
            .bootloader_base = (uint64_t)(uintptr_t)__image_start,
            .bootloader_size =
                (uint64_t)(__bootloader_runtime_end - __image_start),
            .kernel_base = placement->physical_base,
            .kernel_size = placement->physical_size,
            .alignment = arch->descriptor->boot_module_alignment,
        },
        boot_modules,
        boot_module_backings,
        RIBON_BOOT_MODULE_CAPACITY,
        &boot_module_count);
    if (status != RIBON_BOOT_MODULE_BUNDLE_STATUS_OK) {
        bootmgr_fail("module-bundle", status);
    }
    for (uint32_t index = 0u; index < boot_module_count; ++index) {
        reservations[reservation_count++] = (struct RibonRawFdtReservation){
            .base = boot_module_backings[index].base,
            .size = boot_module_backings[index].size,
            .kind = RIBON_MEMORY_REGION_BOOT_MODULE,
        };
        if (boot_modules[index].role == RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE) {
            ++initial_image_count;
        }
    }
#endif
    native_entry = (struct RibonRawFdtEntry){
        .fdt = (const void *)(uintptr_t)fdt_address,
        .fdt_capacity = machine->native_input_capacity,
        .boot_cpu_id = boot_cpu_id,
        .architecture = port->architecture,
        .arch_ops = arch,
        .timer_frequency_hz = port->timer_frequency_hz,
        .payload = ribon_embedded_payload,
        .payload_size = ribon_embedded_payload_size,
        .payload_name = "boot/payload",
        .reservations = reservations,
        .reservation_count = reservation_count,
        .memory_regions = environment_regions,
        .memory_region_capacity = RIBON_BOOTMGR_MAX_MEMORY_REGIONS,
    };
    status = ribon_raw_fdt_environment_capture(&native_entry, &environment);
    if (status != RIBON_RAW_FDT_STATUS_OK) {
        bootmgr_fail("environment-capture", status);
    }
    persistent_inputs = (struct RibonBootEnvironmentPersistentInputs){
        .boot_media = environment.boot_media,
        .boot_modules = {
            .modules = boot_module_count != 0u ? boot_modules : 0,
            .module_count = boot_module_count,
        },
        .command_line = environment.command_line,
    };
    if (!ribon_boot_environment_apply_persistent_inputs(
            &environment, &persistent_inputs)) {
        bootmgr_fail("persistent-inputs", RIBON_BOOT_STATUS_INCOMPLETE_ENVIRONMENT);
    }
    bootmgr_marker("RIBON-R4-FDT-ACCEPTED");
    bootmgr_hex("RIBON-RFDT-MODULES=", boot_module_count);
    bootmgr_hex("RIBON-RFDT-INITIAL-IMAGES=", initial_image_count);

    registry = ribon_generated_plugin_registry();
    protocol_plugin = ribon_plugin_registry_find_selected(
        registry, product, RIBON_PLUGIN_KIND_BOOT_PROTOCOL);
    image_plugin = ribon_plugin_registry_find_selected(
        registry, product, RIBON_PLUGIN_KIND_IMAGE_FORMAT);
    if (protocol_plugin == 0 || image_plugin == 0) {
        bootmgr_fail("product-selection", RIBON_BOOT_STATUS_BAD_ARGUMENT);
    }
    protocol = protocol_plugin->operations;
    image_format = image_plugin->operations;
    ribon_arena_init(&arena, arena_storage, sizeof(arena_storage));
    status = ribon_context_initialize(
        &core,
        product,
        registry,
        services,
        ribon_mode_selected(),
        &arena);
    if (status != RIBON_CORE_STATUS_OK) {
        bootmgr_fail("product-graph", status);
    }
    bootmgr_marker("RIBON-R4-PRODUCT-GRAPH-OK");
    status = ribon_boot_transaction_initialize(
        &transaction,
        &core,
        arch,
        protocol,
        image_format);
    if (status != RIBON_BOOT_STATUS_OK) {
        bootmgr_fail("transaction", status);
    }

    source = (struct RibonBootSource){
        .kind = RIBON_BOOT_MEDIA_MEMORY,
        .source_id = 0u,
        .size = ribon_embedded_payload_size,
    };
    layout = (struct RibonLoadedPayload){
        .segments = load_segments,
        .segment_capacity = RIBON_BOOTMGR_MAX_LOAD_SEGMENTS,
    };
    normalized = (struct RibonMutableMemoryMap){
        .regions = normalized_regions,
        .capacity = RIBON_BOOTMGR_MAX_MEMORY_REGIONS,
    };
    handoff = (struct RibonHandoffArtifact){0};
    status = ribon_boot_transaction_prepare(&transaction, &(struct RibonBootTransactionInput){
        .environment = &environment,
        .normalized_memory_map = &normalized,
        .source = &source,
        .source_offset = 0u,
        .source_size = ribon_embedded_payload_size,
        .payload_buffer = (void *)ribon_embedded_payload,
        .payload_buffer_capacity = ribon_embedded_payload_size,
        .source_name = "boot/payload",
        .kernel_layout = &layout,
        .handoff_buffer = handoff_buffer,
        .handoff_buffer_capacity = sizeof(handoff_buffer),
        .handoff_artifact = &handoff,
    });
    if (status != RIBON_BOOT_STATUS_OK) {
        bootmgr_fail("protocol-handoff", status);
    }
    plan = ribon_boot_transaction_plan(&transaction);
    if (plan == 0) {
        bootmgr_fail("transaction-plan", RIBON_BOOT_STATUS_BAD_STATE);
    }
    bootmgr_marker("RIBON-R4-PROTOCOL-HANDOFF-OK");
    status = bootmgr_place_payload(placement, &transaction.payload, &layout);
    if (status != RIBON_BOOT_STATUS_OK) {
        bootmgr_fail("payload-place", status);
    }
    bootmgr_marker("RIBON-R4-PAYLOAD-LOADED");
    if (ribon_boot_transaction_commit_attempt(&transaction) != RIBON_BOOT_STATUS_OK ||
        ribon_boot_transaction_quiesce_environment(&transaction) != RIBON_BOOT_STATUS_OK) {
        bootmgr_fail("quiesce", RIBON_BOOT_STATUS_BAD_STATE);
    }
    bootmgr_marker("RIBON-R4-RAW-FDT-TRANSFER");
    ribon_boot_transaction_transfer(&transaction);
}
