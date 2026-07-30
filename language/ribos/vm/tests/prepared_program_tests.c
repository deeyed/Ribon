#include "ribos/vm/prepared.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TestAuthority {
    uint8_t helper_digest[RIBOS_VM_DIGEST_BYTES];
    int reject;
    int corrupt_receipt;
    unsigned calls;
} TestAuthority;

static uint32_t
invoke_helper(void *context, RibosVmHelperCall *call)
{
    (void)context;
    (void)call;
    return RIBOS_VM_HELPER_CALLBACK_OK;
}

static uint32_t
invoke_other_helper(void *context, RibosVmHelperCall *call)
{
    (void)context;
    (void)call;
    return RIBOS_VM_HELPER_CALLBACK_POLICY_ERROR;
}

static uint32_t
authorize_for_test(
    void *context,
    const RibosArtifactAuthorizationRequest *request,
    RibosArtifactAuthorizationReceipt *receipt)
{
    TestAuthority *authority = context;

    ++authority->calls;
    if (authority->reject) {
        return RIBOS_VM_STATUS_NOT_AUTHORIZED;
    }
    if (request == NULL || receipt == NULL ||
        request->size != sizeof(*request) ||
        request->authorization_major !=
            RIBOS_VM_AUTHORIZATION_V1_MAJOR ||
        request->authorization_minor !=
            RIBOS_VM_AUTHORIZATION_V1_MINOR ||
        request->artifact == NULL ||
        request->artifact_size < RIBOS_ARTIFACT_ENVELOPE_BYTES ||
        !ribos_vm_digest_is_nonzero(request->artifact_hash) ||
        !ribos_vm_digest_is_nonzero(request->schema_digest)) {
        return RIBOS_VM_STATUS_NOT_AUTHORIZED;
    }
    *receipt = (RibosArtifactAuthorizationReceipt){
        .size = sizeof(*receipt),
        .authorization_major = RIBOS_VM_AUTHORIZATION_V1_MAJOR,
        .authorization_minor = RIBOS_VM_AUTHORIZATION_V1_MINOR,
        .decision = RIBOS_ARTIFACT_AUTHORIZATION_GRANTED,
        .authority_generation = 9,
        .manifest_sequence = 12,
        .rollback_floor = 10,
        .policy_identity_digest = {0x51},
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
        authority->helper_digest,
        RIBOS_VM_DIGEST_BYTES);
    if (authority->corrupt_receipt) {
        receipt->artifact_hash[0] ^= 0xff;
    }
    return RIBOS_VM_STATUS_OK;
}

static int
expect_status(
    const char *name,
    RibosVmStatus actual,
    RibosVmStatus expected)
{
    if (actual == expected) {
        return 1;
    }
    fprintf(
        stderr,
        "%s: expected=%s actual=%s\n",
        name,
        ribos_vm_status_name(expected),
        ribos_vm_status_name(actual));
    return 0;
}

static int
expect_true(const char *name, int condition)
{
    if (condition) {
        return 1;
    }
    fprintf(stderr, "%s: condition failed\n", name);
    return 0;
}

static uint8_t *
read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    uint8_t *bytes;
    long length;

    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        return NULL;
    }
    length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    bytes = malloc((size_t)length);
    if (bytes == NULL ||
        fread(bytes, 1, (size_t)length, file) !=
            (size_t)length) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return bytes;
}

static RibosVmHelperExecutionDescriptor
execution_descriptor(
    uint32_t stable_id,
    uint32_t capabilities,
    uint32_t effect,
    uint32_t transition,
    uint32_t transition_parameter)
{
    uint32_t durability = RIBOS_VM_HELPER_DURABILITY_NONE;

    if (effect == RIBOS_VM_HELPER_EFFECT_TERMINAL) {
        durability = RIBOS_VM_HELPER_DURABILITY_SEALED_INTENT;
    }
    return (RibosVmHelperExecutionDescriptor){
        .size = sizeof(RibosVmHelperExecutionDescriptor),
        .contract_major = RIBOS_VM_HELPER_EXECUTION_V1_MAJOR,
        .contract_minor = RIBOS_VM_HELPER_EXECUTION_V1_MINOR,
        .stable_id = stable_id,
        .required_capabilities = capabilities,
        .effect = effect,
        .execution_mode = RIBOS_VM_HELPER_EXECUTION_SYNCHRONOUS,
        .durability = durability,
        .handle_transition = transition,
        .transition_parameter = transition_parameter,
        .allowed_mode_mask = UINT64_C(1),
        .allowed_phase_mask = UINT64_C(1),
        .maximum_input_bytes = 256,
        .maximum_output_bytes = 256,
        .maximum_operations = 4,
        .maximum_polls = 4,
        .maximum_duration_ns = 10000,
    };
}

static void
initialize_helper_contract(
    RibosVmHelperBinding bindings[5],
    RibosVmHelperContract *contract)
{
    memset(bindings, 0, sizeof(*bindings) * 5);
    bindings[0].execution = execution_descriptor(
        2,
        RIBOS_CAPABILITY_INSPECT,
        RIBOS_VM_HELPER_EFFECT_PURE,
        RIBOS_VM_HANDLE_TRANSITION_NONE,
        RIBOS_VM_INVALID_ID);
    bindings[1].execution = execution_descriptor(
        8,
        RIBOS_CAPABILITY_INSPECT,
        RIBOS_VM_HELPER_EFFECT_PURE,
        RIBOS_VM_HANDLE_TRANSITION_CREATE,
        RIBOS_VM_INVALID_ID);
    bindings[2].execution = execution_descriptor(
        11,
        RIBOS_CAPABILITY_INSPECT,
        RIBOS_VM_HELPER_EFFECT_PURE,
        RIBOS_VM_HANDLE_TRANSITION_REPLACE,
        0);
    bindings[3].execution = execution_descriptor(
        21,
        RIBOS_CAPABILITY_BOOT,
        RIBOS_VM_HELPER_EFFECT_TERMINAL,
        RIBOS_VM_HANDLE_TRANSITION_TERMINAL_CONSUME,
        1);
    bindings[4].execution = execution_descriptor(
        22,
        RIBOS_CAPABILITY_BOOT,
        RIBOS_VM_HELPER_EFFECT_TERMINAL,
        RIBOS_VM_HANDLE_TRANSITION_NONE,
        RIBOS_VM_INVALID_ID);
    for (size_t index = 0; index < 5; ++index) {
        bindings[index].invoke = invoke_helper;
    }
    *contract = (RibosVmHelperContract){
        .size = sizeof(*contract),
        .contract_major = RIBOS_VM_HELPER_EXECUTION_V1_MAJOR,
        .contract_minor = RIBOS_VM_HELPER_EXECUTION_V1_MINOR,
        .binding_count = 5,
        .bindings = bindings,
    };
    if (ribos_vm_helper_contract_compute_identity_v1(
            contract,
            contract->digest) != RIBOS_VM_STATUS_OK) {
        memset(contract->digest, 0, sizeof(contract->digest));
    }
}

static RibosVmLimits
effective_limits(void)
{
    return (RibosVmLimits){
        .size = sizeof(RibosVmLimits),
        .runtime_abi_major = RIBOS_VM_RUNTIME_ABI_V1_MAJOR,
        .runtime_abi_minor = RIBOS_VM_RUNTIME_ABI_V1_MINOR,
        .maximum_instructions = 64,
        .maximum_helper_calls = 4,
        .maximum_stack_bytes = 4096,
        .maximum_arena_bytes = 16384,
        .maximum_input_bytes = 4096,
        .maximum_output_bytes = 4096,
        .maximum_operations = 64,
        .maximum_polls = 64,
        .maximum_execution_duration_ns = 1000000,
        .maximum_helper_duration_ns = 100000,
        .maximum_call_depth = 8,
        .maximum_handles = 16,
        .maximum_trace_records = 32,
    };
}

static uint8_t *
find_bytes(
    uint8_t *haystack,
    size_t haystack_size,
    const uint8_t *needle,
    size_t needle_size)
{
    size_t offset;

    if (needle_size == 0 || needle_size > haystack_size) {
        return NULL;
    }
    for (offset = 0;
         offset <= haystack_size - needle_size;
         ++offset) {
        if (memcmp(
                haystack + offset,
                needle,
                needle_size) == 0) {
            return haystack + offset;
        }
    }
    return NULL;
}

static int
test_authorization_rejections(
    uint8_t *artifact,
    size_t artifact_size,
    RibosArtifactAuthorizer *authorizer,
    TestAuthority *authority,
    size_t workspace_size)
{
    const RibosAuthorizedArtifact *authorized = NULL;
    void *workspace = malloc(workspace_size);
    int passed;

    if (workspace == NULL) {
        return 0;
    }
    authority->reject = 1;
    passed = expect_status(
        "product rejection",
        ribos_authorize_artifact_v1(
            artifact,
            artifact_size,
            authorizer,
            workspace,
            workspace_size,
            &authorized),
        RIBOS_VM_STATUS_NOT_AUTHORIZED) &&
        authorized == NULL;
    authority->reject = 0;
    authority->corrupt_receipt = 1;
    passed = passed && expect_status(
        "receipt hash mismatch",
        ribos_authorize_artifact_v1(
            artifact,
            artifact_size,
            authorizer,
            workspace,
            workspace_size,
            &authorized),
        RIBOS_VM_STATUS_DIGEST_MISMATCH) &&
        authorized == NULL;
    authority->corrupt_receipt = 0;
    free(workspace);
    return passed;
}

static int
test_prepared_lifetime(
    uint8_t *artifact,
    size_t artifact_size,
    RibosVmHelperBinding bindings[5],
    RibosVmHelperContract *contract,
    RibosArtifactAuthorizer *authorizer)
{
    const RibosProductSchema *reference = ribos_schema_reference_v1();
    RibosProductSchema mutable_schema = *reference;
    RibosVmLimits limits = effective_limits();
    TestAuthority *authority = authorizer->authority_context;
    const RibosAuthorizedArtifact *authorized = NULL;
    const RibosPreparedProgram *prepared = NULL;
    RibosVerifierReport report;
    uint8_t *authorized_artifact_bytes;
    uint8_t *prepared_artifact_bytes;
    uint8_t *prepared_binding_bytes;
    uint8_t *artifact_snapshot = malloc(artifact_size);
    RibosVmHelperBinding binding_snapshot[5];
    void *authorized_workspace = NULL;
    void *prepared_workspace = NULL;
    void *mutation_workspace = NULL;
    size_t authorized_workspace_size = 0;
    size_t prepared_workspace_size = 0;
    int passed = artifact_snapshot != NULL;

    if (!passed) {
        return 0;
    }
    memcpy(artifact_snapshot, artifact, artifact_size);
    memcpy(binding_snapshot, bindings, sizeof(binding_snapshot));
    passed = expect_status(
        "authorized workspace query",
        ribos_authorized_artifact_workspace_size_v1(
            artifact_size,
            &authorized_workspace_size),
        RIBOS_VM_STATUS_OK);
    authorized_workspace = passed ?
        malloc(authorized_workspace_size) : NULL;
    passed = passed && expect_true(
        "authorized workspace allocation",
        authorized_workspace != NULL);
    if (passed) {
        passed = expect_status(
            "authorize",
            ribos_authorize_artifact_v1(
                artifact,
                artifact_size,
                authorizer,
                authorized_workspace,
                authorized_workspace_size,
                &authorized),
            RIBOS_VM_STATUS_OK);
    }
    artifact[0] ^= 0xff;
    passed = passed && expect_status(
        "raw source detached",
        ribos_authorized_artifact_validate_v1(authorized),
        RIBOS_VM_STATUS_OK);
    artifact[0] ^= 0xff;

    authorized_artifact_bytes = passed ?
        find_bytes(
            authorized_workspace,
            authorized_workspace_size,
            artifact_snapshot,
            artifact_size) : NULL;
    passed = passed && expect_true(
        "find authorized artifact copy",
        authorized_artifact_bytes != NULL);
    if (passed) {
        authorized_artifact_bytes[artifact_size - 1] ^= 0x01;
        passed = expect_status(
            "authorized bytes mutation",
            ribos_authorized_artifact_validate_v1(authorized),
            RIBOS_VM_STATUS_NOT_AUTHORIZED);
        authorized_artifact_bytes[artifact_size - 1] ^= 0x01;
    }
    passed = passed && expect_status(
        "authorized restored",
        ribos_authorized_artifact_validate_v1(authorized),
        RIBOS_VM_STATUS_OK);

    mutable_schema.product_id = "ribon.hostile.schema";
    if (passed) {
        passed = expect_status(
            "schema mutation rejected",
            ribos_prepare_program_v1(
                authorized,
                &mutable_schema,
                contract,
                &limits,
                authorized_workspace,
                0,
                &report,
                &prepared),
            RIBOS_VM_STATUS_DIGEST_MISMATCH);
    }
    mutable_schema = *reference;
    if (passed) {
        passed = expect_status(
            "mutation workspace query",
            ribos_prepared_program_workspace_size_v1(
                authorized,
                contract,
                &prepared_workspace_size),
            RIBOS_VM_STATUS_OK);
    }
    mutation_workspace = passed ?
        malloc(prepared_workspace_size) : NULL;
    passed = passed && expect_true(
        "mutation workspace allocation",
        mutation_workspace != NULL);
    bindings[0].execution.maximum_operations += 1;
    if (passed) {
        passed = expect_status(
            "helper mutation rejected",
            ribos_prepare_program_v1(
                authorized,
                &mutable_schema,
                contract,
                &limits,
                mutation_workspace,
                prepared_workspace_size,
                &report,
                &prepared),
            RIBOS_VM_STATUS_DIGEST_MISMATCH);
    }
    bindings[0] = binding_snapshot[0];
    free(mutation_workspace);
    mutation_workspace = NULL;

    if (passed) {
        passed = expect_status(
            "prepared workspace query",
            ribos_prepared_program_workspace_size_v1(
                authorized,
                contract,
                &prepared_workspace_size),
            RIBOS_VM_STATUS_OK);
    }
    prepared_workspace = passed ?
        malloc(prepared_workspace_size) : NULL;
    passed = passed && expect_true(
        "prepared workspace allocation",
        prepared_workspace != NULL);
    if (passed) {
        passed = expect_status(
            "prepare",
            ribos_prepare_program_v1(
                authorized,
                &mutable_schema,
                contract,
                &limits,
                prepared_workspace,
                prepared_workspace_size,
                &report,
                &prepared),
            RIBOS_VM_STATUS_OK);
    }
    passed = passed && expect_true(
        "prepared report and accessors",
        report.status == RIBOS_VERIFIER_OK &&
            report.recomputed_instruction_upper_bound == 23 &&
            report.recomputed_helper_upper_bound == 4 &&
            ribos_prepared_program_report_v1(prepared) != NULL &&
            ribos_prepared_program_limits_v1(prepared) != NULL &&
            ribos_prepared_program_binding_digest_v1(prepared) != NULL &&
            ribos_prepared_program_artifact_view_v1(prepared) != NULL &&
            ribos_prepared_program_helper_contract_v1(prepared) != NULL);

    artifact[1] ^= 0xff;
    mutable_schema.product_id = "ribon.hostile.after.prepare";
    bindings[0].execution.maximum_operations += 7;
    bindings[0].invoke = invoke_other_helper;
    passed = passed && expect_status(
        "original inputs detached",
        ribos_prepared_program_validate_v1(prepared),
        RIBOS_VM_STATUS_OK);
    artifact[1] ^= 0xff;
    mutable_schema = *reference;
    memcpy(bindings, binding_snapshot, sizeof(binding_snapshot));

    prepared_artifact_bytes = passed ?
        find_bytes(
            prepared_workspace,
            prepared_workspace_size,
            artifact_snapshot,
            artifact_size) : NULL;
    passed = passed && expect_true(
        "find prepared artifact copy",
        prepared_artifact_bytes != NULL);
    if (passed) {
        prepared_artifact_bytes[artifact_size - 1] ^= 0x01;
        passed = expect_status(
            "prepared artifact mutation",
            ribos_prepared_program_validate_v1(prepared),
            RIBOS_VM_STATUS_NOT_PREPARED);
        prepared_artifact_bytes[artifact_size - 1] ^= 0x01;
    }
    passed = passed && expect_status(
        "prepared artifact restored",
        ribos_prepared_program_validate_v1(prepared),
        RIBOS_VM_STATUS_OK);

    prepared_binding_bytes = passed ?
        find_bytes(
            prepared_workspace,
            prepared_workspace_size,
            (const uint8_t *)binding_snapshot,
            sizeof(binding_snapshot)) : NULL;
    passed = passed && expect_true(
        "find prepared helper copy",
        prepared_binding_bytes != NULL);
    if (passed) {
        RibosVmHelperBinding *copied_bindings =
            (RibosVmHelperBinding *)prepared_binding_bytes;

        copied_bindings[0].execution.maximum_operations += 1;
        passed = expect_status(
            "prepared helper mutation",
            ribos_prepared_program_validate_v1(prepared),
            RIBOS_VM_STATUS_DIGEST_MISMATCH);
        copied_bindings[0].execution.maximum_operations -= 1;
    }
    passed = passed && expect_status(
        "prepared helper restored",
        ribos_prepared_program_validate_v1(prepared),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "authorization callback count",
        authority->calls >= 1);

    free(prepared_workspace);
    free(authorized_workspace);
    free(artifact_snapshot);
    return passed;
}

int
main(int argc, char **argv)
{
    RibosVmHelperBinding bindings[5];
    RibosVmHelperContract contract;
    TestAuthority authority = {0};
    RibosArtifactAuthorizer authorizer = {
        .size = sizeof(authorizer),
        .authorization_major = RIBOS_VM_AUTHORIZATION_V1_MAJOR,
        .authorization_minor = RIBOS_VM_AUTHORIZATION_V1_MINOR,
        .authority_context = &authority,
        .authorize = authorize_for_test,
    };
    uint8_t *artifact;
    size_t artifact_size = 0;
    size_t authorization_workspace_size = 0;
    int passed;

    if (argc != 2) {
        fprintf(stderr, "usage: %s POLICY.rba\n", argv[0]);
        return 2;
    }
    artifact = read_file(argv[1], &artifact_size);
    if (artifact == NULL) {
        fprintf(stderr, "cannot read artifact: %s\n", argv[1]);
        return 2;
    }
    initialize_helper_contract(bindings, &contract);
    memcpy(
        authority.helper_digest,
        contract.digest,
        RIBOS_VM_DIGEST_BYTES);
    passed = expect_true(
            "helper contract identity",
            ribos_vm_digest_is_nonzero(contract.digest)) &&
        expect_status(
            "authorization workspace",
            ribos_authorized_artifact_workspace_size_v1(
                artifact_size,
                &authorization_workspace_size),
            RIBOS_VM_STATUS_OK) &&
        test_authorization_rejections(
            artifact,
            artifact_size,
            &authorizer,
            &authority,
            authorization_workspace_size) &&
        test_prepared_lifetime(
            artifact,
            artifact_size,
            bindings,
            &contract,
            &authorizer);
    free(artifact);
    if (!passed) {
        fprintf(stderr, "RIBOS-PREPARED-PROGRAM-FAIL\n");
        return 1;
    }
    puts(
        "RIBOS-PREPARED-PROGRAM-OK "
        "authorization=separate stage1=yes stage2=yes "
        "artifact-copy=sealed helper-copy=sealed");
    return 0;
}
