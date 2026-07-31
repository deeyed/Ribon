#include <Ribon/service/directory.h>

#include <Ribon/core/status.h>
#include <Ribon/plugin/manifest.h>

/** @brief 두 stable ID의 byte sequence가 같은지 검사한다. */
static int service_streq(const char *lhs, const char *rhs) {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }
    while (*lhs != '\0' && *rhs != '\0') {
        if (*lhs != *rhs) {
            return 0;
        }
        ++lhs;
        ++rhs;
    }
    return *lhs == *rhs;
}

/** @brief 두 stable ID의 byte order를 비교한다. */
static int service_strcmp(const char *lhs, const char *rhs) {
    while (*lhs != '\0' && *rhs != '\0' && *lhs == *rhs) {
        ++lhs;
        ++rhs;
    }
    return (int)(unsigned char)*lhs - (int)(unsigned char)*rhs;
}

/** @brief Service role이 제공해야 하는 capability bitset을 반환한다. */
static uint64_t service_capabilities(enum RibonServiceKind kind) {
    switch (kind) {
    case RIBON_SERVICE_KIND_BOOT_SOURCE:
        return RIBON_CAP_BOOT_SOURCE_READ;
    case RIBON_SERVICE_KIND_INACTIVE_SLOT_STORAGE:
        return RIBON_CAP_INACTIVE_SLOT_WRITE | RIBON_CAP_INACTIVE_SLOT_ERASE;
    case RIBON_SERVICE_KIND_STORAGE_FLUSH:
        return RIBON_CAP_STORAGE_FLUSH;
    case RIBON_SERVICE_KIND_MONOTONIC_TIMER:
        return RIBON_CAP_MONOTONIC_TIMER;
    case RIBON_SERVICE_KIND_WATCHDOG:
        return RIBON_CAP_WATCHDOG;
    case RIBON_SERVICE_KIND_RESET:
        return RIBON_CAP_RESET;
    case RIBON_SERVICE_KIND_PERSISTENT_METADATA:
        return RIBON_CAP_PERSISTENT_METADATA;
    case RIBON_SERVICE_KIND_NETWORK_TRANSPORT:
        return RIBON_CAP_NETWORK_TRANSPORT;
    case RIBON_SERVICE_KIND_RANDOM_NONCE:
        return RIBON_CAP_RANDOM_NONCE;
    case RIBON_SERVICE_KIND_DIAGNOSTIC_SINK:
        return RIBON_CAP_DIAGNOSTIC_SINK;
    case RIBON_SERVICE_KIND_ENVIRONMENT_QUIESCE:
        return RIBON_CAP_ENVIRONMENT_QUIESCE;
    case RIBON_SERVICE_KIND_MACHINE_DESCRIPTION:
        return RIBON_CAP_MACHINE_DESCRIPTION;
    case RIBON_SERVICE_KIND_PAYLOAD_PLACEMENT:
        return RIBON_CAP_PAYLOAD_PLACEMENT;
    default:
        return 0u;
    }
}

/** @brief Service role의 안정적인 이름을 반환한다. */
const char *ribon_service_kind_name(enum RibonServiceKind kind) {
    switch (kind) {
    case RIBON_SERVICE_KIND_BOOT_SOURCE:
        return "boot-source";
    case RIBON_SERVICE_KIND_INACTIVE_SLOT_STORAGE:
        return "inactive-slot-storage";
    case RIBON_SERVICE_KIND_STORAGE_FLUSH:
        return "storage-flush";
    case RIBON_SERVICE_KIND_MONOTONIC_TIMER:
        return "monotonic-timer";
    case RIBON_SERVICE_KIND_WATCHDOG:
        return "watchdog";
    case RIBON_SERVICE_KIND_RESET:
        return "reset";
    case RIBON_SERVICE_KIND_PERSISTENT_METADATA:
        return "persistent-metadata";
    case RIBON_SERVICE_KIND_NETWORK_TRANSPORT:
        return "network-transport";
    case RIBON_SERVICE_KIND_RANDOM_NONCE:
        return "random-nonce";
    case RIBON_SERVICE_KIND_DIAGNOSTIC_SINK:
        return "diagnostic-sink";
    case RIBON_SERVICE_KIND_ENVIRONMENT_QUIESCE:
        return "environment-quiesce";
    case RIBON_SERVICE_KIND_MACHINE_DESCRIPTION:
        return "machine-description";
    case RIBON_SERVICE_KIND_PAYLOAD_PLACEMENT:
        return "payload-placement";
    default:
        return "unknown";
    }
}

/** @brief Service descriptor의 generic ABI와 typed operation table을 검사한다. */
int ribon_service_descriptor_is_valid(const struct RibonServiceDescriptor *descriptor) {
    if (descriptor == 0 ||
        descriptor->magic != RIBON_SERVICE_DESCRIPTOR_MAGIC ||
        descriptor->size != sizeof(*descriptor) ||
        descriptor->abi_version != RIBON_SERVICE_ABI_VERSION ||
        service_capabilities(descriptor->kind) == 0u ||
        descriptor->cardinality < RIBON_SERVICE_CARDINALITY_AUTHORITY ||
        descriptor->cardinality > RIBON_SERVICE_CARDINALITY_COLLECTION ||
        descriptor->lifetime < RIBON_SERVICE_LIFETIME_BOOT ||
        descriptor->lifetime > RIBON_SERVICE_LIFETIME_PERSISTENT ||
        descriptor->phase < RIBON_PLUGIN_PHASE_EARLY ||
        descriptor->phase > RIBON_PLUGIN_PHASE_QUIESCE ||
        descriptor->id == 0 || descriptor->id[0] == '\0' ||
        descriptor->provides != service_capabilities(descriptor->kind) ||
        (descriptor->architecture_mask & ~RIBON_ARCH_MASK_ALL) != 0u ||
        descriptor->architecture_mask == 0u ||
        (descriptor->environment_mask & ~RIBON_ENV_MASK_ALL) != 0u ||
        (descriptor->personality_mask & ~RIBON_PERSONALITY_MASK_ALL) != 0u ||
        (descriptor->environment_mask == 0u && descriptor->personality_mask == 0u) ||
        (descriptor->mode_mask & ~RIBON_MODE_MASK_ALL) != 0u ||
        descriptor->mode_mask == 0u ||
        descriptor->arena_budget == 0u ||
        descriptor->input_budget == 0u ||
        descriptor->output_budget == 0u ||
        descriptor->deadline_ms == 0u ||
        descriptor->operations == 0 ||
        descriptor->operations_size == 0u ||
        descriptor->operations_abi == 0u ||
        descriptor->validate_operations == 0) {
        return 0;
    }
    return descriptor->validate_operations(descriptor);
}

/** @brief Stable ID와 role이 정확히 일치하는 descriptor를 반환한다. */
const struct RibonServiceDescriptor *ribon_service_directory_find_exact(
    const struct RibonServiceDirectory *directory,
    enum RibonServiceKind kind,
    const char *id) {
    if (directory == 0 || directory->services == 0 || id == 0) {
        return 0;
    }
    for (uint32_t index = 0u; index < directory->service_count; ++index) {
        const struct RibonServiceDescriptor *service = directory->services[index];
        if (service != 0 && service->kind == kind && service_streq(service->id, id)) {
            return service;
        }
    }
    return 0;
}

/** @brief Collection selection descriptor의 독립 field를 검사한다. */
static int service_selection_is_valid(const struct RibonServiceSelection *selection) {
    return selection != 0 && service_capabilities(selection->kind) != 0u &&
           selection->id != 0 && selection->id[0] != '\0';
}

/** @brief Product tuple, budget, authority/collection selection을 fail-closed로 검사한다. */
int ribon_service_directory_validate(
    const struct RibonServiceDirectory *directory,
    const struct RibonProductDescriptor *product,
    enum RibonMode mode) {
    uint64_t aggregate = 0u;
    uint64_t arena_budget = 0u;
    const uint64_t service_capability_mask =
        RIBON_CAP_BOOT_SOURCE_READ |
        RIBON_CAP_INACTIVE_SLOT_WRITE |
        RIBON_CAP_INACTIVE_SLOT_ERASE |
        RIBON_CAP_STORAGE_FLUSH |
        RIBON_CAP_MONOTONIC_TIMER |
        RIBON_CAP_WATCHDOG |
        RIBON_CAP_RESET |
        RIBON_CAP_PERSISTENT_METADATA |
        RIBON_CAP_NETWORK_TRANSPORT |
        RIBON_CAP_RANDOM_NONCE |
        RIBON_CAP_DIAGNOSTIC_SINK |
        RIBON_CAP_ENVIRONMENT_QUIESCE |
        RIBON_CAP_MACHINE_DESCRIPTION |
        RIBON_CAP_PAYLOAD_PLACEMENT;

    if (directory == 0 || product == 0 ||
        directory->size != sizeof(*directory) ||
        directory->abi_version != RIBON_SERVICE_DIRECTORY_ABI_VERSION ||
        directory->service_count > RIBON_SERVICE_DIRECTORY_LIMIT ||
        (directory->service_count != 0u && directory->services == 0) ||
        mode < RIBON_MODE_NORMAL || mode > RIBON_MODE_DIAGNOSTIC) {
        return RIBON_CORE_STATUS_INVALID_DESCRIPTOR;
    }
    for (uint32_t index = 0u; index < directory->service_count; ++index) {
        const struct RibonServiceDescriptor *service = directory->services[index];
        const int frontend_matches =
            product->kind == RIBON_PRODUCT_KIND_FIRMWARE ?
                (service != 0 &&
                 (service->personality_mask & product->personality_mask) != 0u) :
                (service != 0 &&
                 (service->environment_mask & product->environment_mask) != 0u);
        if (!ribon_service_descriptor_is_valid(service) ||
            (service->architecture_mask & product->architecture_mask) == 0u ||
            !frontend_matches ||
            (service->mode_mask & RIBON_MODE_MASK(mode)) == 0u ||
            (service->provides & ~product->allowed_capabilities) != 0u ||
            service->input_budget > product->limits.max_input_bytes ||
            service->output_budget > product->limits.max_handoff_bytes ||
            service->deadline_ms > product->limits.operation_deadline_ms ||
            (index != 0u &&
             service_strcmp(directory->services[index - 1u]->id, service->id) >= 0)) {
            return RIBON_CORE_STATUS_INVALID_DESCRIPTOR;
        }
        for (uint32_t previous = 0u; previous < index; ++previous) {
            const struct RibonServiceDescriptor *other = directory->services[previous];
            if (other->kind == service->kind &&
                (other->cardinality == RIBON_SERVICE_CARDINALITY_AUTHORITY ||
                 service->cardinality == RIBON_SERVICE_CARDINALITY_AUTHORITY)) {
                return RIBON_CORE_STATUS_DUPLICATE_PROVIDER;
            }
        }
        if (arena_budget > UINT64_MAX - service->arena_budget) {
            return RIBON_CORE_STATUS_BAD_LIMIT;
        }
        arena_budget += service->arena_budget;
        aggregate |= service->provides;
    }
    if (arena_budget > product->limits.arena_bytes ||
        (aggregate & product->required_capabilities & service_capability_mask) !=
            (product->required_capabilities & service_capability_mask)) {
        return RIBON_CORE_STATUS_MISSING_CAPABILITY;
    }
    for (uint32_t index = 0u; index < product->service_selection_count; ++index) {
        const struct RibonServiceSelection *selection = &product->service_selections[index];
        const struct RibonServiceDescriptor *service;
        if (!service_selection_is_valid(selection) ||
            (index != 0u &&
             product->service_selections[index - 1u].kind >= selection->kind)) {
            return RIBON_CORE_STATUS_INVALID_DESCRIPTOR;
        }
        service = ribon_service_directory_find_exact(directory, selection->kind, selection->id);
        if (service == 0) {
            return RIBON_CORE_STATUS_MISSING_CAPABILITY;
        }
        if (service->cardinality != RIBON_SERVICE_CARDINALITY_COLLECTION) {
            return RIBON_CORE_STATUS_AMBIGUOUS_SELECTION;
        }
    }
    for (uint32_t index = 0u; index < directory->service_count; ++index) {
        const struct RibonServiceDescriptor *service = directory->services[index];
        uint32_t count = 0u;
        int selected = 0;
        if (service->cardinality != RIBON_SERVICE_CARDINALITY_COLLECTION) {
            continue;
        }
        for (uint32_t other = 0u; other < directory->service_count; ++other) {
            count += directory->services[other]->kind == service->kind ? 1u : 0u;
        }
        for (uint32_t selection = 0u;
             selection < product->service_selection_count;
             ++selection) {
            if (product->service_selections[selection].kind == service->kind) {
                selected = 1;
            }
        }
        if (count > 1u && !selected) {
            return RIBON_CORE_STATUS_AMBIGUOUS_SELECTION;
        }
    }
    return RIBON_CORE_STATUS_OK;
}

/** @brief Product가 collection owner로 선택한 descriptor를 반환한다. */
const struct RibonServiceDescriptor *ribon_service_directory_find_selected(
    const struct RibonServiceDirectory *directory,
    const struct RibonProductDescriptor *product,
    enum RibonServiceKind kind) {
    if (directory == 0 || product == 0) {
        return 0;
    }
    for (uint32_t index = 0u; index < product->service_selection_count; ++index) {
        const struct RibonServiceSelection *selection = &product->service_selections[index];
        if (selection->kind == kind) {
            return ribon_service_directory_find_exact(directory, kind, selection->id);
        }
    }
    return 0;
}

/** @brief Environment plugin operation이 local typed directory인지 검사한다. */
int ribon_environment_plugin_operations_are_valid(
    const struct RibonPluginDescriptor *descriptor) {
    const struct RibonServiceDirectory *directory;
    uint64_t aggregate = 0u;
    if (descriptor == 0 || descriptor->kind != RIBON_PLUGIN_KIND_ENVIRONMENT ||
        descriptor->operations_size != sizeof(*directory) ||
        descriptor->operations_abi != RIBON_SERVICE_DIRECTORY_ABI_VERSION) {
        return 0;
    }
    directory = descriptor->operations;
    if (directory == 0 || directory->size != sizeof(*directory) ||
        directory->abi_version != RIBON_SERVICE_DIRECTORY_ABI_VERSION ||
        directory->services == 0 || directory->service_count == 0u) {
        return 0;
    }
    for (uint32_t index = 0u; index < directory->service_count; ++index) {
        if (!ribon_service_descriptor_is_valid(directory->services[index])) {
            return 0;
        }
        aggregate |= directory->services[index]->provides;
    }
    return aggregate == descriptor->provides;
}

/** @brief Diagnostic sink service operation table을 검사한다. */
int ribon_diagnostic_sink_service_operations_are_valid(
    const struct RibonServiceDescriptor *descriptor) {
    const struct RibonDiagnosticSinkServiceOperations *operations;
    if (descriptor == 0 ||
        descriptor->kind != RIBON_SERVICE_KIND_DIAGNOSTIC_SINK ||
        descriptor->provides != RIBON_CAP_DIAGNOSTIC_SINK ||
        descriptor->operations_size != sizeof(*operations) ||
        descriptor->operations_abi != RIBON_SERVICE_ABI_VERSION) {
        return 0;
    }
    operations = descriptor->operations;
    return operations != 0 &&
           operations->size == sizeof(*operations) &&
           operations->abi_version == RIBON_SERVICE_ABI_VERSION &&
           operations->initialize != 0 &&
           operations->write != 0;
}

/** @brief Watchdog service operation table의 exact typed ABI를 검사한다. */
int ribon_watchdog_service_operations_are_valid(
    const struct RibonServiceDescriptor *descriptor) {
    const struct RibonWatchdogServiceOperations *operations;
    if (descriptor == 0 ||
        descriptor->kind != RIBON_SERVICE_KIND_WATCHDOG ||
        descriptor->provides != RIBON_CAP_WATCHDOG ||
        descriptor->operations_size != sizeof(*operations) ||
        descriptor->operations_abi != RIBON_SERVICE_ABI_VERSION) {
        return 0;
    }
    operations = descriptor->operations;
    return operations != 0 &&
           operations->size == sizeof(*operations) &&
           operations->abi_version == RIBON_SERVICE_ABI_VERSION &&
           operations->arm != 0;
}

/** @brief Machine-description service operation table을 검사한다. */
int ribon_machine_description_service_operations_are_valid(
    const struct RibonServiceDescriptor *descriptor) {
    const struct RibonMachineDescriptionServiceOperations *operations;
    if (descriptor == 0 ||
        descriptor->kind != RIBON_SERVICE_KIND_MACHINE_DESCRIPTION ||
        descriptor->provides != RIBON_CAP_MACHINE_DESCRIPTION ||
        descriptor->operations_size != sizeof(*operations) ||
        descriptor->operations_abi != RIBON_SERVICE_ABI_VERSION) {
        return 0;
    }
    operations = descriptor->operations;
    return operations != 0 &&
           operations->size == sizeof(*operations) &&
           operations->abi_version == RIBON_SERVICE_ABI_VERSION &&
           operations->machine_id != 0 &&
           operations->machine_id[0] != '\0' &&
           operations->native_input_capacity != 0u;
}

/** @brief Payload-placement service operation table을 검사한다. */
int ribon_payload_placement_service_operations_are_valid(
    const struct RibonServiceDescriptor *descriptor) {
    const struct RibonPayloadPlacementServiceOperations *operations;
    if (descriptor == 0 ||
        descriptor->kind != RIBON_SERVICE_KIND_PAYLOAD_PLACEMENT ||
        descriptor->provides != RIBON_CAP_PAYLOAD_PLACEMENT ||
        descriptor->operations_size != sizeof(*operations) ||
        descriptor->operations_abi != RIBON_SERVICE_ABI_VERSION) {
        return 0;
    }
    operations = descriptor->operations;
    return operations != 0 &&
           operations->size == sizeof(*operations) &&
           operations->abi_version == RIBON_SERVICE_ABI_VERSION &&
           operations->physical_base != 0u &&
           operations->physical_size != 0u &&
           operations->physical_base <= UINT64_MAX - operations->physical_size;
}
