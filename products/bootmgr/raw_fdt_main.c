#include "../../src/common/drivers/serial/pl011.h"
#include "../../src/environments/raw-fdt/raw_fdt.h"

#include <Ribon/arch/entry.h>
#include <Ribon/boot/transfer.h>
#include <Ribon/platform/facts.h>
#include <Ribon/protocols/parus/rph1.h>

#include <string.h>

#define RIBON_BOOTMGR_MAX_MEMORY_REGIONS 16u
#define RIBON_BOOTMGR_MAX_LOAD_SEGMENTS 16u
#define RIBON_BOOTMGR_HANDOFF_CAPACITY 65536u
#define RIBON_BOOTMGR_ARENA_CAPACITY (256u * 1024u)

extern const unsigned char ribon_embedded_payload[];
extern const uint64_t ribon_embedded_payload_size;
extern unsigned char __image_start[];
extern unsigned char __image_end[];

static struct RibonPl011 diagnostic_uart;
static struct RibonMemoryRegion environment_regions[RIBON_BOOTMGR_MAX_MEMORY_REGIONS];
static struct RibonMemoryRegion normalized_regions[RIBON_BOOTMGR_MAX_MEMORY_REGIONS];
static struct RibonLoadSegment load_segments[RIBON_BOOTMGR_MAX_LOAD_SEGMENTS];
static _Alignas(4096) unsigned char handoff_buffer[RIBON_BOOTMGR_HANDOFF_CAPACITY];
static _Alignas(16) unsigned char arena_storage[RIBON_BOOTMGR_ARENA_CAPACITY];

/** @brief Early serial에 stable marker를 기록한다. */
static void bootmgr_marker(const char *marker) {
    (void)ribon_pl011_write(&diagnostic_uart, marker);
    (void)ribon_pl011_write(&diagnostic_uart, "\r\n");
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
    (void)ribon_pl011_write(&diagnostic_uart, prefix);
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

/** @brief Analyzed segment를 target payload window 안에 복사한다. */
static int bootmgr_place_payload(
    const struct RibonPlatformFacts *platform,
    const struct RibonPayloadImage *payload,
    struct RibonLoadedPayload *layout) {
    const uint64_t window_end =
        platform->payload_load_base + platform->payload_load_size;
    for (uint32_t index = 0u; index < layout->segment_count; ++index) {
        struct RibonLoadSegment *segment = &layout->segments[index];
        uint64_t destination_end;
        if (segment->load_address < platform->payload_load_base ||
            segment->load_address > UINT64_MAX - segment->memory_size ||
            (destination_end = segment->load_address + segment->memory_size) >
                window_end ||
            segment->file_offset > payload->size ||
            segment->file_size > payload->size - segment->file_offset ||
            segment->file_size > segment->memory_size) {
            return RIBON_BOOT_STATUS_INVALID_PAYLOAD;
        }
        memset((void *)(uintptr_t)segment->load_address, 0, (size_t)segment->memory_size);
        memcpy(
            (void *)(uintptr_t)segment->load_address,
            (const unsigned char *)payload->data + segment->file_offset,
            (size_t)segment->file_size);
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
 * @brief raw-FDT target의 native entry를 generic product graph와 Parus protocol에 연결한다.
 *
 * 이 함수는 allocation과 interrupt를 사용하지 않으며 성공 시 payload로 terminal transfer한다.
 */
_Noreturn void ribon_raw_fdt_boot_main(uint64_t fdt_address) {
    const struct RibonPlatformFacts *platform = ribon_platform_selected();
    const struct RibonArchOps *arch = ribon_arch_selected_ops();
    const struct RibonPluginRegistry *registry;
    const struct RibonProductDescriptor *product;
    const struct RibonBootProtocol *protocol =
        (const struct RibonBootProtocol *)
            ribon_parus_protocol_plugin_descriptor.operations;
    const struct RibonImageFormatOps *image_format =
        (const struct RibonImageFormatOps *)
            ribon_elf64_image_plugin_descriptor.operations;
    struct RibonRawFdtReservation reservations[2];
    struct RibonRawFdtEntry native_entry;
    struct RibonBootEnvironment environment;
    struct RibonArena arena;
    struct RibonCoreContext core;
    struct RibonBootSession session;
    struct RibonPayloadImage payload;
    struct RibonLoadedPayload layout;
    struct RibonMutableMemoryMap normalized;
    struct RibonHandoffArtifact handoff;
    struct RibonBootRequest request;
    struct RibonBootPlan plan;
    int status;

    if (!ribon_platform_facts_are_valid(platform) ||
        platform->architecture != RIBON_ARCHITECTURE_AARCH64 ||
        platform->environment != RIBON_ENVIRONMENT_RAW_FDT ||
        ribon_pl011_initialize(
            &diagnostic_uart,
            platform->diagnostic_uart_base,
            platform->diagnostic_poll_limit) != 0) {
        for (;;) {
            __asm__ __volatile__("wfe");
        }
    }
    bootmgr_marker("RIBON-R4-RAW-FDT-ENTRY");
    bootmgr_marker(platform->id);

    reservations[0] = (struct RibonRawFdtReservation){
        .base = (uint64_t)(uintptr_t)__image_start,
        .size = (uint64_t)(__image_end - __image_start),
        .kind = RIBON_MEMORY_REGION_BOOTLOADER,
    };
    reservations[1] = (struct RibonRawFdtReservation){
        .base = platform->payload_load_base,
        .size = platform->payload_load_size,
        .kind = RIBON_MEMORY_REGION_KERNEL_IMAGE,
    };
    native_entry = (struct RibonRawFdtEntry){
        .fdt = (const void *)(uintptr_t)fdt_address,
        .fdt_capacity = platform->native_input_capacity,
        .architecture = RIBON_ARCHITECTURE_AARCH64,
        .arch_ops = arch,
        .timer_frequency_hz = platform->timer_frequency_hz,
        .payload = ribon_embedded_payload,
        .payload_size = ribon_embedded_payload_size,
        .payload_name = "boot/payload.elf",
        .reservations = reservations,
        .reservation_count = 2u,
        .memory_regions = environment_regions,
        .memory_region_capacity = RIBON_BOOTMGR_MAX_MEMORY_REGIONS,
    };
    status = ribon_raw_fdt_environment_capture(&native_entry, &environment);
    if (status != RIBON_RAW_FDT_STATUS_OK) {
        bootmgr_fail("environment-capture", status);
    }
    bootmgr_marker("RIBON-R4-FDT-ACCEPTED");

    registry = ribon_generated_plugin_registry();
    product = ribon_generated_product_descriptor();
    ribon_arena_init(&arena, arena_storage, sizeof(arena_storage));
    status = ribon_context_initialize(
        &core,
        product,
        registry,
        ribon_mode_selected(),
        &arena);
    if (status != RIBON_CORE_STATUS_OK) {
        bootmgr_fail("product-graph", status);
    }
    bootmgr_marker("RIBON-R4-PRODUCT-GRAPH-OK");
    status = ribon_boot_session_initialize(
        &session,
        &core,
        ribon_raw_fdt_services(),
        arch,
        protocol,
        image_format);
    if (status != RIBON_BOOT_STATUS_OK) {
        bootmgr_fail("session", status);
    }

    payload = (struct RibonPayloadImage){
        .data = ribon_embedded_payload,
        .size = ribon_embedded_payload_size,
        .source_name = "boot/payload.elf",
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
    request = (struct RibonBootRequest){
        .environment = &environment,
        .normalized_memory_map = &normalized,
        .kernel_payload = &payload,
        .kernel_layout = &layout,
        .handoff_buffer = handoff_buffer,
        .handoff_buffer_capacity = sizeof(handoff_buffer),
        .handoff_artifact = &handoff,
    };
    status = ribon_boot_prepare(&session, &request, &plan);
    if (status != RIBON_BOOT_STATUS_OK ||
        ribon_parus_parse_rph1(
            handoff.data,
            handoff.size,
            &(struct RibonParusRph1View){0}) != RIBON_PARUS_RPH1_PARSE_OK) {
        bootmgr_fail("protocol-handoff", status);
    }
    bootmgr_marker("RIBON-R4-PARUS-RPH1-OK");
    status = bootmgr_place_payload(platform, &payload, &layout);
    if (status != RIBON_BOOT_STATUS_OK) {
        bootmgr_fail("payload-place", status);
    }
    bootmgr_marker("RIBON-R4-PAYLOAD-LOADED");
    if (ribon_boot_commit(&session) != RIBON_BOOT_STATUS_OK ||
        ribon_environment_quiesce(&session) != RIBON_BOOT_STATUS_OK) {
        bootmgr_fail("quiesce", RIBON_BOOT_STATUS_BAD_STATE);
    }
    bootmgr_marker("RIBON-R4-RAW-FDT-TRANSFER");
    ribon_boot_transfer(
        &session,
        &plan,
        (uint64_t)(uintptr_t)handoff.data,
        RIBON_KERNEL_ENTRY_FLAG_RPH1,
        0u);
}
