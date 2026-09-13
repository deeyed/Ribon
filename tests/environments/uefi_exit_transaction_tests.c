#include "../../src/environments/uefi-app/uefi_app.h"

#include <stdio.h>
#include <string.h>

static UINTN map_generation;
static UINTN get_map_calls;
static UINTN exit_calls;
static UINTN refresh_calls;
static UINTN expected_image;
static int always_change_key;
static UINTN post_exit_calls;
static int post_exit_saw_closed_services;

static _Alignas(4096) unsigned char payload_target[3u * 4096u];
static unsigned char payload_source[256u];

static EFI_STATUS EFIAPI fake_get_memory_map(
    UINTN *size,
    EFI_MEMORY_DESCRIPTOR *map,
    UINTN *key,
    UINTN *descriptor_size,
    UINT32 *descriptor_version) {
    if (size == NULL || map == NULL || key == NULL ||
        descriptor_size == NULL || descriptor_version == NULL ||
        *size < sizeof(*map)) {
        return EFI_INVALID_PARAMETER;
    }
    memset(map, 0, sizeof(*map));
    map->Type = EfiConventionalMemory;
    map->PhysicalStart = UINT64_C(0x40000000);
    map->NumberOfPages = 16u;
    map->Attribute = EFI_MEMORY_WB;
    *size = sizeof(*map);
    *key = (UINTN)0x100u + map_generation;
    *descriptor_size = sizeof(*map);
    *descriptor_version = EFI_MEMORY_DESCRIPTOR_VERSION;
    get_map_calls += 1u;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI fake_exit_boot_services(
    EFI_HANDLE image,
    UINTN key) {
    const UINTN expected_key = (UINTN)0x100u + map_generation;
    if ((UINTN)(uintptr_t)image != expected_image || key != expected_key) {
        return EFI_ABORTED;
    }
    exit_calls += 1u;
    if (always_change_key || exit_calls == 1u) {
        map_generation += 1u;
        return EFI_INVALID_PARAMETER;
    }
    return EFI_SUCCESS;
}

static int refresh_plan(
    void *context,
    struct RibonBootEnvironment *environment) {
    UINTN *last_key = context;
    if (environment == NULL || last_key == NULL ||
        (environment->flags & RIBON_BOOT_ENV_HAS_MEMORY_MAP) == 0u ||
        environment->memory_map.region_count != 1u ||
        environment->memory_map.regions[0].base != UINT64_C(0x40000000) ||
        environment->memory_map.regions[0].length != 16u * 4096u) {
        return -1;
    }
    *last_key = (UINTN)0x100u + map_generation;
    refresh_calls += 1u;
    return 0;
}

/** @brief Successful ExitBootServices 직후 callback ordering을 기록한다. */
static int post_exit_observer(void *context) {
    const struct RibonUefiAppContext *native =
        (const struct RibonUefiAppContext *)context;
    ++post_exit_calls;
    post_exit_saw_closed_services = native != 0 && native->boot_services == 0;
    return post_exit_saw_closed_services ? 0 : -1;
}

static void reset_fixture(
    struct RibonUefiAppContext *context,
    EFI_BOOT_SERVICES *services,
    EFI_MEMORY_DESCRIPTOR *raw_map,
    struct RibonMemoryRegion *regions) {
    memset(context, 0, sizeof(*context));
    memset(services, 0, sizeof(*services));
    memset(raw_map, 0, sizeof(*raw_map));
    memset(regions, 0, sizeof(*regions));
    map_generation = 0u;
    get_map_calls = 0u;
    exit_calls = 0u;
    refresh_calls = 0u;
    post_exit_calls = 0u;
    post_exit_saw_closed_services = 0;
    expected_image = (UINTN)UINT64_C(0x5249424f4e);
    services->GetMemoryMap = fake_get_memory_map;
    services->ExitBootServices = fake_exit_boot_services;
    context->image_handle = (EFI_HANDLE)(uintptr_t)expected_image;
    context->boot_services = services;
    context->raw_memory_map = raw_map;
    context->raw_memory_map_capacity = sizeof(*raw_map);
    context->regions = regions;
    context->region_capacity = 1u;
}

/** @brief Two-page payload plan을 exact target storage에 구성한다. */
static struct RibonDirectLoadPlan make_payload_plan(
    struct RibonLoadSegment segments[2]) {
    memset(segments, 0, 2u * sizeof(*segments));
    segments[0] = (struct RibonLoadSegment){
        .virtual_address = (uint64_t)(uintptr_t)payload_target + 32u,
        .physical_address = (uint64_t)(uintptr_t)payload_target + 32u,
        .load_address = (uint64_t)(uintptr_t)payload_target + 32u,
        .file_offset = 0u,
        .file_size = 16u,
        .memory_size = 128u,
        .flags = RIBON_LOAD_SEGMENT_READ | RIBON_LOAD_SEGMENT_EXECUTE,
    };
    segments[1] = (struct RibonLoadSegment){
        .virtual_address = (uint64_t)(uintptr_t)payload_target + 4096u,
        .physical_address = (uint64_t)(uintptr_t)payload_target + 4096u,
        .load_address = (uint64_t)(uintptr_t)payload_target + 4096u,
        .file_offset = 16u,
        .file_size = 16u,
        .memory_size = 8192u,
        .flags = RIBON_LOAD_SEGMENT_READ | RIBON_LOAD_SEGMENT_WRITE,
    };
    return (struct RibonDirectLoadPlan){
        .segments = segments,
        .segment_count = 2u,
        .segment_capacity = 2u,
        .entry_point = segments[0].virtual_address + 4u,
    };
}

/** @brief Final map validation과 post-exit zero-and-copy hard cut을 검증한다. */
static int check_post_exit_payload(void) {
    EFI_BOOT_SERVICES services;
    EFI_MEMORY_DESCRIPTOR descriptors[3];
    struct RibonLoadSegment segments[2];
    struct RibonDirectLoadPlan layout;
    struct RibonUefiAppContext context;
    struct RibonPayloadImage payload = {
        .data = payload_source,
        .size = 32u,
        .source_name = "/RIBON/LUCA.ELF",
    };

    memset(&services, 0, sizeof(services));
    memset(descriptors, 0, sizeof(descriptors));
    memset(&context, 0, sizeof(context));
    memset(payload_target, 0xa5, sizeof(payload_target));
    for (uint32_t index = 0u; index < sizeof(payload_source); ++index) {
        payload_source[index] = (unsigned char)(index + 1u);
    }
    for (uint32_t index = 0u; index < 3u; ++index) {
        descriptors[index].PhysicalStart =
            (uint64_t)(uintptr_t)payload_target + index * 4096u;
        descriptors[index].NumberOfPages = 1u;
    }
    descriptors[0].Type = EfiConventionalMemory;
    descriptors[1].Type = EfiBootServicesCode;
    descriptors[2].Type = EfiBootServicesData;
    context.boot_services = &services;
    context.raw_memory_map = descriptors;
    context.raw_memory_map_capacity = sizeof(descriptors);
    context.raw_memory_map_size = sizeof(descriptors);
    context.descriptor_size = sizeof(descriptors[0]);
    layout = make_payload_plan(segments);

    if (ribon_uefi_app_validate_post_exit_payload(
            &context, &payload, &layout) != RIBON_UEFI_APP_STATUS_OK) {
        fputs("uefi_exit_transaction_tests: reclaimable final map was rejected\n", stderr);
        return 0;
    }
    context.boot_services = NULL;
    if (ribon_uefi_app_place_payload_after_exit(
            &context, &payload, &layout) != RIBON_UEFI_APP_STATUS_OK ||
        memcmp(payload_target + 32u, payload_source, 16u) != 0 ||
        memcmp(payload_target + 4096u, payload_source + 16u, 16u) != 0 ||
        payload_target[0] != 0u || payload_target[31] != 0u ||
        payload_target[48] != 0u || payload_target[3u * 4096u - 1u] != 0u ||
        segments[0].runtime_address != segments[0].load_address ||
        segments[1].runtime_address != segments[1].load_address ||
        (layout.load_plan_flags &
         (RIBON_LOAD_PLAN_SEGMENTS_PLACED |
          RIBON_LOAD_PLAN_RUNTIME_ENTRY_VALID)) !=
            (RIBON_LOAD_PLAN_SEGMENTS_PLACED |
             RIBON_LOAD_PLAN_RUNTIME_ENTRY_VALID) ||
        layout.runtime_entry_address != segments[0].runtime_address + 4u) {
        fputs("uefi_exit_transaction_tests: post-exit payload copy was incomplete\n", stderr);
        return 0;
    }

    context.boot_services = &services;
    layout = make_payload_plan(segments);
    descriptors[1].Type = EfiLoaderData;
    if (ribon_uefi_app_validate_post_exit_payload(
            &context, &payload, &layout) != RIBON_UEFI_APP_STATUS_PAYLOAD_ERROR) {
        fputs("uefi_exit_transaction_tests: live loader page was admitted\n", stderr);
        return 0;
    }
    descriptors[1].Type = EfiRuntimeServicesData;
    if (ribon_uefi_app_validate_post_exit_payload(
            &context, &payload, &layout) != RIBON_UEFI_APP_STATUS_PAYLOAD_ERROR) {
        fputs("uefi_exit_transaction_tests: runtime firmware page was admitted\n", stderr);
        return 0;
    }
    descriptors[1].Type = EfiBootServicesData;
    context.raw_memory_map_size = sizeof(descriptors[0]);
    if (ribon_uefi_app_validate_post_exit_payload(
            &context, &payload, &layout) != RIBON_UEFI_APP_STATUS_PAYLOAD_ERROR) {
        fputs("uefi_exit_transaction_tests: descriptor gap was admitted\n", stderr);
        return 0;
    }
    return 1;
}

int main(void) {
    EFI_BOOT_SERVICES services;
    EFI_MEMORY_DESCRIPTOR raw_map;
    struct RibonMemoryRegion regions[1];
    struct RibonUefiAppContext context;
    struct RibonBootEnvironment environment;
    const struct RibonBootEnvironmentPersistentInputs persistent_inputs = {0};
    UINTN refreshed_key = 0u;
    int status;

    if (!check_post_exit_payload()) {
        return 1;
    }

    reset_fixture(&context, &services, &raw_map, regions);
    always_change_key = 0;
    status = ribon_uefi_app_exit_boot_services(
        &context,
        &environment,
        &persistent_inputs,
        refresh_plan,
        &refreshed_key,
        post_exit_observer,
        &context);
    if (status != RIBON_UEFI_APP_STATUS_OK || context.boot_services != NULL ||
        get_map_calls != 2u || refresh_calls != 2u || exit_calls != 2u ||
        refreshed_key != (UINTN)0x101u || post_exit_calls != 1u ||
        !post_exit_saw_closed_services) {
        fputs("uefi_exit_transaction_tests: changed map key was not retried\n", stderr);
        return 1;
    }

    reset_fixture(&context, &services, &raw_map, regions);
    always_change_key = 1;
    refreshed_key = 0u;
    status = ribon_uefi_app_exit_boot_services(
        &context,
        &environment,
        &persistent_inputs,
        refresh_plan,
        &refreshed_key,
        post_exit_observer,
        &context);
    if (status != RIBON_UEFI_APP_STATUS_RETRY_EXHAUSTED ||
        context.boot_services != &services ||
        get_map_calls != RIBON_UEFI_EXIT_ATTEMPTS ||
        refresh_calls != RIBON_UEFI_EXIT_ATTEMPTS ||
        exit_calls != RIBON_UEFI_EXIT_ATTEMPTS || post_exit_calls != 0u) {
        fputs("uefi_exit_transaction_tests: changed key retry was not bounded\n", stderr);
        return 1;
    }

    puts("RIBON-UEFI-EXIT-TRANSACTION-OK payload=post-exit-reclaimable changed-map-key=refreshed bounded=3");
    return 0;
}
