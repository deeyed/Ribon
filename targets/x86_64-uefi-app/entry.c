#include "../../src/environments/uefi-app/uefi_app.h"

#include <Ribon/arch/entry.h>
#include <Ribon/boot/transfer.h>
#include <Ribon/config/boot_config.h>
#include <Ribon/platform/facts.h>
#include <Ribon/protocols/parus/rph1.h>

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

struct UefiRefreshContext {
    struct RibonBootTransaction *transaction;
    struct RibonHandoffArtifact *handoff;
};

/** @brief x86 port I/O로 한 byte를 기록한다. */
static void uefi_out8(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

/** @brief x86 port I/O로 한 byte를 읽는다. */
static uint8_t uefi_in8(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/** @brief PC serial resource를 polling diagnostic sink로 초기화한다. */
static void uefi_serial_initialize(uint16_t base) {
    uefi_out8((uint16_t)(base + 1u), 0x00u);
    uefi_out8((uint16_t)(base + 3u), 0x80u);
    uefi_out8((uint16_t)(base + 0u), 0x01u);
    uefi_out8((uint16_t)(base + 1u), 0x00u);
    uefi_out8((uint16_t)(base + 3u), 0x03u);
    uefi_out8((uint16_t)(base + 2u), 0xc7u);
    uefi_out8((uint16_t)(base + 4u), 0x0bu);
}

/** @brief COM transmitter ready를 bounded polling하고 한 byte를 기록한다. */
static void uefi_serial_byte(uint16_t base, uint8_t value) {
    for (uint32_t poll = 0u; poll < 1000000u; ++poll) {
        if ((uefi_in8((uint16_t)(base + 5u)) & 0x20u) != 0u) {
            uefi_out8(base, value);
            return;
        }
    }
}

/** @brief UEFI service lifetime과 무관한 stable serial marker를 기록한다. */
static void uefi_marker(uint16_t base, const char *text) {
    while (text != 0 && *text != '\0') {
        uefi_serial_byte(base, (uint8_t)*text);
        ++text;
    }
    uefi_serial_byte(base, '\r');
    uefi_serial_byte(base, '\n');
}

/** @brief UEFI first divergence를 serial에 남긴다. */
static EFI_STATUS uefi_fail(uint16_t serial_base, const char *stage) {
    uefi_marker(serial_base, "RIBON-R4-UEFI-FAIL");
    uefi_marker(serial_base, stage);
    return EFI_LOAD_ERROR;
}

/** @brief NUL-terminated stable identifier 두 개가 정확히 같은지 검사한다. */
static int uefi_text_equals(const char *left, const char *right) {
    uint32_t index = 0u;
    if (left == 0 || right == 0) {
        return 0;
    }
    while (left[index] == right[index]) {
        if (left[index] == '\0') {
            return 1;
        }
        ++index;
    }
    return 0;
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
    struct RibonParusRph1View view;
    int status;
    if (refresh == 0) {
        return -1;
    }
    status = ribon_boot_transaction_refresh_after_commit(
        refresh->transaction,
        environment);
    if (status != RIBON_BOOT_STATUS_OK ||
        ribon_parus_parse_rph1(
            refresh->handoff->data,
            refresh->handoff->size,
            &view) != RIBON_PARUS_RPH1_PARSE_OK) {
        return -1;
    }
    return 0;
}

/**
 * @brief UEFI application entry에서 consumer transaction과 selected protocol을 실행한다.
 *
 * Final map을 handoff에 반영한 뒤 ExitBootServices를 성공해야만 payload로 전환한다.
 */
EFI_STATUS EFIAPI efi_main(
    EFI_HANDLE image_handle,
    EFI_SYSTEM_TABLE *system_table) {
    const struct RibonPlatformFacts *platform = ribon_platform_selected();
    const struct RibonArchOps *arch = ribon_arch_selected_ops();
    const struct RibonBootProtocol *protocol =
        (const struct RibonBootProtocol *)
            ribon_parus_protocol_plugin_descriptor.operations;
    const struct RibonImageFormatOps *image_format =
        (const struct RibonImageFormatOps *)
            ribon_elf64_image_plugin_descriptor.operations;
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
        .handoff = &handoff,
    };
    const struct RibonBootConfigEntry *selected_config = 0;
    uint64_t config_size = 0u;
    uint16_t serial_base;
    int status;

    if (!ribon_platform_facts_are_valid(platform) ||
        platform->architecture != RIBON_ARCHITECTURE_X86_64 ||
        platform->environment != RIBON_ENVIRONMENT_UEFI) {
        return EFI_UNSUPPORTED;
    }
    serial_base = (uint16_t)platform->diagnostic_uart_base;
    uefi_serial_initialize(serial_base);
    uefi_marker(serial_base, "RIBON-R4-UEFI-ENTRY");

    status = ribon_uefi_app_initialize(
        &native,
        image_handle,
        system_table);
    if (status != RIBON_UEFI_APP_STATUS_OK) {
        return uefi_fail(serial_base, "environment-initialize");
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
        !uefi_text_equals(selected_config->protocol, "parus") ||
        !uefi_text_equals(selected_config->image_format, "elf64") ||
        selected_config->module_count != 0u ||
        ribon_uefi_app_open_boot_source(
            &native,
            selected_config->kernel_path,
            &source) != RIBON_UEFI_APP_STATUS_OK ||
        source.size > sizeof(payload_bytes)) {
        return uefi_fail(serial_base, "esp-config");
    }
    uefi_marker(serial_base, "RIBON-R8-UEFI-CONFIG-OK");
    if (ribon_uefi_app_capture_environment(&native, &environment) !=
        RIBON_UEFI_APP_STATUS_OK) {
        return uefi_fail(serial_base, "environment-capture");
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
    uefi_marker(serial_base, "RIBON-R4-UEFI-MEMORY-MAP");

    ribon_arena_init(&arena, arena_storage, sizeof(arena_storage));
    status = ribon_context_initialize(
        &core,
        ribon_generated_product_descriptor(),
        ribon_generated_plugin_registry(),
        ribon_generated_service_directory(),
        ribon_mode_selected(),
        &arena);
    if (status != RIBON_CORE_STATUS_OK) {
        return uefi_fail(serial_base, "product-graph");
    }
    uefi_marker(serial_base, "RIBON-R4-UEFI-PRODUCT-GRAPH-OK");
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
        return uefi_fail(serial_base, "boot-prepare");
    }
    uefi_marker(serial_base, "RIBON-R4-UEFI-PAYLOAD-LOADED");
    uefi_marker(serial_base, "RIBON-R8-UEFI-ESP-PAYLOAD-OK");
    status = ribon_uefi_app_exit_boot_services(
        &native,
        &environment,
        uefi_refresh_plan,
        &refresh);
    if (status != RIBON_UEFI_APP_STATUS_OK) {
        return uefi_fail(serial_base, "exit-boot-services");
    }
    uefi_marker(serial_base, "RIBON-R4-UEFI-FINAL-MAP-RPH1-OK");
    uefi_marker(serial_base, "RIBON-R4-UEFI-EXIT-BOOT-SERVICES-OK");
    if (ribon_boot_transaction_quiesce_environment(&transaction) != RIBON_BOOT_STATUS_OK) {
        arch->halt();
    }
    if (arch->cache_sync(
            layout.runtime_load_base,
            layout.runtime_load_end - layout.runtime_load_base) !=
        RIBON_ARCH_OPERATION_OK) {
        arch->halt();
    }
    uefi_marker(serial_base, "RIBON-R4-UEFI-TRANSFER");
    ribon_boot_transaction_transfer(
        &transaction,
        (uint64_t)(uintptr_t)handoff.data,
        RIBON_KERNEL_ENTRY_FLAG_RPH1,
        0u);
}
