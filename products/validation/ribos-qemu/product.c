#include "product.h"

#include <Ribon/plugin/descriptor.h>
#include <Ribon/service/directory.h>

#include <ribos/artifact/format.h>
#include <ribos/vm/handles.h>
#include <ribos/vm/helpers.h>
#include <ribos/vm/prepared.h>

#include <string.h>

#define VALIDATION_ACTION_CAPACITY 64u
#define VALIDATION_METADATA_CAPACITY 64u
#define VALIDATION_SIGNATURE_BYTE 0xa5u

static const uint8_t validation_key_id[] = "ribon-r18-fixture-key";

struct ValidationLifecycle {
    const uint8_t *source;
    uint64_t source_size;
    uint8_t metadata[VALIDATION_METADATA_CAPACITY];
    uint64_t metadata_size;
    uint64_t timer_ticks;
    uint64_t timer_step;
    uint64_t watchdog_timeout_ms;
    uint32_t writes;
    uint32_t flushes;
    uint32_t quiesces;
    uint32_t watchdog_arms;
};

static struct ValidationLifecycle lifecycle;

static const struct RibonMemoryRegion validation_memory_map[] = {
    {
        .base = 0x00100000ull,
        .length = 0x00100000ull,
        .kind = RIBON_MEMORY_REGION_FIRMWARE,
        .attributes = RIBON_MEMORY_ATTR_READ,
    },
    {
        .base = 0x00200000ull,
        .length = 0x1fe00000ull,
        .kind = RIBON_MEMORY_REGION_USABLE,
        .attributes = RIBON_MEMORY_ATTR_READ | RIBON_MEMORY_ATTR_WRITE,
    },
};

static int
validation_source_read(
    void *context,
    const struct RibonBootSource *source,
    uint64_t offset,
    void *buffer,
    uint64_t size,
    uint64_t deadline_ticks)
{
    uint64_t index;

    (void)deadline_ticks;
    if (context != &lifecycle || source == NULL || buffer == NULL ||
        source->kind != RIBON_BOOT_MEDIA_MEMORY ||
        lifecycle.source == NULL || source->size != lifecycle.source_size ||
        offset > source->size || size > source->size - offset) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    for (index = 0u; index < size; ++index) {
        ((uint8_t *)buffer)[index] = lifecycle.source[offset + index];
    }
    return RIBON_SERVICE_STATUS_OK;
}

static int
validation_timer_now(void *context, uint64_t *ticks_out)
{
    if (context != &lifecycle || ticks_out == NULL) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    *ticks_out = lifecycle.timer_ticks;
    lifecycle.timer_ticks += lifecycle.timer_step;
    return RIBON_SERVICE_STATUS_OK;
}

static int
validation_watchdog_arm(void *context, uint64_t timeout_ms)
{
    if (context != &lifecycle || timeout_ms == 0u) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    ++lifecycle.watchdog_arms;
    lifecycle.watchdog_timeout_ms = timeout_ms;
    return RIBON_SERVICE_STATUS_OK;
}

static int
validation_metadata_read(
    void *context,
    uint64_t offset,
    void *buffer,
    uint64_t size)
{
    uint64_t index;

    if (context != &lifecycle || buffer == NULL ||
        offset > lifecycle.metadata_size ||
        size > lifecycle.metadata_size - offset) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    for (index = 0u; index < size; ++index) {
        ((uint8_t *)buffer)[index] = lifecycle.metadata[offset + index];
    }
    return RIBON_SERVICE_STATUS_OK;
}

static int
validation_metadata_write(
    void *context,
    uint64_t offset,
    const void *buffer,
    uint64_t size)
{
    uint64_t index;

    if (context != &lifecycle || buffer == NULL ||
        offset > sizeof(lifecycle.metadata) ||
        size > sizeof(lifecycle.metadata) - offset) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    for (index = 0u; index < size; ++index) {
        lifecycle.metadata[offset + index] =
            ((const uint8_t *)buffer)[index];
    }
    if (offset + size > lifecycle.metadata_size) {
        lifecycle.metadata_size = offset + size;
    }
    ++lifecycle.writes;
    return RIBON_SERVICE_STATUS_OK;
}

static int
validation_flush(
    void *context,
    uint32_t slot,
    uint64_t deadline_ticks)
{
    (void)slot;
    (void)deadline_ticks;
    if (context != &lifecycle) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    ++lifecycle.flushes;
    return RIBON_SERVICE_STATUS_OK;
}

static int
validation_quiesce(void *context)
{
    if (context != &lifecycle) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    ++lifecycle.quiesces;
    return RIBON_SERVICE_STATUS_OK;
}

static struct RibonBootSourceServiceOperations source_operations = {
    .size = sizeof(source_operations),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .context = &lifecycle,
    .read = validation_source_read,
};

static const struct RibonMonotonicTimerServiceOperations timer_operations = {
    .size = sizeof(timer_operations),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .context = &lifecycle,
    .frequency_hz = 1000000u,
    .now = validation_timer_now,
};

static const struct RibonWatchdogServiceOperations watchdog_operations = {
    .size = sizeof(watchdog_operations),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .context = &lifecycle,
    .arm = validation_watchdog_arm,
};

static const struct RibonPersistentMetadataServiceOperations
    metadata_operations = {
        .size = sizeof(metadata_operations),
        .abi_version = RIBON_SERVICE_ABI_VERSION,
        .context = &lifecycle,
        .read = validation_metadata_read,
        .write = validation_metadata_write,
    };

static const struct RibonStorageFlushServiceOperations flush_operations = {
    .size = sizeof(flush_operations),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .context = &lifecycle,
    .flush = validation_flush,
};

static const struct RibonEnvironmentQuiesceServiceOperations
    quiesce_operations = {
        .size = sizeof(quiesce_operations),
        .abi_version = RIBON_SERVICE_ABI_VERSION,
        .context = &lifecycle,
        .quiesce = validation_quiesce,
    };

static int
source_validate(const struct RibonServiceDescriptor *descriptor)
{
    return descriptor != NULL &&
           descriptor->operations == &source_operations &&
           descriptor->operations_size == sizeof(source_operations) &&
           descriptor->operations_abi == RIBON_SERVICE_ABI_VERSION;
}

static int
timer_validate(const struct RibonServiceDescriptor *descriptor)
{
    return descriptor != NULL &&
           descriptor->operations == &timer_operations &&
           descriptor->operations_size == sizeof(timer_operations) &&
           descriptor->operations_abi == RIBON_SERVICE_ABI_VERSION;
}

static int
metadata_validate(const struct RibonServiceDescriptor *descriptor)
{
    return descriptor != NULL &&
           descriptor->operations == &metadata_operations &&
           descriptor->operations_size == sizeof(metadata_operations) &&
           descriptor->operations_abi == RIBON_SERVICE_ABI_VERSION;
}

static int
flush_validate(const struct RibonServiceDescriptor *descriptor)
{
    return descriptor != NULL &&
           descriptor->operations == &flush_operations &&
           descriptor->operations_size == sizeof(flush_operations) &&
           descriptor->operations_abi == RIBON_SERVICE_ABI_VERSION;
}

static int
quiesce_validate(const struct RibonServiceDescriptor *descriptor)
{
    return descriptor != NULL &&
           descriptor->operations == &quiesce_operations &&
           descriptor->operations_size == sizeof(quiesce_operations) &&
           descriptor->operations_abi == RIBON_SERVICE_ABI_VERSION;
}

#define VALIDATION_SERVICE_FIELDS(kind_value, id_value, capability_value, \
                                  operations_value, validator_value, \
                                  lifetime_value, phase_value, \
                                  input_budget_value) \
    .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC, \
    .size = sizeof(struct RibonServiceDescriptor), \
    .abi_version = RIBON_SERVICE_ABI_VERSION, \
    .kind = (kind_value), \
    .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY, \
    .lifetime = (lifetime_value), \
    .phase = (phase_value), \
    .id = (id_value), \
    .provides = (capability_value), \
    .architecture_mask = RIBON_ARCH_MASK_ALL, \
    .environment_mask = RIBON_ENV_MASK_HOST, \
    .mode_mask = RIBON_MODE_MASK(RIBON_MODE_NORMAL), \
    .arena_budget = 1024u, \
    .input_budget = (input_budget_value), \
    .output_budget = 4096u, \
    .deadline_ms = 30000u, \
    .operations = &(operations_value), \
    .operations_size = sizeof(operations_value), \
    .operations_abi = RIBON_SERVICE_ABI_VERSION, \
    .validate_operations = (validator_value)

const struct RibonServiceDescriptor
    ribon_validation_boot_source_service_descriptor = {
        VALIDATION_SERVICE_FIELDS(
            RIBON_SERVICE_KIND_BOOT_SOURCE,
            "service.validation.boot-source",
            RIBON_CAP_BOOT_SOURCE_READ,
            source_operations,
            source_validate,
            RIBON_SERVICE_LIFETIME_BOOT,
            RIBON_PLUGIN_PHASE_FOUNDATION,
            64ull * 1024ull * 1024ull),
    };

const struct RibonServiceDescriptor
    ribon_validation_environment_quiesce_service_descriptor = {
        VALIDATION_SERVICE_FIELDS(
            RIBON_SERVICE_KIND_ENVIRONMENT_QUIESCE,
            "service.validation.environment-quiesce",
            RIBON_CAP_ENVIRONMENT_QUIESCE,
            quiesce_operations,
            quiesce_validate,
            RIBON_SERVICE_LIFETIME_QUIESCE,
            RIBON_PLUGIN_PHASE_QUIESCE,
            4096u),
    };

const struct RibonServiceDescriptor
    ribon_validation_monotonic_timer_service_descriptor = {
        VALIDATION_SERVICE_FIELDS(
            RIBON_SERVICE_KIND_MONOTONIC_TIMER,
            "service.validation.monotonic-timer",
            RIBON_CAP_MONOTONIC_TIMER,
            timer_operations,
            timer_validate,
            RIBON_SERVICE_LIFETIME_BOOT,
            RIBON_PLUGIN_PHASE_FOUNDATION,
            4096u),
    };

const struct RibonServiceDescriptor
    ribon_validation_persistent_metadata_service_descriptor = {
        VALIDATION_SERVICE_FIELDS(
            RIBON_SERVICE_KIND_PERSISTENT_METADATA,
            "service.validation.persistent-metadata",
            RIBON_CAP_PERSISTENT_METADATA,
            metadata_operations,
            metadata_validate,
            RIBON_SERVICE_LIFETIME_BOOT,
            RIBON_PLUGIN_PHASE_FOUNDATION,
            4096u),
    };

const struct RibonServiceDescriptor
    ribon_validation_storage_flush_service_descriptor = {
        VALIDATION_SERVICE_FIELDS(
            RIBON_SERVICE_KIND_STORAGE_FLUSH,
            "service.validation.storage-flush",
            RIBON_CAP_STORAGE_FLUSH,
            flush_operations,
            flush_validate,
            RIBON_SERVICE_LIFETIME_BOOT,
            RIBON_PLUGIN_PHASE_FOUNDATION,
            4096u),
    };

const struct RibonServiceDescriptor
    ribon_validation_watchdog_service_descriptor = {
        VALIDATION_SERVICE_FIELDS(
            RIBON_SERVICE_KIND_WATCHDOG,
            "service.validation.watchdog",
            RIBON_CAP_WATCHDOG,
            watchdog_operations,
            ribon_watchdog_service_operations_are_valid,
            RIBON_SERVICE_LIFETIME_BOOT,
            RIBON_PLUGIN_PHASE_FOUNDATION,
            4096u),
    };

static const struct RibonServiceDescriptor *const validation_services[] = {
    &ribon_validation_boot_source_service_descriptor,
    &ribon_validation_environment_quiesce_service_descriptor,
    &ribon_validation_monotonic_timer_service_descriptor,
    &ribon_validation_persistent_metadata_service_descriptor,
    &ribon_validation_storage_flush_service_descriptor,
    &ribon_validation_watchdog_service_descriptor,
};

static const struct RibonServiceDirectory validation_directory = {
    .size = sizeof(validation_directory),
    .abi_version = RIBON_SERVICE_DIRECTORY_ABI_VERSION,
    .services = validation_services,
    .service_count =
        (uint32_t)(sizeof(validation_services) / sizeof(validation_services[0])),
};

static int
validation_environment_validate(
    const struct RibonPluginDescriptor *descriptor)
{
    return descriptor != NULL &&
           descriptor->operations == &validation_directory &&
           ribon_environment_plugin_operations_are_valid(descriptor);
}

const struct RibonPluginDescriptor
    ribon_validation_environment_plugin_descriptor = {
        .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
        .size = sizeof(ribon_validation_environment_plugin_descriptor),
        .abi_major = RIBON_PLUGIN_ABI_MAJOR,
        .abi_minor = RIBON_PLUGIN_ABI_MINOR,
        .kind = RIBON_PLUGIN_KIND_ENVIRONMENT,
        .phase = RIBON_PLUGIN_PHASE_FOUNDATION,
        .id = "environment.host",
        .provides =
            RIBON_CAP_BOOT_SOURCE_READ |
            RIBON_CAP_MONOTONIC_TIMER |
            RIBON_CAP_WATCHDOG |
            RIBON_CAP_PERSISTENT_METADATA |
            RIBON_CAP_STORAGE_FLUSH |
            RIBON_CAP_ENVIRONMENT_QUIESCE,
        .requires = RIBON_CAP_ARCHITECTURE,
        .architecture_mask = RIBON_ARCH_MASK_ALL,
        .environment_mask = RIBON_ENV_MASK_HOST,
        .mode_mask = RIBON_MODE_MASK(RIBON_MODE_NORMAL),
        .arena_budget = 4096u,
        .input_budget = 64ull * 1024ull * 1024ull,
        .output_budget = 4096u,
        .deadline_ms = 30000u,
        .operations = &validation_directory,
        .operations_size = sizeof(validation_directory),
        .operations_abi = RIBON_SERVICE_DIRECTORY_ABI_VERSION,
        .validate_operations = validation_environment_validate,
    };

int
ribon_validation_source_bind(const void *data, uint64_t size)
{
    if (data == NULL || size == 0u) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    lifecycle.source = data;
    lifecycle.source_size = size;
    return RIBON_SERVICE_STATUS_OK;
}

void
ribon_validation_lifecycle_reset(void)
{
    const uint8_t *source = lifecycle.source;
    const uint64_t source_size = lifecycle.source_size;

    lifecycle = (struct ValidationLifecycle){
        .source = source,
        .source_size = source_size,
        .timer_ticks = 1u,
    };
}

void
ribon_validation_timer_step_set(uint64_t step)
{
    lifecycle.timer_step = step;
}

uint32_t
ribon_validation_write_count(void)
{
    return lifecycle.writes;
}

uint32_t
ribon_validation_flush_count(void)
{
    return lifecycle.flushes;
}

uint32_t
ribon_validation_quiesce_count(void)
{
    return lifecycle.quiesces;
}

uint32_t
ribon_validation_watchdog_count(void)
{
    return lifecycle.watchdog_arms;
}

int
ribon_validation_environment_collect(
    enum RibonArchitectureId architecture,
    struct RibonBootEnvironment *out)
{
    if (out == NULL) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    ribon_boot_environment_init(out, RIBON_ENVIRONMENT_HOST, architecture);
    out->memory_map.regions = validation_memory_map;
    out->memory_map.region_count =
        (uint32_t)(sizeof(validation_memory_map) /
                   sizeof(validation_memory_map[0]));
    out->boot_media.kind = RIBON_BOOT_MEDIA_FILE;
    out->boot_media.path = "validation.elf";
    out->command_line.text = "policy=ribos validation=qemu";
    out->command_line.length = 28u;
    out->flags =
        RIBON_BOOT_ENV_HAS_MEMORY_MAP |
        RIBON_BOOT_ENV_HAS_BOOT_MEDIA |
        RIBON_BOOT_ENV_HAS_COMMAND_LINE;
    return RIBON_SERVICE_STATUS_OK;
}

static int
validation_signature_is_allowed(
    const struct RibonValidationRibosFixture *fixture,
    const RibosArtifactAuthorizationRequest *request)
{
    uint64_t index;

    if (fixture->reject_signature != 0u ||
        request->envelope_flags != RIBOS_ARTIFACT_ENVELOPE_SIGNED ||
        request->signature_algorithm != RIBOS_ARTIFACT_SIGNATURE_ED25519 ||
        request->key_id_size != sizeof(validation_key_id) - 1u ||
        request->signature_size != RIBOS_ARTIFACT_ED25519_SIGNATURE_BYTES ||
        memcmp(
            request->key_id,
            validation_key_id,
            sizeof(validation_key_id) - 1u) != 0) {
        return 0;
    }
    for (index = 0u; index < request->signature_size; ++index) {
        if (request->signature[index] != VALIDATION_SIGNATURE_BYTE) {
            return 0;
        }
    }
    return 1;
}

uint32_t
ribon_validation_ribos_authorize(
    void *context,
    const RibosArtifactAuthorizationRequest *request,
    RibosArtifactAuthorizationReceipt *receipt)
{
    struct RibonValidationRibosFixture *fixture = context;

    if (fixture == NULL || fixture->binding == NULL ||
        request == NULL || receipt == NULL ||
        !validation_signature_is_allowed(fixture, request)) {
        return RIBOS_VM_STATUS_NOT_AUTHORIZED;
    }
    *receipt = (RibosArtifactAuthorizationReceipt){
        .size = sizeof(*receipt),
        .authorization_major = RIBOS_VM_AUTHORIZATION_V1_MAJOR,
        .authorization_minor = RIBOS_VM_AUTHORIZATION_V1_MINOR,
        .decision = RIBOS_ARTIFACT_AUTHORIZATION_GRANTED,
        .authority_generation = 18u,
        .manifest_sequence = 18u,
        .rollback_floor = 18u,
        .key_identity_digest = {0x18u},
        .policy_identity_digest = {0x52u},
    };
    memcpy(
        receipt->artifact_hash,
        request->artifact_hash,
        RIBOS_VM_DIGEST_BYTES);
    memcpy(
        receipt->schema_digest,
        request->schema_digest,
        RIBOS_VM_DIGEST_BYTES);
    memcpy(
        receipt->helper_execution_digest,
        fixture->binding->helper_contract->digest,
        RIBOS_VM_DIGEST_BYTES);
    return RIBOS_VM_STATUS_OK;
}

void
ribon_validation_ribos_factory_recovery(
    void *context,
    const struct RibonRibosFailureReceipt *receipt)
{
    struct RibonValidationRibosFixture *fixture = context;

    if (fixture == NULL || receipt == NULL) {
        return;
    }
    ++fixture->fallback_calls;
    fixture->last_failure = *receipt;
}

static int
validation_action_payload(
    const struct RibonValidationRibosFixture *fixture,
    uint8_t payload[VALIDATION_ACTION_CAPACITY])
{
    if (fixture == NULL || fixture->boot_action_size < 8u ||
        fixture->boot_action_size > VALIDATION_ACTION_CAPACITY) {
        return 0;
    }
    memset(payload, 0, VALIDATION_ACTION_CAPACITY);
    payload[0] = 'R';
    payload[1] = 'B';
    payload[2] = 'I';
    payload[3] = '1';
    payload[4] = 1u;
    return 1;
}

int
ribon_validation_ribos_validate_boot_action(
    void *context,
    const RibosVmBootAction *action,
    const struct RibonBootTransaction *transaction)
{
    struct RibonValidationRibosFixture *fixture = context;
    uint8_t expected[VALIDATION_ACTION_CAPACITY];

    return fixture != NULL && fixture->reject_action == 0u &&
           action != NULL && action->terminal_helper_id == 21u &&
           action->action_type_id == fixture->boot_action_type &&
           action->payload_size == fixture->boot_action_size &&
           action->payload != NULL && transaction != NULL &&
           transaction->stage == RIBON_BOOT_STAGE_PREPARE_PROTOCOL &&
           validation_action_payload(fixture, expected) &&
           memcmp(action->payload, expected, fixture->boot_action_size) == 0;
}

static int
validation_call_begin(
    struct RibonValidationRibosFixture *fixture,
    RibosVmHelperCall *call,
    uint32_t stable_id)
{
    RibosVmHelperCallInfo info;

    if (fixture == NULL ||
        ribos_vm_helper_call_info_v1(call, &info) != RIBOS_VM_STATUS_OK ||
        info.stable_id != stable_id ||
        ribos_vm_helper_call_consume_operations_v1(call, 1u) !=
            RIBOS_VM_STATUS_OK) {
        return 0;
    }
    ++fixture->helper_calls;
    return 1;
}

static uint32_t
validation_drop(void *context, void *object)
{
    struct RibonValidationRibosFixture *fixture = context;

    if (fixture == NULL || object != &fixture->image_object) {
        return RIBOS_VM_HANDLE_DROP_FAILED;
    }
    ++fixture->drop_calls;
    return RIBOS_VM_HANDLE_DROP_COMPLETE;
}

uint32_t
ribon_validation_ribos_slot_selected(
    void *context,
    const struct RibonServiceDescriptor *service,
    RibosVmHelperCall *call)
{
    struct RibonValidationRibosFixture *fixture = context;

    if (!validation_call_begin(fixture, call, 2u) ||
        service == NULL ||
        service->kind != RIBON_SERVICE_KIND_PERSISTENT_METADATA) {
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    return ribos_vm_helper_call_set_success_handle_v1(
               call,
               fixture->slot_type,
               &fixture->slot_object,
               NULL,
               NULL) == RIBOS_VM_STATUS_OK ?
        RIBOS_VM_HELPER_CALLBACK_OK :
        RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
}

uint32_t
ribon_validation_ribos_slot_image(
    void *context,
    const struct RibonServiceDescriptor *service,
    RibosVmHelperCall *call)
{
    struct RibonValidationRibosFixture *fixture = context;
    void *slot = NULL;

    if (!validation_call_begin(fixture, call, 8u) ||
        service == NULL || service->kind != RIBON_SERVICE_KIND_BOOT_SOURCE ||
        ribos_vm_helper_call_argument_handle_v1(
            call, 0u, fixture->slot_type, &slot) != RIBOS_VM_STATUS_OK ||
        slot != &fixture->slot_object) {
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    return ribos_vm_helper_call_set_success_handle_v1(
               call,
               fixture->image_type,
               &fixture->image_object,
               validation_drop,
               fixture) == RIBOS_VM_STATUS_OK ?
        RIBOS_VM_HELPER_CALLBACK_OK :
        RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
}

uint32_t
ribon_validation_ribos_image_verify(
    void *context,
    const struct RibonServiceDescriptor *service,
    RibosVmHelperCall *call)
{
    struct RibonValidationRibosFixture *fixture = context;
    void *image = NULL;

    if (!validation_call_begin(fixture, call, 11u) || service != NULL ||
        ribos_vm_helper_call_argument_handle_v1(
            call, 0u, fixture->image_type, &image) != RIBOS_VM_STATUS_OK ||
        image != &fixture->image_object) {
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    return ribos_vm_helper_call_set_success_handle_v1(
               call,
               fixture->verified_image_type,
               &fixture->image_object,
               validation_drop,
               fixture) == RIBOS_VM_STATUS_OK ?
        RIBOS_VM_HELPER_CALLBACK_OK :
        RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
}

uint32_t
ribon_validation_ribos_boot_slot(
    void *context,
    const struct RibonServiceDescriptor *service,
    RibosVmHelperCall *call)
{
    struct RibonValidationRibosFixture *fixture = context;
    uint8_t payload[VALIDATION_ACTION_CAPACITY];
    void *slot = NULL;
    void *verified = NULL;

    if (!validation_call_begin(fixture, call, 21u) || service != NULL ||
        ribos_vm_helper_call_argument_handle_v1(
            call, 0u, fixture->slot_type, &slot) != RIBOS_VM_STATUS_OK ||
        ribos_vm_helper_call_argument_handle_v1(
            call, 1u, fixture->verified_image_type, &verified) !=
            RIBOS_VM_STATUS_OK ||
        slot != &fixture->slot_object ||
        verified != &fixture->image_object ||
        ribos_vm_helper_call_mark_consumed_transferred_v1(call) !=
            RIBOS_VM_STATUS_OK ||
        !validation_action_payload(fixture, payload)) {
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    return ribos_vm_helper_call_set_success_value_v1(
               call,
               fixture->boot_action_type,
               payload,
               fixture->boot_action_size) == RIBOS_VM_STATUS_OK ?
        RIBOS_VM_HELPER_CALLBACK_OK :
        RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
}

uint32_t
ribon_validation_ribos_boot_recovery(
    void *context,
    const struct RibonServiceDescriptor *service,
    RibosVmHelperCall *call)
{
    struct RibonValidationRibosFixture *fixture = context;
    uint8_t payload[VALIDATION_ACTION_CAPACITY];

    if (!validation_call_begin(fixture, call, 22u) || service != NULL ||
        !validation_action_payload(fixture, payload)) {
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    payload[4] = 2u;
    return ribos_vm_helper_call_set_success_value_v1(
               call,
               fixture->boot_action_type,
               payload,
               fixture->boot_action_size) == RIBOS_VM_STATUS_OK ?
        RIBOS_VM_HELPER_CALLBACK_OK :
        RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
}

#undef VALIDATION_SERVICE_FIELDS
