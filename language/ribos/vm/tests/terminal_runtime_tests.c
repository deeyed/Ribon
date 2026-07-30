#include "ribos/vm/terminal.h"

#include "internal.h"
#include "storage_internal.h"
#include "terminal_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TestAuthority {
    uint8_t helper_digest[RIBOS_VM_DIGEST_BYTES];
} TestAuthority;

typedef struct TestPrepared {
    uint8_t *artifact;
    size_t artifact_size;
    void *authorized_workspace;
    const RibosAuthorizedArtifact *authorized;
    void *prepared_workspace;
    const RibosPreparedProgram *prepared;
    RibosVmHelperBinding bindings[2];
    RibosVmHelperContract contract;
    RibosVmLimits limits;
    RibosVerifierReport report;
    TestAuthority authority;
    uint32_t context_type;
    uint32_t context_size;
    uint32_t unit_type;
    uint32_t action_type;
    uint32_t action_size;
} TestPrepared;

typedef struct TestArena {
    void *allocation;
    size_t arena_size;
    RibosVmStorage *storage;
    RibosVmStoragePlan plan;
    RibosVmHandleHostTable handles;
} TestArena;

typedef struct TestEmbedder {
    const TestPrepared *prepared;
    uint64_t now_ns;
    uint32_t recovery_calls;
    uint32_t helper_calls;
    uint32_t journal_fault;
    uint32_t journal_state;
    RibosVmFaultReceipt recovery_receipt;
} TestEmbedder;

static uint16_t
read_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] |
        ((uint16_t)bytes[1] << 8);
}

static uint32_t
read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
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

static const uint8_t *
section_row(
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
find_named_type(
    const RibosArtifactView *view,
    const char *name,
    uint32_t *type_id,
    uint32_t *byte_size)
{
    const RibosArtifactSectionView *types =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_TYPES);
    size_t name_size = strlen(name);
    uint32_t index;

    if (types == NULL || type_id == NULL ||
        byte_size == NULL) {
        return 0;
    }
    for (index = 0; index < types->count; ++index) {
        const uint8_t *row = section_row(types, index);
        uint32_t length;

        if (row == NULL) {
            return 0;
        }
        length = read_u32(row + 56);
        if (length == name_size &&
            length <= RIBOS_ARTIFACT_TYPE_ROW_BYTES - 60u &&
            memcmp(row + 60, name, length) == 0) {
            *type_id = index;
            *byte_size = read_u32(row + 40);
            return 1;
        }
    }
    return 0;
}

static int
find_unit_type(
    const RibosArtifactView *view,
    uint32_t *type_id)
{
    const RibosArtifactSectionView *types =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_TYPES);
    uint32_t index;

    if (types == NULL || type_id == NULL) {
        return 0;
    }
    for (index = 0; index < types->count; ++index) {
        const uint8_t *row = section_row(types, index);

        if (row != NULL &&
            read_u16(row + 4) == RIBOS_BC_TYPE_UNIT &&
            read_u32(row + 40) == 0) {
            *type_id = index;
            return 1;
        }
    }
    return 0;
}

static int
entry_context_type(
    const RibosArtifactView *view,
    uint32_t *type_id,
    uint32_t *byte_size)
{
    const RibosArtifactSectionView *functions =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_FUNCTIONS);
    const RibosArtifactSectionView *slots =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_SLOTS);
    const uint8_t *function =
        section_row(functions, view->entry_function);
    const uint8_t *slot;

    if (function == NULL ||
        read_u32(function + 36) != 1) {
        return 0;
    }
    slot = section_row(slots, read_u32(function + 32));
    if (slot == NULL) {
        return 0;
    }
    *type_id = read_u32(slot + 8);
    *byte_size = read_u32(slot + 16);
    return 1;
}

static uint32_t
authorize_for_test(
    void *context,
    const RibosArtifactAuthorizationRequest *request,
    RibosArtifactAuthorizationReceipt *receipt)
{
    TestAuthority *authority = context;

    if (authority == NULL || request == NULL ||
        receipt == NULL) {
        return RIBOS_VM_STATUS_NOT_AUTHORIZED;
    }
    *receipt = (RibosArtifactAuthorizationReceipt){
        .size = sizeof(*receipt),
        .authorization_major =
            RIBOS_VM_AUTHORIZATION_V1_MAJOR,
        .authorization_minor =
            RIBOS_VM_AUTHORIZATION_V1_MINOR,
        .decision = RIBOS_ARTIFACT_AUTHORIZATION_GRANTED,
        .authority_generation = 7,
        .manifest_sequence = 11,
        .rollback_floor = 3,
        .policy_identity_digest = {0x91},
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
    return RIBOS_VM_STATUS_OK;
}

static RibosVmHelperExecutionDescriptor
execution_descriptor(
    uint32_t stable_id,
    uint32_t capabilities,
    uint32_t effect,
    uint32_t durability)
{
    return (RibosVmHelperExecutionDescriptor){
        .size = sizeof(RibosVmHelperExecutionDescriptor),
        .contract_major =
            RIBOS_VM_HELPER_EXECUTION_V1_MAJOR,
        .contract_minor =
            RIBOS_VM_HELPER_EXECUTION_V1_MINOR,
        .stable_id = stable_id,
        .required_capabilities = capabilities,
        .effect = effect,
        .execution_mode =
            RIBOS_VM_HELPER_EXECUTION_SYNCHRONOUS,
        .durability = durability,
        .handle_transition =
            RIBOS_VM_HANDLE_TRANSITION_NONE,
        .transition_parameter = RIBOS_VM_INVALID_ID,
        .allowed_mode_mask = UINT64_C(1),
        .allowed_phase_mask = UINT64_C(1),
        .maximum_input_bytes = 256,
        .maximum_output_bytes = 256,
        .maximum_operations = 4,
        .maximum_polls = 4,
        .maximum_duration_ns = 10000,
    };
}

static uint32_t
test_now(void *context, uint64_t *now_ns)
{
    TestEmbedder *embedder = context;

    if (embedder == NULL || now_ns == NULL ||
        embedder->now_ns > UINT64_MAX - 10) {
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    embedder->now_ns += 10;
    *now_ns = embedder->now_ns;
    return RIBOS_VM_HELPER_CALLBACK_OK;
}

static void
test_recovery(
    void *context,
    const RibosVmFaultReceipt *receipt)
{
    TestEmbedder *embedder = context;

    if (embedder == NULL || receipt == NULL) {
        return;
    }
    ++embedder->recovery_calls;
    embedder->recovery_receipt = *receipt;
}

static uint32_t
test_helper(void *context, RibosVmHelperCall *call)
{
    TestEmbedder *embedder = context;
    RibosVmHelperCallInfo info;
    uint8_t action[32];
    uint8_t journal_digest[RIBOS_VM_DIGEST_BYTES] = {0x4a};

    if (embedder == NULL ||
        ribos_vm_helper_call_info_v1(call, &info) !=
            RIBOS_VM_STATUS_OK) {
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    ++embedder->helper_calls;
    if (ribos_vm_helper_call_consume_operations_v1(
            call,
            1) != RIBOS_VM_STATUS_OK) {
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    if (info.stable_id == 1) {
        uint32_t journal_state = embedder->journal_fault ?
            RIBOS_VM_JOURNAL_RECEIPT_PARTIAL :
            RIBOS_VM_JOURNAL_RECEIPT_COMMITTED;

        if (ribos_vm_helper_call_set_journal_receipt_v1(
                call,
                journal_state,
                journal_digest) != RIBOS_VM_STATUS_OK) {
            return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
        }
        embedder->journal_state = journal_state;
        if (embedder->journal_fault) {
            return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
        }
        return ribos_vm_helper_call_set_success_value_v1(
                call,
                embedder->prepared->unit_type,
                NULL,
                0) == RIBOS_VM_STATUS_OK ?
            RIBOS_VM_HELPER_CALLBACK_OK :
            RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    if (info.stable_id != 22 ||
        embedder->prepared->action_size > sizeof(action)) {
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    memset(action, 0x5a, sizeof(action));
    return ribos_vm_helper_call_set_success_value_v1(
            call,
            embedder->prepared->action_type,
            action,
            embedder->prepared->action_size) ==
            RIBOS_VM_STATUS_OK ?
        RIBOS_VM_HELPER_CALLBACK_OK :
        RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
}

static int
prepare_program(const char *path, TestPrepared *test)
{
    RibosArtifactAuthorizer authorizer = {
        .size = sizeof(authorizer),
        .authorization_major =
            RIBOS_VM_AUTHORIZATION_V1_MAJOR,
        .authorization_minor =
            RIBOS_VM_AUTHORIZATION_V1_MINOR,
        .authority_context = &test->authority,
        .authorize = authorize_for_test,
    };
    RibosArtifactView view;
    size_t workspace_size = 0;

    memset(test, 0, sizeof(*test));
    test->artifact = read_file(path, &test->artifact_size);
    if (test->artifact == NULL ||
        ribos_artifact_open_v1(
            test->artifact,
            test->artifact_size,
            &view) != RIBOS_ARTIFACT_OK ||
        !entry_context_type(
            &view,
            &test->context_type,
            &test->context_size) ||
        !find_unit_type(&view, &test->unit_type) ||
        !find_named_type(
            &view,
            "BootAction",
            &test->action_type,
            &test->action_size)) {
        return 0;
    }
    test->bindings[0].execution = execution_descriptor(
        1,
        RIBOS_CAPABILITY_DEVICE,
        RIBOS_VM_HELPER_EFFECT_JOURNALED,
        RIBOS_VM_HELPER_DURABILITY_JOURNAL_RECEIPT);
    test->bindings[1].execution = execution_descriptor(
        22,
        RIBOS_CAPABILITY_BOOT,
        RIBOS_VM_HELPER_EFFECT_TERMINAL,
        RIBOS_VM_HELPER_DURABILITY_SEALED_INTENT);
    test->bindings[0].invoke = test_helper;
    test->bindings[1].invoke = test_helper;
    test->contract = (RibosVmHelperContract){
        .size = sizeof(test->contract),
        .contract_major =
            RIBOS_VM_HELPER_EXECUTION_V1_MAJOR,
        .contract_minor =
            RIBOS_VM_HELPER_EXECUTION_V1_MINOR,
        .binding_count = 2,
        .bindings = test->bindings,
    };
    if (ribos_vm_helper_contract_compute_identity_v1(
            &test->contract,
            test->contract.digest) != RIBOS_VM_STATUS_OK) {
        return 0;
    }
    memcpy(
        test->authority.helper_digest,
        test->contract.digest,
        RIBOS_VM_DIGEST_BYTES);
    test->limits = (RibosVmLimits){
        .size = sizeof(test->limits),
        .runtime_abi_major = RIBOS_VM_RUNTIME_ABI_V1_MAJOR,
        .runtime_abi_minor = RIBOS_VM_RUNTIME_ABI_V1_MINOR,
        .maximum_instructions =
            view.instruction_upper_bound,
        .maximum_helper_calls =
            view.helper_upper_bound == 0 ?
                1 : view.helper_upper_bound,
        .maximum_stack_bytes = view.maximum_stack_bytes,
        .maximum_arena_bytes = 65536,
        .maximum_input_bytes = 512,
        .maximum_output_bytes = 256,
        .maximum_operations = 16,
        .maximum_polls = 16,
        .maximum_execution_duration_ns = 1000000,
        .maximum_helper_duration_ns = 100000,
        .maximum_call_depth = view.maximum_call_depth,
        .maximum_handles = 0,
        .maximum_trace_records = 4,
    };
    if (ribos_authorized_artifact_workspace_size_v1(
            test->artifact_size,
            &workspace_size) != RIBOS_VM_STATUS_OK) {
        return 0;
    }
    test->authorized_workspace = malloc(workspace_size);
    if (test->authorized_workspace == NULL ||
        ribos_authorize_artifact_v1(
            test->artifact,
            test->artifact_size,
            &authorizer,
            test->authorized_workspace,
            workspace_size,
            &test->authorized) != RIBOS_VM_STATUS_OK ||
        ribos_prepared_program_workspace_size_v1(
            test->authorized,
            &test->contract,
            &workspace_size) != RIBOS_VM_STATUS_OK) {
        return 0;
    }
    test->prepared_workspace = malloc(workspace_size);
    if (test->prepared_workspace == NULL ||
        ribos_prepare_program_v1(
            test->authorized,
            ribos_schema_reference_v1(),
            &test->contract,
            &test->limits,
            test->prepared_workspace,
            workspace_size,
            &test->report,
            &test->prepared) != RIBOS_VM_STATUS_OK) {
        return 0;
    }
    return 1;
}

static void
release_program(TestPrepared *test)
{
    free(test->prepared_workspace);
    free(test->authorized_workspace);
    free(test->artifact);
    memset(test, 0, sizeof(*test));
}

static int
initialize_arena(
    const TestPrepared *prepared,
    TestArena *arena)
{
    size_t required_size = 0;

    memset(arena, 0, sizeof(*arena));
    if (ribos_vm_runtime_size_v1(
            prepared->prepared,
            &arena->plan,
            &required_size) != RIBOS_VM_STATUS_OK) {
        return 0;
    }
    arena->allocation = malloc(required_size);
    if (arena->allocation == NULL ||
        ((uintptr_t)arena->allocation &
         (ribos_vm_runtime_alignment_v1() - 1u)) != 0) {
        return 0;
    }
    arena->arena_size = required_size;
    if (ribos_vm_storage_initialize_v1(
            prepared->prepared,
            &arena->plan,
            arena->allocation,
            required_size,
            0,
            &arena->storage) != RIBOS_VM_STATUS_OK ||
        ribos_vm_handle_host_table_initialize_v1(
            &arena->handles,
            NULL,
            0) != RIBOS_VM_STATUS_OK) {
        return 0;
    }
    return 1;
}

static void
release_arena(TestArena *arena)
{
    free(arena->allocation);
    memset(arena, 0, sizeof(*arena));
}

static int
initialize_execution(
    const TestPrepared *prepared,
    TestArena *arena,
    TestEmbedder *test_embedder,
    RibosVmContext *context,
    RibosVmEmbedder *embedder,
    RibosVmHelperEnvironment *environment,
    uint8_t *context_bytes,
    uint64_t generation)
{
    memset(context_bytes, 0, prepared->context_size);
    *context = (RibosVmContext){
        .size = sizeof(*context),
        .runtime_abi_major = RIBOS_VM_RUNTIME_ABI_V1_MAJOR,
        .runtime_abi_minor = RIBOS_VM_RUNTIME_ABI_V1_MINOR,
        .context_type_id = prepared->context_type,
        .generation = generation,
        .bytes = context_bytes,
        .byte_size = prepared->context_size,
    };
    ribos_artifact_sha256(
        context_bytes,
        prepared->context_size,
        context->digest);
    test_embedder->prepared = prepared;
    test_embedder->now_ns = 100;
    *embedder = (RibosVmEmbedder){
        .size = sizeof(*embedder),
        .runtime_abi_major = RIBOS_VM_RUNTIME_ABI_V1_MAJOR,
        .runtime_abi_minor = RIBOS_VM_RUNTIME_ABI_V1_MINOR,
        .granted_capabilities =
            RIBOS_CAPABILITY_DEVICE |
            RIBOS_CAPABILITY_BOOT,
        .helper_contract = &prepared->contract,
        .embedder_context = test_embedder,
        .monotonic_now_ns = test_now,
        .factory_recovery = test_recovery,
    };
    *environment = (RibosVmHelperEnvironment){
        .size = sizeof(*environment),
        .helpers_major = RIBOS_VM_HELPERS_V1_MAJOR,
        .helpers_minor = RIBOS_VM_HELPERS_V1_MINOR,
        .embedder = embedder,
        .handle_table = &arena->handles,
    };
    return 1;
}

static int
test_action_and_double_consume(const char *path)
{
    TestPrepared prepared;
    TestArena arena;
    TestEmbedder test_embedder = {0};
    RibosVmContext context;
    RibosVmEmbedder embedder;
    RibosVmHelperEnvironment environment;
    RibosVmOutcome outcome;
    RibosVmBootAction action;
    RibosVmTerminalSnapshot terminal;
    uint8_t context_bytes[512];
    int ok;

    if (!prepare_program(path, &prepared) ||
        prepared.context_size > sizeof(context_bytes) ||
        !initialize_arena(&prepared, &arena)) {
        fprintf(stderr, "action: setup failed\n");
        return 0;
    }
    initialize_execution(
        &prepared,
        &arena,
        &test_embedder,
        &context,
        &embedder,
        &environment,
        context_bytes,
        101);
    ok = expect_status(
            "action execute",
            ribos_vm_policy_execute_v1(
                prepared.prepared,
                &context,
                &environment,
                arena.storage,
                arena.arena_size,
                &outcome),
            RIBOS_VM_STATUS_OK) &&
        expect_true(
            "action outcome",
            outcome.kind == RIBOS_VM_OUTCOME_BOOT_ACTION &&
            test_embedder.recovery_calls == 0) &&
        expect_status(
            "action snapshot",
            ribos_vm_terminal_snapshot_v1(
                prepared.prepared,
                arena.storage,
                arena.arena_size,
                &terminal),
            RIBOS_VM_STATUS_OK) &&
        expect_true(
            "action sealed",
            terminal.state ==
                RIBOS_VM_TERMINAL_ACTION_SEALED &&
            terminal.action_consumed == 0 &&
            terminal.payload_size == prepared.action_size);
    action = outcome.value.boot_action;
    ok = ok &&
        expect_status(
            "action consume",
            ribos_vm_boot_action_consume_v1(
                prepared.prepared,
                arena.storage,
                arena.arena_size,
                &action),
            RIBOS_VM_STATUS_OK) &&
        expect_status(
            "action double consume",
            ribos_vm_boot_action_consume_v1(
                prepared.prepared,
                arena.storage,
                arena.arena_size,
                &action),
            RIBOS_VM_STATUS_ALREADY_CONSUMED) &&
        expect_status(
            "action consumed snapshot",
            ribos_vm_terminal_snapshot_v1(
                prepared.prepared,
                arena.storage,
                arena.arena_size,
                &terminal),
            RIBOS_VM_STATUS_OK) &&
        expect_true(
            "action consumed state",
            terminal.state ==
                RIBOS_VM_TERMINAL_ACTION_CONSUMED &&
            terminal.action_consumed == 1);
    release_arena(&arena);
    release_program(&prepared);
    return ok;
}

static int
test_policy_error(const char *path)
{
    TestPrepared prepared;
    TestArena arena;
    TestEmbedder test_embedder = {0};
    RibosVmContext context;
    RibosVmEmbedder embedder;
    RibosVmHelperEnvironment environment;
    RibosVmOutcome outcome;
    RibosVmTerminalSnapshot terminal;
    uint8_t context_bytes[512];
    int ok;

    if (!prepare_program(path, &prepared) ||
        prepared.context_size > sizeof(context_bytes) ||
        !initialize_arena(&prepared, &arena)) {
        fprintf(stderr, "policy-error: setup failed\n");
        return 0;
    }
    initialize_execution(
        &prepared,
        &arena,
        &test_embedder,
        &context,
        &embedder,
        &environment,
        context_bytes,
        102);
    ok = expect_status(
            "policy error execute",
            ribos_vm_policy_execute_v1(
                prepared.prepared,
                &context,
                &environment,
                arena.storage,
                arena.arena_size,
                &outcome),
            RIBOS_VM_STATUS_OK) &&
        expect_true(
            "policy error outcome",
            outcome.kind == RIBOS_VM_OUTCOME_POLICY_ERROR &&
            outcome.value.policy_error.payload != NULL &&
            outcome.value.policy_error.payload_size != 0 &&
            test_embedder.recovery_calls == 0) &&
        expect_status(
            "policy error snapshot",
            ribos_vm_terminal_snapshot_v1(
                prepared.prepared,
                arena.storage,
                arena.arena_size,
                &terminal),
            RIBOS_VM_STATUS_OK) &&
        expect_true(
            "policy error distinct",
            terminal.state == RIBOS_VM_TERMINAL_POLICY_ERROR &&
            terminal.outcome_kind ==
                RIBOS_VM_OUTCOME_POLICY_ERROR &&
            terminal.terminal_helper_id ==
                RIBOS_VM_INVALID_ID);
    release_arena(&arena);
    release_program(&prepared);
    return ok;
}

static int
test_journal(
    const char *path,
    uint32_t inject_fault)
{
    TestPrepared prepared;
    TestArena arena;
    TestEmbedder test_embedder = {
        .journal_fault = inject_fault,
    };
    RibosVmContext context;
    RibosVmEmbedder embedder;
    RibosVmHelperEnvironment environment;
    RibosVmOutcome outcome;
    RibosVmTerminalSnapshot terminal;
    uint8_t context_bytes[512];
    int ok;

    if (!prepare_program(path, &prepared) ||
        prepared.context_size > sizeof(context_bytes) ||
        !initialize_arena(&prepared, &arena)) {
        fprintf(stderr, "journal: setup failed\n");
        return 0;
    }
    initialize_execution(
        &prepared,
        &arena,
        &test_embedder,
        &context,
        &embedder,
        &environment,
        context_bytes,
        inject_fault ? 104 : 103);
    ok = expect_status(
            "journal execute",
            ribos_vm_policy_execute_v1(
                prepared.prepared,
                &context,
                &environment,
                arena.storage,
                arena.arena_size,
                &outcome),
            RIBOS_VM_STATUS_OK) &&
        expect_status(
            "journal snapshot",
            ribos_vm_terminal_snapshot_v1(
                prepared.prepared,
                arena.storage,
                arena.arena_size,
                &terminal),
            RIBOS_VM_STATUS_OK) &&
        expect_true(
            "journal receipt chain",
            terminal.journal_count == 1 &&
            terminal.journal_sequence == 1 &&
            ribos_vm_digest_is_nonzero(
                terminal.journal_chain_digest));
    if (inject_fault) {
        ok = ok &&
            expect_true(
                "journal partial fault",
                outcome.kind == RIBOS_VM_OUTCOME_VM_FAULT &&
                outcome.value.vm_fault.fault_code ==
                    RIBOS_VM_FAULT_HELPER_CONTRACT &&
                terminal.state == RIBOS_VM_TERMINAL_VM_FAULT &&
                terminal.journal_state ==
                    RIBOS_VM_JOURNAL_RECEIPT_PARTIAL &&
                terminal.authority_revoked == 1 &&
                terminal.recovery_notified == 1 &&
                test_embedder.recovery_calls == 1 &&
                memcmp(
                    outcome.value.vm_fault.trace_digest,
                    terminal.trace_digest,
                    RIBOS_VM_DIGEST_BYTES) == 0) &&
            expect_status(
                "journal fault no reexecute",
                ribos_vm_policy_execute_v1(
                    prepared.prepared,
                    &context,
                    &environment,
                    arena.storage,
                    arena.arena_size,
                    &outcome),
                RIBOS_VM_STATUS_INVALID_STATE) &&
            expect_true(
                "journal recovery once",
                test_embedder.recovery_calls == 1);
    } else {
        ok = ok &&
            expect_true(
                "journal committed action",
                outcome.kind ==
                    RIBOS_VM_OUTCOME_BOOT_ACTION &&
                terminal.state ==
                    RIBOS_VM_TERMINAL_ACTION_SEALED &&
                terminal.journal_state ==
                    RIBOS_VM_JOURNAL_RECEIPT_COMMITTED &&
                test_embedder.recovery_calls == 0);
    }
    release_arena(&arena);
    release_program(&prepared);
    return ok;
}

static int
terminal_helper_receipt(
    const TestPrepared *prepared,
    uint64_t frame_base,
    uint64_t receipt_sequence,
    RibosVmTerminalHelperReceiptInternal *receipt)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(
            prepared->prepared);
    const RibosArtifactSectionView *instructions =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_INSTRUCTIONS);
    const RibosArtifactSectionView *blocks =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_BLOCKS);
    const RibosArtifactSectionView *slots =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_SLOTS);
    uint32_t index;

    for (index = 0; index < instructions->count; ++index) {
        const uint8_t *instruction =
            section_row(instructions, index);
        const uint8_t *block;
        const uint8_t *slot;

        if (instruction == NULL ||
            instruction[0] != RIBOS_BC_CALL_HELPER ||
            read_u32(instruction + 20) != 22) {
            continue;
        }
        block = section_row(
            blocks,
            read_u32(instruction + 8));
        slot = section_row(
            slots,
            read_u32(instruction + 12));
        if (block == NULL || slot == NULL) {
            return 0;
        }
        memset(receipt, 0, sizeof(*receipt));
        receipt->helper_id = 22;
        receipt->function_id = read_u32(block + 4);
        receipt->instruction_id = index;
        receipt->source_map_id =
            read_u32(instruction + 28);
        receipt->result_slot_id =
            read_u32(instruction + 12);
        receipt->result_type_id = read_u32(slot + 8);
        receipt->result_byte_size = read_u32(slot + 16);
        receipt->result_kind =
            RIBOS_VM_HELPER_RESULT_SUCCESS_VALUE;
        receipt->callback_status =
            RIBOS_VM_HELPER_CALLBACK_OK;
        receipt->effect = RIBOS_VM_HELPER_EFFECT_TERMINAL;
        receipt->durability =
            RIBOS_VM_HELPER_DURABILITY_SEALED_INTENT;
        receipt->frame_base = frame_base;
        receipt->receipt_sequence = receipt_sequence;
        return 1;
    }
    return 0;
}

static int
test_fault_after_action_and_double_action(const char *path)
{
    TestPrepared prepared;
    TestArena arena;
    TestEmbedder test_embedder = {0};
    RibosVmContext context;
    RibosVmEmbedder embedder;
    RibosVmHelperEnvironment environment;
    RibosVmInterpreterSnapshot interpreter;
    RibosVmTerminalSnapshot terminal;
    RibosVmStorageExecutionControl control;
    RibosVmHelperExecutionSnapshot helper;
    RibosVmTerminalHelperReceiptInternal duplicate;
    RibosVmFaultReceipt fault;
    RibosVmOutcome outcome;
    const RibosArtifactView *view;
    uint8_t context_bytes[512];
    uint64_t remaining;
    int ok = 1;

    if (!prepare_program(path, &prepared) ||
        prepared.context_size > sizeof(context_bytes) ||
        !initialize_arena(&prepared, &arena)) {
        fprintf(stderr, "fault-after-action: setup failed\n");
        return 0;
    }
    initialize_execution(
        &prepared,
        &arena,
        &test_embedder,
        &context,
        &embedder,
        &environment,
        context_bytes,
        105);
    ok = expect_status(
            "hostile terminal init",
            ribos_vm_terminal_initialize_internal_v1(
                prepared.prepared,
                &context,
                arena.storage,
                arena.arena_size),
            RIBOS_VM_STATUS_OK) &&
        expect_status(
            "hostile interpreter init",
            ribos_vm_interpreter_initialize_v1(
                prepared.prepared,
                &context,
                arena.storage,
                arena.arena_size),
            RIBOS_VM_STATUS_OK) &&
        expect_status(
            "hostile helper init",
            ribos_vm_helper_execution_initialize_v1(
                prepared.prepared,
                &context,
                &environment,
                arena.storage,
                arena.arena_size),
            RIBOS_VM_STATUS_OK);
    do {
        ok = ok &&
            expect_status(
                "hostile step",
                ribos_vm_interpreter_step_with_helpers_v1(
                    prepared.prepared,
                    &context,
                    &environment,
                    arena.storage,
                    arena.arena_size,
                    &interpreter),
                RIBOS_VM_STATUS_OK) &&
            expect_status(
                "hostile terminal snapshot",
                ribos_vm_terminal_snapshot_v1(
                    prepared.prepared,
                    arena.storage,
                    arena.arena_size,
                    &terminal),
                RIBOS_VM_STATUS_OK);
    } while (ok &&
             terminal.state !=
                 RIBOS_VM_TERMINAL_ACTION_PENDING &&
             (interpreter.state ==
                  RIBOS_VM_INTERPRETER_READY ||
              interpreter.state ==
                  RIBOS_VM_INTERPRETER_RUNNING));
    ok = ok &&
        expect_true(
            "hostile pending action",
            terminal.state ==
                RIBOS_VM_TERMINAL_ACTION_PENDING) &&
        expect_status(
            "hostile control",
            ribos_vm_storage_execution_load_internal_v1(
                prepared.prepared,
                arena.storage,
                arena.arena_size,
                &control,
                &remaining),
            RIBOS_VM_STATUS_OK) &&
        expect_status(
            "hostile helper snapshot",
            ribos_vm_helper_execution_snapshot_v1(
                prepared.prepared,
                arena.storage,
                arena.arena_size,
                &helper),
            RIBOS_VM_STATUS_OK) &&
        expect_true(
            "hostile derive duplicate",
            terminal_helper_receipt(
                &prepared,
                control.frame_base,
                helper.receipt_sequence,
                &duplicate)) &&
        expect_status(
            "hostile double action",
            ribos_vm_terminal_record_helper_internal_v1(
                prepared.prepared,
                &context,
                arena.storage,
                arena.arena_size,
                &duplicate),
            RIBOS_VM_STATUS_INVALID_STATE);
    view = ribos_prepared_program_artifact_view_v1(
        prepared.prepared);
    memset(&fault, 0, sizeof(fault));
    fault.fault_code = RIBOS_VM_FAULT_TERMINAL_ACTION;
    fault.subject = RIBOS_VM_FAULT_SUBJECT_TERMINAL;
    fault.function_id = control.function_id;
    fault.instruction_id = control.instruction_id;
    fault.helper_id = terminal.terminal_helper_id;
    fault.detail = terminal.action_type_id;
    fault.last_effect = RIBOS_VM_HELPER_EFFECT_TERMINAL;
    fault.last_durability =
        RIBOS_VM_HELPER_DURABILITY_SEALED_INTENT;
    fault.consumed_instructions =
        control.consumed_instructions;
    fault.consumed_helper_calls =
        helper.consumed_helper_calls;
    memcpy(
        fault.artifact_hash,
        view->artifact_hash,
        RIBOS_VM_DIGEST_BYTES);
    ok = ok &&
        expect_status(
            "hostile seal fault",
            ribos_vm_storage_seal_fault_internal_v1(
                prepared.prepared,
                arena.storage,
                arena.arena_size,
                &fault),
            RIBOS_VM_STATUS_OK) &&
        expect_status(
            "hostile close fault",
            ribos_vm_terminal_close_fault_internal_v1(
                prepared.prepared,
                &environment,
                arena.storage,
                arena.arena_size,
                &outcome),
            RIBOS_VM_STATUS_OK) &&
        expect_true(
            "hostile pending revoked",
            outcome.kind == RIBOS_VM_OUTCOME_VM_FAULT &&
            test_embedder.recovery_calls == 1) &&
        expect_status(
            "hostile final snapshot",
            ribos_vm_terminal_snapshot_v1(
                prepared.prepared,
                arena.storage,
                arena.arena_size,
                &terminal),
            RIBOS_VM_STATUS_OK) &&
        expect_true(
            "hostile no action outcome",
            terminal.state == RIBOS_VM_TERMINAL_VM_FAULT &&
            terminal.terminal_helper_id ==
                RIBOS_VM_INVALID_ID &&
            terminal.payload_size == 0);
    release_arena(&arena);
    release_program(&prepared);
    return ok;
}

static int
test_no_action_success(const char *path)
{
    TestPrepared prepared;
    TestArena arena;
    TestEmbedder test_embedder = {0};
    RibosVmContext context;
    RibosVmEmbedder embedder;
    RibosVmHelperEnvironment environment;
    RibosVmInterpreterSnapshot interpreter;
    RibosVmStorageExecutionControl control;
    RibosVmOutcome outcome;
    RibosVmTerminalSnapshot terminal;
    uint8_t context_bytes[512];
    uint8_t ok_tag = 0;
    uint64_t remaining;
    int ok;

    if (!prepare_program(path, &prepared) ||
        prepared.context_size > sizeof(context_bytes) ||
        !initialize_arena(&prepared, &arena)) {
        fprintf(stderr, "no-action-success: setup failed\n");
        return 0;
    }
    initialize_execution(
        &prepared,
        &arena,
        &test_embedder,
        &context,
        &embedder,
        &environment,
        context_bytes,
        106);
    ok = expect_status(
            "no-action terminal init",
            ribos_vm_terminal_initialize_internal_v1(
                prepared.prepared,
                &context,
                arena.storage,
                arena.arena_size),
            RIBOS_VM_STATUS_OK) &&
        expect_status(
            "no-action interpreter init",
            ribos_vm_interpreter_initialize_v1(
                prepared.prepared,
                &context,
                arena.storage,
                arena.arena_size),
            RIBOS_VM_STATUS_OK) &&
        expect_status(
            "no-action helper init",
            ribos_vm_helper_execution_initialize_v1(
                prepared.prepared,
                &context,
                &environment,
                arena.storage,
                arena.arena_size),
            RIBOS_VM_STATUS_OK) &&
        expect_status(
            "no-action run",
            ribos_vm_interpreter_run_with_helpers_v1(
                prepared.prepared,
                &context,
                &environment,
                arena.storage,
                arena.arena_size,
                &interpreter),
            RIBOS_VM_STATUS_OK) &&
        expect_true(
            "no-action returned error",
            interpreter.state ==
                RIBOS_VM_INTERPRETER_RETURNED) &&
        expect_status(
            "no-action control",
            ribos_vm_storage_execution_load_internal_v1(
                prepared.prepared,
                arena.storage,
                arena.arena_size,
                &control,
                &remaining),
            RIBOS_VM_STATUS_OK) &&
        expect_status(
            "no-action mutate tag",
            ribos_vm_storage_slot_slice_write_internal_v1(
                prepared.prepared,
                arena.storage,
                arena.arena_size,
                control.function_id,
                control.frame_base,
                control.return_slot_id,
                0,
                &ok_tag,
                1),
            RIBOS_VM_STATUS_OK) &&
        expect_status(
            "no-action finalize",
            ribos_vm_terminal_finalize_interpreter_internal_v1(
                prepared.prepared,
                &environment,
                arena.storage,
                arena.arena_size,
                &interpreter,
                &outcome),
            RIBOS_VM_STATUS_OK) &&
        expect_true(
            "no-action fail closed",
            outcome.kind == RIBOS_VM_OUTCOME_VM_FAULT &&
            outcome.value.vm_fault.fault_code ==
                RIBOS_VM_FAULT_TERMINAL_ACTION &&
            test_embedder.recovery_calls == 1) &&
        expect_status(
            "no-action snapshot",
            ribos_vm_terminal_snapshot_v1(
                prepared.prepared,
                arena.storage,
                arena.arena_size,
                &terminal),
            RIBOS_VM_STATUS_OK) &&
        expect_true(
            "no-action authority revoked",
            terminal.state == RIBOS_VM_TERMINAL_VM_FAULT &&
            terminal.authority_revoked == 1);
    release_arena(&arena);
    release_program(&prepared);
    return ok;
}

int
main(int argc, char **argv)
{
    int ok;

    if (argc != 4) {
        fprintf(
            stderr,
            "usage: %s ACTION ERROR JOURNAL\n",
            argv[0]);
        return 2;
    }
    ok = test_action_and_double_consume(argv[1]);
    ok = test_policy_error(argv[2]) && ok;
    ok = test_journal(argv[3], 0) && ok;
    ok = test_journal(argv[3], 1) && ok;
    ok = test_fault_after_action_and_double_action(argv[1]) && ok;
    ok = test_no_action_success(argv[2]) && ok;
    if (!ok) {
        return 1;
    }
    puts(
        "RIBOS-VM-TERMINAL-OK action=single-consume "
        "policy-error=distinct journal=receipt-chain "
        "fault=fail-closed recovery=once evidence=host-unit");
    puts(
        "RIBOS-VM-FAULTS-OK fault-after-action=closed "
        "no-action-success=closed double-action=closed "
        "durable-effect=partial-not-rollback evidence=host-unit");
    return 0;
}
