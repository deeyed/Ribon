#include "ribos/vm/interpreter.h"

#include "internal.h"
#include "storage_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TestAuthority {
    uint8_t helper_digest[RIBOS_VM_DIGEST_BYTES];
} TestAuthority;

typedef struct TestProgram {
    uint8_t *artifact;
    size_t artifact_size;
    void *authorized_workspace;
    const RibosAuthorizedArtifact *authorized;
    void *prepared_workspace;
    const RibosPreparedProgram *prepared;
    RibosVmHelperBinding bindings[5];
    RibosVmHelperContract contract;
    RibosVmLimits limits;
    RibosVerifierReport report;
    TestAuthority authority;
} TestProgram;

typedef struct TestArena {
    uint8_t *allocation;
    uint8_t *bytes;
    size_t size;
    RibosVmStorage *storage;
    RibosVmStoragePlan plan;
} TestArena;

typedef struct TestContext {
    uint8_t bytes[8];
    RibosVmContext value;
} TestContext;

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

static uint64_t
read_u64(const uint8_t *bytes)
{
    return (uint64_t)bytes[0] |
        ((uint64_t)bytes[1] << 8) |
        ((uint64_t)bytes[2] << 16) |
        ((uint64_t)bytes[3] << 24) |
        ((uint64_t)bytes[4] << 32) |
        ((uint64_t)bytes[5] << 40) |
        ((uint64_t)bytes[6] << 48) |
        ((uint64_t)bytes[7] << 56);
}

static void
write_u64(uint8_t bytes[8], uint64_t value)
{
    uint32_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
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

static uint32_t
invoke_helper(void *context, RibosVmHelperCall *call)
{
    (void)context;
    (void)call;
    return RIBOS_VM_HELPER_CALLBACK_OK;
}

static uint32_t
authorize_for_test(
    void *context,
    const RibosArtifactAuthorizationRequest *request,
    RibosArtifactAuthorizationReceipt *receipt)
{
    TestAuthority *authority = context;

    if (request == NULL || receipt == NULL ||
        !ribos_vm_digest_is_nonzero(request->artifact_hash) ||
        !ribos_vm_digest_is_nonzero(request->schema_digest)) {
        return RIBOS_VM_STATUS_NOT_AUTHORIZED;
    }
    *receipt = (RibosArtifactAuthorizationReceipt){
        .size = sizeof(*receipt),
        .authorization_major = RIBOS_VM_AUTHORIZATION_V1_MAJOR,
        .authorization_minor = RIBOS_VM_AUTHORIZATION_V1_MINOR,
        .decision = RIBOS_ARTIFACT_AUTHORIZATION_GRANTED,
        .authority_generation = 233,
        .manifest_sequence = 377,
        .rollback_floor = 144,
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
    uint32_t transition,
    uint32_t transition_parameter)
{
    uint32_t durability = effect == RIBOS_VM_HELPER_EFFECT_TERMINAL ?
        RIBOS_VM_HELPER_DURABILITY_SEALED_INTENT :
        RIBOS_VM_HELPER_DURABILITY_NONE;

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

static int
initialize_helper_contract(TestProgram *test)
{
    test->bindings[0].execution = execution_descriptor(
        2,
        RIBOS_CAPABILITY_INSPECT,
        RIBOS_VM_HELPER_EFFECT_PURE,
        RIBOS_VM_HANDLE_TRANSITION_NONE,
        RIBOS_VM_INVALID_ID);
    test->bindings[1].execution = execution_descriptor(
        8,
        RIBOS_CAPABILITY_INSPECT,
        RIBOS_VM_HELPER_EFFECT_PURE,
        RIBOS_VM_HANDLE_TRANSITION_CREATE,
        RIBOS_VM_INVALID_ID);
    test->bindings[2].execution = execution_descriptor(
        11,
        RIBOS_CAPABILITY_INSPECT,
        RIBOS_VM_HELPER_EFFECT_PURE,
        RIBOS_VM_HANDLE_TRANSITION_REPLACE,
        0);
    test->bindings[3].execution = execution_descriptor(
        21,
        RIBOS_CAPABILITY_BOOT,
        RIBOS_VM_HELPER_EFFECT_TERMINAL,
        RIBOS_VM_HANDLE_TRANSITION_TERMINAL_CONSUME,
        1);
    test->bindings[4].execution = execution_descriptor(
        22,
        RIBOS_CAPABILITY_BOOT,
        RIBOS_VM_HELPER_EFFECT_TERMINAL,
        RIBOS_VM_HANDLE_TRANSITION_NONE,
        RIBOS_VM_INVALID_ID);
    for (size_t index = 0; index < 5; ++index) {
        test->bindings[index].invoke = invoke_helper;
    }
    test->contract = (RibosVmHelperContract){
        .size = sizeof(test->contract),
        .contract_major = RIBOS_VM_HELPER_EXECUTION_V1_MAJOR,
        .contract_minor = RIBOS_VM_HELPER_EXECUTION_V1_MINOR,
        .binding_count = 5,
        .bindings = test->bindings,
    };
    return ribos_vm_helper_contract_compute_identity_v1(
        &test->contract,
        test->contract.digest) == RIBOS_VM_STATUS_OK;
}

static int
prepare_program(const char *path, TestProgram *test)
{
    RibosArtifactAuthorizer authorizer = {
        .size = sizeof(authorizer),
        .authorization_major = RIBOS_VM_AUTHORIZATION_V1_MAJOR,
        .authorization_minor = RIBOS_VM_AUTHORIZATION_V1_MINOR,
        .authority_context = &test->authority,
        .authorize = authorize_for_test,
    };
    size_t workspace_size = 0;

    memset(test, 0, sizeof(*test));
    test->artifact = read_file(path, &test->artifact_size);
    if (test->artifact == NULL ||
        !initialize_helper_contract(test)) {
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
        .maximum_instructions = 4096,
        .maximum_helper_calls = 2,
        .maximum_stack_bytes = 4096,
        .maximum_arena_bytes = 65536,
        .maximum_input_bytes = 256,
        .maximum_output_bytes = 256,
        .maximum_operations = 16,
        .maximum_polls = 16,
        .maximum_execution_duration_ns = 1000000,
        .maximum_helper_duration_ns = 100000,
        .maximum_call_depth = 8,
        .maximum_handles = 4,
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
    return test->prepared_workspace != NULL &&
        ribos_prepare_program_v1(
            test->authorized,
            ribos_schema_reference_v1(),
            &test->contract,
            &test->limits,
            test->prepared_workspace,
            workspace_size,
            &test->report,
            &test->prepared) == RIBOS_VM_STATUS_OK;
}

static void
release_program(TestProgram *test)
{
    free(test->prepared_workspace);
    free(test->authorized_workspace);
    free(test->artifact);
    memset(test, 0, sizeof(*test));
}

static const uint8_t *
section_row(
    const RibosArtifactSectionView *section,
    uint32_t id)
{
    if (section == NULL || id >= section->count) {
        return NULL;
    }
    return section->bytes + (size_t)id * section->row_size;
}

static uint32_t
instruction_operand(
    const RibosArtifactView *view,
    const uint8_t *instruction,
    uint32_t ordinal)
{
    const RibosArtifactSectionView *operands =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_OPERANDS);
    uint32_t start = read_u32(instruction + 16);
    uint32_t count = read_u16(instruction + 2);
    const uint8_t *row;

    if (operands == NULL || ordinal >= count ||
        start > operands->count ||
        ordinal >= operands->count - start) {
        return RIBOS_VM_INVALID_ID;
    }
    row = section_row(operands, start + ordinal);
    return row == NULL ? RIBOS_VM_INVALID_ID : read_u32(row);
}

static int
initialize_context(TestProgram *test, TestContext *context)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(test->prepared);
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
    uint32_t parameter_start;
    uint32_t byte_size;

    if (function == NULL || read_u32(function + 36) != 1) {
        return 0;
    }
    parameter_start = read_u32(function + 32);
    slot = section_row(slots, parameter_start);
    if (slot == NULL || (byte_size = read_u32(slot + 16)) >
            sizeof(context->bytes)) {
        return 0;
    }
    memset(context, 0, sizeof(*context));
    for (uint32_t index = 0; index < byte_size; ++index) {
        context->bytes[index] = (uint8_t)(0x41u + index);
    }
    context->value = (RibosVmContext){
        .size = sizeof(context->value),
        .runtime_abi_major = RIBOS_VM_RUNTIME_ABI_V1_MAJOR,
        .runtime_abi_minor = RIBOS_VM_RUNTIME_ABI_V1_MINOR,
        .context_type_id = read_u32(slot + 8),
        .selected_mode = 0,
        .selected_phase = 0,
        .generation = 2,
        .bytes = context->bytes,
        .byte_size = byte_size,
    };
    ribos_artifact_sha256(
        context->bytes,
        byte_size,
        context->value.digest);
    return 1;
}

static int
initialize_arena(
    TestProgram *test,
    const TestContext *context,
    TestArena *arena)
{
    size_t allocation_size;
    uintptr_t aligned;

    memset(arena, 0, sizeof(*arena));
    if (ribos_vm_runtime_size_v1(
            test->prepared,
            &arena->plan,
            &arena->size) != RIBOS_VM_STATUS_OK ||
        arena->size > SIZE_MAX - RIBOS_VM_STORAGE_ALIGNMENT_V1) {
        return 0;
    }
    allocation_size = arena->size + RIBOS_VM_STORAGE_ALIGNMENT_V1;
    arena->allocation = malloc(allocation_size);
    if (arena->allocation == NULL) {
        return 0;
    }
    aligned = ((uintptr_t)arena->allocation +
        RIBOS_VM_STORAGE_ALIGNMENT_V1 - 1u) &
        ~(uintptr_t)(RIBOS_VM_STORAGE_ALIGNMENT_V1 - 1u);
    arena->bytes = (uint8_t *)aligned;
    if (ribos_vm_storage_initialize_v1(
            test->prepared,
            &arena->plan,
            arena->bytes,
            arena->size,
            RIBOS_VM_STORAGE_INITIALIZE_DIAGNOSTIC_POISON,
            &arena->storage) != RIBOS_VM_STATUS_OK ||
        ribos_vm_interpreter_initialize_v1(
            test->prepared,
            &context->value,
            arena->storage,
            arena->size) != RIBOS_VM_STATUS_OK) {
        free(arena->allocation);
        memset(arena, 0, sizeof(*arena));
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
reposition_execution(
    TestProgram *test,
    TestArena *arena,
    uint32_t instruction_id)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(test->prepared);
    const RibosArtifactSectionView *instructions =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_INSTRUCTIONS);
    const uint8_t *instruction =
        section_row(instructions, instruction_id);
    RibosVmStorageExecutionControl control;
    uint64_t remaining;

    if (instruction == NULL ||
        ribos_vm_storage_execution_load_internal_v1(
            test->prepared,
            arena->storage,
            arena->size,
            &control,
            &remaining) != RIBOS_VM_STATUS_OK ||
        read_u32(instruction + 8) == RIBOS_VM_INVALID_ID) {
        return 0;
    }
    (void)remaining;
    control.state = RIBOS_VM_INTERPRETER_RUNNING;
    control.block_id = read_u32(instruction + 8);
    control.instruction_id = instruction_id;
    control.return_slot_id = RIBOS_VM_INVALID_ID;
    return ribos_vm_storage_execution_store_internal_v1(
        test->prepared,
        arena->storage,
        arena->size,
        &control) == RIBOS_VM_STATUS_OK;
}

static uint32_t
find_entry_direct_call(
    const RibosArtifactView *view)
{
    const RibosArtifactSectionView *blocks =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_BLOCKS);
    const RibosArtifactSectionView *instructions =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_INSTRUCTIONS);

    for (uint32_t id = 0; id < instructions->count; ++id) {
        const uint8_t *instruction = section_row(instructions, id);
        const uint8_t *block = section_row(
            blocks,
            read_u32(instruction + 8));

        if (instruction[0] == RIBOS_BC_CALL_DIRECT &&
            block != NULL &&
            read_u32(block + 4) == view->entry_function) {
            return id;
        }
    }
    return RIBOS_VM_INVALID_ID;
}

static int
advance_to_instruction(
    TestProgram *test,
    const TestContext *context,
    TestArena *arena,
    uint32_t function_id,
    uint32_t instruction_id,
    RibosVmInterpreterSnapshot *snapshot)
{
    if (ribos_vm_interpreter_snapshot_v1(
            test->prepared,
            arena->storage,
            arena->size,
            snapshot) != RIBOS_VM_STATUS_OK) {
        return 0;
    }
    while ((snapshot->function_id != function_id ||
            snapshot->instruction_id != instruction_id) &&
           (snapshot->state == RIBOS_VM_INTERPRETER_READY ||
            snapshot->state == RIBOS_VM_INTERPRETER_RUNNING)) {
        if (ribos_vm_interpreter_step_v1(
                test->prepared,
                &context->value,
                arena->storage,
                arena->size,
                snapshot) != RIBOS_VM_STATUS_OK) {
            return 0;
        }
    }
    return snapshot->function_id == function_id &&
        snapshot->instruction_id == instruction_id &&
        (snapshot->state == RIBOS_VM_INTERPRETER_READY ||
         snapshot->state == RIBOS_VM_INTERPRETER_RUNNING);
}

static int
test_direct_calls(
    TestProgram *test,
    const TestContext *context)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(test->prepared);
    const RibosArtifactSectionView *functions =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_FUNCTIONS);
    const RibosArtifactSectionView *instructions =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_INSTRUCTIONS);
    uint32_t call_id = find_entry_direct_call(view);
    const uint8_t *call = section_row(instructions, call_id);
    uint32_t callee_id = call == NULL ?
        RIBOS_VM_INVALID_ID : read_u32(call + 20);
    const uint8_t *callee = section_row(functions, callee_id);
    const uint8_t *entry = section_row(
        functions,
        view->entry_function);
    uint32_t continuation = call == NULL ?
        RIBOS_VM_INVALID_ID : read_u32(call + 32);
    uint32_t result_slot = call == NULL ?
        RIBOS_VM_INVALID_ID : read_u32(call + 12);
    uint64_t callee_bound = callee == NULL ?
        0 : read_u64(callee + 56);
    TestArena arena;
    RibosVmInterpreterSnapshot snapshot;
    uint64_t before;
    uint64_t result = 0;
    uint64_t maximum_stack = 0;
    uint32_t maximum_depth = 0;
    int passed;

    memset(&arena, 0, sizeof(arena));
    if (call == NULL || callee == NULL || entry == NULL ||
        !initialize_arena(test, context, &arena) ||
        !advance_to_instruction(
            test,
            context,
            &arena,
            view->entry_function,
            call_id,
            &snapshot)) {
        release_arena(&arena);
        return expect_true("direct call fixture", 0);
    }
    before = snapshot.consumed_instructions;
    passed = expect_status(
        "enter direct callee",
        ribos_vm_interpreter_step_v1(
            test->prepared,
            &context->value,
            arena.storage,
            arena.size,
            &snapshot),
        RIBOS_VM_STATUS_OK);
    while (passed &&
           (snapshot.function_id != view->entry_function ||
            snapshot.instruction_id != continuation) &&
           (snapshot.state == RIBOS_VM_INTERPRETER_READY ||
            snapshot.state == RIBOS_VM_INTERPRETER_RUNNING)) {
        if (snapshot.frame_depth > maximum_depth) {
            maximum_depth = snapshot.frame_depth;
        }
        if (snapshot.stack_bytes > maximum_stack) {
            maximum_stack = snapshot.stack_bytes;
        }
        passed = expect_status(
            "execute direct call path",
            ribos_vm_interpreter_step_v1(
                test->prepared,
                &context->value,
                arena.storage,
                arena.size,
                &snapshot),
            RIBOS_VM_STATUS_OK);
    }
    if (snapshot.frame_depth > maximum_depth) {
        maximum_depth = snapshot.frame_depth;
    }
    if (snapshot.stack_bytes > maximum_stack) {
        maximum_stack = snapshot.stack_bytes;
    }
    passed = passed && expect_true(
        "direct call returns to exact continuation",
        snapshot.state == RIBOS_VM_INTERPRETER_RUNNING &&
            snapshot.function_id == view->entry_function &&
            snapshot.instruction_id == continuation &&
            snapshot.frame_depth == 1 &&
            snapshot.stack_bytes == read_u32(entry + 88));
    passed = passed && expect_true(
        "callee dispatch matches verifier upper bound",
        snapshot.consumed_instructions - before ==
            UINT64_C(1) + callee_bound);
    passed = passed && expect_true(
        "maximum frame closure matches verifier",
        maximum_depth == test->report.recomputed_call_depth &&
            maximum_stack == test->report.recomputed_stack_bytes);
    passed = passed && expect_status(
        "direct call result transfer",
        ribos_vm_storage_slot_load_unsigned_v1(
            test->prepared,
            arena.storage,
            arena.size,
            view->entry_function,
            0,
            result_slot,
            32,
            &result),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "sequential callee frame reuse",
        result == 6);
    release_arena(&arena);
    return passed;
}

static int
test_call_depth_and_nested_fault(
    TestProgram *test,
    const TestContext *context)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(test->prepared);
    const RibosArtifactSectionView *instructions =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_INSTRUCTIONS);
    TestArena arena;
    RibosVmInterpreterSnapshot snapshot;
    RibosVmInterpreterSnapshot before;
    RibosVmStorageExecutionControl control;
    RibosVmStorageCallTarget target;
    RibosVmFaultReceipt receipt;
    uint64_t remaining;
    uint32_t fault_code;
    int passed = 1;

    memset(&arena, 0, sizeof(arena));
    if (!initialize_arena(test, context, &arena) ||
        ribos_vm_interpreter_snapshot_v1(
            test->prepared,
            arena.storage,
            arena.size,
            &snapshot) != RIBOS_VM_STATUS_OK) {
        release_arena(&arena);
        return expect_true("nested fault arena", 0);
    }
    while (snapshot.frame_depth < 2 &&
           (snapshot.state == RIBOS_VM_INTERPRETER_READY ||
            snapshot.state == RIBOS_VM_INTERPRETER_RUNNING)) {
        passed = expect_status(
            "reach nested caller",
            ribos_vm_interpreter_step_v1(
                test->prepared,
                &context->value,
                arena.storage,
                arena.size,
                &snapshot),
            RIBOS_VM_STATUS_OK);
        if (!passed) {
            break;
        }
    }
    before = snapshot;
    passed = passed && expect_status(
        "load nested caller",
        ribos_vm_storage_execution_load_internal_v1(
            test->prepared,
            arena.storage,
            arena.size,
            &control,
            &remaining),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "active recursion rejected",
        ribos_vm_storage_call_target_internal_v1(
            test->prepared,
            arena.storage,
            arena.size,
            &control,
            view->entry_function,
            &target,
            &fault_code),
        RIBOS_VM_STATUS_LIMIT_EXCEEDED);
    passed = passed && expect_true(
        "recursion rejection is non-mutating",
        fault_code == RIBOS_VM_FAULT_CALL_DEPTH &&
            snapshot.frame_depth == 2 &&
            snapshot.frame_depth <
                test->report.recomputed_call_depth &&
            ribos_vm_interpreter_snapshot_v1(
                test->prepared,
                arena.storage,
                arena.size,
                &snapshot) == RIBOS_VM_STATUS_OK &&
            snapshot.frame_depth == before.frame_depth &&
            snapshot.stack_bytes == before.stack_bytes &&
            snapshot.consumed_instructions ==
                before.consumed_instructions);
    passed = passed && expect_status(
        "malformed callee rejected",
        ribos_vm_storage_call_target_internal_v1(
            test->prepared,
            arena.storage,
            arena.size,
            &control,
            RIBOS_VM_INVALID_ID,
            &target,
            &fault_code),
        RIBOS_VM_STATUS_INVALID_DESCRIPTOR);
    passed = passed && expect_true(
        "malformed callee rejection is non-mutating",
        ribos_vm_interpreter_snapshot_v1(
            test->prepared,
            arena.storage,
            arena.size,
            &snapshot) == RIBOS_VM_STATUS_OK &&
            snapshot.frame_depth == before.frame_depth &&
            snapshot.stack_bytes == before.stack_bytes &&
            snapshot.consumed_instructions ==
                before.consumed_instructions);
    while (snapshot.frame_depth <
               test->report.recomputed_call_depth &&
           (snapshot.state == RIBOS_VM_INTERPRETER_READY ||
            snapshot.state == RIBOS_VM_INTERPRETER_RUNNING)) {
        passed = expect_status(
            "reach maximum call depth",
            ribos_vm_interpreter_step_v1(
                test->prepared,
                &context->value,
                arena.storage,
                arena.size,
                &snapshot),
            RIBOS_VM_STATUS_OK);
        if (!passed) {
            break;
        }
    }
    passed = passed && expect_true(
        "maximum depth reached",
        snapshot.frame_depth ==
            test->report.recomputed_call_depth &&
            snapshot.stack_bytes ==
                test->report.recomputed_stack_bytes);
    before = snapshot;
    passed = passed && expect_status(
        "load deepest frame",
        ribos_vm_storage_execution_load_internal_v1(
            test->prepared,
            arena.storage,
            arena.size,
            &control,
            &remaining),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "depth overflow rejected before push",
        ribos_vm_storage_call_target_internal_v1(
            test->prepared,
            arena.storage,
            arena.size,
            &control,
            view->entry_function,
            &target,
            &fault_code),
        RIBOS_VM_STATUS_LIMIT_EXCEEDED);
    passed = passed && expect_true(
        "depth rejection is non-mutating",
        fault_code == RIBOS_VM_FAULT_CALL_DEPTH &&
            ribos_vm_interpreter_snapshot_v1(
                test->prepared,
                arena.storage,
                arena.size,
                &snapshot) == RIBOS_VM_STATUS_OK &&
            snapshot.frame_depth == before.frame_depth &&
            snapshot.stack_bytes == before.stack_bytes &&
            snapshot.consumed_instructions ==
                before.consumed_instructions);
    write_u64(
        arena.bytes +
            RIBOS_VM_STORAGE_CONTROL_REMAINING_INSTRUCTIONS_OFFSET_V1,
        0);
    passed = passed && expect_status(
        "nested zero fuel",
        ribos_vm_interpreter_step_v1(
            test->prepared,
            &context->value,
            arena.storage,
            arena.size,
            &snapshot),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "nested fault receipt",
        ribos_vm_interpreter_fault_v1(
            test->prepared,
            arena.storage,
            arena.size,
            &receipt),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "nested receipt preserves location",
        snapshot.state == RIBOS_VM_INTERPRETER_FAULTED &&
            snapshot.fault_code ==
                RIBOS_VM_FAULT_INSTRUCTION_BUDGET &&
            snapshot.function_id == before.function_id &&
            snapshot.instruction_id == before.instruction_id &&
            snapshot.frame_depth == before.frame_depth &&
            receipt.function_id == before.function_id &&
            receipt.instruction_id == before.instruction_id &&
            receipt.consumed_instructions ==
                before.consumed_instructions &&
            section_row(instructions, before.instruction_id) != NULL);
    release_arena(&arena);
    return passed;
}

static uint32_t
find_external_header_transition(
    const RibosArtifactView *view,
    uint32_t function_id,
    uint32_t header_block,
    uint32_t latch_block)
{
    const RibosArtifactSectionView *blocks =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_BLOCKS);
    const RibosArtifactSectionView *instructions =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_INSTRUCTIONS);

    for (uint32_t id = 0; id < instructions->count; ++id) {
        const uint8_t *instruction = section_row(instructions, id);
        uint32_t owner_block = read_u32(instruction + 8);
        const uint8_t *block = section_row(blocks, owner_block);

        if (block == NULL || read_u32(block + 4) != function_id ||
            owner_block == latch_block) {
            continue;
        }
        if ((instruction[0] == RIBOS_BC_JUMP &&
             read_u32(instruction + 20) == header_block) ||
            (instruction[0] == RIBOS_BC_BRANCH &&
             (read_u32(instruction + 20) == header_block ||
              read_u32(instruction + 24) == header_block))) {
            return id;
        }
    }
    return RIBOS_VM_INVALID_ID;
}

static int
store_branch_condition(
    TestProgram *test,
    TestArena *arena,
    const uint8_t *branch,
    uint32_t value)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(test->prepared);
    return ribos_vm_storage_slot_store_bool_v1(
        test->prepared,
        arena->storage,
        arena->size,
        view->entry_function,
        0,
        instruction_operand(view, branch, 0),
        value) == RIBOS_VM_STATUS_OK;
}

static int
exercise_loop_trip_count(
    TestProgram *test,
    const TestContext *context,
    uint32_t loop_id,
    int verify_reset)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(test->prepared);
    const RibosArtifactSectionView *loops =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_LOOPS);
    const RibosArtifactSectionView *blocks =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_BLOCKS);
    const RibosArtifactSectionView *instructions =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_INSTRUCTIONS);
    const uint8_t *loop = section_row(loops, loop_id);
    uint32_t function_id = loop == NULL ?
        RIBOS_VM_INVALID_ID : read_u32(loop + 4);
    uint32_t header_id = loop == NULL ?
        RIBOS_VM_INVALID_ID : read_u32(loop + 8);
    uint32_t body_id = loop == NULL ?
        RIBOS_VM_INVALID_ID : read_u32(loop + 12);
    uint32_t latch_id = loop == NULL ?
        RIBOS_VM_INVALID_ID : read_u32(loop + 20);
    uint32_t trip_count = loop == NULL ? 0 : read_u32(loop + 24);
    const uint8_t *header = section_row(blocks, header_id);
    const uint8_t *latch = section_row(blocks, latch_id);
    uint32_t branch_id = header == NULL ?
        RIBOS_VM_INVALID_ID : read_u32(header + 12);
    uint32_t jump_id = latch == NULL ?
        RIBOS_VM_INVALID_ID : read_u32(latch + 12);
    const uint8_t *branch = section_row(instructions, branch_id);
    const uint8_t *jump = section_row(instructions, jump_id);
    uint32_t incoming_id = find_external_header_transition(
        view,
        function_id,
        header_id,
        latch_id);
    const uint8_t *incoming = section_row(instructions, incoming_id);
    TestArena arena;
    RibosVmInterpreterSnapshot snapshot;
    uint64_t before;
    int passed;

    memset(&arena, 0, sizeof(arena));
    if (loop == NULL || function_id != view->entry_function ||
        trip_count == 0 || branch == NULL || jump == NULL ||
        branch[0] != RIBOS_BC_BRANCH ||
        jump[0] != RIBOS_BC_JUMP ||
        read_u32(branch + 20) != body_id ||
        !initialize_arena(test, context, &arena)) {
        release_arena(&arena);
        return expect_true("bounded loop fixture", 0);
    }
    passed = expect_status(
        "loop initial snapshot",
        ribos_vm_interpreter_snapshot_v1(
            test->prepared,
            arena.storage,
            arena.size,
            &snapshot),
        RIBOS_VM_STATUS_OK);
    before = snapshot.consumed_instructions;
    for (uint32_t iteration = 0;
         passed && iteration < trip_count;
         ++iteration) {
        passed = expect_true(
            "position loop header",
            reposition_execution(test, &arena, branch_id));
        passed = passed && expect_true(
            "initialize loop condition",
            store_branch_condition(test, &arena, branch, 1));
        passed = passed && expect_status(
            "enter loop body",
            ribos_vm_interpreter_step_v1(
                test->prepared,
                &context->value,
                arena.storage,
                arena.size,
                &snapshot),
            RIBOS_VM_STATUS_OK);
        passed = passed && expect_true(
            "position loop latch",
            snapshot.state == RIBOS_VM_INTERPRETER_RUNNING &&
                snapshot.block_id == body_id &&
                reposition_execution(test, &arena, jump_id));
        passed = passed && expect_status(
            "consume loop backedge",
            ribos_vm_interpreter_step_v1(
                test->prepared,
                &context->value,
                arena.storage,
                arena.size,
                &snapshot),
            RIBOS_VM_STATUS_OK);
        passed = passed && expect_true(
            "latch returns to header",
            snapshot.state == RIBOS_VM_INTERPRETER_RUNNING &&
                snapshot.block_id == header_id);
    }
    passed = passed && expect_true(
        "loop dispatch accounting exact",
        snapshot.consumed_instructions - before ==
            (uint64_t)trip_count * UINT64_C(2));
    if (verify_reset) {
        passed = passed && expect_true(
            "external loop entry exists",
            incoming != NULL);
        if (passed && incoming[0] == RIBOS_BC_BRANCH) {
            uint32_t target_is_true =
                read_u32(incoming + 20) == header_id;

            passed = expect_true(
                "external branch condition",
                store_branch_condition(
                    test,
                    &arena,
                    incoming,
                    target_is_true));
        }
        passed = passed && expect_true(
            "position external loop entry",
            reposition_execution(test, &arena, incoming_id));
        passed = passed && expect_status(
            "reset loop on external entry",
            ribos_vm_interpreter_step_v1(
                test->prepared,
                &context->value,
                arena.storage,
                arena.size,
                &snapshot),
            RIBOS_VM_STATUS_OK);
        passed = passed && expect_true(
            "external entry reaches header",
            snapshot.state == RIBOS_VM_INTERPRETER_RUNNING &&
                snapshot.block_id == header_id &&
                reposition_execution(test, &arena, branch_id) &&
                store_branch_condition(test, &arena, branch, 1));
        passed = passed && expect_status(
            "reset loop admits new body",
            ribos_vm_interpreter_step_v1(
                test->prepared,
                &context->value,
                arena.storage,
                arena.size,
                &snapshot),
            RIBOS_VM_STATUS_OK);
        passed = passed && expect_true(
            "reset counter is live",
            snapshot.state == RIBOS_VM_INTERPRETER_RUNNING &&
                snapshot.block_id == body_id);
    } else {
        RibosVmFaultReceipt receipt;

        passed = passed && expect_true(
            "position exhausted header",
            reposition_execution(test, &arena, branch_id) &&
                store_branch_condition(test, &arena, branch, 1));
        passed = passed && expect_status(
            "exhausted loop faults",
            ribos_vm_interpreter_step_v1(
                test->prepared,
                &context->value,
                arena.storage,
                arena.size,
                &snapshot),
            RIBOS_VM_STATUS_OK);
        passed = passed && expect_status(
            "loop bound receipt",
            ribos_vm_interpreter_fault_v1(
                test->prepared,
                arena.storage,
                arena.size,
                &receipt),
            RIBOS_VM_STATUS_OK);
        passed = passed && expect_true(
            "loop bound is fail closed",
            snapshot.state == RIBOS_VM_INTERPRETER_FAULTED &&
                snapshot.fault_code == RIBOS_VM_FAULT_LOOP_BOUND &&
                receipt.fault_code == RIBOS_VM_FAULT_LOOP_BOUND &&
                receipt.detail == loop_id &&
                receipt.function_id == function_id &&
                receipt.instruction_id == branch_id);
    }
    release_arena(&arena);
    return passed;
}

static int
test_loops(
    TestProgram *test,
    const TestContext *context)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(test->prepared);
    const RibosArtifactSectionView *loops =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_LOOPS);
    int passed;

    if (loops == NULL || loops->count != 2) {
        return expect_true("nested loop rows", 0);
    }
    passed = exercise_loop_trip_count(
        test,
        context,
        0,
        0);
    passed = exercise_loop_trip_count(
        test,
        context,
        1,
        1) && passed;
    return passed;
}

int
main(int argc, char **argv)
{
    TestProgram test;
    TestContext context;
    int passed;

    if (argc != 3 ||
        (strcmp(argv[2], "calls") != 0 &&
         strcmp(argv[2], "loops") != 0)) {
        fprintf(stderr, "usage: %s POLICY.rba calls|loops\n", argv[0]);
        return 2;
    }
    if (!prepare_program(argv[1], &test) ||
        !initialize_context(&test, &context)) {
        fprintf(stderr, "failed to prepare calls/loops fixture\n");
        release_program(&test);
        return 1;
    }
    if (strcmp(argv[2], "calls") == 0) {
        passed = test_direct_calls(&test, &context);
        passed = test_call_depth_and_nested_fault(
            &test,
            &context) && passed;
    } else {
        passed = test_loops(&test, &context);
    }
    release_program(&test);
    if (!passed) {
        return 1;
    }
    if (strcmp(argv[2], "calls") == 0) {
        puts(
            "RIBOS-VM-CALLS-OK calls=direct frames=explicit "
            "resources=exact evidence=host-interpreter");
    } else {
        puts(
            "RIBOS-VM-LOOPS-OK loops=latch-bounded "
            "dispatch=exact reset=external-entry "
            "evidence=host-interpreter");
    }
    return 0;
}
