#include "prepared_internal.h"

#include "internal.h"

#include <stdint.h>
#include <string.h>

#define RIBOS_AUTHORIZED_STATE_READY 1u
#define RIBOS_PREPARED_STATE_READY 1u

typedef struct RibosPreparedArena {
    uint8_t *bytes;
    size_t capacity;
    size_t offset;
    int failed;
} RibosPreparedArena;

static int
ribos_prepared_size_add(size_t left, size_t right, size_t *result)
{
    return ribos_artifact_size_add(left, right, result);
}

static int
ribos_prepared_size_align(
    size_t value,
    size_t alignment,
    size_t *result)
{
    return ribos_artifact_size_align(value, alignment, result);
}

static int
ribos_prepared_size_append(
    size_t *size,
    size_t alignment,
    size_t byte_count)
{
    size_t aligned;

    return size != NULL &&
        ribos_prepared_size_align(*size, alignment, &aligned) &&
        ribos_prepared_size_add(aligned, byte_count, size);
}

static void *
ribos_prepared_arena_take(
    RibosPreparedArena *arena,
    size_t alignment,
    size_t byte_count)
{
    size_t aligned;
    size_t end;

    if (arena == NULL || arena->failed ||
        !ribos_prepared_size_align(
            arena->offset,
            alignment,
            &aligned) ||
        !ribos_prepared_size_add(aligned, byte_count, &end) ||
        end > arena->capacity) {
        if (arena != NULL) {
            arena->failed = 1;
        }
        return NULL;
    }
    arena->offset = end;
    return arena->bytes + aligned;
}

static int
ribos_prepared_workspace_is_aligned(
    const void *workspace,
    size_t alignment)
{
    return workspace != NULL && alignment != 0 &&
        ((uintptr_t)workspace % alignment) == 0;
}

static int
ribos_prepared_digest_equal(
    const uint8_t left[RIBOS_VM_DIGEST_BYTES],
    const uint8_t right[RIBOS_VM_DIGEST_BYTES])
{
    return memcmp(left, right, RIBOS_VM_DIGEST_BYTES) == 0;
}

static void
ribos_prepared_hash_bytes(
    RibosArtifactSha256 *hash,
    const void *bytes,
    size_t byte_count)
{
    ribos_artifact_sha256_update(
        hash,
        (const uint8_t *)bytes,
        byte_count);
}

static void
ribos_prepared_hash_u16(RibosArtifactSha256 *hash, uint16_t value)
{
    uint8_t bytes[2] = {
        (uint8_t)value,
        (uint8_t)(value >> 8),
    };

    ribos_prepared_hash_bytes(hash, bytes, sizeof(bytes));
}

static void
ribos_prepared_hash_u32(RibosArtifactSha256 *hash, uint32_t value)
{
    uint8_t bytes[4] = {
        (uint8_t)value,
        (uint8_t)(value >> 8),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 24),
    };

    ribos_prepared_hash_bytes(hash, bytes, sizeof(bytes));
}

static void
ribos_prepared_hash_u64(RibosArtifactSha256 *hash, uint64_t value)
{
    uint8_t bytes[8] = {
        (uint8_t)value,
        (uint8_t)(value >> 8),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 24),
        (uint8_t)(value >> 32),
        (uint8_t)(value >> 40),
        (uint8_t)(value >> 48),
        (uint8_t)(value >> 56),
    };

    ribos_prepared_hash_bytes(hash, bytes, sizeof(bytes));
}

static void
ribos_prepared_hash_domain(
    RibosArtifactSha256 *hash,
    const char *domain)
{
    uint8_t bytes[RIBOS_VM_DIGEST_BYTES] = {0};
    size_t length = strlen(domain);

    if (length > sizeof(bytes)) {
        length = sizeof(bytes);
    }
    memcpy(bytes, domain, length);
    ribos_prepared_hash_bytes(hash, bytes, sizeof(bytes));
}

static void
ribos_authorization_receipt_identity(
    const RibosArtifactAuthorizationReceipt *receipt,
    uint8_t digest[RIBOS_VM_DIGEST_BYTES])
{
    RibosArtifactSha256 hash;

    ribos_artifact_sha256_initialize(&hash);
    ribos_prepared_hash_domain(
        &hash,
        "RIBOS-AUTHORIZATION-RECEIPT-V1");
    ribos_prepared_hash_u16(&hash, receipt->authorization_major);
    ribos_prepared_hash_u16(&hash, receipt->authorization_minor);
    ribos_prepared_hash_u32(&hash, receipt->flags);
    ribos_prepared_hash_u32(&hash, receipt->decision);
    ribos_prepared_hash_u64(&hash, receipt->authority_generation);
    ribos_prepared_hash_u64(&hash, receipt->manifest_sequence);
    ribos_prepared_hash_u64(&hash, receipt->rollback_floor);
    ribos_prepared_hash_bytes(
        &hash,
        receipt->artifact_hash,
        RIBOS_VM_DIGEST_BYTES);
    ribos_prepared_hash_bytes(
        &hash,
        receipt->schema_digest,
        RIBOS_VM_DIGEST_BYTES);
    ribos_prepared_hash_bytes(
        &hash,
        receipt->helper_execution_digest,
        RIBOS_VM_DIGEST_BYTES);
    ribos_prepared_hash_bytes(
        &hash,
        receipt->key_identity_digest,
        RIBOS_VM_DIGEST_BYTES);
    ribos_prepared_hash_bytes(
        &hash,
        receipt->policy_identity_digest,
        RIBOS_VM_DIGEST_BYTES);
    ribos_artifact_sha256_finish(&hash, digest);
}

static RibosVmStatus
ribos_authorizer_validate_v1(const RibosArtifactAuthorizer *authorizer)
{
    if (authorizer == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    if (authorizer->authorization_major !=
            RIBOS_VM_AUTHORIZATION_V1_MAJOR ||
        authorizer->authorization_minor !=
            RIBOS_VM_AUTHORIZATION_V1_MINOR) {
        return RIBOS_VM_STATUS_NOT_AUTHORIZED;
    }
    if (authorizer->size != (uint32_t)sizeof(*authorizer)) {
        return RIBOS_VM_STATUS_INVALID_SIZE;
    }
    if (authorizer->flags != 0 || authorizer->reserved0 != 0 ||
        !ribos_vm_reserved_words_are_zero(
            authorizer->reserved,
            4)) {
        return RIBOS_VM_STATUS_RESERVED_NONZERO;
    }
    if (authorizer->authorize == NULL) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    return RIBOS_VM_STATUS_OK;
}

static RibosVmStatus
ribos_authorization_receipt_validate_v1(
    const RibosArtifactAuthorizationReceipt *receipt,
    const RibosArtifactView *view)
{
    if (receipt == NULL || view == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    if (receipt->authorization_major !=
            RIBOS_VM_AUTHORIZATION_V1_MAJOR ||
        receipt->authorization_minor !=
            RIBOS_VM_AUTHORIZATION_V1_MINOR) {
        return RIBOS_VM_STATUS_NOT_AUTHORIZED;
    }
    if (receipt->size != (uint32_t)sizeof(*receipt)) {
        return RIBOS_VM_STATUS_INVALID_SIZE;
    }
    if (receipt->flags != 0 ||
        !ribos_vm_reserved_words_are_zero(receipt->reserved, 4)) {
        return RIBOS_VM_STATUS_RESERVED_NONZERO;
    }
    if (receipt->decision != RIBOS_ARTIFACT_AUTHORIZATION_GRANTED ||
        receipt->authority_generation == 0 ||
        receipt->manifest_sequence < receipt->rollback_floor ||
        !ribos_vm_digest_is_nonzero(
            receipt->helper_execution_digest) ||
        !ribos_vm_digest_is_nonzero(
            receipt->policy_identity_digest)) {
        return RIBOS_VM_STATUS_NOT_AUTHORIZED;
    }
    if (!ribos_prepared_digest_equal(
            receipt->artifact_hash,
            view->artifact_hash) ||
        !ribos_prepared_digest_equal(
            receipt->schema_digest,
            view->schema_digest)) {
        return RIBOS_VM_STATUS_DIGEST_MISMATCH;
    }
    if ((view->envelope_flags & RIBOS_ARTIFACT_ENVELOPE_SIGNED) != 0 &&
        !ribos_vm_digest_is_nonzero(receipt->key_identity_digest)) {
        return RIBOS_VM_STATUS_NOT_AUTHORIZED;
    }
    return RIBOS_VM_STATUS_OK;
}

size_t
ribos_authorized_artifact_workspace_alignment_v1(void)
{
    return _Alignof(struct RibosAuthorizedArtifact);
}

RibosVmStatus
ribos_authorized_artifact_workspace_size_v1(
    size_t artifact_size,
    size_t *required_size)
{
    size_t size = 0;

    if (required_size == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *required_size = 0;
    if (artifact_size < RIBOS_ARTIFACT_ENVELOPE_BYTES ||
        artifact_size > RIBOS_ARTIFACT_MAX_BYTES ||
        !ribos_prepared_size_append(
            &size,
            _Alignof(struct RibosAuthorizedArtifact),
            sizeof(struct RibosAuthorizedArtifact)) ||
        !ribos_prepared_size_append(
            &size,
            1,
            artifact_size)) {
        return RIBOS_VM_STATUS_INVALID_SIZE;
    }
    *required_size = size;
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_authorize_artifact_v1(
    const uint8_t *artifact,
    size_t artifact_size,
    const RibosArtifactAuthorizer *authorizer,
    void *workspace,
    size_t workspace_size,
    const RibosAuthorizedArtifact **authorized_artifact)
{
    RibosPreparedArena arena;
    struct RibosAuthorizedArtifact *authorized;
    uint8_t *copy;
    RibosArtifactAuthorizationRequest request;
    RibosArtifactAuthorizationReceipt receipt;
    RibosArtifactView view_after;
    RibosVmStatus status;
    size_t required_size;
    uint32_t callback_status;

    if (authorized_artifact == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *authorized_artifact = NULL;
    status = ribos_authorizer_validate_v1(authorizer);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_authorized_artifact_workspace_size_v1(
        artifact_size,
        &required_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (artifact == NULL || workspace == NULL ||
        !ribos_prepared_workspace_is_aligned(
            workspace,
            ribos_authorized_artifact_workspace_alignment_v1())) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    if (workspace_size < required_size) {
        return RIBOS_VM_STATUS_ARENA_TOO_SMALL;
    }
    memset(workspace, 0, required_size);
    arena = (RibosPreparedArena){
        .bytes = workspace,
        .capacity = required_size,
    };
    authorized = ribos_prepared_arena_take(
        &arena,
        _Alignof(struct RibosAuthorizedArtifact),
        sizeof(*authorized));
    copy = ribos_prepared_arena_take(&arena, 1, artifact_size);
    if (arena.failed || authorized == NULL || copy == NULL) {
        return RIBOS_VM_STATUS_INTERNAL_ERROR;
    }
    memcpy(copy, artifact, artifact_size);
    if (ribos_artifact_open_v1(
            copy,
            artifact_size,
            &authorized->view) != RIBOS_ARTIFACT_OK) {
        return RIBOS_VM_STATUS_NOT_AUTHORIZED;
    }
    request = (RibosArtifactAuthorizationRequest){
        .size = sizeof(request),
        .authorization_major = RIBOS_VM_AUTHORIZATION_V1_MAJOR,
        .authorization_minor = RIBOS_VM_AUTHORIZATION_V1_MINOR,
        .envelope_flags = authorized->view.envelope_flags,
        .signature_algorithm =
            (uint32_t)authorized->view.signature_algorithm,
        .artifact = copy,
        .artifact_size = artifact_size,
        .key_id = authorized->view.key_id,
        .key_id_size = authorized->view.key_id_length,
        .signature = authorized->view.signature,
        .signature_size = authorized->view.signature_length,
    };
    memcpy(
        request.artifact_hash,
        authorized->view.artifact_hash,
        RIBOS_VM_DIGEST_BYTES);
    memcpy(
        request.schema_digest,
        authorized->view.schema_digest,
        RIBOS_VM_DIGEST_BYTES);
    memset(&receipt, 0, sizeof(receipt));
    callback_status = authorizer->authorize(
        authorizer->authority_context,
        &request,
        &receipt);
    if (callback_status != RIBOS_VM_STATUS_OK) {
        return callback_status == RIBOS_VM_STATUS_NOT_AUTHORIZED ?
            RIBOS_VM_STATUS_NOT_AUTHORIZED :
            RIBOS_VM_STATUS_NOT_AUTHORIZED;
    }
    if (ribos_artifact_open_v1(
            copy,
            artifact_size,
            &view_after) != RIBOS_ARTIFACT_OK ||
        !ribos_prepared_digest_equal(
            view_after.artifact_hash,
            request.artifact_hash) ||
        !ribos_prepared_digest_equal(
            view_after.schema_digest,
            request.schema_digest)) {
        return RIBOS_VM_STATUS_DIGEST_MISMATCH;
    }
    status = ribos_authorization_receipt_validate_v1(
        &receipt,
        &view_after);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }

    authorized->major = RIBOS_VM_AUTHORIZATION_V1_MAJOR;
    authorized->minor = RIBOS_VM_AUTHORIZATION_V1_MINOR;
    authorized->artifact = copy;
    authorized->artifact_size = artifact_size;
    authorized->view = view_after;
    authorized->receipt = receipt;
    ribos_artifact_sha256(
        copy,
        artifact_size,
        authorized->artifact_bytes_digest);
    ribos_authorization_receipt_identity(
        &receipt,
        authorized->receipt_digest);
    authorized->state = RIBOS_AUTHORIZED_STATE_READY;
    authorized->magic = RIBOS_AUTHORIZED_ARTIFACT_MAGIC;
    *authorized_artifact = authorized;
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_authorized_artifact_validate_v1(
    const RibosAuthorizedArtifact *authorized_artifact)
{
    RibosArtifactView view;
    uint8_t artifact_bytes_digest[RIBOS_VM_DIGEST_BYTES];
    uint8_t receipt_digest[RIBOS_VM_DIGEST_BYTES];
    RibosVmStatus status;

    if (authorized_artifact == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    if (authorized_artifact->magic !=
            RIBOS_AUTHORIZED_ARTIFACT_MAGIC ||
        authorized_artifact->major !=
            RIBOS_VM_AUTHORIZATION_V1_MAJOR ||
        authorized_artifact->minor !=
            RIBOS_VM_AUTHORIZATION_V1_MINOR ||
        authorized_artifact->state != RIBOS_AUTHORIZED_STATE_READY ||
        authorized_artifact->artifact == NULL ||
        authorized_artifact->artifact_size <
            RIBOS_ARTIFACT_ENVELOPE_BYTES ||
        authorized_artifact->artifact_size >
            RIBOS_ARTIFACT_MAX_BYTES) {
        return RIBOS_VM_STATUS_NOT_AUTHORIZED;
    }
    if (ribos_artifact_open_v1(
            authorized_artifact->artifact,
            authorized_artifact->artifact_size,
            &view) != RIBOS_ARTIFACT_OK) {
        return RIBOS_VM_STATUS_NOT_AUTHORIZED;
    }
    status = ribos_authorization_receipt_validate_v1(
        &authorized_artifact->receipt,
        &view);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    ribos_artifact_sha256(
        authorized_artifact->artifact,
        authorized_artifact->artifact_size,
        artifact_bytes_digest);
    ribos_authorization_receipt_identity(
        &authorized_artifact->receipt,
        receipt_digest);
    if (!ribos_prepared_digest_equal(
            artifact_bytes_digest,
            authorized_artifact->artifact_bytes_digest) ||
        !ribos_prepared_digest_equal(
            receipt_digest,
            authorized_artifact->receipt_digest)) {
        return RIBOS_VM_STATUS_DIGEST_MISMATCH;
    }
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_helper_contract_compute_identity_v1(
    const RibosVmHelperContract *contract,
    uint8_t digest[RIBOS_VM_DIGEST_BYTES])
{
    RibosArtifactSha256 hash;
    RibosVmHelperContract descriptor;
    uint32_t index;
    RibosVmStatus status;

    if (contract == NULL || digest == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    memset(digest, 0, RIBOS_VM_DIGEST_BYTES);
    descriptor = *contract;
    memset(descriptor.digest, 0, sizeof(descriptor.digest));
    descriptor.digest[0] = 1;
    status = ribos_vm_helper_contract_validate_v1(&descriptor);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    ribos_artifact_sha256_initialize(&hash);
    ribos_prepared_hash_domain(
        &hash,
        "RIBOS-HELPER-EXECUTION-V1");
    ribos_prepared_hash_u16(&hash, contract->contract_major);
    ribos_prepared_hash_u16(&hash, contract->contract_minor);
    ribos_prepared_hash_u32(&hash, contract->binding_count);
    for (index = 0; index < contract->binding_count; ++index) {
        const RibosVmHelperExecutionDescriptor *execution =
            &contract->bindings[index].execution;

        ribos_prepared_hash_u32(&hash, execution->stable_id);
        ribos_prepared_hash_u32(&hash, execution->flags);
        ribos_prepared_hash_u32(
            &hash,
            execution->required_capabilities);
        ribos_prepared_hash_u32(&hash, execution->effect);
        ribos_prepared_hash_u32(
            &hash,
            execution->execution_mode);
        ribos_prepared_hash_u32(&hash, execution->durability);
        ribos_prepared_hash_u32(
            &hash,
            execution->handle_transition);
        ribos_prepared_hash_u32(
            &hash,
            execution->transition_parameter);
        ribos_prepared_hash_u64(
            &hash,
            execution->allowed_mode_mask);
        ribos_prepared_hash_u64(
            &hash,
            execution->allowed_phase_mask);
        ribos_prepared_hash_u64(
            &hash,
            execution->maximum_input_bytes);
        ribos_prepared_hash_u64(
            &hash,
            execution->maximum_output_bytes);
        ribos_prepared_hash_u64(
            &hash,
            execution->maximum_operations);
        ribos_prepared_hash_u64(
            &hash,
            execution->maximum_polls);
        ribos_prepared_hash_u64(
            &hash,
            execution->maximum_duration_ns);
    }
    ribos_artifact_sha256_finish(&hash, digest);
    return RIBOS_VM_STATUS_OK;
}

static const RibosSchemaHelper *
ribos_prepared_schema_helper_by_id(
    const RibosProductSchema *schema,
    uint32_t stable_id)
{
    size_t index;

    for (index = 0; index < schema->helper_count; ++index) {
        if (schema->helpers[index].stable_id == stable_id) {
            return &schema->helpers[index];
        }
    }
    return NULL;
}

static uint32_t
ribos_prepared_expected_transition(
    const RibosProductSchema *schema,
    const RibosSchemaHelper *helper)
{
    const RibosSchemaType *result_type;

    if ((helper->flags &
         RIBOS_SCHEMA_HELPER_TERMINAL_BOOT_ACTION) != 0) {
        return (helper->flags &
                RIBOS_SCHEMA_HELPER_TYPESTATE_TRANSITION) != 0 ?
            RIBOS_VM_HANDLE_TRANSITION_TERMINAL_CONSUME :
            RIBOS_VM_HANDLE_TRANSITION_NONE;
    }
    if ((helper->flags &
         RIBOS_SCHEMA_HELPER_TYPESTATE_TRANSITION) != 0) {
        return strcmp(helper->result_type, "Unit") == 0 ?
            RIBOS_VM_HANDLE_TRANSITION_CONSUME :
            RIBOS_VM_HANDLE_TRANSITION_REPLACE;
    }
    result_type = ribos_schema_find_type(
        schema,
        helper->result_type,
        strlen(helper->result_type));
    if (result_type != NULL &&
        result_type->ownership != RIBOS_SCHEMA_OWNERSHIP_COPY) {
        return RIBOS_VM_HANDLE_TRANSITION_CREATE;
    }
    return RIBOS_VM_HANDLE_TRANSITION_NONE;
}

static RibosVmStatus
ribos_prepared_validate_helper_schema(
    const RibosProductSchema *schema,
    const RibosVmHelperContract *contract,
    const RibosArtifactView *view)
{
    const RibosArtifactSectionView *imports;
    uint32_t binding_index;
    uint32_t import_index;

    for (binding_index = 0;
         binding_index < contract->binding_count;
         ++binding_index) {
        const RibosVmHelperExecutionDescriptor *execution =
            &contract->bindings[binding_index].execution;
        const RibosSchemaHelper *helper =
            ribos_prepared_schema_helper_by_id(
                schema,
                execution->stable_id);
        uint32_t expected_transition;
        uint32_t expected_parameter = RIBOS_VM_INVALID_ID;

        if (helper == NULL ||
            execution->required_capabilities !=
                helper->capabilities) {
            return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
        }
        expected_transition =
            ribos_prepared_expected_transition(schema, helper);
        if (expected_transition ==
                RIBOS_VM_HANDLE_TRANSITION_CONSUME ||
            expected_transition ==
                RIBOS_VM_HANDLE_TRANSITION_REPLACE ||
            expected_transition ==
                RIBOS_VM_HANDLE_TRANSITION_TERMINAL_CONSUME) {
            expected_parameter = helper->transition_parameter;
        }
        if (execution->handle_transition != expected_transition ||
            execution->transition_parameter != expected_parameter ||
            (((helper->flags &
               RIBOS_SCHEMA_HELPER_TERMINAL_BOOT_ACTION) != 0) !=
             (execution->effect ==
              RIBOS_VM_HELPER_EFFECT_TERMINAL))) {
            return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
        }
    }

    imports = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_HELPER_IMPORTS);
    if (imports == NULL) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    for (import_index = 0;
         import_index < imports->count;
         ++import_index) {
        const uint8_t *row =
            imports->bytes +
            (size_t)import_index * imports->row_size;
        uint32_t stable_id =
            (uint32_t)row[0] |
            ((uint32_t)row[1] << 8) |
            ((uint32_t)row[2] << 16) |
            ((uint32_t)row[3] << 24);
        uint32_t capabilities =
            (uint32_t)row[4] |
            ((uint32_t)row[5] << 8) |
            ((uint32_t)row[6] << 16) |
            ((uint32_t)row[7] << 24);
        int found = 0;

        for (binding_index = 0;
             binding_index < contract->binding_count;
             ++binding_index) {
            const RibosVmHelperExecutionDescriptor *execution =
                &contract->bindings[binding_index].execution;

            if (execution->stable_id == stable_id &&
                execution->required_capabilities == capabilities) {
                found = 1;
                break;
            }
        }
        if (!found) {
            return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
        }
    }
    return RIBOS_VM_STATUS_OK;
}

static RibosVmStatus
ribos_prepared_validate_limits(
    const RibosVmLimits *limits,
    const RibosArtifactView *view,
    const RibosVerifierReport *report)
{
    RibosVmStatus status = ribos_vm_limits_validate_v1(limits);

    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (limits->maximum_instructions <
            report->recomputed_instruction_upper_bound ||
        limits->maximum_instructions > view->instruction_budget ||
        limits->maximum_helper_calls <
            report->recomputed_helper_upper_bound ||
        limits->maximum_helper_calls > view->helper_budget ||
        limits->maximum_stack_bytes <
            report->recomputed_stack_bytes ||
        limits->maximum_call_depth <
            report->recomputed_call_depth) {
        return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
    }
    return RIBOS_VM_STATUS_OK;
}

static void
ribos_prepared_binding_identity(
    const RibosArtifactView *view,
    const uint8_t helper_digest[RIBOS_VM_DIGEST_BYTES],
    const RibosVmLimits *limits,
    uint8_t digest[RIBOS_VM_DIGEST_BYTES])
{
    RibosArtifactSha256 hash;

    ribos_artifact_sha256_initialize(&hash);
    ribos_prepared_hash_domain(&hash, "RIBOS-PREPARED-BINDING-V1");
    ribos_prepared_hash_bytes(
        &hash,
        view->artifact_hash,
        RIBOS_VM_DIGEST_BYTES);
    ribos_prepared_hash_bytes(
        &hash,
        view->schema_digest,
        RIBOS_VM_DIGEST_BYTES);
    ribos_prepared_hash_bytes(
        &hash,
        helper_digest,
        RIBOS_VM_DIGEST_BYTES);
    ribos_prepared_hash_u16(
        &hash,
        RIBOS_VM_RUNTIME_ABI_V1_MAJOR);
    ribos_prepared_hash_u16(
        &hash,
        RIBOS_VM_RUNTIME_ABI_V1_MINOR);
    ribos_prepared_hash_u32(&hash, limits->flags);
    ribos_prepared_hash_u64(
        &hash,
        limits->maximum_instructions);
    ribos_prepared_hash_u64(
        &hash,
        limits->maximum_helper_calls);
    ribos_prepared_hash_u64(
        &hash,
        limits->maximum_stack_bytes);
    ribos_prepared_hash_u64(
        &hash,
        limits->maximum_arena_bytes);
    ribos_prepared_hash_u64(
        &hash,
        limits->maximum_input_bytes);
    ribos_prepared_hash_u64(
        &hash,
        limits->maximum_output_bytes);
    ribos_prepared_hash_u64(
        &hash,
        limits->maximum_operations);
    ribos_prepared_hash_u64(
        &hash,
        limits->maximum_polls);
    ribos_prepared_hash_u64(
        &hash,
        limits->maximum_execution_duration_ns);
    ribos_prepared_hash_u64(
        &hash,
        limits->maximum_helper_duration_ns);
    ribos_prepared_hash_u32(
        &hash,
        limits->maximum_call_depth);
    ribos_prepared_hash_u32(
        &hash,
        limits->maximum_handles);
    ribos_prepared_hash_u32(
        &hash,
        limits->maximum_trace_records);
    ribos_artifact_sha256_finish(&hash, digest);
}

size_t
ribos_prepared_program_workspace_alignment_v1(void)
{
    size_t alignment = _Alignof(struct RibosPreparedProgram);

    if (_Alignof(RibosVmHelperBinding) > alignment) {
        alignment = _Alignof(RibosVmHelperBinding);
    }
    if (_Alignof(uint64_t) > alignment) {
        alignment = _Alignof(uint64_t);
    }
    return alignment;
}

RibosVmStatus
ribos_prepared_program_workspace_size_v1(
    const RibosAuthorizedArtifact *authorized_artifact,
    const RibosVmHelperContract *helper_contract,
    size_t *required_size)
{
    RibosVerifierReport report;
    size_t verifier_size;
    size_t binding_bytes;
    size_t size = 0;
    RibosVmStatus status;

    if (required_size == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *required_size = 0;
    status = ribos_authorized_artifact_validate_v1(
        authorized_artifact);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_helper_contract_validate_v1(helper_contract);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (!ribos_artifact_size_multiply(
            helper_contract->binding_count,
            sizeof(RibosVmHelperBinding),
            &binding_bytes) ||
        ribos_verifier_workspace_size_v1(
            authorized_artifact->artifact,
            authorized_artifact->artifact_size,
            &verifier_size,
            &report) != RIBOS_VERIFIER_OK ||
        !ribos_prepared_size_append(
            &size,
            _Alignof(struct RibosPreparedProgram),
            sizeof(struct RibosPreparedProgram)) ||
        !ribos_prepared_size_append(
            &size,
            1,
            authorized_artifact->artifact_size) ||
        !ribos_prepared_size_append(
            &size,
            _Alignof(RibosVmHelperBinding),
            binding_bytes) ||
        !ribos_prepared_size_append(
            &size,
            8,
            verifier_size)) {
        return RIBOS_VM_STATUS_INVALID_SIZE;
    }
    *required_size = size;
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_prepare_program_v1(
    const RibosAuthorizedArtifact *authorized_artifact,
    const RibosProductSchema *schema,
    const RibosVmHelperContract *helper_contract,
    const RibosVmLimits *effective_limits,
    void *workspace,
    size_t workspace_size,
    RibosVerifierReport *report,
    const RibosPreparedProgram **prepared_program)
{
    RibosPreparedArena arena;
    struct RibosPreparedProgram *prepared;
    RibosVmHelperBinding *bindings;
    uint8_t *artifact;
    void *verifier_workspace;
    uint8_t schema_digest[RIBOS_VM_DIGEST_BYTES];
    uint8_t schema_digest_after[RIBOS_VM_DIGEST_BYTES];
    uint8_t helper_digest[RIBOS_VM_DIGEST_BYTES];
    uint8_t copied_helper_digest[RIBOS_VM_DIGEST_BYTES];
    size_t required_size;
    size_t verifier_size;
    size_t binding_bytes;
    RibosVerifierReport local_report;
    RibosVerifierStatus verifier_status;
    RibosVmStatus status;

    if (prepared_program == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *prepared_program = NULL;
    if (report == NULL) {
        report = &local_report;
    }
    memset(report, 0, sizeof(*report));
    status = ribos_authorized_artifact_validate_v1(
        authorized_artifact);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (schema == NULL ||
        ribos_schema_compute_identity(schema, schema_digest) !=
            RIBOS_SCHEMA_OK) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    if (!ribos_prepared_digest_equal(
            schema_digest,
            authorized_artifact->view.schema_digest) ||
        !ribos_prepared_digest_equal(
            schema_digest,
            authorized_artifact->receipt.schema_digest)) {
        return RIBOS_VM_STATUS_DIGEST_MISMATCH;
    }
    status = ribos_prepared_program_workspace_size_v1(
        authorized_artifact,
        helper_contract,
        &required_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (workspace == NULL ||
        !ribos_prepared_workspace_is_aligned(
            workspace,
            ribos_prepared_program_workspace_alignment_v1())) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    if (workspace_size < required_size) {
        return RIBOS_VM_STATUS_ARENA_TOO_SMALL;
    }
    if (!ribos_artifact_size_multiply(
            helper_contract->binding_count,
            sizeof(RibosVmHelperBinding),
            &binding_bytes) ||
        ribos_verifier_workspace_size_v1(
            authorized_artifact->artifact,
            authorized_artifact->artifact_size,
            &verifier_size,
            report) != RIBOS_VERIFIER_OK) {
        return RIBOS_VM_STATUS_NOT_PREPARED;
    }
    memset(workspace, 0, required_size);
    arena = (RibosPreparedArena){
        .bytes = workspace,
        .capacity = required_size,
    };
    prepared = ribos_prepared_arena_take(
        &arena,
        _Alignof(struct RibosPreparedProgram),
        sizeof(*prepared));
    artifact = ribos_prepared_arena_take(
        &arena,
        1,
        authorized_artifact->artifact_size);
    bindings = ribos_prepared_arena_take(
        &arena,
        _Alignof(RibosVmHelperBinding),
        binding_bytes);
    verifier_workspace = ribos_prepared_arena_take(
        &arena,
        8,
        verifier_size);
    if (arena.failed || prepared == NULL || artifact == NULL ||
        bindings == NULL || verifier_workspace == NULL) {
        return RIBOS_VM_STATUS_INTERNAL_ERROR;
    }
    memcpy(
        artifact,
        authorized_artifact->artifact,
        authorized_artifact->artifact_size);
    if (ribos_artifact_open_v1(
            artifact,
            authorized_artifact->artifact_size,
            &prepared->view) != RIBOS_ARTIFACT_OK) {
        return RIBOS_VM_STATUS_NOT_PREPARED;
    }
    ribos_artifact_sha256(
        artifact,
        authorized_artifact->artifact_size,
        prepared->artifact_bytes_digest);
    if (!ribos_prepared_digest_equal(
            prepared->artifact_bytes_digest,
            authorized_artifact->artifact_bytes_digest)) {
        return RIBOS_VM_STATUS_DIGEST_MISMATCH;
    }

    verifier_status = ribos_verify_artifact_stage1_v1(
        artifact,
        authorized_artifact->artifact_size,
        schema,
        verifier_workspace,
        verifier_size,
        report);
    if (verifier_status != RIBOS_VERIFIER_OK) {
        return RIBOS_VM_STATUS_NOT_PREPARED;
    }
    verifier_status = ribos_verify_artifact_stage2_v1(
        artifact,
        authorized_artifact->artifact_size,
        schema,
        verifier_workspace,
        verifier_size,
        report);
    if (verifier_status != RIBOS_VERIFIER_OK) {
        return RIBOS_VM_STATUS_NOT_PREPARED;
    }
    if (ribos_schema_compute_identity(
            schema,
            schema_digest_after) != RIBOS_SCHEMA_OK ||
        !ribos_prepared_digest_equal(
            schema_digest,
            schema_digest_after)) {
        return RIBOS_VM_STATUS_DIGEST_MISMATCH;
    }

    status = ribos_vm_helper_contract_compute_identity_v1(
        helper_contract,
        helper_digest);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (!ribos_prepared_digest_equal(
            helper_digest,
            helper_contract->digest) ||
        !ribos_prepared_digest_equal(
            helper_digest,
            authorized_artifact->
                receipt.helper_execution_digest)) {
        return RIBOS_VM_STATUS_DIGEST_MISMATCH;
    }
    status = ribos_prepared_validate_helper_schema(
        schema,
        helper_contract,
        &prepared->view);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    memcpy(bindings, helper_contract->bindings, binding_bytes);
    prepared->helper_contract = *helper_contract;
    prepared->helper_contract.bindings = bindings;
    status = ribos_vm_helper_contract_compute_identity_v1(
        &prepared->helper_contract,
        copied_helper_digest);
    if (status != RIBOS_VM_STATUS_OK ||
        !ribos_prepared_digest_equal(
            helper_digest,
            copied_helper_digest)) {
        return RIBOS_VM_STATUS_DIGEST_MISMATCH;
    }
    status = ribos_prepared_validate_limits(
        effective_limits,
        &prepared->view,
        report);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }

    prepared->major = RIBOS_VM_PREPARED_PROGRAM_V1_MAJOR;
    prepared->minor = RIBOS_VM_PREPARED_PROGRAM_V1_MINOR;
    prepared->artifact = artifact;
    prepared->artifact_size = authorized_artifact->artifact_size;
    prepared->authorization = authorized_artifact->receipt;
    memcpy(
        prepared->authorization_digest,
        authorized_artifact->receipt_digest,
        RIBOS_VM_DIGEST_BYTES);
    memcpy(
        prepared->schema_digest,
        schema_digest,
        RIBOS_VM_DIGEST_BYTES);
    memcpy(
        prepared->helper_execution_digest,
        helper_digest,
        RIBOS_VM_DIGEST_BYTES);
    prepared->report = *report;
    prepared->limits = *effective_limits;
    ribos_prepared_binding_identity(
        &prepared->view,
        helper_digest,
        effective_limits,
        prepared->binding_digest);
    prepared->state = RIBOS_PREPARED_STATE_READY;
    prepared->magic = RIBOS_PREPARED_PROGRAM_MAGIC;
    *prepared_program = prepared;
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_prepared_program_validate_v1(
    const RibosPreparedProgram *prepared_program)
{
    RibosArtifactView view;
    uint8_t artifact_bytes_digest[RIBOS_VM_DIGEST_BYTES];
    uint8_t authorization_digest[RIBOS_VM_DIGEST_BYTES];
    uint8_t helper_digest[RIBOS_VM_DIGEST_BYTES];
    uint8_t binding_digest[RIBOS_VM_DIGEST_BYTES];
    RibosVmStatus status;

    if (prepared_program == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    if (prepared_program->magic != RIBOS_PREPARED_PROGRAM_MAGIC ||
        prepared_program->major !=
            RIBOS_VM_PREPARED_PROGRAM_V1_MAJOR ||
        prepared_program->minor !=
            RIBOS_VM_PREPARED_PROGRAM_V1_MINOR ||
        prepared_program->state != RIBOS_PREPARED_STATE_READY ||
        prepared_program->artifact == NULL ||
        prepared_program->artifact_size <
            RIBOS_ARTIFACT_ENVELOPE_BYTES ||
        prepared_program->report.status != RIBOS_VERIFIER_OK) {
        return RIBOS_VM_STATUS_NOT_PREPARED;
    }
    if (ribos_artifact_open_v1(
            prepared_program->artifact,
            prepared_program->artifact_size,
            &view) != RIBOS_ARTIFACT_OK) {
        return RIBOS_VM_STATUS_NOT_PREPARED;
    }
    ribos_artifact_sha256(
        prepared_program->artifact,
        prepared_program->artifact_size,
        artifact_bytes_digest);
    if (!ribos_prepared_digest_equal(
            artifact_bytes_digest,
            prepared_program->artifact_bytes_digest) ||
        !ribos_prepared_digest_equal(
            view.schema_digest,
            prepared_program->schema_digest) ||
        !ribos_prepared_digest_equal(
            view.artifact_hash,
            prepared_program->authorization.artifact_hash)) {
        return RIBOS_VM_STATUS_DIGEST_MISMATCH;
    }
    status = ribos_authorization_receipt_validate_v1(
        &prepared_program->authorization,
        &view);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    ribos_authorization_receipt_identity(
        &prepared_program->authorization,
        authorization_digest);
    if (!ribos_prepared_digest_equal(
            authorization_digest,
            prepared_program->authorization_digest)) {
        return RIBOS_VM_STATUS_DIGEST_MISMATCH;
    }
    status = ribos_vm_helper_contract_compute_identity_v1(
        &prepared_program->helper_contract,
        helper_digest);
    if (status != RIBOS_VM_STATUS_OK ||
        !ribos_prepared_digest_equal(
            helper_digest,
            prepared_program->helper_execution_digest) ||
        !ribos_prepared_digest_equal(
            helper_digest,
            prepared_program->helper_contract.digest) ||
        !ribos_prepared_digest_equal(
            helper_digest,
            prepared_program->
                authorization.helper_execution_digest)) {
        return RIBOS_VM_STATUS_DIGEST_MISMATCH;
    }
    status = ribos_prepared_validate_limits(
        &prepared_program->limits,
        &view,
        &prepared_program->report);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    ribos_prepared_binding_identity(
        &view,
        helper_digest,
        &prepared_program->limits,
        binding_digest);
    if (!ribos_prepared_digest_equal(
            binding_digest,
            prepared_program->binding_digest)) {
        return RIBOS_VM_STATUS_DIGEST_MISMATCH;
    }
    return RIBOS_VM_STATUS_OK;
}

const RibosVerifierReport *
ribos_prepared_program_report_v1(
    const RibosPreparedProgram *prepared_program)
{
    return ribos_prepared_program_validate_v1(prepared_program) ==
            RIBOS_VM_STATUS_OK ?
        &prepared_program->report : NULL;
}

const RibosVmLimits *
ribos_prepared_program_limits_v1(
    const RibosPreparedProgram *prepared_program)
{
    return ribos_prepared_program_validate_v1(prepared_program) ==
            RIBOS_VM_STATUS_OK ?
        &prepared_program->limits : NULL;
}

const uint8_t *
ribos_prepared_program_binding_digest_v1(
    const RibosPreparedProgram *prepared_program)
{
    return ribos_prepared_program_validate_v1(prepared_program) ==
            RIBOS_VM_STATUS_OK ?
        prepared_program->binding_digest : NULL;
}

const RibosArtifactView *
ribos_prepared_program_artifact_view_v1(
    const RibosPreparedProgram *prepared_program)
{
    return ribos_prepared_program_validate_v1(prepared_program) ==
            RIBOS_VM_STATUS_OK ?
        &prepared_program->view : NULL;
}

const RibosVmHelperContract *
ribos_prepared_program_helper_contract_v1(
    const RibosPreparedProgram *prepared_program)
{
    return ribos_prepared_program_validate_v1(prepared_program) ==
            RIBOS_VM_STATUS_OK ?
        &prepared_program->helper_contract : NULL;
}
