#include "../../src/environments/uefi-app/uefi_app.h"

#include <Ribon/arch/entry.h>
#include <Ribon/boot/transfer.h>
#include <Ribon/config/boot_config.h>
#include <Ribon/port/port.h>

#define RIBON_UEFI_MEMORY_MAP_CAPACITY (128u * 1024u)
#define RIBON_UEFI_REGION_CAPACITY 512u
#define RIBON_UEFI_SEGMENT_CAPACITY 16u
#define RIBON_UEFI_HANDOFF_CAPACITY 65536u
#define RIBON_UEFI_ARENA_CAPACITY (256u * 1024u)
#define RIBON_UEFI_CONFIG_CAPACITY 4096u
#define RIBON_UEFI_PAYLOAD_CAPACITY (2u * 1024u * 1024u)

static _Alignas(16) unsigned char raw_memory_map[RIBON_UEFI_MEMORY_MAP_CAPACITY];
static struct RibonMemoryRegion environment_regions[RIBON_UEFI_REGION_CAPACITY];
static struct RibonMemoryRegion normalized_regions[RIBON_UEFI_REGION_CAPACITY];
static struct RibonLoadSegment load_segments[RIBON_UEFI_SEGMENT_CAPACITY];
static _Alignas(4096) unsigned char handoff_buffer[RIBON_UEFI_HANDOFF_CAPACITY];
static _Alignas(16) unsigned char arena_storage[RIBON_UEFI_ARENA_CAPACITY];
static unsigned char boot_config_bytes[RIBON_UEFI_CONFIG_CAPACITY];
static _Alignas(4096) unsigned char payload_bytes[RIBON_UEFI_PAYLOAD_CAPACITY];
static struct RibonBootConfiguration boot_configuration;
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

/** @brief UEFI first divergence를 serial에 남긴다. */
static EFI_STATUS uefi_fail(const char *stage) {
    uefi_marker("RIBON-R4-UEFI-FAIL");
    uefi_marker(stage);
    return EFI_LOAD_ERROR;
}

/** @brief Bounded configuration command line의 NUL 제외 byte 수를 계산한다. */
static uint32_t uefi_text_length(const char *text, uint32_t capacity) {
    uint32_t length = 0u;
    while (length < capacity && text[length] != '\0') {
        ++length;
    }
    return length;
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
    struct RibonLoadedPayload layout = {
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
    uint64_t config_size = 0u;
    int status;

    if (!ribon_port_descriptor_is_valid(port) ||
        port->architecture != RIBON_ARCHITECTURE_X86_64 ||
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
            &selected_config) != RIBON_BOOT_CONFIG_STATUS_OK ||
        selected_config->module_count != 0u ||
        ribon_uefi_app_open_boot_source(
            &native,
            selected_config->kernel_path,
            &source) != RIBON_UEFI_APP_STATUS_OK ||
        source.size > sizeof(payload_bytes)) {
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
    uefi_marker("RIBON-R8-UEFI-CONFIG-OK");
    if (ribon_uefi_app_capture_environment(&native, &environment) !=
        RIBON_UEFI_APP_STATUS_OK) {
        return uefi_fail("environment-capture");
    }
    environment.boot_media = (struct RibonBootMedia){
        .kind = source.kind,
        .path = selected_config->kernel_path,
        .size = source.size,
    };
    environment.command_line = (struct RibonCommandLine){
        .text = selected_config->command_line,
        .length = uefi_text_length(
            selected_config->command_line,
            RIBON_BOOT_CONFIG_COMMAND_LINE_CAPACITY),
    };
    environment.flags |= RIBON_BOOT_ENV_HAS_BOOT_MEDIA | RIBON_BOOT_ENV_HAS_COMMAND_LINE;
    uefi_marker("RIBON-R4-UEFI-MEMORY-MAP");

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
    if (status != RIBON_BOOT_STATUS_OK ||
        ribon_boot_transaction_prepare(&transaction, &(struct RibonBootTransactionInput){
            .environment = &environment,
            .normalized_memory_map = &normalized,
            .source = &source,
            .source_offset = 0u,
            .source_size = source.size,
            .payload_buffer = payload_bytes,
            .payload_buffer_capacity = sizeof(payload_bytes),
            .source_name = selected_config->kernel_path,
            .kernel_layout = &layout,
            .handoff_buffer = handoff_buffer,
            .handoff_buffer_capacity = sizeof(handoff_buffer),
            .handoff_artifact = &handoff,
        }) != RIBON_BOOT_STATUS_OK ||
        ribon_uefi_app_place_payload(&native, &transaction.payload, &layout) !=
            RIBON_UEFI_APP_STATUS_OK ||
        ribon_boot_transaction_commit_attempt(&transaction) != RIBON_BOOT_STATUS_OK) {
        return uefi_fail("boot-prepare");
    }
    uefi_marker("RIBON-R4-PROTOCOL-HANDOFF-OK");
    uefi_marker("RIBON-R4-UEFI-PAYLOAD-LOADED");
    uefi_marker("RIBON-R8-UEFI-ESP-PAYLOAD-OK");
    status = ribon_uefi_app_exit_boot_services(
        &native,
        &environment,
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
    uefi_marker("RIBON-R4-UEFI-TRANSFER");
    ribon_boot_transaction_transfer(&transaction);
}
