#include <Ribon/firmware/personality.h>

/** @brief 두 stable ID의 byte order를 비교한다. */
static int personality_strcmp(const char *lhs, const char *rhs) {
    while (*lhs != '\0' && *rhs != '\0' && *lhs == *rhs) {
        ++lhs;
        ++rhs;
    }
    return (int)(unsigned char)*lhs - (int)(unsigned char)*rhs;
}

/** @brief 64-bit 값이 정확히 한 service bit인지 검사한다. */
static int personality_one_bit(uint64_t value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}

/** @brief Personality와 service descriptor 전체를 fail-closed로 검사한다. */
int ribon_firmware_personality_is_valid(
    const struct RibonFirmwarePersonality *personality) {
    uint64_t published = 0u;
    uint64_t runtime = 0u;

    if (personality == 0 ||
        personality->magic != RIBON_FIRMWARE_PERSONALITY_MAGIC ||
        personality->size != sizeof(*personality) ||
        personality->abi_version != RIBON_FIRMWARE_PERSONALITY_ABI_VERSION ||
        personality->kind < RIBON_FIRMWARE_PERSONALITY_UEFI_COMPATIBLE ||
        personality->kind > RIBON_FIRMWARE_PERSONALITY_BIOS_COMPATIBLE ||
        personality->id == 0 ||
        personality->id[0] == '\0' ||
        personality->services == 0 ||
        personality->service_count == 0u ||
        personality->service_count > RIBON_FIRMWARE_SERVICE_LIMIT ||
        (personality->published_services & ~RIBON_FIRMWARE_SERVICE_ALL) != 0u ||
        (personality->runtime_services &
         ~personality->published_services) != 0u) {
        return 0;
    }

    for (uint32_t index = 0; index < personality->service_count; ++index) {
        const struct RibonFirmwareServiceDescriptor *service =
            personality->services[index];
        if (service == 0 ||
            service->magic != RIBON_FIRMWARE_SERVICE_MAGIC ||
            service->size != sizeof(*service) ||
            service->abi_version == 0u ||
            service->id == 0 ||
            service->id[0] == '\0' ||
            !personality_one_bit(service->service) ||
            (service->service & ~RIBON_FIRMWARE_SERVICE_ALL) != 0u ||
            service->phase < RIBON_PLUGIN_PHASE_FOUNDATION ||
            service->phase > RIBON_PLUGIN_PHASE_RUNTIME ||
            service->lifetime < RIBON_FIRMWARE_SERVICE_LIFETIME_BOOT ||
            service->lifetime > RIBON_FIRMWARE_SERVICE_LIFETIME_RUNTIME ||
            service->operations == 0 ||
            service->operations_size == 0u ||
            service->validate_operations == 0 ||
            !service->validate_operations(service) ||
            (index != 0u &&
             personality_strcmp(
                 personality->services[index - 1u]->id,
                 service->id) >= 0) ||
            (published & service->service) != 0u) {
            return 0;
        }
        if ((service->lifetime == RIBON_FIRMWARE_SERVICE_LIFETIME_RUNTIME) !=
            (service->phase == RIBON_PLUGIN_PHASE_RUNTIME)) {
            return 0;
        }
        published |= service->service;
        if (service->lifetime == RIBON_FIRMWARE_SERVICE_LIFETIME_RUNTIME) {
            runtime |= service->service;
        }
    }
    return published == personality->published_services &&
           runtime == personality->runtime_services;
}

/** @brief 허용 phase까지 service를 caller-owned directory에 publish한다. */
int ribon_firmware_personality_publish(
    const struct RibonFirmwarePersonality *personality,
    enum RibonPluginPhase phase,
    void *context,
    struct RibonFirmwareServiceDirectory *directory) {
    uint32_t count = 0u;
    uint64_t published = 0u;

    if (directory == 0) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    directory->personality = 0;
    directory->service_count = 0u;
    directory->published_services = 0u;
    directory->context = 0;
    if (!ribon_firmware_personality_is_valid(personality) ||
        directory->size != sizeof(*directory) ||
        directory->abi_version != RIBON_FIRMWARE_PERSONALITY_ABI_VERSION ||
        directory->services == 0 ||
        directory->service_capacity == 0u ||
        directory->service_capacity > RIBON_FIRMWARE_SERVICE_LIMIT ||
        phase < RIBON_PLUGIN_PHASE_FOUNDATION ||
        phase > RIBON_PLUGIN_PHASE_RUNTIME) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    for (uint32_t index = 0; index < personality->service_count; ++index) {
        const struct RibonFirmwareServiceDescriptor *service =
            personality->services[index];
        if (service->phase > phase) {
            continue;
        }
        if (count == directory->service_capacity) {
            directory->service_count = 0u;
            directory->published_services = 0u;
            return RIBON_SERVICE_STATUS_OUT_OF_RANGE;
        }
        directory->services[count++] = service;
        published |= service->service;
    }
    directory->personality = personality;
    directory->service_count = count;
    directory->published_services = published;
    directory->context = context;
    return RIBON_SERVICE_STATUS_OK;
}

/** @brief Directory metadata와 publish된 service pointer 집합을 검증한다. */
static int firmware_service_directory_is_valid(
    const struct RibonFirmwareServiceDirectory *directory) {
    uint64_t published = 0u;

    if (directory == 0 ||
        directory->size != sizeof(*directory) ||
        directory->abi_version != RIBON_FIRMWARE_PERSONALITY_ABI_VERSION ||
        directory->services == 0 ||
        directory->personality == 0 ||
        directory->service_capacity == 0u ||
        directory->service_capacity > RIBON_FIRMWARE_SERVICE_LIMIT ||
        directory->service_count > directory->service_capacity ||
        !ribon_firmware_personality_is_valid(directory->personality)) {
        return 0;
    }
    for (uint32_t index = 0; index < directory->service_count; ++index) {
        const struct RibonFirmwareServiceDescriptor *service =
            directory->services[index];
        int belongs_to_personality = 0;

        if (service == 0 || (published & service->service) != 0u) {
            return 0;
        }
        for (uint32_t candidate = 0;
             candidate < directory->personality->service_count;
             ++candidate) {
            if (directory->personality->services[candidate] == service) {
                belongs_to_personality = 1;
                break;
            }
        }
        if (!belongs_to_personality) {
            return 0;
        }
        published |= service->service;
    }
    return published == directory->published_services;
}

/** @brief Directory에서 정확히 한 service bit의 descriptor를 찾는다. */
const struct RibonFirmwareServiceDescriptor *ribon_firmware_service_directory_find(
    const struct RibonFirmwareServiceDirectory *directory,
    uint64_t service) {
    if (!firmware_service_directory_is_valid(directory) ||
        !personality_one_bit(service) ||
        (service & ~RIBON_FIRMWARE_SERVICE_ALL) != 0u) {
        return 0;
    }
    for (uint32_t index = 0; index < directory->service_count; ++index) {
        if (directory->services[index] != 0 &&
            directory->services[index]->service == service) {
            return directory->services[index];
        }
    }
    return 0;
}

/** @brief Directory가 요청 service를 모두 publish했는지 검사한다. */
int ribon_firmware_service_directory_require(
    const struct RibonFirmwareServiceDirectory *directory,
    uint64_t services) {
    if (!firmware_service_directory_is_valid(directory) ||
        services == 0u ||
        (services & ~RIBON_FIRMWARE_SERVICE_ALL) != 0u) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    return (directory->published_services & services) == services ?
        RIBON_SERVICE_STATUS_OK :
        RIBON_SERVICE_STATUS_UNSUPPORTED;
}

/** @brief Firmware personality plugin operation table과 descriptor를 함께 검사한다. */
int ribon_firmware_personality_plugin_operations_are_valid(
    const struct RibonPluginDescriptor *descriptor) {
    const struct RibonFirmwarePersonalityOperations *operations;
    uint32_t expected_mask;

    if (descriptor == 0 ||
        descriptor->kind != RIBON_PLUGIN_KIND_FIRMWARE_PERSONALITY ||
        descriptor->operations == 0 ||
        descriptor->operations_size != sizeof(*operations) ||
        descriptor->operations_abi !=
            RIBON_FIRMWARE_PERSONALITY_ABI_VERSION ||
        (descriptor->provides &
         (RIBON_CAP_FIRMWARE_PERSONALITY |
          RIBON_CAP_FIRMWARE_SERVICE_DIRECTORY)) !=
            (RIBON_CAP_FIRMWARE_PERSONALITY |
             RIBON_CAP_FIRMWARE_SERVICE_DIRECTORY)) {
        return 0;
    }
    operations = descriptor->operations;
    if (operations->size != sizeof(*operations) ||
        operations->abi_version !=
            RIBON_FIRMWARE_PERSONALITY_ABI_VERSION ||
        !ribon_firmware_personality_is_valid(operations->personality)) {
        return 0;
    }
    expected_mask =
        operations->personality->kind ==
            RIBON_FIRMWARE_PERSONALITY_UEFI_COMPATIBLE ?
            RIBON_PERSONALITY_MASK_UEFI_COMPATIBLE :
            RIBON_PERSONALITY_MASK_BIOS_COMPATIBLE;
    return descriptor->personality_mask == expected_mask;
}
