#include "ribos/vm/helpers.h"
#include "ribos/vm/interpreter.h"

#include "helpers_internal.h"
#include "storage_internal.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#define RIBOS_VM_HELPER_CALL_MAGIC UINT64_C(0x52424843414c4c31)

typedef struct RibosVmHelperPendingHandle {
    uint32_t type_id;
    void *trusted_object;
    RibosVmHandleDropFn drop;
    void *drop_context;
} RibosVmHelperPendingHandle;

struct RibosVmHelperCall {
    uint64_t magic;
    uint32_t active;
    uint32_t result_kind;
    const RibosPreparedProgram *prepared_program;
    const RibosVmHelperEnvironment *environment;
    RibosVmStorage *storage;
    size_t arena_size;
    const RibosVmHelperDispatchRequest *request;
    const RibosVmHelperBinding *binding;
    uint64_t start_ns;
    uint64_t deadline_ns;
    uint64_t last_now_ns;
    uint64_t operations;
    uint64_t polls;
    uint32_t budget_fault;
    uint32_t consumed_transferred;
    RibosVmHandleBorrow borrows[RIBOS_SCHEMA_MAX_PARAMETERS];
    uint8_t borrow_active[RIBOS_SCHEMA_MAX_PARAMETERS];
    RibosVmHandleConsumeLease consume;
    uint32_t consume_active;
    uint32_t reserved0;
    RibosVmHelperPendingHandle pending_handle;
};

static int
ribos_vm_helper_u64_add(
    uint64_t left,
    uint64_t right,
    uint64_t *result)
{
    if (result == NULL || right > UINT64_MAX - left) {
        return 0;
    }
    *result = left + right;
    return 1;
}

static uint64_t
ribos_vm_helper_min_u64(uint64_t left, uint64_t right)
{
    return left < right ? left : right;
}

static const RibosVmHelperBinding *
ribos_vm_helper_find_binding(
    const RibosVmHelperContract *contract,
    uint32_t stable_id)
{
    uint32_t low = 0;
    uint32_t high;

    if (contract == NULL) {
        return NULL;
    }
    high = contract->binding_count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2;
        uint32_t candidate =
            contract->bindings[middle].execution.stable_id;

        if (candidate == stable_id) {
            return &contract->bindings[middle];
        }
        if (candidate < stable_id) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return NULL;
}

static RibosVmStatus
ribos_vm_helper_now(
    const RibosVmHelperEnvironment *environment,
    uint64_t *now_ns)
{
    uint32_t callback_status;

    if (environment == NULL || environment->embedder == NULL ||
        environment->embedder->monotonic_now_ns == NULL ||
        now_ns == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *now_ns = 0;
    callback_status =
        environment->embedder->monotonic_now_ns(
            environment->embedder->embedder_context,
            now_ns);
    if (callback_status != RIBOS_VM_HELPER_CALLBACK_OK ||
        *now_ns == 0) {
        *now_ns = 0;
        return RIBOS_VM_STATUS_EMBEDDER_REJECTED;
    }
    return RIBOS_VM_STATUS_OK;
}

static int
ribos_vm_helper_table_matches_limits(
    const RibosVmHandleHostTable *table,
    uint32_t maximum_handles)
{
    if (maximum_handles == 0) {
        return table == NULL ||
            (table->size == (uint32_t)sizeof(*table) &&
             table->capacity == 0);
    }
    return table != NULL &&
        table->size == (uint32_t)sizeof(*table) &&
        table->handles_major == RIBOS_VM_HANDLES_V1_MAJOR &&
        table->handles_minor == RIBOS_VM_HANDLES_V1_MINOR &&
        table->flags == 0 &&
        table->capacity == maximum_handles &&
        table->entries != NULL &&
        ribos_vm_reserved_words_are_zero(table->reserved, 4);
}

RibosVmStatus
ribos_vm_helper_environment_validate_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmContext *context,
    const RibosVmHelperEnvironment *environment)
{
    const RibosVmLimits *limits;
    const RibosArtifactView *view;
    const RibosVmHelperContract *prepared_contract;
    uint8_t digest[RIBOS_VM_DIGEST_BYTES];
    RibosVmStatus status;

    if (environment == NULL || context == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    if (environment->size != (uint32_t)sizeof(*environment) ||
        environment->helpers_major != RIBOS_VM_HELPERS_V1_MAJOR ||
        environment->helpers_minor != RIBOS_VM_HELPERS_V1_MINOR) {
        return RIBOS_VM_STATUS_INVALID_SIZE;
    }
    if (environment->flags != 0 || environment->reserved0 != 0 ||
        !ribos_vm_reserved_words_are_zero(environment->reserved, 4)) {
        return RIBOS_VM_STATUS_RESERVED_NONZERO;
    }
    status = ribos_prepared_program_validate_v1(prepared_program);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    limits = ribos_prepared_program_limits_v1(prepared_program);
    view = ribos_prepared_program_artifact_view_v1(prepared_program);
    prepared_contract =
        ribos_prepared_program_helper_contract_v1(prepared_program);
    if (limits == NULL || view == NULL || prepared_contract == NULL) {
        return RIBOS_VM_STATUS_NOT_PREPARED;
    }
    status = ribos_vm_context_validate_v1(context, limits);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_embedder_validate_v1(environment->embedder);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (context->selected_mode !=
            environment->embedder->selected_mode ||
        context->selected_phase !=
            environment->embedder->selected_phase ||
        (view->required_capabilities &
         ~environment->embedder->granted_capabilities) != 0 ||
        !ribos_vm_helper_table_matches_limits(
            environment->handle_table,
            limits->maximum_handles)) {
        return RIBOS_VM_STATUS_EMBEDDER_REJECTED;
    }
    status = ribos_vm_helper_contract_compute_identity_v1(
        environment->embedder->helper_contract,
        digest);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (memcmp(
            digest,
            prepared_contract->digest,
            RIBOS_VM_DIGEST_BYTES) != 0 ||
        memcmp(
            digest,
            environment->embedder->helper_contract->digest,
            RIBOS_VM_DIGEST_BYTES) != 0) {
        return RIBOS_VM_STATUS_DIGEST_MISMATCH;
    }
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_helper_execution_initialize_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmContext *context,
    const RibosVmHelperEnvironment *environment,
    RibosVmStorage *storage,
    size_t arena_size)
{
    const RibosVmLimits *limits;
    const RibosVmHelperContract *contract;
    RibosVmHelperExecutionSnapshot snapshot;
    RibosVmStorageExecutionControl control;
    uint64_t now_ns;
    uint64_t deadline_ns;
    uint64_t remaining_instructions;
    RibosVmStatus status;

    status = ribos_vm_helper_environment_validate_v1(
        prepared_program,
        context,
        environment);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    limits = ribos_prepared_program_limits_v1(prepared_program);
    contract =
        ribos_prepared_program_helper_contract_v1(prepared_program);
    if (limits == NULL || contract == NULL) {
        return RIBOS_VM_STATUS_NOT_PREPARED;
    }
    status = ribos_vm_storage_execution_load_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &control,
        &remaining_instructions);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    (void)remaining_instructions;
    if (control.state != RIBOS_VM_INTERPRETER_READY) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    if (control.context_generation != context->generation ||
        control.context_type_id != context->context_type_id ||
        memcmp(
            control.context_digest,
            context->digest,
            RIBOS_VM_DIGEST_BYTES) != 0) {
        return RIBOS_VM_STATUS_DIGEST_MISMATCH;
    }
    status = ribos_vm_helper_now(environment, &now_ns);
    if (status != RIBOS_VM_STATUS_OK ||
        !ribos_vm_helper_u64_add(
            now_ns,
            limits->maximum_execution_duration_ns,
            &deadline_ns)) {
        return status != RIBOS_VM_STATUS_OK ?
            status : RIBOS_VM_STATUS_LIMIT_EXCEEDED;
    }
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.size = sizeof(snapshot);
    snapshot.helpers_major = RIBOS_VM_HELPERS_V1_MAJOR;
    snapshot.helpers_minor = RIBOS_VM_HELPERS_V1_MINOR;
    snapshot.state = RIBOS_VM_HELPER_EXECUTION_READY;
    snapshot.selected_mode = context->selected_mode;
    snapshot.selected_phase = context->selected_phase;
    snapshot.granted_capabilities =
        environment->embedder->granted_capabilities;
    snapshot.active_helper_id = RIBOS_VM_INVALID_ID;
    snapshot.execution_start_ns = now_ns;
    snapshot.execution_deadline_ns = deadline_ns;
    snapshot.last_now_ns = now_ns;
    snapshot.consumed_input_bytes = context->byte_size;
    snapshot.last_helper_id = RIBOS_VM_INVALID_ID;
    snapshot.context_generation = context->generation;
    memcpy(
        snapshot.context_digest,
        context->digest,
        RIBOS_VM_DIGEST_BYTES);
    memcpy(
        snapshot.helper_execution_digest,
        contract->digest,
        RIBOS_VM_DIGEST_BYTES);
    if (snapshot.consumed_input_bytes >
            limits->maximum_input_bytes) {
        return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
    }
    return ribos_vm_storage_helper_execution_initialize_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &snapshot);
}

RibosVmStatus
ribos_vm_helper_execution_snapshot_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHelperExecutionSnapshot *snapshot)
{
    return ribos_vm_storage_helper_execution_load_internal_v1(
        prepared_program,
        storage,
        arena_size,
        snapshot);
}

static int
ribos_vm_helper_call_is_active(const RibosVmHelperCall *call)
{
    return call != NULL &&
        call->magic == RIBOS_VM_HELPER_CALL_MAGIC &&
        call->active == 1 &&
        call->request != NULL &&
        call->binding != NULL;
}

RibosVmStatus
ribos_vm_helper_call_info_v1(
    const RibosVmHelperCall *call,
    RibosVmHelperCallInfo *info)
{
    const RibosVmHelperExecutionDescriptor *execution;

    if (!ribos_vm_helper_call_is_active(call) || info == NULL) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    execution = &call->binding->execution;
    memset(info, 0, sizeof(*info));
    info->size = sizeof(*info);
    info->helpers_major = RIBOS_VM_HELPERS_V1_MAJOR;
    info->helpers_minor = RIBOS_VM_HELPERS_V1_MINOR;
    info->stable_id = execution->stable_id;
    info->function_id = call->request->function_id;
    info->instruction_id = call->request->instruction_id;
    info->source_map_id = call->request->source_map_id;
    info->argument_count = call->request->argument_count;
    info->result_type_id = call->request->result.type_id;
    info->effect = execution->effect;
    info->durability = execution->durability;
    info->handle_transition = execution->handle_transition;
    info->transition_parameter = execution->transition_parameter;
    info->deadline_ns = call->deadline_ns;
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_helper_call_argument_info_v1(
    const RibosVmHelperCall *call,
    uint32_t ordinal,
    RibosVmHelperArgumentInfo *info)
{
    const RibosVmHelperArgumentInternal *argument;
    const RibosVmHelperExecutionDescriptor *execution;

    if (!ribos_vm_helper_call_is_active(call) || info == NULL ||
        ordinal >= call->request->argument_count) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    argument = &call->request->arguments[ordinal];
    execution = &call->binding->execution;
    memset(info, 0, sizeof(*info));
    info->size = sizeof(*info);
    info->helpers_major = RIBOS_VM_HELPERS_V1_MAJOR;
    info->helpers_minor = RIBOS_VM_HELPERS_V1_MINOR;
    info->ordinal = ordinal;
    info->type_id = argument->type_id;
    info->byte_size = argument->byte_size;
    info->ownership = argument->ownership;
    info->schema_type_class = argument->schema_type_class;
    info->parameter_mode =
        execution->handle_transition !=
                RIBOS_VM_HANDLE_TRANSITION_NONE &&
            execution->handle_transition !=
                RIBOS_VM_HANDLE_TRANSITION_CREATE &&
            execution->transition_parameter == ordinal ?
        RIBOS_SCHEMA_PARAMETER_CONSUME :
        RIBOS_SCHEMA_PARAMETER_BORROW;
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_helper_call_argument_copy_v1(
    const RibosVmHelperCall *call,
    uint32_t ordinal,
    uint32_t expected_type_id,
    uint8_t *output,
    size_t output_capacity,
    size_t *required_size)
{
    const RibosVmHelperArgumentInternal *argument;

    if (!ribos_vm_helper_call_is_active(call) ||
        required_size == NULL ||
        ordinal >= call->request->argument_count) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    argument = &call->request->arguments[ordinal];
    *required_size = argument->byte_size;
    if (argument->type_id != expected_type_id) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    if (argument->byte_size > output_capacity ||
        (argument->byte_size != 0 && output == NULL)) {
        return RIBOS_VM_STATUS_INVALID_SIZE;
    }
    return ribos_vm_storage_slot_slice_read_internal_v1(
        call->prepared_program,
        call->storage,
        call->arena_size,
        call->request->function_id,
        call->request->frame_base,
        argument->slot_id,
        0,
        output,
        argument->byte_size);
}

RibosVmStatus
ribos_vm_helper_call_argument_handle_v1(
    const RibosVmHelperCall *call,
    uint32_t ordinal,
    uint32_t expected_type_id,
    void **trusted_object)
{
    const RibosVmHelperArgumentInternal *argument;
    const RibosVmHelperExecutionDescriptor *execution;

    if (!ribos_vm_helper_call_is_active(call) ||
        trusted_object == NULL ||
        ordinal >= call->request->argument_count) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *trusted_object = NULL;
    argument = &call->request->arguments[ordinal];
    execution = &call->binding->execution;
    if (argument->type_id != expected_type_id ||
        argument->schema_type_class !=
            RIBOS_SCHEMA_TYPE_OPAQUE_HANDLE) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    if (call->consume_active != 0 &&
        execution->transition_parameter == ordinal) {
        if (call->consume.source_type_id != expected_type_id ||
            call->consume.trusted_object == NULL) {
            return RIBOS_VM_STATUS_INVALID_STATE;
        }
        *trusted_object = call->consume.trusted_object;
        return RIBOS_VM_STATUS_OK;
    }
    if (call->borrow_active[ordinal] == 0 ||
        call->borrows[ordinal].type_id != expected_type_id ||
        call->borrows[ordinal].trusted_object == NULL) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    *trusted_object = call->borrows[ordinal].trusted_object;
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_helper_call_consume_operations_v1(
    RibosVmHelperCall *call,
    uint64_t count)
{
    const RibosVmHelperExecutionDescriptor *execution;
    RibosVmStatus status;

    if (!ribos_vm_helper_call_is_active(call) || count == 0) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    execution = &call->binding->execution;
    if (call->operations > execution->maximum_operations ||
        count > execution->maximum_operations - call->operations) {
        call->budget_fault = RIBOS_VM_FAULT_OPERATION_BUDGET;
        return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
    }
    status = ribos_vm_storage_consume_operations_internal_v1(
        call->prepared_program,
        call->storage,
        call->arena_size,
        count);
    if (status != RIBOS_VM_STATUS_OK) {
        call->budget_fault = RIBOS_VM_FAULT_OPERATION_BUDGET;
        return status;
    }
    call->operations += count;
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_helper_call_consume_polls_v1(
    RibosVmHelperCall *call,
    uint64_t count)
{
    const RibosVmHelperExecutionDescriptor *execution;
    uint64_t now_ns;
    RibosVmStatus status;

    if (!ribos_vm_helper_call_is_active(call) || count == 0) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    execution = &call->binding->execution;
    if (call->polls > execution->maximum_polls ||
        count > execution->maximum_polls - call->polls) {
        call->budget_fault = RIBOS_VM_FAULT_POLL_BUDGET;
        return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
    }
    status = ribos_vm_storage_consume_polls_internal_v1(
        call->prepared_program,
        call->storage,
        call->arena_size,
        count);
    if (status != RIBOS_VM_STATUS_OK) {
        call->budget_fault = RIBOS_VM_FAULT_POLL_BUDGET;
        return status;
    }
    call->polls += count;
    status = ribos_vm_helper_now(call->environment, &now_ns);
    if (status != RIBOS_VM_STATUS_OK ||
        now_ns < call->last_now_ns) {
        call->budget_fault = RIBOS_VM_FAULT_EMBEDDER;
        return RIBOS_VM_STATUS_EMBEDDER_REJECTED;
    }
    call->last_now_ns = now_ns;
    if (now_ns > call->deadline_ns) {
        call->budget_fault = RIBOS_VM_FAULT_DEADLINE;
        return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
    }
    return RIBOS_VM_STATUS_OK;
}

static RibosVmStatus
ribos_vm_helper_write_variant_value(
    RibosVmHelperCall *call,
    uint32_t tag,
    uint32_t expected_type_id,
    const uint8_t *bytes,
    size_t byte_size,
    uint32_t result_kind)
{
    const RibosVmHelperResultInternal *result = &call->request->result;
    uint8_t tag_byte = (uint8_t)tag;
    uint32_t payload_offset = 0;
    uint32_t expected_size;
    RibosVmStatus status;

    if (call->result_kind != RIBOS_VM_HELPER_RESULT_NONE) {
        return RIBOS_VM_STATUS_ALREADY_CONSUMED;
    }
    if (tag == 0) {
        expected_size = result->success_byte_size;
    } else {
        expected_size = result->error_byte_size;
    }
    if (expected_type_id == RIBOS_VM_INVALID_ID ||
        byte_size != expected_size ||
        (byte_size != 0 && bytes == NULL)) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    if (result->error_type_id == RIBOS_VM_INVALID_ID) {
        if (tag != 0 || result->type_id != expected_type_id ||
            result->byte_size != expected_size) {
            return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
        }
    } else {
        if ((tag == 0 &&
             result->success_type_id != expected_type_id) ||
            (tag == 1 &&
             result->error_type_id != expected_type_id) ||
            tag > 1 ||
            result->payload_offset > result->byte_size ||
            expected_size >
                result->byte_size - result->payload_offset) {
            return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
        }
        payload_offset = result->payload_offset;
    }
    status = ribos_vm_storage_slot_zero_internal_v1(
        call->prepared_program,
        call->storage,
        call->arena_size,
        call->request->function_id,
        call->request->frame_base,
        result->slot_id);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (result->error_type_id != RIBOS_VM_INVALID_ID) {
        status = ribos_vm_storage_slot_slice_write_internal_v1(
            call->prepared_program,
            call->storage,
            call->arena_size,
            call->request->function_id,
            call->request->frame_base,
            result->slot_id,
            0,
            &tag_byte,
            1);
        if (status != RIBOS_VM_STATUS_OK) {
            return status;
        }
    }
    status = ribos_vm_storage_slot_slice_write_internal_v1(
        call->prepared_program,
        call->storage,
        call->arena_size,
        call->request->function_id,
        call->request->frame_base,
        result->slot_id,
        payload_offset,
        bytes,
        expected_size);
    if (status == RIBOS_VM_STATUS_OK) {
        call->result_kind = result_kind;
    }
    return status;
}

RibosVmStatus
ribos_vm_helper_call_set_success_value_v1(
    RibosVmHelperCall *call,
    uint32_t type_id,
    const uint8_t *bytes,
    size_t byte_size)
{
    if (!ribos_vm_helper_call_is_active(call)) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    if (call->binding->execution.handle_transition ==
            RIBOS_VM_HANDLE_TRANSITION_CREATE ||
        call->binding->execution.handle_transition ==
            RIBOS_VM_HANDLE_TRANSITION_REPLACE) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    return ribos_vm_helper_write_variant_value(
        call,
        0,
        type_id,
        bytes,
        byte_size,
        RIBOS_VM_HELPER_RESULT_SUCCESS_VALUE);
}

RibosVmStatus
ribos_vm_helper_call_set_success_handle_v1(
    RibosVmHelperCall *call,
    uint32_t type_id,
    void *trusted_object,
    RibosVmHandleDropFn drop,
    void *drop_context)
{
    uint32_t ownership;
    uint32_t type_class;
    RibosVmStatus status;

    if (!ribos_vm_helper_call_is_active(call) ||
        trusted_object == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    if (call->result_kind != RIBOS_VM_HELPER_RESULT_NONE ||
        call->request->result.success_type_id != type_id) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    status = ribos_prepared_program_type_semantics_v1(
        call->prepared_program,
        type_id,
        &ownership,
        &type_class);
    if (status != RIBOS_VM_STATUS_OK ||
        type_class != RIBOS_SCHEMA_TYPE_OPAQUE_HANDLE ||
        (ownership == RIBOS_SCHEMA_OWNERSHIP_LINEAR &&
         drop == NULL)) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    call->pending_handle = (RibosVmHelperPendingHandle){
        .type_id = type_id,
        .trusted_object = trusted_object,
        .drop = drop,
        .drop_context = drop_context,
    };
    call->result_kind = RIBOS_VM_HELPER_RESULT_SUCCESS_HANDLE;
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_helper_call_set_policy_error_v1(
    RibosVmHelperCall *call,
    uint32_t error_type_id,
    const uint8_t *bytes,
    size_t byte_size)
{
    if (!ribos_vm_helper_call_is_active(call) ||
        call->request->result.error_type_id == RIBOS_VM_INVALID_ID) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    return ribos_vm_helper_write_variant_value(
        call,
        1,
        error_type_id,
        bytes,
        byte_size,
        RIBOS_VM_HELPER_RESULT_POLICY_ERROR);
}

RibosVmStatus
ribos_vm_helper_call_mark_consumed_transferred_v1(
    RibosVmHelperCall *call)
{
    if (!ribos_vm_helper_call_is_active(call) ||
        call->consume_active == 0) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    if (call->consumed_transferred != 0) {
        return RIBOS_VM_STATUS_ALREADY_CONSUMED;
    }
    call->consumed_transferred = 1;
    return RIBOS_VM_STATUS_OK;
}

static RibosVmStatus
ribos_vm_helper_prepare_handles(RibosVmHelperCall *call)
{
    const RibosVmHelperExecutionDescriptor *execution =
        &call->binding->execution;
    uint32_t index;

    for (index = 0; index < call->request->argument_count; ++index) {
        const RibosVmHelperArgumentInternal *argument =
            &call->request->arguments[index];
        uint8_t token[RIBOS_VM_HANDLE_TOKEN_BYTES_V1];
        RibosVmStatus status;

        if (argument->schema_type_class !=
                RIBOS_SCHEMA_TYPE_OPAQUE_HANDLE) {
            continue;
        }
        status = ribos_vm_storage_slot_slice_read_internal_v1(
            call->prepared_program,
            call->storage,
            call->arena_size,
            call->request->function_id,
            call->request->frame_base,
            argument->slot_id,
            0,
            token,
            sizeof(token));
        if (status != RIBOS_VM_STATUS_OK) {
            return status;
        }
        if (execution->handle_transition !=
                RIBOS_VM_HANDLE_TRANSITION_NONE &&
            execution->handle_transition !=
                RIBOS_VM_HANDLE_TRANSITION_CREATE &&
            execution->transition_parameter == index) {
            status = ribos_vm_handle_consume_begin_v1(
                call->prepared_program,
                call->storage,
                call->arena_size,
                call->environment->handle_table,
                token,
                argument->type_id,
                &call->consume);
            if (status != RIBOS_VM_STATUS_OK) {
                return status;
            }
            call->consume_active = 1;
            status = ribos_vm_storage_slot_mark_moved_v1(
                call->prepared_program,
                call->storage,
                call->arena_size,
                call->request->function_id,
                call->request->frame_base,
                argument->slot_id);
            if (status != RIBOS_VM_STATUS_OK) {
                return status;
            }
        } else {
            status = ribos_vm_handle_borrow_begin_v1(
                call->prepared_program,
                call->storage,
                call->arena_size,
                call->environment->handle_table,
                token,
                argument->type_id,
                &call->borrows[index]);
            if (status != RIBOS_VM_STATUS_OK) {
                return status;
            }
            call->borrow_active[index] = 1;
        }
    }
    return RIBOS_VM_STATUS_OK;
}

static RibosVmStatus
ribos_vm_helper_close_borrows(RibosVmHelperCall *call)
{
    uint32_t index = call->request->argument_count;
    RibosVmStatus result = RIBOS_VM_STATUS_OK;

    while (index != 0) {
        RibosVmStatus status;

        --index;
        if (call->borrow_active[index] == 0) {
            continue;
        }
        status = ribos_vm_handle_borrow_end_v1(
            call->prepared_program,
            call->storage,
            call->arena_size,
            call->environment->handle_table,
            &call->borrows[index]);
        call->borrow_active[index] = 0;
        if (status != RIBOS_VM_STATUS_OK) {
            result = status;
        }
    }
    return result;
}

static RibosVmStatus
ribos_vm_helper_finish_consume(
    RibosVmHelperCall *call,
    int replace,
    uint8_t token[RIBOS_VM_HANDLE_TOKEN_BYTES_V1])
{
    RibosVmStatus status;

    if (call->consume_active == 0) {
        return RIBOS_VM_STATUS_OK;
    }
    if (replace) {
        status = ribos_vm_handle_consume_replace_v1(
            call->prepared_program,
            call->storage,
            call->arena_size,
            call->environment->handle_table,
            &call->consume,
            call->pending_handle.type_id,
            call->pending_handle.trusted_object,
            call->pending_handle.drop,
            call->pending_handle.drop_context,
            token);
    } else {
        status = ribos_vm_handle_consume_finish_v1(
            call->prepared_program,
            call->storage,
            call->arena_size,
            call->environment->handle_table,
            &call->consume,
            call->consumed_transferred != 0 ?
                RIBOS_VM_HANDLE_CONSUME_TRANSFERRED :
                RIBOS_VM_HANDLE_CONSUME_DROP);
    }
    call->consume_active = 0;
    return status;
}

static RibosVmStatus
ribos_vm_helper_write_success_token(
    RibosVmHelperCall *call,
    const uint8_t token[RIBOS_VM_HANDLE_TOKEN_BYTES_V1])
{
    return ribos_vm_helper_write_variant_value(
        call,
        0,
        call->pending_handle.type_id,
        token,
        RIBOS_VM_HANDLE_TOKEN_BYTES_V1,
        RIBOS_VM_HELPER_RESULT_SUCCESS_HANDLE);
}

static void
ribos_vm_helper_release_pending_handle(
    RibosVmHelperCall *call)
{
    if (call->pending_handle.trusted_object == NULL) {
        return;
    }
    if (call->pending_handle.drop != NULL) {
        (void)call->pending_handle.drop(
            call->pending_handle.drop_context,
            call->pending_handle.trusted_object);
    }
    memset(
        &call->pending_handle,
        0,
        sizeof(call->pending_handle));
}

static RibosVmStatus
ribos_vm_helper_commit_result(
    RibosVmHelperCall *call,
    uint32_t callback_status)
{
    const RibosVmHelperExecutionDescriptor *execution =
        &call->binding->execution;
    uint8_t token[RIBOS_VM_HANDLE_TOKEN_BYTES_V1] = {0};
    RibosVmStatus status;

    if (callback_status == RIBOS_VM_HELPER_CALLBACK_POLICY_ERROR) {
        if (call->result_kind !=
                RIBOS_VM_HELPER_RESULT_POLICY_ERROR) {
            return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
        }
        return ribos_vm_helper_finish_consume(call, 0, token);
    }
    if (callback_status != RIBOS_VM_HELPER_CALLBACK_OK) {
        return ribos_vm_helper_finish_consume(call, 0, token);
    }
    if (call->result_kind ==
            RIBOS_VM_HELPER_RESULT_SUCCESS_HANDLE) {
        if (execution->handle_transition ==
                RIBOS_VM_HANDLE_TRANSITION_REPLACE) {
            status = ribos_vm_helper_finish_consume(
                call,
                1,
                token);
        } else {
            status = ribos_vm_handle_create_v1(
                call->prepared_program,
                call->storage,
                call->arena_size,
                call->environment->handle_table,
                call->pending_handle.type_id,
                call->pending_handle.trusted_object,
                call->pending_handle.drop,
                call->pending_handle.drop_context,
                token);
        }
        if (status != RIBOS_VM_STATUS_OK) {
            return status;
        }
        call->result_kind = RIBOS_VM_HELPER_RESULT_NONE;
        status = ribos_vm_helper_write_success_token(call, token);
        if (status != RIBOS_VM_STATUS_OK) {
            (void)ribos_vm_handle_revoke_v1(
                call->prepared_program,
                call->storage,
                call->arena_size,
                call->environment->handle_table,
                token);
        }
        memset(
            &call->pending_handle,
            0,
            sizeof(call->pending_handle));
        return status;
    }
    if (call->result_kind !=
            RIBOS_VM_HELPER_RESULT_SUCCESS_VALUE) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    return ribos_vm_helper_finish_consume(call, 0, token);
}

static void
ribos_vm_helper_fill_fault_result(
    RibosVmHelperDispatchResult *result,
    const RibosVmHelperExecutionSnapshot *snapshot,
    uint32_t helper_id,
    uint32_t effect,
    uint32_t durability,
    uint32_t fault_code,
    uint32_t detail)
{
    memset(result, 0, sizeof(*result));
    result->fault_code = fault_code;
    result->fault_subject = RIBOS_VM_FAULT_SUBJECT_HELPER;
    result->fault_detail = detail;
    result->helper_id = helper_id;
    result->last_effect = effect;
    result->last_durability = durability;
    if (snapshot != NULL) {
        result->consumed_helper_calls =
            snapshot->consumed_helper_calls;
        result->consumed_input_bytes =
            snapshot->consumed_input_bytes;
        result->consumed_output_bytes =
            snapshot->consumed_output_bytes;
        result->consumed_operations =
            snapshot->consumed_operations;
        result->consumed_polls = snapshot->consumed_polls;
        result->elapsed_ns =
            snapshot->last_now_ns >=
                    snapshot->execution_start_ns ?
                snapshot->last_now_ns -
                    snapshot->execution_start_ns :
                0;
    }
}

static RibosVmStatus
ribos_vm_helper_store_pre_callback_fault(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHelperExecutionSnapshot *snapshot,
    const RibosVmHelperExecutionDescriptor *execution,
    uint64_t now_ns)
{
    snapshot->state = RIBOS_VM_HELPER_EXECUTION_FAULTED;
    snapshot->callback_active = 0;
    snapshot->active_helper_id = RIBOS_VM_INVALID_ID;
    if (now_ns >= snapshot->last_now_ns) {
        snapshot->last_now_ns = now_ns;
    }
    if (execution != NULL) {
        if (snapshot->receipt_sequence == UINT64_MAX) {
            return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
        }
        ++snapshot->receipt_sequence;
        snapshot->last_helper_id = execution->stable_id;
        snapshot->last_callback_status =
            RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
        snapshot->last_effect = execution->effect;
        snapshot->last_durability = execution->durability;
        snapshot->last_handle_transition =
            execution->handle_transition;
        snapshot->last_result_kind =
            RIBOS_VM_HELPER_RESULT_NONE;
        snapshot->last_input_bytes = 0;
        snapshot->last_output_bytes = 0;
        snapshot->last_operations = 0;
        snapshot->last_polls = 0;
        snapshot->last_duration_ns = 0;
    }
    return ribos_vm_storage_helper_execution_store_internal_v1(
        prepared_program,
        storage,
        arena_size,
        snapshot);
}

static RibosVmStatus
ribos_vm_helper_environment_matches_snapshot(
    const RibosPreparedProgram *prepared_program,
    const RibosVmContext *context,
    const RibosVmHelperEnvironment *environment,
    const RibosVmHelperExecutionSnapshot *snapshot)
{
    const RibosVmHelperContract *contract =
        ribos_prepared_program_helper_contract_v1(prepared_program);
    RibosVmStatus status =
        ribos_vm_helper_environment_validate_v1(
            prepared_program,
            context,
            environment);

    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (contract == NULL ||
        snapshot->context_generation != context->generation ||
        snapshot->selected_mode != context->selected_mode ||
        snapshot->selected_phase != context->selected_phase ||
        snapshot->granted_capabilities !=
            environment->embedder->granted_capabilities ||
        memcmp(
            snapshot->context_digest,
            context->digest,
            RIBOS_VM_DIGEST_BYTES) != 0 ||
        memcmp(
            snapshot->helper_execution_digest,
            contract->digest,
            RIBOS_VM_DIGEST_BYTES) != 0) {
        return RIBOS_VM_STATUS_DIGEST_MISMATCH;
    }
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_helper_dispatch_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmContext *context,
    const RibosVmHelperEnvironment *environment,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmHelperDispatchRequest *request,
    RibosVmHelperDispatchResult *result)
{
    const RibosVmLimits *limits;
    const RibosVmHelperContract *contract;
    const RibosVmHelperBinding *binding;
    const RibosVmHelperExecutionDescriptor *execution;
    RibosVmHelperExecutionSnapshot snapshot;
    RibosVmHelperCall call;
    uint64_t input_bytes = 0;
    uint64_t output_bytes;
    uint64_t now_ns;
    uint64_t helper_deadline;
    uint64_t per_helper_deadline;
    uint32_t callback_status =
        RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    uint32_t index;
    uint32_t fault_code = RIBOS_VM_FAULT_NONE;
    RibosVmStatus status;

    if (request == NULL || result == NULL ||
        request->argument_count > RIBOS_SCHEMA_MAX_PARAMETERS ||
        request->stable_id == RIBOS_VM_INVALID_ID) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    memset(result, 0, sizeof(*result));
    status = ribos_vm_storage_helper_execution_load_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &snapshot);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_helper_environment_matches_snapshot(
        prepared_program,
        context,
        environment,
        &snapshot);
    if (status != RIBOS_VM_STATUS_OK) {
        RibosVmStatus store_status =
            ribos_vm_helper_store_pre_callback_fault(
                prepared_program,
                storage,
                arena_size,
                &snapshot,
                NULL,
                snapshot.last_now_ns);

        if (store_status != RIBOS_VM_STATUS_OK) {
            return store_status;
        }
        ribos_vm_helper_fill_fault_result(
            result,
            &snapshot,
            request->stable_id,
            RIBOS_VM_HELPER_EFFECT_NONE,
            RIBOS_VM_HELPER_DURABILITY_NONE,
            status == RIBOS_VM_STATUS_EMBEDDER_REJECTED ?
                RIBOS_VM_FAULT_CAPABILITY :
                RIBOS_VM_FAULT_HELPER_CONTRACT,
            request->stable_id);
        return RIBOS_VM_STATUS_OK;
    }
    if (snapshot.state != RIBOS_VM_HELPER_EXECUTION_READY ||
        snapshot.callback_active != 0) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    limits = ribos_prepared_program_limits_v1(prepared_program);
    contract =
        ribos_prepared_program_helper_contract_v1(prepared_program);
    binding = ribos_vm_helper_find_binding(
        contract,
        request->stable_id);
    if (limits == NULL || binding == NULL) {
        status = ribos_vm_helper_store_pre_callback_fault(
            prepared_program,
            storage,
            arena_size,
            &snapshot,
            NULL,
            snapshot.last_now_ns);
        if (status != RIBOS_VM_STATUS_OK) {
            return status;
        }
        ribos_vm_helper_fill_fault_result(
            result,
            &snapshot,
            request->stable_id,
            RIBOS_VM_HELPER_EFFECT_NONE,
            RIBOS_VM_HELPER_DURABILITY_NONE,
            RIBOS_VM_FAULT_HELPER_CONTRACT,
            request->stable_id);
        return RIBOS_VM_STATUS_OK;
    }
    execution = &binding->execution;
    if ((execution->required_capabilities &
         ~snapshot.granted_capabilities) != 0) {
        fault_code = RIBOS_VM_FAULT_CAPABILITY;
    } else if ((execution->allowed_mode_mask &
                (UINT64_C(1) << snapshot.selected_mode)) == 0 ||
               (execution->allowed_phase_mask &
                (UINT64_C(1) << snapshot.selected_phase)) == 0) {
        fault_code = RIBOS_VM_FAULT_MODE_PHASE;
    }
    for (index = 0;
         fault_code == RIBOS_VM_FAULT_NONE &&
         index < request->argument_count;
         ++index) {
        if (!ribos_vm_helper_u64_add(
                input_bytes,
                request->arguments[index].byte_size,
                &input_bytes)) {
            fault_code = RIBOS_VM_FAULT_HELPER_CONTRACT;
        }
    }
    output_bytes = request->result.byte_size;
    if (fault_code == RIBOS_VM_FAULT_NONE &&
        (input_bytes > execution->maximum_input_bytes ||
         output_bytes > execution->maximum_output_bytes ||
         snapshot.consumed_input_bytes >
             limits->maximum_input_bytes ||
         snapshot.consumed_output_bytes >
             limits->maximum_output_bytes ||
         input_bytes >
             limits->maximum_input_bytes -
                 snapshot.consumed_input_bytes ||
         output_bytes >
             limits->maximum_output_bytes -
                 snapshot.consumed_output_bytes)) {
        fault_code = RIBOS_VM_FAULT_HELPER_CONTRACT;
    }
    status = ribos_vm_helper_now(environment, &now_ns);
    if (fault_code == RIBOS_VM_FAULT_NONE &&
        (status != RIBOS_VM_STATUS_OK ||
         now_ns < snapshot.last_now_ns)) {
        fault_code = RIBOS_VM_FAULT_EMBEDDER;
    }
    if (fault_code == RIBOS_VM_FAULT_NONE &&
        now_ns > snapshot.execution_deadline_ns) {
        fault_code = RIBOS_VM_FAULT_DEADLINE;
    }
    if (fault_code == RIBOS_VM_FAULT_NONE &&
        (!ribos_vm_helper_u64_add(
             now_ns,
             execution->maximum_duration_ns,
             &per_helper_deadline) ||
         !ribos_vm_helper_u64_add(
             now_ns,
             limits->maximum_helper_duration_ns,
             &helper_deadline))) {
        fault_code = RIBOS_VM_FAULT_DEADLINE;
    }
    if (fault_code != RIBOS_VM_FAULT_NONE) {
        snapshot.last_now_ns =
            now_ns >= snapshot.last_now_ns ?
                now_ns : snapshot.last_now_ns;
        status = ribos_vm_helper_store_pre_callback_fault(
            prepared_program,
            storage,
            arena_size,
            &snapshot,
            execution,
            snapshot.last_now_ns);
        if (status != RIBOS_VM_STATUS_OK) {
            return status;
        }
        ribos_vm_helper_fill_fault_result(
            result,
            &snapshot,
            request->stable_id,
            execution->effect,
            execution->durability,
            fault_code,
            request->stable_id);
        return RIBOS_VM_STATUS_OK;
    }
    helper_deadline = ribos_vm_helper_min_u64(
        helper_deadline,
        per_helper_deadline);
    helper_deadline = ribos_vm_helper_min_u64(
        helper_deadline,
        snapshot.execution_deadline_ns);
    status = ribos_vm_storage_consume_helper_call_internal_v1(
        prepared_program,
        storage,
        arena_size,
        request->stable_id);
    if (status != RIBOS_VM_STATUS_OK) {
        RibosVmStatus store_status =
            ribos_vm_helper_store_pre_callback_fault(
                prepared_program,
                storage,
                arena_size,
                &snapshot,
                execution,
                snapshot.last_now_ns);

        if (store_status != RIBOS_VM_STATUS_OK) {
            return store_status;
        }
        ribos_vm_helper_fill_fault_result(
            result,
            &snapshot,
            request->stable_id,
            execution->effect,
            execution->durability,
            status == RIBOS_VM_STATUS_LIMIT_EXCEEDED ?
                RIBOS_VM_FAULT_HELPER_BUDGET :
                RIBOS_VM_FAULT_HELPER_CONTRACT,
            request->stable_id);
        return RIBOS_VM_STATUS_OK;
    }
    ++snapshot.consumed_helper_calls;
    snapshot.consumed_input_bytes += input_bytes;
    snapshot.consumed_output_bytes += output_bytes;
    snapshot.last_now_ns = now_ns;
    snapshot.state = RIBOS_VM_HELPER_EXECUTION_CALLBACK_ACTIVE;
    snapshot.callback_active = 1;
    snapshot.active_helper_id = request->stable_id;
    status = ribos_vm_storage_helper_execution_store_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &snapshot);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }

    memset(&call, 0, sizeof(call));
    call.magic = RIBOS_VM_HELPER_CALL_MAGIC;
    call.active = 1;
    call.prepared_program = prepared_program;
    call.environment = environment;
    call.storage = storage;
    call.arena_size = arena_size;
    call.request = request;
    call.binding = binding;
    call.start_ns = now_ns;
    call.deadline_ns = helper_deadline;
    call.last_now_ns = now_ns;
    status = ribos_vm_helper_prepare_handles(&call);
    if (status == RIBOS_VM_STATUS_OK) {
        callback_status = binding->invoke(
            environment->embedder->embedder_context,
            &call);
    } else {
        callback_status =
            RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
        call.budget_fault = RIBOS_VM_FAULT_HANDLE_VIOLATION;
    }
    call.active = 0;
    status = ribos_vm_helper_now(environment, &now_ns);
    if (status != RIBOS_VM_STATUS_OK ||
        now_ns < call.last_now_ns) {
        call.budget_fault = RIBOS_VM_FAULT_EMBEDDER;
        now_ns = call.last_now_ns;
    } else if (now_ns > helper_deadline) {
        call.budget_fault = RIBOS_VM_FAULT_DEADLINE;
    }
    if (ribos_vm_helper_close_borrows(&call) !=
            RIBOS_VM_STATUS_OK &&
        call.budget_fault == RIBOS_VM_FAULT_NONE) {
        call.budget_fault = RIBOS_VM_FAULT_HANDLE_VIOLATION;
    }
    if (callback_status >
            RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT &&
        call.budget_fault == RIBOS_VM_FAULT_NONE) {
        call.budget_fault = RIBOS_VM_FAULT_HELPER_CONTRACT;
    }
    if (call.budget_fault == RIBOS_VM_FAULT_NONE) {
        status = ribos_vm_helper_commit_result(
            &call,
            callback_status);
        if (status != RIBOS_VM_STATUS_OK) {
            call.budget_fault =
                status == RIBOS_VM_STATUS_EMBEDDER_REJECTED ?
                    RIBOS_VM_FAULT_EMBEDDER :
                    status == RIBOS_VM_STATUS_LIMIT_EXCEEDED ?
                        RIBOS_VM_FAULT_HELPER_BUDGET :
                        RIBOS_VM_FAULT_HELPER_CONTRACT;
        }
    } else if (call.consume_active != 0) {
        (void)ribos_vm_helper_finish_consume(
            &call,
            0,
            (uint8_t[RIBOS_VM_HANDLE_TOKEN_BYTES_V1]){0});
    }
    ribos_vm_helper_release_pending_handle(&call);

    snapshot.state = call.budget_fault == RIBOS_VM_FAULT_NONE ?
        RIBOS_VM_HELPER_EXECUTION_READY :
        RIBOS_VM_HELPER_EXECUTION_FAULTED;
    snapshot.callback_active = 0;
    snapshot.active_helper_id = RIBOS_VM_INVALID_ID;
    snapshot.last_now_ns = now_ns;
    snapshot.consumed_operations += call.operations;
    snapshot.consumed_polls += call.polls;
    if (snapshot.receipt_sequence != UINT64_MAX) {
        ++snapshot.receipt_sequence;
    } else if (call.budget_fault == RIBOS_VM_FAULT_NONE) {
        call.budget_fault = RIBOS_VM_FAULT_HELPER_CONTRACT;
        snapshot.state = RIBOS_VM_HELPER_EXECUTION_FAULTED;
    }
    snapshot.last_helper_id = request->stable_id;
    snapshot.last_callback_status = callback_status;
    snapshot.last_effect = execution->effect;
    snapshot.last_durability = execution->durability;
    snapshot.last_handle_transition = execution->handle_transition;
    snapshot.last_result_kind = call.result_kind;
    snapshot.last_input_bytes = input_bytes;
    snapshot.last_output_bytes = output_bytes;
    snapshot.last_operations = call.operations;
    snapshot.last_polls = call.polls;
    snapshot.last_duration_ns =
        now_ns >= call.start_ns ?
            now_ns - call.start_ns : 0;
    status = ribos_vm_storage_helper_execution_store_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &snapshot);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (call.budget_fault != RIBOS_VM_FAULT_NONE ||
        callback_status ==
            RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT) {
        ribos_vm_helper_fill_fault_result(
            result,
            &snapshot,
            request->stable_id,
            execution->effect,
            execution->durability,
            call.budget_fault != RIBOS_VM_FAULT_NONE ?
                call.budget_fault :
                RIBOS_VM_FAULT_HELPER_CONTRACT,
            request->stable_id);
    }
    return RIBOS_VM_STATUS_OK;
}
