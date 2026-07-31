#include "ribos_policy.h"

#include <ribos/artifact/format.h>
#include <ribos/vm/handles.h>
#include <ribos/vm/helpers.h>
#include <ribos/vm/prepared.h>

#include <string.h>

#define RIBON_HOST_RIBOS_ACTION_CAPACITY 64u

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

uint32_t
ribon_host_ribos_authorize(
    void *context,
    const RibosArtifactAuthorizationRequest *request,
    RibosArtifactAuthorizationReceipt *receipt)
{
    struct RibonHostRibosFixture *fixture = context;

    if (fixture == NULL || fixture->binding == NULL ||
        request == NULL || receipt == NULL ||
        (fixture->reject_unsigned != 0u &&
         (request->envelope_flags &
          RIBOS_ARTIFACT_ENVELOPE_SIGNED) == 0u)) {
        return RIBOS_VM_STATUS_NOT_AUTHORIZED;
    }
    *receipt = (RibosArtifactAuthorizationReceipt){
        .size = sizeof(*receipt),
        .authorization_major = RIBOS_VM_AUTHORIZATION_V1_MAJOR,
        .authorization_minor = RIBOS_VM_AUTHORIZATION_V1_MINOR,
        .decision = RIBOS_ARTIFACT_AUTHORIZATION_GRANTED,
        .authority_generation = 1u,
        .manifest_sequence = 1u,
        .rollback_floor = 1u,
        .key_identity_digest = {0x48u},
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
