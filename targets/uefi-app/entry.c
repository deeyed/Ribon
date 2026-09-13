#include "../../src/environments/uefi-app/uefi_app.h"

#include <Ribon/arch/entry.h>
#include <Ribon/boot/transfer.h>
#include <Ribon/config/boot_config.h>
#include <Ribon/port/port.h>
#include <Ribon/protocols/os/luca/direct_fdt.h>

#define RIBON_UEFI_MEMORY_MAP_CAPACITY (128u * 1024u)
#define RIBON_UEFI_REGION_CAPACITY 512u
#define RIBON_UEFI_SEGMENT_CAPACITY 16u
#define RIBON_UEFI_HANDOFF_CAPACITY 65536u
#define RIBON_UEFI_ARENA_CAPACITY (256u * 1024u)
#define RIBON_UEFI_CONFIG_CAPACITY 4096u
#define RIBON_UEFI_PAYLOAD_CAPACITY (8u * 1024u * 1024u)

#if defined(__aarch64__)
#define RIBON_UEFI_TARGET_ARCHITECTURE RIBON_ARCHITECTURE_AARCH64
#elif defined(__x86_64__)
#define RIBON_UEFI_TARGET_ARCHITECTURE RIBON_ARCHITECTURE_X86_64
#else
#error "UEFI entry supports only x86_64 and AArch64"
#endif

static _Alignas(16) unsigned char raw_memory_map[RIBON_UEFI_MEMORY_MAP_CAPACITY];
static struct RibonMemoryRegion environment_regions[RIBON_UEFI_REGION_CAPACITY];
static struct RibonMemoryRegion normalized_regions[RIBON_UEFI_REGION_CAPACITY];
static struct RibonLoadSegment load_segments[RIBON_UEFI_SEGMENT_CAPACITY];
static _Alignas(4096) unsigned char handoff_buffer[RIBON_UEFI_HANDOFF_CAPACITY];
static _Alignas(16) unsigned char arena_storage[RIBON_UEFI_ARENA_CAPACITY];
static unsigned char boot_config_bytes[RIBON_UEFI_CONFIG_CAPACITY];
static _Alignas(4096) unsigned char payload_bytes[RIBON_UEFI_PAYLOAD_CAPACITY];
static struct RibonBootConfiguration boot_configuration;
static struct RibonBootModule boot_modules[RIBON_BOOT_CONFIG_MAX_MODULES];
static const struct RibonDiagnosticSinkServiceOperations *diagnostic_sink;

struct UefiRefreshContext {
    struct RibonBootTransaction *transaction;
};

/** @brief UEFI service lifetime과 무관한 stable serial marker를 기록한다. */
static void uefi_marker(const char *text) {
    uint64_t length = 0u;
    if (diagnostic_sink == 0 || text == 0) {
        return;
    }
    while (text[length] != '\0') {
        ++length;
    }
    (void)diagnostic_sink->write(diagnostic_sink->context, text, length);
    (void)diagnostic_sink->write(diagnostic_sink->context, "\r\n", 2u);
}

/** @brief Physical identity를 allocation-free fixed-width hexadecimal로 기록한다. */
static void uefi_marker_address(const char *label, uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    char line[96];
    uint32_t cursor = 0u;
    if (diagnostic_sink == 0 || label == 0) {
        return;
    }
    while (label[cursor] != '\0' && cursor + 19u < sizeof(line)) {
        line[cursor] = label[cursor];
        ++cursor;
    }
    if (cursor + 19u >= sizeof(line)) {
        return;
    }
    line[cursor++] = '0';
    line[cursor++] = 'x';
    for (uint32_t nibble = 0u; nibble < 16u; ++nibble) {
        line[cursor++] = digits[(value >> ((15u - nibble) * 4u)) & 0xfu];
    }
    line[cursor] = '\0';
    uefi_marker(line);
}

/** @brief UEFI first divergence를 serial에 남긴다. */
static EFI_STATUS uefi_fail(const char *stage) {
    uefi_marker("RIBON-R4-UEFI-FAIL");
    uefi_marker(stage);
    return EFI_LOAD_ERROR;
}

/** @brief Pointer-free transaction receipt를 stable first-divergence marker로 낮춘다. */
static EFI_STATUS uefi_transaction_fail(
    const struct RibonBootTransaction *transaction) {
    const struct RibonBootFailureReceipt *receipt =
        ribon_boot_transaction_failure_receipt(transaction);
    if (receipt == 0) {
        return uefi_fail("transaction-prepare-no-receipt");
    }
    switch (receipt->reason) {
    case RIBON_BOOT_FAILURE_BAD_INPUT:
        return uefi_fail("transaction-prepare-bad-input");
    case RIBON_BOOT_FAILURE_BUDGET:
        return uefi_fail("transaction-prepare-budget");
    case RIBON_BOOT_FAILURE_TIMEOUT:
        return uefi_fail("transaction-prepare-timeout");
    case RIBON_BOOT_FAILURE_SOURCE:
        return uefi_fail("transaction-prepare-source");
    case RIBON_BOOT_FAILURE_IMAGE:
        return uefi_fail("transaction-prepare-image");
    case RIBON_BOOT_FAILURE_PROTOCOL:
        return uefi_fail("transaction-prepare-protocol");
    default:
        return uefi_fail("transaction-prepare-other");
    }
}

/** @brief Bounded configuration command line의 NUL 제외 byte 수를 계산한다. */
static uint32_t uefi_text_length(const char *text, uint32_t capacity) {
    uint32_t length = 0u;
    while (length < capacity && text[length] != '\0') {
        ++length;
    }
    return length;
}

/** @brief Selected config identifier의 exact equality를 검사한다. */
static int uefi_text_equal(const char *lhs, const char *rhs) {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }
    while (*lhs != '\0' && *rhs != '\0' && *lhs == *rhs) {
        ++lhs;
        ++rhs;
    }
    return *lhs == *rhs;
}

/** @brief Final memory-map capture 뒤 committed handoff plan을 재생성한다. */
static int uefi_refresh_plan(
    void *context,
    struct RibonBootEnvironment *environment) {
    struct UefiRefreshContext *refresh =
        (struct UefiRefreshContext *)context;
    if (refresh == 0) {
        return -1;
    }
    return ribon_boot_transaction_refresh_after_commit(
               refresh->transaction,
               environment) == RIBON_BOOT_STATUS_OK ?
        0 :
        -1;
}

/**
 * @brief UEFI application entry에서 consumer transaction과 selected protocol을 실행한다.
 *
 * Final map을 handoff에 반영한 뒤 ExitBootServices를 성공해야만 payload로 전환한다.
 */
EFI_STATUS EFIAPI efi_main(
    EFI_HANDLE image_handle,
    EFI_SYSTEM_TABLE *system_table) {
    const struct RibonPortDescriptor *port = ribon_port_selected();
    const struct RibonArchOps *arch = ribon_arch_selected_ops();
    const struct RibonPluginRegistry *registry =
        ribon_generated_plugin_registry();
    const struct RibonProductDescriptor *product =
        ribon_generated_product_descriptor();
    const struct RibonPluginDescriptor *protocol_plugin;
    const struct RibonPluginDescriptor *image_plugin;
    const struct RibonBootProtocol *protocol;
    const struct RibonImageFormatOps *image_format;
    struct RibonUefiAppContext native = {
        .raw_memory_map = raw_memory_map,
        .raw_memory_map_capacity = sizeof(raw_memory_map),
        .regions = environment_regions,
        .region_capacity = RIBON_UEFI_REGION_CAPACITY,
    };
    struct RibonBootEnvironment environment;
    struct RibonArena arena;
    struct RibonCoreContext core;
    struct RibonBootTransaction transaction;
    struct RibonBootSource source;
    struct RibonValidatedImage validated_image;
    struct RibonDirectLoadPlan layout = {
        .segments = load_segments,
        .segment_capacity = RIBON_UEFI_SEGMENT_CAPACITY,
    };
    struct RibonMutableMemoryMap normalized = {
        .regions = normalized_regions,
        .capacity = RIBON_UEFI_REGION_CAPACITY,
    };
    struct RibonHandoffArtifact handoff = {0};
    struct UefiRefreshContext refresh = {
        .transaction = &transaction,
    };
    const struct RibonBootConfigEntry *selected_config = 0;
    struct RibonBootEnvironmentPersistentInputs persistent_inputs = {0};
    void *handoff_storage = handoff_buffer;
    uint64_t handoff_storage_capacity = sizeof(handoff_buffer);
    int direct_fdt_development = 0;
    uint32_t boot_module_count = 0u;
    uint64_t config_size = 0u;
    int status;

    if (!ribon_port_descriptor_is_valid(port) ||
        port->architecture != RIBON_UEFI_TARGET_ARCHITECTURE ||
        port->environment != RIBON_ENVIRONMENT_UEFI ||
        port->diagnostic_sink == 0) {
        return EFI_UNSUPPORTED;
    }
    diagnostic_sink = port->diagnostic_sink->operations;
    if (diagnostic_sink->initialize(diagnostic_sink->context) !=
        RIBON_SERVICE_STATUS_OK) {
        return EFI_DEVICE_ERROR;
    }
    uefi_marker("RIBON-R4-UEFI-ENTRY");

    status = ribon_uefi_app_initialize(
        &native,
        image_handle,
        system_table);
    if (status != RIBON_UEFI_APP_STATUS_OK) {
        return uefi_fail("environment-initialize");
    }
    if (ribon_uefi_app_read_file(
            &native,
            "/RIBON/BOOT.CFG",
            boot_config_bytes,
            sizeof(boot_config_bytes),
            &config_size) != RIBON_UEFI_APP_STATUS_OK ||
        ribon_boot_configuration_parse(
            boot_config_bytes,
            config_size,
            &boot_configuration) != RIBON_BOOT_CONFIG_STATUS_OK ||
        ribon_boot_configuration_select(
            &boot_configuration,
            &selected_config) != RIBON_BOOT_CONFIG_STATUS_OK) {
        return uefi_fail("esp-config");
    }
    protocol_plugin = ribon_plugin_registry_find(
        registry,
        RIBON_PLUGIN_KIND_BOOT_PROTOCOL,
        selected_config->protocol);
    image_plugin = ribon_plugin_registry_find(
        registry,
        RIBON_PLUGIN_KIND_IMAGE_FORMAT,
        selected_config->image_format);
    if (protocol_plugin == 0 || image_plugin == 0) {
        return uefi_fail("plugin-selection");
    }
    protocol = protocol_plugin->operations;
    image_format = image_plugin->operations;
    direct_fdt_development =
        uefi_text_equal(protocol->id, "luca-direct-fdt-dev");
    if (direct_fdt_development &&
        (selected_config->has_init_image == 0u ||
         selected_config->module_count != 0u ||
         selected_config->command_line[0] != '\0')) {
        return uefi_fail("development-profile");
    }
    if (direct_fdt_development &&
        ribon_uefi_app_allocate_boot_buffer_in_window(
            &native,
            RIBON_LUCA_DIRECT_FDT_RUNTIME_RAM_START,
            RIBON_LUCA_DIRECT_FDT_RUNTIME_RAM_END,
            handoff_storage_capacity,
            &handoff_storage) != RIBON_UEFI_APP_STATUS_OK) {
        return uefi_fail("development-handoff-allocation");
    }
    if (ribon_uefi_app_open_boot_source(
            &native,
            selected_config->kernel_path,
            &source) != RIBON_UEFI_APP_STATUS_OK ||
        source.size > sizeof(payload_bytes)) {
        return uefi_fail("esp-kernel-source");
    }
    uefi_marker("RIBON-R8-UEFI-CONFIG-OK");
    if (selected_config->has_init_image != 0u) {
        const int module_status = direct_fdt_development ?
            ribon_uefi_app_load_boot_module_in_window(
                &native,
                selected_config->init_image_path,
                RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE,
                RIBON_LUCA_DIRECT_FDT_RUNTIME_RAM_START,
                RIBON_LUCA_DIRECT_FDT_RUNTIME_RAM_END,
                &boot_modules[boot_module_count]) :
            ribon_uefi_app_load_boot_module(
                &native,
                selected_config->init_image_path,
                RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE,
                &boot_modules[boot_module_count]);
        if (module_status !=
            RIBON_UEFI_APP_STATUS_OK) {
            return uefi_fail("init-image-load");
        }
        ++boot_module_count;
    }
    for (uint32_t index = 0u; index < selected_config->module_count; ++index) {
        if (boot_module_count == RIBON_BOOT_CONFIG_MAX_MODULES ||
            ribon_uefi_app_load_boot_module(
                &native,
                selected_config->module_paths[index],
                RIBON_BOOT_MODULE_ROLE_AUXILIARY,
                &boot_modules[boot_module_count]) !=
                RIBON_UEFI_APP_STATUS_OK) {
            return uefi_fail("module-load");
        }
        ++boot_module_count;
    }
    if (boot_module_count != 0u) {
        uefi_marker("RIBON-R9-UEFI-MODULE-LOADED");
        uefi_marker_address(
            "RIBON-R24-UEFI-WORLD-BASE=",
            boot_modules[0].physical_address);
        uefi_marker_address(
            "RIBON-R24-UEFI-WORLD-SIZE=",
            boot_modules[0].size);
    }
    if (ribon_uefi_app_capture_environment(&native, &environment) !=
        RIBON_UEFI_APP_STATUS_OK) {
        return uefi_fail("environment-capture");
    }
    persistent_inputs = (struct RibonBootEnvironmentPersistentInputs){
        .boot_media = {
            .kind = source.kind,
            .path = selected_config->kernel_path,
            .size = source.size,
        },
        .boot_modules = {
            .modules = boot_modules,
            .module_count = boot_module_count,
        },
        .command_line = {
            .text = selected_config->command_line,
            .length = uefi_text_length(
                selected_config->command_line,
                RIBON_BOOT_CONFIG_COMMAND_LINE_CAPACITY),
        },
    };
    if (!ribon_boot_environment_apply_persistent_inputs(
            &environment,
            &persistent_inputs)) {
        return uefi_fail("persistent-inputs");
    }
    uefi_marker("RIBON-R4-UEFI-MEMORY-MAP");
    if (environment.device_tree.data != 0) {
        uefi_marker_address(
            "RIBON-R24-UEFI-DTB-BASE=",
            environment.device_tree.physical_address);
        uefi_marker_address(
            "RIBON-R24-UEFI-DTB-SIZE=",
            environment.device_tree.size);
    }
    if (direct_fdt_development) {
        uefi_marker_address(
            "RIBON-R24-UEFI-HANDOFF-BASE=",
            (uint64_t)(uintptr_t)handoff_storage);
        uefi_marker_address(
            "RIBON-R24-UEFI-HANDOFF-CAPACITY=",
            handoff_storage_capacity);
    }

    ribon_arena_init(&arena, arena_storage, sizeof(arena_storage));
    status = ribon_context_initialize(
        &core,
        product,
        registry,
        ribon_generated_service_directory(),
        ribon_mode_selected(),
        &arena);
    if (status != RIBON_CORE_STATUS_OK) {
        return uefi_fail("product-graph");
    }
    uefi_marker("RIBON-R4-UEFI-PRODUCT-GRAPH-OK");
    status = ribon_boot_transaction_initialize(
        &transaction,
        &core,
        arch,
        protocol,
        image_format);
    if (status != RIBON_BOOT_STATUS_OK) {
        return uefi_fail("transaction-initialize");
    }
    status = ribon_boot_transaction_prepare(
        &transaction, &(struct RibonBootTransactionInput){
            .environment = &environment,
            .normalized_memory_map = &normalized,
            .source = &source,
            .source_offset = 0u,
            .source_size = source.size,
            .payload_buffer = payload_bytes,
            .payload_buffer_capacity = sizeof(payload_bytes),
            .source_name = selected_config->kernel_path,
            .validated_image = &validated_image,
            .direct_load_plan = protocol->terminal_execution ==
                    RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY ? &layout : 0,
            .handoff_buffer = protocol->terminal_execution ==
                    RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY ? handoff_storage : 0,
            .handoff_buffer_capacity = protocol->terminal_execution ==
                    RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY ?
                        handoff_storage_capacity : 0u,
            .handoff_artifact = protocol->terminal_execution ==
                    RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY ? &handoff : 0,
        });
    if (status != RIBON_BOOT_STATUS_OK) {
        return uefi_transaction_fail(&transaction);
    }
    if (protocol->terminal_execution == RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY &&
        ribon_uefi_app_place_payload(&native, &transaction.payload, &layout) !=
            RIBON_UEFI_APP_STATUS_OK) {
        return uefi_fail("payload-place");
    }
    if (ribon_boot_transaction_commit_attempt(&transaction) != RIBON_BOOT_STATUS_OK) {
        return uefi_fail("attempt-commit");
    }
    if (protocol->terminal_execution ==
        RIBON_TERMINAL_EXECUTION_FIRMWARE_MANAGED_IMAGE) {
        uefi_marker("RIBON-R02-UEFI-MANAGED-IMAGE-VALIDATED");
        uefi_marker("RIBON-R02-UEFI-MANAGED-LAUNCH-ATTEMPT");
        (void)ribon_boot_transaction_execute_terminal(&transaction);
        return uefi_fail("managed-image-returned");
    }
    uefi_marker("RIBON-R4-PROTOCOL-HANDOFF-OK");
    uefi_marker("RIBON-R4-UEFI-PAYLOAD-LOADED");
    uefi_marker("RIBON-R8-UEFI-ESP-PAYLOAD-OK");
    status = ribon_uefi_app_exit_boot_services(
        &native,
        &environment,
        &persistent_inputs,
        uefi_refresh_plan,
        &refresh);
    if (status != RIBON_UEFI_APP_STATUS_OK) {
        return uefi_fail("exit-boot-services");
    }
    uefi_marker("RIBON-R4-UEFI-FINAL-HANDOFF-OK");
    uefi_marker("RIBON-R4-UEFI-EXIT-BOOT-SERVICES-OK");
    if (ribon_boot_transaction_quiesce_environment(&transaction) != RIBON_BOOT_STATUS_OK) {
        arch->halt();
    }
    if (arch->cache_sync(
            layout.runtime_load_base,
            layout.runtime_load_end - layout.runtime_load_base) !=
        RIBON_ARCH_OPERATION_OK) {
        arch->halt();
    }
    if (arch->cache_sync(
            (uint64_t)(uintptr_t)handoff.data,
            handoff.size) != RIBON_ARCH_OPERATION_OK) {
        arch->halt();
    }
    for (uint32_t index = 0u; index < boot_module_count; ++index) {
        if (arch->cache_sync(
                boot_modules[index].physical_address,
                boot_modules[index].size) != RIBON_ARCH_OPERATION_OK) {
            arch->halt();
        }
    }
    uefi_marker("RIBON-R4-UEFI-TRANSFER");
    ribon_boot_transaction_transfer(&transaction);
}
