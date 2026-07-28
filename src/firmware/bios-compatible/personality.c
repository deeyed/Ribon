#include "reference.h"

#include <Ribon/firmware/personality.h>

/** @brief Caller-owned BIOS reference context를 빈 E820 table로 초기화한다. */
void ribon_bios_reference_context_init(
    struct RibonBiosReferenceContext *context) {
    if (context == 0) {
        return;
    }
    *context = (struct RibonBiosReferenceContext){0};
}

/** @brief Overflow와 overlap을 거부하며 E820 range를 append한다. */
static int bios_e820_append(
    void *opaque,
    uint64_t base,
    uint64_t length,
    uint32_t type) {
    struct RibonBiosReferenceContext *context = opaque;

    if (context == 0 ||
        length == 0u ||
        base > UINT64_MAX - length ||
        type == 0u) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    if (context->entry_count == RIBON_BIOS_REFERENCE_E820_LIMIT) {
        return RIBON_SERVICE_STATUS_OUT_OF_RANGE;
    }
    for (uint32_t index = 0; index < context->entry_count; ++index) {
        const uint64_t existing_end =
            context->entries[index].base + context->entries[index].length;
        const uint64_t new_end = base + length;
        if (base < existing_end && context->entries[index].base < new_end) {
            return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
        }
    }
    context->entries[context->entry_count++] =
        (struct RibonBiosReferenceE820Entry){
            .base = base,
            .length = length,
            .type = type,
        };
    return RIBON_SERVICE_STATUS_OK;
}

/** @brief Bounded index의 E820 range를 caller-owned output으로 복사한다. */
static int bios_e820_read(
    const void *opaque,
    uint32_t index,
    struct RibonBiosReferenceE820Entry *out) {
    const struct RibonBiosReferenceContext *context = opaque;

    if (context == 0 || out == 0 || index >= context->entry_count) {
        return RIBON_SERVICE_STATUS_OUT_OF_RANGE;
    }
    *out = context->entries[index];
    return RIBON_SERVICE_STATUS_OK;
}

static const struct RibonBiosReferenceE820Operations e820_operations = {
    .size = sizeof(e820_operations),
    .append = bios_e820_append,
    .read = bios_e820_read,
};

/** @brief BIOS E820 operation table의 bounded 계약을 검사한다. */
static int bios_e820_operations_are_valid(
    const struct RibonFirmwareServiceDescriptor *descriptor) {
    const struct RibonBiosReferenceE820Operations *operations;

    if (descriptor == 0 ||
        descriptor->operations_size != sizeof(*operations) ||
        descriptor->operations == 0) {
        return 0;
    }
    operations = descriptor->operations;
    return operations->size == sizeof(*operations) &&
           operations->append != 0 &&
           operations->read != 0;
}

static const struct RibonFirmwareServiceDescriptor e820_service = {
    .magic = RIBON_FIRMWARE_SERVICE_MAGIC,
    .size = sizeof(e820_service),
    .abi_version = 1u,
    .id = "bios.e820",
    .service = RIBON_FIRMWARE_SERVICE_E820,
    .phase = RIBON_PLUGIN_PHASE_BOOT,
    .lifetime = RIBON_FIRMWARE_SERVICE_LIFETIME_BOOT,
    .operations = &e820_operations,
    .operations_size = sizeof(e820_operations),
    .validate_operations = bios_e820_operations_are_valid,
};

static const struct RibonFirmwareServiceDescriptor *const bios_services[] = {
    &e820_service,
};

/** @brief BIOS-compatible provider의 minimal bounded personality descriptor다. */
const struct RibonFirmwarePersonality ribon_bios_reference_personality = {
    .magic = RIBON_FIRMWARE_PERSONALITY_MAGIC,
    .size = sizeof(ribon_bios_reference_personality),
    .abi_version = RIBON_FIRMWARE_PERSONALITY_ABI_VERSION,
    .kind = RIBON_FIRMWARE_PERSONALITY_BIOS_COMPATIBLE,
    .id = "bios-compatible.reference",
    .published_services = RIBON_FIRMWARE_SERVICE_E820,
    .services = bios_services,
    .service_count = 1u,
};

static const struct RibonFirmwarePersonalityOperations personality_operations = {
    .size = sizeof(personality_operations),
    .abi_version = RIBON_FIRMWARE_PERSONALITY_ABI_VERSION,
    .personality = &ribon_bios_reference_personality,
};

/** @brief BIOS-compatible firmware provider plugin descriptor다. */
const struct RibonPluginDescriptor ribon_firmware_personality_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_firmware_personality_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_FIRMWARE_PERSONALITY,
    .phase = RIBON_PLUGIN_PHASE_BOOT,
    .id = "personality.bios-compatible",
    .provides =
        RIBON_CAP_FIRMWARE_PERSONALITY |
        RIBON_CAP_FIRMWARE_SERVICE_DIRECTORY,
    .requires = RIBON_CAP_ARCHITECTURE,
    .architecture_mask = RIBON_ARCH_MASK_X86_64,
    .personality_mask = RIBON_PERSONALITY_MASK_BIOS_COMPATIBLE,
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
