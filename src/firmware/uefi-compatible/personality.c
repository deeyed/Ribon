#include "reference.h"

#include <Ribon/firmware/personality.h>

/** @brief Caller-owned UEFI reference context를 빈 handle database로 초기화한다. */
void ribon_uefi_reference_context_init(
    struct RibonUefiReferenceContext *context) {
    if (context == 0) {
        return;
    }
    *context = (struct RibonUefiReferenceContext){0};
}

/** @brief 중복을 거부하며 bounded handle/protocol tuple을 설치한다. */
static int uefi_handle_install(
    void *opaque,
    uint64_t handle,
    uint64_t protocol,
    const void *interface) {
    struct RibonUefiReferenceContext *context = opaque;

    if (context == 0 || handle == 0u || protocol == 0u || interface == 0) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    for (uint32_t index = 0; index < context->entry_count; ++index) {
        if (context->entries[index].handle == handle &&
            context->entries[index].protocol == protocol) {
            return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
        }
    }
    if (context->entry_count == RIBON_UEFI_REFERENCE_HANDLE_LIMIT) {
        return RIBON_SERVICE_STATUS_OUT_OF_RANGE;
    }
    context->entries[context->entry_count++] =
        (struct RibonUefiReferenceHandleEntry){
            .handle = handle,
            .protocol = protocol,
            .interface = interface,
        };
    return RIBON_SERVICE_STATUS_OK;
}

/** @brief 정확한 handle/protocol tuple에 설치된 interface를 찾는다. */
static const void *uefi_handle_locate(
    const void *opaque,
    uint64_t handle,
    uint64_t protocol) {
    const struct RibonUefiReferenceContext *context = opaque;

    if (context == 0 || handle == 0u || protocol == 0u) {
        return 0;
    }
    for (uint32_t index = 0; index < context->entry_count; ++index) {
        if (context->entries[index].handle == handle &&
            context->entries[index].protocol == protocol) {
            return context->entries[index].interface;
        }
    }
    return 0;
}

static const struct RibonUefiReferenceHandleDatabaseOperations handle_operations = {
    .size = sizeof(handle_operations),
    .install = uefi_handle_install,
    .locate = uefi_handle_locate,
};

/** @brief UEFI handle database operation table의 bounded 계약을 검사한다. */
static int uefi_handle_operations_are_valid(
    const struct RibonFirmwareServiceDescriptor *descriptor) {
    const struct RibonUefiReferenceHandleDatabaseOperations *operations;

    if (descriptor == 0 ||
        descriptor->operations_size != sizeof(*operations) ||
        descriptor->operations == 0) {
        return 0;
    }
    operations = descriptor->operations;
    return operations->size == sizeof(*operations) &&
           operations->install != 0 &&
           operations->locate != 0;
}

static const struct RibonFirmwareServiceDescriptor handle_service = {
    .magic = RIBON_FIRMWARE_SERVICE_MAGIC,
    .size = sizeof(handle_service),
    .abi_version = 1u,
    .id = "uefi.handle-database",
    .service = RIBON_FIRMWARE_SERVICE_HANDLE_DATABASE,
    .phase = RIBON_PLUGIN_PHASE_BOOT,
    .lifetime = RIBON_FIRMWARE_SERVICE_LIFETIME_BOOT,
    .operations = &handle_operations,
    .operations_size = sizeof(handle_operations),
    .validate_operations = uefi_handle_operations_are_valid,
};

static const struct RibonFirmwareServiceDescriptor *const uefi_services[] = {
    &handle_service,
};

/** @brief UEFI-compatible provider의 minimal bounded personality descriptor다. */
const struct RibonFirmwarePersonality ribon_uefi_reference_personality = {
    .magic = RIBON_FIRMWARE_PERSONALITY_MAGIC,
    .size = sizeof(ribon_uefi_reference_personality),
    .abi_version = RIBON_FIRMWARE_PERSONALITY_ABI_VERSION,
    .kind = RIBON_FIRMWARE_PERSONALITY_UEFI_COMPATIBLE,
    .id = "uefi-compatible.reference",
    .published_services = RIBON_FIRMWARE_SERVICE_HANDLE_DATABASE,
    .services = uefi_services,
    .service_count = 1u,
};

static const struct RibonFirmwarePersonalityOperations personality_operations = {
    .size = sizeof(personality_operations),
    .abi_version = RIBON_FIRMWARE_PERSONALITY_ABI_VERSION,
    .personality = &ribon_uefi_reference_personality,
};

/** @brief UEFI-compatible firmware provider plugin descriptor다. */
const struct RibonPluginDescriptor ribon_firmware_personality_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_firmware_personality_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_FIRMWARE_PERSONALITY,
    .phase = RIBON_PLUGIN_PHASE_BOOT,
    .id = "personality.uefi-compatible",
    .provides =
        RIBON_CAP_FIRMWARE_PERSONALITY |
        RIBON_CAP_FIRMWARE_SERVICE_DIRECTORY,
    .requires = RIBON_CAP_ARCHITECTURE,
    .architecture_mask = RIBON_ARCH_MASK_X86_64,
    .personality_mask = RIBON_PERSONALITY_MASK_UEFI_COMPATIBLE,
    .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 4096u,
    .input_budget = 4096u,
    .output_budget = 4096u,
    .deadline_ms = 1000u,
    .operations = &personality_operations,
    .operations_size = sizeof(personality_operations),
    .operations_abi = RIBON_FIRMWARE_PERSONALITY_ABI_VERSION,
    .validate_operations =
        ribon_firmware_personality_plugin_operations_are_valid,
};
