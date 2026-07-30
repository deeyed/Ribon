#include "ribos/vm/terminal.h"

#include "internal.h"
#include "storage_internal.h"
#include "terminal_internal.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#define RIBOS_VM_TERMINAL_HASH_DOMAIN "RIBOS-TERMINAL-V1"
#define RIBOS_VM_JOURNAL_HASH_DOMAIN "RIBOS-JOURNAL-V1"
#define RIBOS_VM_FAULT_HASH_DOMAIN "RIBOS-FAULT-CLOSURE-V1"

typedef struct RibosVmTerminalType {
    uint32_t id;
    uint16_t kind;
    uint32_t first_type;
    uint32_t second_type;
    uint32_t byte_size;
    uint32_t payload_offset;
} RibosVmTerminalType;

static uint16_t
ribos_vm_terminal_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] |
        ((uint16_t)bytes[1] << 8);
}

static uint32_t
ribos_vm_terminal_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

static void
ribos_vm_terminal_hash_u32(
    RibosArtifactSha256 *hash,
    uint32_t value)
{
    uint8_t bytes[4];

    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
    ribos_artifact_sha256_update(hash, bytes, sizeof(bytes));
}

static void
ribos_vm_terminal_hash_u64(
    RibosArtifactSha256 *hash,
    uint64_t value)
{
    uint8_t bytes[8];

    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
    bytes[4] = (uint8_t)(value >> 32);
    bytes[5] = (uint8_t)(value >> 40);
    bytes[6] = (uint8_t)(value >> 48);
    bytes[7] = (uint8_t)(value >> 56);
    ribos_artifact_sha256_update(hash, bytes, sizeof(bytes));
}

static const uint8_t *
ribos_vm_terminal_row(
    const RibosArtifactSectionView *section,
    uint32_t index)
{
    size_t offset;

    if (section == NULL || index >= section->count ||
        section->row_size == 0 ||
        (size_t)index > SIZE_MAX / section->row_size) {
        return NULL;
    }
    offset = (size_t)index * section->row_size;
    if (offset > section->byte_length ||
        section->row_size > section->byte_length - offset) {
        return NULL;
    }
    return section->bytes + offset;
}

static int
ribos_vm_terminal_type(
    const RibosArtifactSectionView *types,
    uint32_t type_id,
    RibosVmTerminalType *type)
{
    const uint8_t *row;

    if (type == NULL) {
        return 0;
    }
    memset(type, 0, sizeof(*type));
    row = ribos_vm_terminal_row(types, type_id);
    if (row == NULL ||
        ribos_vm_terminal_u32(row) != type_id) {
        return 0;
    }
    *type = (RibosVmTerminalType){
        .id = type_id,
        .kind = ribos_vm_terminal_u16(row + 4),
        .first_type = ribos_vm_terminal_u32(row + 8),
        .second_type = ribos_vm_terminal_u32(row + 12),
        .byte_size = ribos_vm_terminal_u32(row + 40),
        .payload_offset = ribos_vm_terminal_u32(row + 48),
    };
    return 1;
}

static int
ribos_vm_terminal_entry_result(
    const RibosPreparedProgram *prepared_program,
    RibosVmTerminalType *result_type,
    RibosVmTerminalType *action_type,
    RibosVmTerminalType *error_type)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(prepared_program);
    const RibosArtifactSectionView *functions;
    const RibosArtifactSectionView *types;
    const uint8_t *function;
    uint32_t return_type;

    if (view == NULL || result_type == NULL ||
        action_type == NULL || error_type == NULL) {
        return 0;
    }
    functions = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_FUNCTIONS);
    types = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_TYPES);
    function = ribos_vm_terminal_row(
        functions,
        view->entry_function);
    if (function == NULL ||
        ribos_vm_terminal_u32(function) != view->entry_function) {
        return 0;
    }
    return_type = ribos_vm_terminal_u32(function + 8);
    return ribos_vm_terminal_type(types, return_type, result_type) &&
        result_type->kind == RIBOS_BC_TYPE_RESULT &&
        ribos_vm_terminal_type(
            types,
            result_type->first_type,
            action_type) &&
        ribos_vm_terminal_type(
            types,
            result_type->second_type,
            error_type) &&
        result_type->payload_offset <= result_type->byte_size &&
        action_type->byte_size <=
            result_type->byte_size - result_type->payload_offset &&
        error_type->byte_size <=
            result_type->byte_size - result_type->payload_offset;
}

static void
ribos_vm_terminal_initialize_snapshot(
    RibosVmTerminalSnapshot *snapshot,
    const RibosVmContext *context,
    const uint8_t binding_digest[RIBOS_VM_DIGEST_BYTES])
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->size = sizeof(*snapshot);
    snapshot->terminal_major = RIBOS_VM_TERMINAL_V1_MAJOR;
    snapshot->terminal_minor = RIBOS_VM_TERMINAL_V1_MINOR;
    snapshot->state = RIBOS_VM_TERMINAL_EXECUTING;
    snapshot->terminal_helper_id = RIBOS_VM_INVALID_ID;
    snapshot->action_type_id = RIBOS_VM_INVALID_ID;
    snapshot->error_type_id = RIBOS_VM_INVALID_ID;
    snapshot->source_map_id = RIBOS_VM_INVALID_ID;
    snapshot->context_generation = context->generation;
    snapshot->last_journal_helper_id = RIBOS_VM_INVALID_ID;
    memcpy(
        snapshot->binding_digest,
        binding_digest,
        RIBOS_VM_DIGEST_BYTES);
    memcpy(
        snapshot->context_digest,
        context->digest,
        RIBOS_VM_DIGEST_BYTES);
}

RibosVmStatus
ribos_vm_terminal_initialize_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmContext *context,
    RibosVmStorage *storage,
    size_t arena_size)
{
    const uint8_t *binding_digest =
        ribos_prepared_program_binding_digest_v1(prepared_program);
    const RibosVmLimits *limits =
        ribos_prepared_program_limits_v1(prepared_program);
    RibosVmTerminalSnapshot snapshot;
    RibosVmStatus status;

    status = ribos_prepared_program_validate_v1(prepared_program);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_context_validate_v1(context, limits);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (binding_digest == NULL ||
        !ribos_vm_digest_is_nonzero(binding_digest)) {
        return RIBOS_VM_STATUS_NOT_PREPARED;
    }
    ribos_vm_terminal_initialize_snapshot(
        &snapshot,
        context,
        binding_digest);
    return ribos_vm_storage_terminal_initialize_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &snapshot);
}

static RibosVmStatus
ribos_vm_terminal_record_journal(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmTerminalSnapshot *snapshot,
    const RibosVmTerminalHelperReceiptInternal *receipt)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(prepared_program);
    RibosArtifactSha256 hash;

    if (view == NULL ||
        snapshot->state != RIBOS_VM_TERMINAL_EXECUTING ||
        receipt->effect != RIBOS_VM_HELPER_EFFECT_JOURNALED ||
        receipt->durability !=
            RIBOS_VM_HELPER_DURABILITY_JOURNAL_RECEIPT ||
        receipt->journal_state <
            RIBOS_VM_JOURNAL_RECEIPT_COMMITTED ||
        receipt->journal_state >
            RIBOS_VM_JOURNAL_RECEIPT_UNCERTAIN ||
        !ribos_vm_digest_is_nonzero(
            receipt->journal_receipt_digest) ||
        snapshot->journal_count == UINT64_MAX ||
        receipt->receipt_sequence <= snapshot->journal_sequence) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    ribos_artifact_sha256_initialize(&hash);
    ribos_artifact_sha256_update(
        &hash,
        (const uint8_t *)RIBOS_VM_JOURNAL_HASH_DOMAIN,
        sizeof(RIBOS_VM_JOURNAL_HASH_DOMAIN));
    ribos_artifact_sha256_update(
        &hash,
        view->artifact_hash,
        RIBOS_VM_DIGEST_BYTES);
    ribos_artifact_sha256_update(
        &hash,
        snapshot->binding_digest,
        RIBOS_VM_DIGEST_BYTES);
    ribos_artifact_sha256_update(
        &hash,
        snapshot->context_digest,
        RIBOS_VM_DIGEST_BYTES);
    ribos_artifact_sha256_update(
        &hash,
        snapshot->journal_chain_digest,
        RIBOS_VM_DIGEST_BYTES);
    ribos_vm_terminal_hash_u32(&hash, receipt->helper_id);
    ribos_vm_terminal_hash_u32(&hash, receipt->function_id);
    ribos_vm_terminal_hash_u32(&hash, receipt->instruction_id);
    ribos_vm_terminal_hash_u32(&hash, receipt->source_map_id);
    ribos_vm_terminal_hash_u32(
        &hash,
        receipt->callback_status);
    ribos_vm_terminal_hash_u32(&hash, receipt->journal_state);
    ribos_vm_terminal_hash_u64(
        &hash,
        receipt->receipt_sequence);
    ribos_artifact_sha256_update(
        &hash,
        receipt->journal_receipt_digest,
        RIBOS_VM_DIGEST_BYTES);
    ribos_artifact_sha256_finish(
        &hash,
        snapshot->journal_chain_digest);
    snapshot->journal_sequence = receipt->receipt_sequence;
    ++snapshot->journal_count;
    snapshot->last_journal_helper_id = receipt->helper_id;
    snapshot->last_journal_callback_status =
        receipt->callback_status;
    if (snapshot->journal_state ==
            RIBOS_VM_JOURNAL_RECEIPT_NONE ||
        receipt->journal_state >
            snapshot->journal_state) {
        snapshot->journal_state = receipt->journal_state;
    }
    return ribos_vm_storage_terminal_store_internal_v1(
        prepared_program,
        storage,
        arena_size,
        snapshot);
}

static RibosVmStatus
ribos_vm_terminal_record_action(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmTerminalSnapshot *snapshot,
    const RibosVmTerminalHelperReceiptInternal *receipt)
{
    const uint8_t *output;
    RibosVmStatus status;

    if (snapshot->state != RIBOS_VM_TERMINAL_EXECUTING ||
        receipt->effect != RIBOS_VM_HELPER_EFFECT_TERMINAL ||
        receipt->durability !=
            RIBOS_VM_HELPER_DURABILITY_SEALED_INTENT ||
        receipt->callback_status !=
            RIBOS_VM_HELPER_CALLBACK_OK ||
        receipt->result_kind !=
            RIBOS_VM_HELPER_RESULT_SUCCESS_VALUE ||
        receipt->helper_id == RIBOS_VM_INVALID_ID ||
        receipt->result_type_id == RIBOS_VM_INVALID_ID ||
        receipt->result_byte_size == 0) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    status = ribos_vm_storage_output_zero_internal_v1(
        prepared_program,
        storage,
        arena_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_storage_output_view_internal_v1(
        prepared_program,
        storage,
        arena_size,
        receipt->result_byte_size,
        &output);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_storage_slot_slice_read_internal_v1(
        prepared_program,
        storage,
        arena_size,
        receipt->function_id,
        receipt->frame_base,
        receipt->result_slot_id,
        0,
        (uint8_t *)(void *)output,
        receipt->result_byte_size);
    if (status != RIBOS_VM_STATUS_OK) {
        (void)ribos_vm_storage_output_zero_internal_v1(
            prepared_program,
            storage,
            arena_size);
        return status;
    }
    snapshot->state = RIBOS_VM_TERMINAL_ACTION_PENDING;
    snapshot->terminal_helper_id = receipt->helper_id;
    snapshot->action_type_id = receipt->result_type_id;
    snapshot->source_map_id = receipt->source_map_id;
    snapshot->payload_size = receipt->result_byte_size;
    return ribos_vm_storage_terminal_store_internal_v1(
        prepared_program,
        storage,
        arena_size,
        snapshot);
}

RibosVmStatus
ribos_vm_terminal_record_helper_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmContext *context,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmTerminalHelperReceiptInternal *receipt)
{
    RibosVmTerminalSnapshot snapshot;
    RibosVmStatus status;

    if (context == NULL || receipt == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    status = ribos_vm_storage_terminal_load_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &snapshot);
    if (status == RIBOS_VM_STATUS_INVALID_STATE) {
        /*
         * Low-level interpreter/helper APIs remain usable without the
         * high-level terminal layer. Such runs do not claim sealed outcomes.
         */
        return RIBOS_VM_STATUS_OK;
    }
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (snapshot.context_generation != context->generation ||
        memcmp(
            snapshot.context_digest,
            context->digest,
            RIBOS_VM_DIGEST_BYTES) != 0) {
        return RIBOS_VM_STATUS_DIGEST_MISMATCH;
    }
    if (receipt->effect == RIBOS_VM_HELPER_EFFECT_JOURNALED) {
        return ribos_vm_terminal_record_journal(
            prepared_program,
            storage,
            arena_size,
            &snapshot,
            receipt);
    }
    if (receipt->effect == RIBOS_VM_HELPER_EFFECT_TERMINAL) {
        return ribos_vm_terminal_record_action(
            prepared_program,
            storage,
            arena_size,
            &snapshot,
            receipt);
    }
    return RIBOS_VM_STATUS_INVALID_ARGUMENT;
}

static int
ribos_vm_terminal_slot_equals_output(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    uint32_t slot_offset,
    const uint8_t *output,
    uint32_t byte_size)
{
    uint8_t buffer[64];
    uint32_t offset = 0;

    while (offset < byte_size) {
        uint32_t amount = byte_size - offset;

        if (amount > sizeof(buffer)) {
            amount = sizeof(buffer);
        }
        if (ribos_vm_storage_slot_slice_read_internal_v1(
                prepared_program,
                storage,
                arena_size,
                function_id,
                frame_base,
                slot_id,
                slot_offset + offset,
                buffer,
                amount) != RIBOS_VM_STATUS_OK ||
            memcmp(buffer, output + offset, amount) != 0) {
            return 0;
        }
        offset += amount;
    }
    return 1;
}

static void
ribos_vm_terminal_action_digest(
    const RibosPreparedProgram *prepared_program,
    const RibosVmTerminalSnapshot *snapshot,
    const uint8_t *payload,
    uint8_t digest[RIBOS_VM_DIGEST_BYTES])
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(prepared_program);
    RibosArtifactSha256 hash;

    ribos_artifact_sha256_initialize(&hash);
    ribos_artifact_sha256_update(
        &hash,
        (const uint8_t *)RIBOS_VM_TERMINAL_HASH_DOMAIN,
        sizeof(RIBOS_VM_TERMINAL_HASH_DOMAIN));
    ribos_artifact_sha256_update(
        &hash,
        view->artifact_hash,
        RIBOS_VM_DIGEST_BYTES);
    ribos_artifact_sha256_update(
        &hash,
        snapshot->binding_digest,
        RIBOS_VM_DIGEST_BYTES);
    ribos_artifact_sha256_update(
        &hash,
        snapshot->context_digest,
        RIBOS_VM_DIGEST_BYTES);
    ribos_vm_terminal_hash_u32(
        &hash,
        snapshot->terminal_helper_id);
    ribos_vm_terminal_hash_u32(
        &hash,
        snapshot->action_type_id);
    ribos_vm_terminal_hash_u64(
        &hash,
        snapshot->context_generation);
    ribos_vm_terminal_hash_u64(&hash, snapshot->payload_size);
    ribos_vm_terminal_hash_u64(&hash, snapshot->journal_count);
    ribos_vm_terminal_hash_u64(
        &hash,
        snapshot->journal_sequence);
    ribos_artifact_sha256_update(
        &hash,
        snapshot->journal_chain_digest,
        RIBOS_VM_DIGEST_BYTES);
    ribos_artifact_sha256_update(
        &hash,
        payload,
        (size_t)snapshot->payload_size);
    ribos_artifact_sha256_finish(&hash, digest);
}

static RibosVmStatus
ribos_vm_terminal_make_action_outcome(
    const RibosPreparedProgram *prepared_program,
    const RibosVmTerminalSnapshot *snapshot,
    const RibosVmStorage *storage,
    size_t arena_size,
    RibosVmOutcome *outcome)
{
    const uint8_t *payload;
    RibosVmStatus status =
        ribos_vm_storage_output_view_internal_v1(
            prepared_program,
            storage,
            arena_size,
            (size_t)snapshot->payload_size,
            &payload);

    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    memset(outcome, 0, sizeof(*outcome));
    outcome->size = sizeof(*outcome);
    outcome->runtime_abi_major = RIBOS_VM_RUNTIME_ABI_V1_MAJOR;
    outcome->runtime_abi_minor = RIBOS_VM_RUNTIME_ABI_V1_MINOR;
    outcome->kind = RIBOS_VM_OUTCOME_BOOT_ACTION;
    outcome->value.boot_action = (RibosVmBootAction){
        .terminal_helper_id = snapshot->terminal_helper_id,
        .action_type_id = snapshot->action_type_id,
        .generation = snapshot->context_generation,
        .payload = payload,
        .payload_size = snapshot->payload_size,
    };
    memcpy(
        outcome->value.boot_action.receipt_digest,
        snapshot->action_receipt_digest,
        RIBOS_VM_DIGEST_BYTES);
    return ribos_vm_outcome_validate_v1(outcome);
}

static RibosVmStatus
ribos_vm_terminal_make_error_outcome(
    const RibosPreparedProgram *prepared_program,
    const RibosVmTerminalSnapshot *snapshot,
    const RibosVmStorage *storage,
    size_t arena_size,
    RibosVmOutcome *outcome)
{
    const uint8_t *payload;
    RibosVmStatus status =
        ribos_vm_storage_output_view_internal_v1(
            prepared_program,
            storage,
            arena_size,
            (size_t)snapshot->payload_size,
            &payload);

    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    memset(outcome, 0, sizeof(*outcome));
    outcome->size = sizeof(*outcome);
    outcome->runtime_abi_major = RIBOS_VM_RUNTIME_ABI_V1_MAJOR;
    outcome->runtime_abi_minor = RIBOS_VM_RUNTIME_ABI_V1_MINOR;
    outcome->kind = RIBOS_VM_OUTCOME_POLICY_ERROR;
    outcome->value.policy_error = (RibosVmPolicyError){
        .stable_code = snapshot->stable_error_code,
        .error_type_id = snapshot->error_type_id,
        .source_map_id = snapshot->source_map_id,
        .payload = payload,
        .payload_size = snapshot->payload_size,
    };
    return ribos_vm_outcome_validate_v1(outcome);
}

static RibosVmStatus
ribos_vm_terminal_finalize_return(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmInterpreterSnapshot *interpreter,
    RibosVmOutcome *outcome)
{
    RibosVmStorageExecutionControl control;
    RibosVmTerminalSnapshot terminal;
    RibosVmTerminalType result_type;
    RibosVmTerminalType action_type;
    RibosVmTerminalType error_type;
    const uint8_t *output;
    uint64_t remaining;
    uint8_t tag;
    RibosVmStatus status;

    status = ribos_vm_storage_terminal_load_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &terminal);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_storage_execution_load_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &control,
        &remaining);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    (void)remaining;
    if (interpreter->state != RIBOS_VM_INTERPRETER_RETURNED ||
        control.state != RIBOS_VM_INTERPRETER_RETURNED ||
        control.return_slot_id != interpreter->return_slot_id ||
        control.function_id !=
            ribos_prepared_program_artifact_view_v1(
                prepared_program)->entry_function ||
        !ribos_vm_terminal_entry_result(
            prepared_program,
            &result_type,
            &action_type,
            &error_type) ||
        result_type.byte_size == 0 ||
        result_type.payload_offset == 0 ||
        ribos_vm_storage_slot_slice_read_internal_v1(
            prepared_program,
            storage,
            arena_size,
            control.function_id,
            control.frame_base,
            control.return_slot_id,
            0,
            &tag,
            1) != RIBOS_VM_STATUS_OK ||
        tag > 1) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    if (tag == 0) {
        if (terminal.state !=
                RIBOS_VM_TERMINAL_ACTION_PENDING ||
            terminal.action_type_id != action_type.id ||
            terminal.payload_size != action_type.byte_size ||
            ribos_vm_storage_output_view_internal_v1(
                prepared_program,
                storage,
                arena_size,
                action_type.byte_size,
                &output) != RIBOS_VM_STATUS_OK ||
            !ribos_vm_terminal_slot_equals_output(
                prepared_program,
                storage,
                arena_size,
                control.function_id,
                control.frame_base,
                control.return_slot_id,
                result_type.payload_offset,
                output,
                action_type.byte_size)) {
            return RIBOS_VM_STATUS_INVALID_STATE;
        }
        terminal.state = RIBOS_VM_TERMINAL_ACTION_SEALED;
        terminal.outcome_kind = RIBOS_VM_OUTCOME_BOOT_ACTION;
        terminal.source_map_id = interpreter->source_map_id;
        ribos_vm_terminal_action_digest(
            prepared_program,
            &terminal,
            output,
            terminal.action_receipt_digest);
        status = ribos_vm_storage_terminal_store_internal_v1(
            prepared_program,
            storage,
            arena_size,
            &terminal);
        if (status != RIBOS_VM_STATUS_OK) {
            return status;
        }
        return ribos_vm_terminal_make_action_outcome(
            prepared_program,
            &terminal,
            storage,
            arena_size,
            outcome);
    }
    if (terminal.state != RIBOS_VM_TERMINAL_EXECUTING ||
        error_type.byte_size == 0 ||
        ribos_vm_storage_output_zero_internal_v1(
            prepared_program,
            storage,
            arena_size) != RIBOS_VM_STATUS_OK ||
        ribos_vm_storage_output_view_internal_v1(
            prepared_program,
            storage,
            arena_size,
            error_type.byte_size,
            &output) != RIBOS_VM_STATUS_OK ||
        ribos_vm_storage_slot_slice_read_internal_v1(
            prepared_program,
            storage,
            arena_size,
            control.function_id,
            control.frame_base,
            control.return_slot_id,
            result_type.payload_offset,
            (uint8_t *)(void *)output,
            error_type.byte_size) != RIBOS_VM_STATUS_OK) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    terminal.state = RIBOS_VM_TERMINAL_POLICY_ERROR;
    terminal.outcome_kind = RIBOS_VM_OUTCOME_POLICY_ERROR;
    terminal.error_type_id = error_type.id;
    terminal.stable_error_code = output[0];
    if (error_type.byte_size > 1) {
        terminal.stable_error_code |=
            (uint32_t)output[1] << 8;
    }
    if (error_type.byte_size > 2) {
        terminal.stable_error_code |=
            (uint32_t)output[2] << 16;
    }
    if (error_type.byte_size > 3) {
        terminal.stable_error_code |=
            (uint32_t)output[3] << 24;
    }
    terminal.source_map_id = interpreter->source_map_id;
    terminal.payload_size = error_type.byte_size;
    status = ribos_vm_storage_terminal_store_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &terminal);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    return ribos_vm_terminal_make_error_outcome(
        prepared_program,
        &terminal,
        storage,
        arena_size,
        outcome);
}

static void
ribos_vm_terminal_fault_digest(
    const RibosVmTerminalSnapshot *terminal,
    const RibosVmFaultReceipt *fault,
    const RibosVmHandleCleanupReport *cleanup,
    uint8_t digest[RIBOS_VM_DIGEST_BYTES])
{
    RibosArtifactSha256 hash;

    ribos_artifact_sha256_initialize(&hash);
    ribos_artifact_sha256_update(
        &hash,
        (const uint8_t *)RIBOS_VM_FAULT_HASH_DOMAIN,
        sizeof(RIBOS_VM_FAULT_HASH_DOMAIN));
    ribos_artifact_sha256_update(
        &hash,
        fault->artifact_hash,
        RIBOS_VM_DIGEST_BYTES);
    ribos_artifact_sha256_update(
        &hash,
        terminal->binding_digest,
        RIBOS_VM_DIGEST_BYTES);
    ribos_artifact_sha256_update(
        &hash,
        terminal->context_digest,
        RIBOS_VM_DIGEST_BYTES);
    ribos_vm_terminal_hash_u32(&hash, fault->fault_code);
    ribos_vm_terminal_hash_u32(&hash, fault->subject);
    ribos_vm_terminal_hash_u32(&hash, fault->function_id);
    ribos_vm_terminal_hash_u32(&hash, fault->instruction_id);
    ribos_vm_terminal_hash_u32(&hash, fault->helper_id);
    ribos_vm_terminal_hash_u32(&hash, fault->detail);
    ribos_vm_terminal_hash_u64(
        &hash,
        fault->consumed_instructions);
    ribos_vm_terminal_hash_u64(
        &hash,
        fault->consumed_helper_calls);
    ribos_vm_terminal_hash_u64(&hash, terminal->journal_count);
    ribos_vm_terminal_hash_u32(&hash, terminal->journal_state);
    ribos_artifact_sha256_update(
        &hash,
        terminal->journal_chain_digest,
        RIBOS_VM_DIGEST_BYTES);
    ribos_vm_terminal_hash_u32(&hash, cleanup->scanned);
    ribos_vm_terminal_hash_u32(&hash, cleanup->revoked);
    ribos_vm_terminal_hash_u32(&hash, cleanup->drop_calls);
    ribos_vm_terminal_hash_u32(&hash, cleanup->drop_failures);
    ribos_artifact_sha256_finish(&hash, digest);
}

RibosVmStatus
ribos_vm_terminal_close_fault_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmHelperEnvironment *environment,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmOutcome *outcome)
{
    RibosVmTerminalSnapshot terminal;
    RibosVmHelperExecutionSnapshot helper;
    RibosVmHandleCleanupReport cleanup;
    RibosVmFaultReceipt fault;
    RibosVmStatus cleanup_status;
    RibosVmStatus status;

    status = ribos_vm_storage_read_fault_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &fault);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_storage_terminal_load_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &terminal);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    memset(&cleanup, 0, sizeof(cleanup));
    cleanup_status = ribos_vm_handle_fault_cleanup_v1(
        prepared_program,
        storage,
        arena_size,
        environment->handle_table,
        &cleanup);
    if (cleanup_status != RIBOS_VM_STATUS_OK &&
        cleanup_status != RIBOS_VM_STATUS_EMBEDDER_REJECTED) {
        return cleanup_status;
    }
    if (ribos_vm_storage_helper_execution_load_internal_v1(
            prepared_program,
            storage,
            arena_size,
            &helper) == RIBOS_VM_STATUS_OK) {
        helper.state = RIBOS_VM_HELPER_EXECUTION_FAULTED;
        helper.callback_active = 0;
        helper.active_helper_id = RIBOS_VM_INVALID_ID;
        status = ribos_vm_storage_helper_execution_store_internal_v1(
            prepared_program,
            storage,
            arena_size,
            &helper);
        if (status != RIBOS_VM_STATUS_OK) {
            return status;
        }
    }
    terminal.state = RIBOS_VM_TERMINAL_VM_FAULT;
    terminal.outcome_kind = RIBOS_VM_OUTCOME_VM_FAULT;
    terminal.terminal_helper_id = RIBOS_VM_INVALID_ID;
    terminal.action_type_id = RIBOS_VM_INVALID_ID;
    terminal.error_type_id = RIBOS_VM_INVALID_ID;
    terminal.payload_size = 0;
    terminal.authority_revoked = 1;
    terminal.action_consumed = 0;
    (void)ribos_vm_storage_output_zero_internal_v1(
        prepared_program,
        storage,
        arena_size);
    ribos_vm_terminal_fault_digest(
        &terminal,
        &fault,
        &cleanup,
        terminal.trace_digest);
    status = ribos_vm_storage_fault_trace_digest_internal_v1(
        prepared_program,
        storage,
        arena_size,
        terminal.trace_digest);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_storage_fault_recovery_mark_internal_v1(
        prepared_program,
        storage,
        arena_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    terminal.recovery_notified = 1;
    status = ribos_vm_storage_terminal_store_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &terminal);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_storage_read_fault_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &fault);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    environment->embedder->factory_recovery(
        environment->embedder->embedder_context,
        &fault);
    memset(outcome, 0, sizeof(*outcome));
    outcome->size = sizeof(*outcome);
    outcome->runtime_abi_major = RIBOS_VM_RUNTIME_ABI_V1_MAJOR;
    outcome->runtime_abi_minor = RIBOS_VM_RUNTIME_ABI_V1_MINOR;
    outcome->kind = RIBOS_VM_OUTCOME_VM_FAULT;
    outcome->value.vm_fault = fault;
    return ribos_vm_outcome_validate_v1(outcome);
}

static RibosVmStatus
ribos_vm_terminal_seal_return_fault(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmInterpreterSnapshot *interpreter)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(prepared_program);
    RibosVmHelperExecutionSnapshot helper;
    RibosVmFaultReceipt fault;

    if (view == NULL) {
        return RIBOS_VM_STATUS_NOT_PREPARED;
    }
    memset(&fault, 0, sizeof(fault));
    fault.fault_code = RIBOS_VM_FAULT_TERMINAL_ACTION;
    fault.subject = RIBOS_VM_FAULT_SUBJECT_TERMINAL;
    fault.function_id = interpreter->function_id;
    fault.instruction_id = interpreter->instruction_id;
    fault.helper_id = RIBOS_VM_INVALID_ID;
    fault.detail = interpreter->return_slot_id;
    fault.consumed_instructions =
        interpreter->consumed_instructions;
    if (ribos_vm_storage_helper_execution_load_internal_v1(
            prepared_program,
            storage,
            arena_size,
            &helper) == RIBOS_VM_STATUS_OK) {
        fault.helper_id = helper.last_helper_id;
        fault.last_effect = helper.last_effect;
        fault.last_durability = helper.last_durability;
        fault.consumed_helper_calls =
            helper.consumed_helper_calls;
        fault.consumed_input_bytes =
            helper.consumed_input_bytes;
        fault.consumed_output_bytes =
            helper.consumed_output_bytes;
        fault.consumed_operations =
            helper.consumed_operations;
        fault.consumed_polls = helper.consumed_polls;
        fault.elapsed_ns =
            helper.last_now_ns >= helper.execution_start_ns ?
                helper.last_now_ns -
                    helper.execution_start_ns : 0;
    }
    memcpy(
        fault.artifact_hash,
        view->artifact_hash,
        RIBOS_VM_DIGEST_BYTES);
    return ribos_vm_storage_seal_fault_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &fault);
}

RibosVmStatus
ribos_vm_terminal_finalize_interpreter_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmHelperEnvironment *environment,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmInterpreterSnapshot *interpreter,
    RibosVmOutcome *outcome)
{
    RibosVmStatus status;

    if (environment == NULL || interpreter == NULL ||
        outcome == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    memset(outcome, 0, sizeof(*outcome));
    if (interpreter->state == RIBOS_VM_INTERPRETER_FAULTED) {
        return ribos_vm_terminal_close_fault_internal_v1(
            prepared_program,
            environment,
            storage,
            arena_size,
            outcome);
    }
    if (interpreter->state != RIBOS_VM_INTERPRETER_RETURNED) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    status = ribos_vm_terminal_finalize_return(
        prepared_program,
        storage,
        arena_size,
        interpreter,
        outcome);
    if (status == RIBOS_VM_STATUS_OK) {
        return RIBOS_VM_STATUS_OK;
    }
    status = ribos_vm_terminal_seal_return_fault(
        prepared_program,
        storage,
        arena_size,
        interpreter);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    return ribos_vm_terminal_close_fault_internal_v1(
        prepared_program,
        environment,
        storage,
        arena_size,
        outcome);
}

RibosVmStatus
ribos_vm_policy_execute_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmContext *context,
    const RibosVmHelperEnvironment *environment,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmOutcome *outcome)
{
    RibosVmInterpreterSnapshot interpreter;
    RibosVmStatus status;

    if (outcome == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    memset(outcome, 0, sizeof(*outcome));
    status = ribos_vm_helper_environment_validate_v1(
        prepared_program,
        context,
        environment);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_terminal_initialize_internal_v1(
        prepared_program,
        context,
        storage,
        arena_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_interpreter_initialize_v1(
        prepared_program,
        context,
        storage,
        arena_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_helper_execution_initialize_v1(
        prepared_program,
        context,
        environment,
        storage,
        arena_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_interpreter_run_with_helpers_v1(
        prepared_program,
        context,
        environment,
        storage,
        arena_size,
        &interpreter);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    return ribos_vm_terminal_finalize_interpreter_internal_v1(
        prepared_program,
        environment,
        storage,
        arena_size,
        &interpreter,
        outcome);
}

RibosVmStatus
ribos_vm_terminal_snapshot_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    RibosVmTerminalSnapshot *snapshot)
{
    return ribos_vm_storage_terminal_load_internal_v1(
        prepared_program,
        storage,
        arena_size,
        snapshot);
}

RibosVmStatus
ribos_vm_boot_action_consume_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmBootAction *action)
{
    RibosVmTerminalSnapshot terminal;
    const uint8_t *payload;
    RibosVmStatus status;

    if (action == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    status = ribos_vm_storage_terminal_load_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &terminal);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (terminal.state == RIBOS_VM_TERMINAL_ACTION_CONSUMED ||
        terminal.action_consumed != 0) {
        return RIBOS_VM_STATUS_ALREADY_CONSUMED;
    }
    status = ribos_vm_storage_output_view_internal_v1(
        prepared_program,
        storage,
        arena_size,
        (size_t)terminal.payload_size,
        &payload);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (terminal.state != RIBOS_VM_TERMINAL_ACTION_SEALED ||
        terminal.outcome_kind != RIBOS_VM_OUTCOME_BOOT_ACTION ||
        action->terminal_helper_id != terminal.terminal_helper_id ||
        action->action_type_id != terminal.action_type_id ||
        action->generation != terminal.context_generation ||
        action->payload != payload ||
        action->payload_size != terminal.payload_size ||
        memcmp(
            action->receipt_digest,
            terminal.action_receipt_digest,
            RIBOS_VM_DIGEST_BYTES) != 0 ||
        !ribos_vm_reserved_words_are_zero(action->reserved, 2)) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    status = ribos_vm_storage_output_zero_internal_v1(
        prepared_program,
        storage,
        arena_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    terminal.state = RIBOS_VM_TERMINAL_ACTION_CONSUMED;
    terminal.action_consumed = 1;
    return ribos_vm_storage_terminal_store_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &terminal);
}
