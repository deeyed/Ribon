#include "ribos_policy.h"

#include <ribos/vm/handles.h>
#include <ribos/vm/helpers.h>
#include <ribos/vm/prepared.h>

#include <string.h>

#define RIBON_HOST_RIBOS_ACTION_CAPACITY 64u

struct RibonHostProtectedState {
    uint8_t durable[2][2][RIBON_PROTECTED_STATE_RECORD_BYTES];
    uint8_t pending[2][2][RIBON_PROTECTED_STATE_RECORD_BYTES];
    uint8_t pending_valid[2][2];
};

static struct RibonHostProtectedState ribon_host_protected_state;

/** @brief Host reference provider가 nonzero domain만 받도록 검사한다. */
static int
ribon_host_protected_domain_is_valid(const uint8_t *domain)
{
    uint32_t index;
    uint8_t value = 0u;

    if (domain == NULL) {
        return 0;
    }
    for (index = 0u; index < RIBON_PROTECTED_STATE_DIGEST_BYTES; ++index) {
        value |= domain[index];
    }
    return value != 0u;
}

/** @brief Host durable array에서 exact logical object를 읽는다. */
static int
ribon_host_protected_read(
    const struct RibonProtectedStateProvider *provider,
    const uint8_t domain[RIBON_PROTECTED_STATE_DIGEST_BYTES],
    enum RibonProtectedStateObject object,
    uint32_t slot,
    uint8_t *bytes,
    size_t size)
{
    const uint32_t object_index = (uint32_t)object - 1u;

    if (provider == NULL || provider->context != &ribon_host_protected_state ||
        !ribon_host_protected_domain_is_valid(domain) || bytes == NULL ||
        object_index >= 2u || slot >= 2u ||
        size != RIBON_PROTECTED_STATE_RECORD_BYTES) {
        return RIBON_PROTECTED_STATE_PROVIDER_IO_ERROR;
    }
    memcpy(bytes, ribon_host_protected_state.durable[object_index][slot], size);
    return RIBON_PROTECTED_STATE_PROVIDER_OK;
}

/** @brief Host pending array에 exact logical object를 쓴다. */
static int
ribon_host_protected_write(
    const struct RibonProtectedStateProvider *provider,
    const uint8_t domain[RIBON_PROTECTED_STATE_DIGEST_BYTES],
    enum RibonProtectedStateObject object,
    uint32_t slot,
    const uint8_t *bytes,
    size_t size)
{
    const uint32_t object_index = (uint32_t)object - 1u;

    if (provider == NULL || provider->context != &ribon_host_protected_state ||
        !ribon_host_protected_domain_is_valid(domain) || bytes == NULL ||
        object_index >= 2u || slot >= 2u ||
        size != RIBON_PROTECTED_STATE_RECORD_BYTES) {
        return RIBON_PROTECTED_STATE_PROVIDER_IO_ERROR;
    }
    memcpy(ribon_host_protected_state.pending[object_index][slot], bytes, size);
    ribon_host_protected_state.pending_valid[object_index][slot] = 1u;
    return RIBON_PROTECTED_STATE_PROVIDER_OK;
}

/** @brief Host pending array를 durable array에 원자적 fixture 단위로 반영한다. */
static int
ribon_host_protected_flush(
    const struct RibonProtectedStateProvider *provider,
    const uint8_t domain[RIBON_PROTECTED_STATE_DIGEST_BYTES])
{
    uint32_t object;
    uint32_t slot;

    if (provider == NULL || provider->context != &ribon_host_protected_state ||
        !ribon_host_protected_domain_is_valid(domain)) {
        return RIBON_PROTECTED_STATE_PROVIDER_IO_ERROR;
    }
    for (object = 0u; object < 2u; ++object) {
        for (slot = 0u; slot < 2u; ++slot) {
            if (ribon_host_protected_state.pending_valid[object][slot] == 0u) {
                continue;
            }
            memcpy(ribon_host_protected_state.durable[object][slot],
                   ribon_host_protected_state.pending[object][slot],
                   RIBON_PROTECTED_STATE_RECORD_BYTES);
            ribon_host_protected_state.pending_valid[object][slot] = 0u;
        }
    }
    return RIBON_PROTECTED_STATE_PROVIDER_OK;
}

const struct RibonProtectedStateProvider
ribon_host_protected_state_provider_descriptor = {
    .magic = RIBON_PROTECTED_STATE_PROVIDER_MAGIC,
    .size = sizeof(ribon_host_protected_state_provider_descriptor),
    .abi_version = RIBON_PROTECTED_STATE_ABI_VERSION,
    .provider_class = RIBON_PROTECTED_STATE_PROVIDER_CLASS_REFERENCE,
    .record_slots = RIBON_PROTECTED_STATE_RECORD_SLOTS,
    .selector_slots = RIBON_PROTECTED_STATE_RECORD_SLOTS,
    .record_bytes = RIBON_PROTECTED_STATE_RECORD_BYTES,
    .selector_bytes = RIBON_PROTECTED_STATE_SELECTOR_BYTES,
    .id = "security.protected-state.reference.host-reference",
    .context = &ribon_host_protected_state,
    .read = ribon_host_protected_read,
    .write = ribon_host_protected_write,
    .flush = ribon_host_protected_flush,
};

int
ribon_host_ribos_protected_state_provision(
    const struct RibonRibosProductBinding *binding,
    uint64_t confirmed_floor)
{
    struct RibonProtectedStateJournal journal;
    struct RibonProtectedStateSnapshot snapshot;

    memset(&ribon_host_protected_state, 0, sizeof(ribon_host_protected_state));
    if (binding == NULL || binding->signed_policy == NULL ||
        ribon_protected_state_journal_bind(
            binding->signed_policy->protected_state,
            binding->signed_policy->rollback_domain_digest,
            &journal) != RIBON_PROTECTED_STATE_STATUS_OK) {
        return 0;
    }
    return ribon_protected_state_initialize(
               &journal,
               confirmed_floor,
               &snapshot) == RIBON_PROTECTED_STATE_STATUS_OK;
}

void
ribon_host_ribos_protected_state_corrupt(void)
{
    uint32_t object;
    uint32_t slot;

    for (object = 0u; object < 2u; ++object) {
        for (slot = 0u; slot < 2u; ++slot) {
            ribon_host_protected_state.durable[object][slot][0] ^= 0x80u;
        }
    }
}

static uint32_t
ribon_host_ribos_drop(void *context, void *object)
{
    struct RibonHostRibosFixture *fixture = context;

    if (fixture == NULL || object != &fixture->image_object) {
        return RIBOS_VM_HANDLE_DROP_FAILED;
    }
    ++fixture->drop_calls;
    return RIBOS_VM_HANDLE_DROP_COMPLETE;
}

void
ribon_host_ribos_factory_recovery(
    void *context,
    const struct RibonRibosFailureReceipt *receipt)
{
    struct RibonHostRibosFixture *fixture = context;

    if (fixture == NULL || receipt == NULL) {
        return;
    }
    ++fixture->fallback_calls;
    fixture->last_failure = *receipt;
}

static int
ribon_host_ribos_action_payload(
    const struct RibonHostRibosFixture *fixture,
    uint8_t payload[RIBON_HOST_RIBOS_ACTION_CAPACITY])
{
    if (fixture == NULL || fixture->boot_action_size < 8u ||
        fixture->boot_action_size > RIBON_HOST_RIBOS_ACTION_CAPACITY) {
        return 0;
    }
    memset(payload, 0, RIBON_HOST_RIBOS_ACTION_CAPACITY);
    payload[0] = 'R';
    payload[1] = 'B';
    payload[2] = 'I';
    payload[3] = '1';
    payload[4] = 1u;
    return 1;
}

int
ribon_host_ribos_validate_boot_action(
    void *context,
    const RibosVmBootAction *action,
    const struct RibonBootTransaction *transaction)
{
    struct RibonHostRibosFixture *fixture = context;
    uint8_t expected[RIBON_HOST_RIBOS_ACTION_CAPACITY];

    return fixture != NULL &&
           fixture->reject_action == 0u &&
           action != NULL &&
           action->terminal_helper_id == 21u &&
           action->action_type_id == fixture->boot_action_type &&
           action->payload_size == fixture->boot_action_size &&
           action->payload != NULL &&
           transaction != NULL &&
           transaction->stage == RIBON_BOOT_STAGE_PREPARE_PROTOCOL &&
           ribon_host_ribos_action_payload(fixture, expected) &&
           memcmp(action->payload, expected, fixture->boot_action_size) == 0;
}

static int
ribon_host_ribos_call_begin(
    struct RibonHostRibosFixture *fixture,
    RibosVmHelperCall *call,
    uint32_t stable_id)
{
    RibosVmHelperCallInfo info;

    if (fixture == NULL ||
        ribos_vm_helper_call_info_v1(call, &info) !=
            RIBOS_VM_STATUS_OK ||
        info.stable_id != stable_id ||
        ribos_vm_helper_call_consume_operations_v1(call, 1u) !=
            RIBOS_VM_STATUS_OK) {
        return 0;
    }
    ++fixture->helper_calls;
    return 1;
}

uint32_t
ribon_host_ribos_slot_selected(
    void *context,
    const struct RibonServiceDescriptor *service,
    RibosVmHelperCall *call)
{
    struct RibonHostRibosFixture *fixture = context;

    if (!ribon_host_ribos_call_begin(fixture, call, 2u) ||
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
ribon_host_ribos_slot_image(
    void *context,
    const struct RibonServiceDescriptor *service,
    RibosVmHelperCall *call)
{
    struct RibonHostRibosFixture *fixture = context;
    void *slot = NULL;

    if (!ribon_host_ribos_call_begin(fixture, call, 8u) ||
        service == NULL ||
        service->kind != RIBON_SERVICE_KIND_BOOT_SOURCE ||
        ribos_vm_helper_call_argument_handle_v1(
            call,
            0u,
            fixture->slot_type,
            &slot) != RIBOS_VM_STATUS_OK ||
        slot != &fixture->slot_object) {
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    return ribos_vm_helper_call_set_success_handle_v1(
               call,
               fixture->image_type,
               &fixture->image_object,
               ribon_host_ribos_drop,
               fixture) == RIBOS_VM_STATUS_OK ?
        RIBOS_VM_HELPER_CALLBACK_OK :
        RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
}

uint32_t
ribon_host_ribos_image_verify(
    void *context,
    const struct RibonServiceDescriptor *service,
    RibosVmHelperCall *call)
{
    struct RibonHostRibosFixture *fixture = context;
    void *image = NULL;

    if (!ribon_host_ribos_call_begin(fixture, call, 11u) ||
        service != NULL ||
        ribos_vm_helper_call_argument_handle_v1(
            call,
            0u,
            fixture->image_type,
            &image) != RIBOS_VM_STATUS_OK ||
        image != &fixture->image_object) {
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    return ribos_vm_helper_call_set_success_handle_v1(
               call,
               fixture->verified_image_type,
               &fixture->image_object,
               ribon_host_ribos_drop,
               fixture) == RIBOS_VM_STATUS_OK ?
        RIBOS_VM_HELPER_CALLBACK_OK :
        RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
}

uint32_t
ribon_host_ribos_boot_slot(
    void *context,
    const struct RibonServiceDescriptor *service,
    RibosVmHelperCall *call)
{
    struct RibonHostRibosFixture *fixture = context;
    uint8_t payload[RIBON_HOST_RIBOS_ACTION_CAPACITY];
    void *slot = NULL;
    void *verified = NULL;

    if (!ribon_host_ribos_call_begin(fixture, call, 21u) ||
        service != NULL ||
        ribos_vm_helper_call_argument_handle_v1(
            call,
            0u,
            fixture->slot_type,
            &slot) != RIBOS_VM_STATUS_OK ||
        ribos_vm_helper_call_argument_handle_v1(
            call,
            1u,
            fixture->verified_image_type,
            &verified) != RIBOS_VM_STATUS_OK ||
        slot != &fixture->slot_object ||
        verified != &fixture->image_object ||
        ribos_vm_helper_call_mark_consumed_transferred_v1(call) !=
            RIBOS_VM_STATUS_OK ||
        !ribon_host_ribos_action_payload(fixture, payload)) {
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
ribon_host_ribos_boot_recovery(
    void *context,
    const struct RibonServiceDescriptor *service,
    RibosVmHelperCall *call)
{
    struct RibonHostRibosFixture *fixture = context;
    uint8_t payload[RIBON_HOST_RIBOS_ACTION_CAPACITY];

    if (!ribon_host_ribos_call_begin(fixture, call, 22u) ||
        service != NULL ||
        !ribon_host_ribos_action_payload(fixture, payload)) {
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
