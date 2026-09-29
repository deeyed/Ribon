#include "../../src/environments/uefi-app/uefi_app.h"

#include <Ribon/arch/entry.h>
#if defined(__aarch64__)
#include <Ribon/arch/aarch64/entry_state.h>
#endif
#include <Ribon/boot/transfer.h>
#include <Ribon/config/boot_config.h>
#include <Ribon/port/port.h>
#include <Ribon/protocols/os/luca/direct_fdt.h>

#define RIBON_UEFI_MEMORY_MAP_CAPACITY (128u * 1024u)
#define RIBON_UEFI_REGION_CAPACITY 512u
#define RIBON_UEFI_SEGMENT_CAPACITY 16u
#if defined(__aarch64__)
#define RIBON_UEFI_POST_EXIT_BRIDGE_BYTES RIBON_AARCH64_BRIDGE_TABLE_BYTES
#define RIBON_UEFI_POST_EXIT_STACK_BYTES (64u * 1024u)
#elif defined(__x86_64__)
#define RIBON_UEFI_DIRECT_HIGH_TABLE_BYTES (64u * 4096u)
#endif
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
#if defined(__aarch64__)
static _Alignas(4096) unsigned char
    post_exit_bridge_tables[RIBON_UEFI_POST_EXIT_BRIDGE_BYTES];
static _Alignas(16) unsigned char
    post_exit_stack[RIBON_UEFI_POST_EXIT_STACK_BYTES];
#elif defined(__x86_64__)
static _Alignas(4096) unsigned char
    direct_high_tables[RIBON_UEFI_DIRECT_HIGH_TABLE_BYTES];
#endif
static _Alignas(4096) unsigned char handoff_buffer[RIBON_UEFI_HANDOFF_CAPACITY];
static _Alignas(16) unsigned char arena_storage[RIBON_UEFI_ARENA_CAPACITY];
static unsigned char boot_config_bytes[RIBON_UEFI_CONFIG_CAPACITY];
static _Alignas(4096) unsigned char payload_bytes[RIBON_UEFI_PAYLOAD_CAPACITY];
static struct RibonBootConfiguration boot_configuration;
static struct RibonBootModule boot_modules[RIBON_BOOT_CONFIG_MAX_MODULES];
static const struct RibonDiagnosticSinkServiceOperations *diagnostic_sink;

struct UefiRefreshContext {
    struct RibonBootTransaction *transaction;
    struct RibonUefiAppContext *native;
    const struct RibonPayloadImage *payload;
    const struct RibonDirectLoadPlan *layout;
    int post_exit_payload;
};

#if defined(__aarch64__)
struct UefiPostExitBridgeContext {
    const struct RibonArchOps *arch;
    struct RibonAarch64EntryState entry_state;
    struct RibonAarch64BridgeRangeSet ranges;
    struct RibonAarch64PostExitTransition transition;
};
#endif

/* Direct-FDT post-exit execution may overwrite the firmware stack range. */
static struct RibonUefiAppContext native;
static struct RibonBootEnvironment environment;
static struct RibonArena arena;
static struct RibonCoreContext core;
static struct RibonBootTransaction transaction;
static struct RibonBootSource source;
static struct RibonValidatedImage validated_image;
static struct RibonDirectLoadPlan layout;
static struct RibonMutableMemoryMap normalized;
static struct RibonHandoffArtifact handoff;
static struct UefiRefreshContext refresh;
#if defined(__aarch64__)
static struct UefiPostExitBridgeContext post_exit_bridge;
#endif
static struct RibonBootEnvironmentPersistentInputs persistent_inputs;
static uint32_t boot_module_count;

#if defined(__aarch64__)
/** @brief ExitBootServices 성공 뒤에만 bounded bridge와 EL1 stack으로 전환한다. */
static int uefi_activate_post_exit_bridge(void *context) {
    struct UefiPostExitBridgeContext *identity =
        (struct UefiPostExitBridgeContext *)context;
    if (identity != &post_exit_bridge || identity->arch == 0) {
        return -1;
    }
    ribon_aarch64_post_exit_transition(&identity->transition);
    return -1;
}
#endif

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
    if (refresh == 0 || refresh->native == 0 || refresh->payload == 0 ||
        refresh->layout == 0) {
        return -1;
    }
    if (ribon_boot_transaction_refresh_after_commit(
            refresh->transaction,
            environment) != RIBON_BOOT_STATUS_OK) {
        return -1;
    }
    if (!refresh->post_exit_payload) {
        return 0;
    }
    return ribon_uefi_app_validate_post_exit_payload(
               refresh->native,
               refresh->payload,
               refresh->layout) == RIBON_UEFI_APP_STATUS_OK ? 0 : -1;
}

#if defined(__aarch64__)
/** @brief Bounded bridge의 EL1 stack에서 post-exit copy와 LUCA transfer를 끝낸다. */
static void uefi_direct_fdt_exit_and_transfer(void *context) {
    const struct RibonArchOps *arch = transaction.arch;
    if (context != &post_exit_bridge || arch == 0) {
        if (arch != 0) {
            arch->halt();
        }
        for (;;) {
        }
    }
    uefi_marker("RIBON-R4-UEFI-FINAL-HANDOFF-OK");
    uefi_marker("RIBON-R4-UEFI-EXIT-BOOT-SERVICES-OK");
    uefi_marker_address(
        "RIBON-R12-AARCH64-EL1-BRIDGE-SOURCE-EL=",
        post_exit_bridge.entry_state.current_el);
    if (ribon_boot_transaction_quiesce_environment(&transaction) !=
        RIBON_BOOT_STATUS_OK) {
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
    if (arch->cache_sync(
            (uint64_t)(uintptr_t)transaction.payload.data,
            transaction.payload.size) != RIBON_ARCH_OPERATION_OK) {
        arch->halt();
    }
    uefi_marker("RIBON-R4-UEFI-TRANSFER");
    if ((refresh.post_exit_payload &&
         ribon_uefi_app_place_payload_after_exit(
             &native,
             &transaction.payload,
             &layout) != RIBON_UEFI_APP_STATUS_OK) ||
        layout.runtime_load_end <= layout.runtime_load_base ||
        arch->cache_sync(
            layout.runtime_load_base,
            layout.runtime_load_end - layout.runtime_load_base) !=
            RIBON_ARCH_OPERATION_OK) {
        arch->halt();
    }
    ribon_boot_transaction_transfer(&transaction);
}

/** @brief Post-exit에 실제 접근할 normal-memory span을 bridge set에 추가한다. */
static int uefi_bridge_add_normal(uint64_t base, uint64_t size) {
    return ribon_aarch64_bridge_range_add(
               &post_exit_bridge.ranges,
               base,
               size,
               RIBON_AARCH64_BRIDGE_MEMORY_NORMAL) ==
           RIBON_ARCH_OPERATION_OK;
}

/** @brief Firmware active translation에서 pointer가 physical identity인지 검사한다. */
static int uefi_bridge_pointer_is_identity(uint64_t base, uint64_t size) {
    return size != 0u && base <= UINT64_MAX - size &&
           ribon_aarch64_entry_address_is_identity(
               &post_exit_bridge.entry_state, base) &&
           ribon_aarch64_entry_address_is_identity(
               &post_exit_bridge.entry_state, base + size - 1u);
}

/** @brief Final handoff consumer에 필요한 range만 갖는 EL1 bridge를 준비한다. */
static int uefi_prepare_post_exit_bridge(
    const struct RibonPortDescriptor *port,
    void *handoff_storage,
    uint64_t handoff_storage_capacity) {
    const uint64_t image_base = (uint64_t)(uintptr_t)native.image_base;
    const uint64_t table_base =
        (uint64_t)(uintptr_t)post_exit_bridge_tables;
    const uint64_t stack_base = (uint64_t)(uintptr_t)post_exit_stack;
    const uint64_t code =
        (uint64_t)(uintptr_t)&uefi_direct_fdt_exit_and_transfer;
    struct RibonAarch64BridgeRequest request;
    post_exit_bridge.ranges = (struct RibonAarch64BridgeRangeSet){0};
    if (port == 0 || native.image_base == 0 || native.image_size == 0u ||
        handoff_storage == 0 || handoff_storage_capacity == 0u ||
        ribon_aarch64_entry_state_capture(
            &post_exit_bridge.entry_state) != RIBON_ARCH_OPERATION_OK ||
        image_base > UINT64_MAX - native.image_size ||
        code < image_base || code >= image_base + native.image_size ||
        table_base < image_base ||
        table_base + sizeof(post_exit_bridge_tables) < table_base ||
        table_base + sizeof(post_exit_bridge_tables) >
            image_base + native.image_size ||
        stack_base < image_base ||
        stack_base + sizeof(post_exit_stack) < stack_base ||
        stack_base + sizeof(post_exit_stack) > image_base + native.image_size ||
        !uefi_bridge_pointer_is_identity(image_base, native.image_size) ||
        !uefi_bridge_pointer_is_identity(
            (uint64_t)(uintptr_t)handoff_storage,
            handoff_storage_capacity) ||
        !uefi_bridge_pointer_is_identity(
            (uint64_t)(uintptr_t)transaction.payload.data,
            transaction.payload.size) ||
        !uefi_bridge_add_normal(image_base, native.image_size) ||
        !uefi_bridge_add_normal(
            (uint64_t)(uintptr_t)handoff_storage,
            handoff_storage_capacity) ||
        !uefi_bridge_add_normal(
            (uint64_t)(uintptr_t)transaction.payload.data,
            transaction.payload.size)) {
        return 0;
    }
    if (environment.device_tree.data != 0 &&
        (!uefi_bridge_pointer_is_identity(
             (uint64_t)(uintptr_t)environment.device_tree.data,
             environment.device_tree.size) ||
         !uefi_bridge_add_normal(
             (uint64_t)(uintptr_t)environment.device_tree.data,
             environment.device_tree.size))) {
        return 0;
    }
    for (uint32_t index = 0u; index < layout.segment_count; ++index) {
        if (!uefi_bridge_add_normal(
                layout.segments[index].load_address,
                layout.segments[index].memory_size)) {
            return 0;
        }
    }
    for (uint32_t index = 0u; index < boot_module_count; ++index) {
        if (!uefi_bridge_pointer_is_identity(
                boot_modules[index].physical_address,
                boot_modules[index].size) ||
            !uefi_bridge_add_normal(
                boot_modules[index].physical_address,
                boot_modules[index].size)) {
            return 0;
        }
    }
    if (port->post_exit_mmio_size != 0u &&
        ribon_aarch64_bridge_range_add(
            &post_exit_bridge.ranges,
            port->post_exit_mmio_base,
            port->post_exit_mmio_size,
            RIBON_AARCH64_BRIDGE_MEMORY_DEVICE) !=
            RIBON_ARCH_OPERATION_OK) {
        return 0;
    }
    request = (struct RibonAarch64BridgeRequest){
        .size = sizeof(request),
        .abi_version = RIBON_AARCH64_ENTRY_STATE_ABI_VERSION,
        .ranges = post_exit_bridge.ranges.ranges,
        .range_count = post_exit_bridge.ranges.count,
        .table_buffer = post_exit_bridge_tables,
        .table_buffer_size = sizeof(post_exit_bridge_tables),
    };
    return ribon_aarch64_prepare_post_exit_bridge(
               &request,
               &post_exit_bridge.entry_state,
               (uint64_t)(uintptr_t)(post_exit_stack + sizeof(post_exit_stack)),
               uefi_direct_fdt_exit_and_transfer,
               &post_exit_bridge,
               &post_exit_bridge.transition) == RIBON_ARCH_OPERATION_OK;
}
#endif

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
    native = (struct RibonUefiAppContext){
        .raw_memory_map = raw_memory_map,
        .raw_memory_map_capacity = sizeof(raw_memory_map),
        .regions = environment_regions,
        .region_capacity = RIBON_UEFI_REGION_CAPACITY,
    };
    environment = (struct RibonBootEnvironment){0};
    arena = (struct RibonArena){0};
    core = (struct RibonCoreContext){0};
    transaction = (struct RibonBootTransaction){0};
    source = (struct RibonBootSource){0};
    validated_image = (struct RibonValidatedImage){0};
    layout = (struct RibonDirectLoadPlan){
        .segments = load_segments,
        .segment_capacity = RIBON_UEFI_SEGMENT_CAPACITY,
    };
    normalized = (struct RibonMutableMemoryMap){
        .regions = normalized_regions,
        .capacity = RIBON_UEFI_REGION_CAPACITY,
    };
    handoff = (struct RibonHandoffArtifact){0};
    refresh = (struct UefiRefreshContext){
        .transaction = &transaction,
        .native = &native,
        .payload = &transaction.payload,
        .layout = &layout,
    };
#if defined(__aarch64__)
    post_exit_bridge = (struct UefiPostExitBridgeContext){
        .arch = arch,
    };
#endif
    const struct RibonBootConfigEntry *selected_config = 0;
    void *handoff_storage = handoff_buffer;
    uint64_t handoff_storage_capacity = sizeof(handoff_buffer);
    int direct_fdt_development = 0;
    uint64_t config_size = 0u;
    int status;

    persistent_inputs = (struct RibonBootEnvironmentPersistentInputs){0};
    boot_module_count = 0u;

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
#if defined(__x86_64__)
            .transition_buffer = protocol->terminal_execution ==
                    RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY ?
                        direct_high_tables : 0,
            .transition_buffer_physical_address =
                protocol->terminal_execution ==
                    RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY ?
                        (uint64_t)(uintptr_t)direct_high_tables : 0u,
            .transition_buffer_capacity = protocol->terminal_execution ==
                    RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY ?
                        sizeof(direct_high_tables) : 0u,
#endif
        });
    if (status != RIBON_BOOT_STATUS_OK) {
        return uefi_transaction_fail(&transaction);
    }
    refresh.post_exit_payload = direct_fdt_development;
    if (!direct_fdt_development &&
        protocol->terminal_execution == RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY &&
        ribon_uefi_app_place_payload(&native, &transaction.payload, &layout) !=
            RIBON_UEFI_APP_STATUS_OK) {
        return uefi_fail("payload-place");
    }
    if (!direct_fdt_development &&
        protocol->terminal_execution == RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY) {
        uefi_marker("RIBON-R4-UEFI-PAYLOAD-LOADED");
        uefi_marker("RIBON-R8-UEFI-ESP-PAYLOAD-OK");
    }
#if defined(__aarch64__)
    if (protocol->terminal_execution == RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY &&
        (!uefi_prepare_post_exit_bridge(
             port, handoff_storage, handoff_storage_capacity) ||
         arch->cache_sync(
             (uint64_t)(uintptr_t)post_exit_bridge_tables,
             sizeof(post_exit_bridge_tables)) != RIBON_ARCH_OPERATION_OK)) {
        return uefi_fail("post-exit-bridge-prepare");
    }
#endif
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
#if defined(__aarch64__)
    if (protocol->terminal_execution == RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY) {
        uefi_marker("RIBON-R4-PROTOCOL-HANDOFF-OK");
        uefi_marker_address(
            "RIBON-R12-AARCH64-FIRMWARE-SOURCE-EL=",
            post_exit_bridge.entry_state.current_el);
        status = ribon_uefi_app_exit_boot_services(
            &native,
            &environment,
            &persistent_inputs,
            uefi_refresh_plan,
            &refresh,
            uefi_activate_post_exit_bridge,
            &post_exit_bridge);
        if (status != RIBON_UEFI_APP_STATUS_OK) {
            return uefi_fail("exit-boot-services");
        }
        arch->halt();
    }
#endif
    uefi_marker("RIBON-R4-PROTOCOL-HANDOFF-OK");
    status = ribon_uefi_app_exit_boot_services(
        &native,
        &environment,
        &persistent_inputs,
        uefi_refresh_plan,
        &refresh,
        0,
        0);
    if (status != RIBON_UEFI_APP_STATUS_OK) {
        return uefi_fail("exit-boot-services");
    }
    uefi_marker("RIBON-R4-UEFI-FINAL-HANDOFF-OK");
    uefi_marker("RIBON-R4-UEFI-EXIT-BOOT-SERVICES-OK");
    if (ribon_boot_transaction_quiesce_environment(&transaction) != RIBON_BOOT_STATUS_OK) {
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
    if (arch->cache_sync(
            layout.runtime_load_base,
            layout.runtime_load_end - layout.runtime_load_base) !=
        RIBON_ARCH_OPERATION_OK) {
        arch->halt();
    }
    uefi_marker("RIBON-R4-UEFI-TRANSFER");
    ribon_boot_transaction_transfer(&transaction);
}
