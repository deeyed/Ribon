#include "../../src/environments/uefi-app/uefi_app.h"

#include <stdio.h>
#include <string.h>

static UINTN map_generation;
static UINTN get_map_calls;
static UINTN exit_calls;
static UINTN refresh_calls;
static UINTN expected_image;
static int always_change_key;

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

int main(void) {
    EFI_BOOT_SERVICES services;
    EFI_MEMORY_DESCRIPTOR raw_map;
    struct RibonMemoryRegion regions[1];
    struct RibonUefiAppContext context;
    struct RibonBootEnvironment environment;
    const struct RibonBootEnvironmentPersistentInputs persistent_inputs = {0};
    UINTN refreshed_key = 0u;
    int status;

    reset_fixture(&context, &services, &raw_map, regions);
    always_change_key = 0;
    status = ribon_uefi_app_exit_boot_services(
        &context,
        &environment,
        &persistent_inputs,
        refresh_plan,
        &refreshed_key);
    if (status != RIBON_UEFI_APP_STATUS_OK || context.boot_services != NULL ||
        get_map_calls != 2u || refresh_calls != 2u || exit_calls != 2u ||
        refreshed_key != (UINTN)0x101u) {
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
        &refreshed_key);
    if (status != RIBON_UEFI_APP_STATUS_RETRY_EXHAUSTED ||
        context.boot_services != &services ||
        get_map_calls != RIBON_UEFI_EXIT_ATTEMPTS ||
        refresh_calls != RIBON_UEFI_EXIT_ATTEMPTS ||
        exit_calls != RIBON_UEFI_EXIT_ATTEMPTS) {
        fputs("uefi_exit_transaction_tests: changed key retry was not bounded\n", stderr);
        return 1;
    }

    puts("RIBON-UEFI-EXIT-TRANSACTION-OK changed-map-key=refreshed bounded=3");
    return 0;
}
