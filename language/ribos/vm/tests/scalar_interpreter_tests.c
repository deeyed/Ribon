#include "ribos/vm/interpreter.h"

#include "internal.h"
#include "storage_internal.h"

#include <limits.h>
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

static uint32_t helper_invocations;

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
    ++helper_invocations;
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
        .authority_generation = 89,
        .manifest_sequence = 144,
        .rollback_floor = 55,
        .policy_identity_digest = {0x72},
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
        .maximum_instructions = 512,
        .maximum_helper_calls = 2,
        .maximum_stack_bytes = 4096,
        .maximum_arena_bytes = 65536,
        .maximum_input_bytes = 256,
        .maximum_output_bytes = 256,
        .maximum_operations = 16,
        .maximum_polls = 16,
        .maximum_execution_duration_ns = 1000000,
        .maximum_helper_duration_ns = 100000,
        .maximum_call_depth = 4,
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
    uint32_t parameter_count;
    uint32_t byte_size;

    if (function == NULL) {
        return 0;
    }
    parameter_start = read_u32(function + 32);
    parameter_count = read_u32(function + 36);
    slot = section_row(slots, parameter_start);
    if (parameter_count != 1 || slot == NULL) {
        return 0;
    }
    byte_size = read_u32(slot + 16);
    if (byte_size > sizeof(context->bytes)) {
        return 0;
    }
    memset(context, 0, sizeof(*context));
    for (uint32_t index = 0; index < byte_size; ++index) {
        context->bytes[index] = (uint8_t)(0x31u + index);
    }
    context->value = (RibosVmContext){
        .size = sizeof(context->value),
        .runtime_abi_major = RIBOS_VM_RUNTIME_ABI_V1_MAJOR,
        .runtime_abi_minor = RIBOS_VM_RUNTIME_ABI_V1_MINOR,
        .context_type_id = read_u32(slot + 8),
        .selected_mode = 0,
        .selected_phase = 0,
        .generation = 1,
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

static uint32_t
instruction_opcode(const TestProgram *test, uint32_t instruction_id)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(test->prepared);
    const RibosArtifactSectionView *instructions =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_INSTRUCTIONS);
    const uint8_t *instruction =
        section_row(instructions, instruction_id);

    return instruction == NULL ? 0 : instruction[0];
}

static int
instruction_slot_type(
    const TestProgram *test,
    uint32_t slot_id,
    uint16_t *kind,
    uint16_t *width)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(test->prepared);
    const RibosArtifactSectionView *slots =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_SLOTS);
    const RibosArtifactSectionView *types =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_TYPES);
    const uint8_t *slot = section_row(slots, slot_id);
    const uint8_t *type;

    if (slot == NULL) {
        return 0;
    }
    type = section_row(types, read_u32(slot + 8));
    if (type == NULL) {
        return 0;
    }
    *kind = read_u16(type + 4);
    *width = read_u16(type + 6);
    return 1;
}

static uint32_t
instruction_operand(
    const TestProgram *test,
    const uint8_t *instruction,
    uint32_t ordinal)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(test->prepared);
    const RibosArtifactSectionView *operands =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_OPERANDS);
    uint32_t start = read_u32(instruction + 16);
    uint32_t count = read_u16(instruction + 2);
    const uint8_t *row;

    if (ordinal >= count || start > operands->count ||
        ordinal >= operands->count - start) {
        return RIBOS_VM_INVALID_ID;
    }
    row = section_row(operands, start + ordinal);
    return row == NULL ? RIBOS_VM_INVALID_ID : read_u32(row);
}

static uint32_t
find_checked_instruction(
    const TestProgram *test,
    uint32_t opcode,
    uint32_t operation,
    uint16_t kind,
    uint16_t width)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(test->prepared);
    const RibosArtifactSectionView *instructions =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_INSTRUCTIONS);

    for (uint32_t id = 0; id < instructions->count; ++id) {
        const uint8_t *instruction =
            section_row(instructions, id);
        uint32_t operand;
        uint16_t actual_kind;
        uint16_t actual_width;

        if (instruction[0] != opcode ||
            read_u32(instruction + 20) != operation) {
            continue;
        }
        operand = instruction_operand(test, instruction, 0);
        if (operand != RIBOS_VM_INVALID_ID &&
            instruction_slot_type(
                test,
                operand,
                &actual_kind,
                &actual_width) &&
            actual_kind == kind &&
            actual_width == width) {
            return id;
        }
    }
    return RIBOS_VM_INVALID_ID;
}

static uint32_t
find_opcode_instruction(
    const TestProgram *test,
    uint32_t opcode)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(test->prepared);
    const RibosArtifactSectionView *instructions =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_INSTRUCTIONS);

    for (uint32_t id = 0; id < instructions->count; ++id) {
        const uint8_t *instruction =
            section_row(instructions, id);

        if (instruction != NULL && instruction[0] == opcode) {
            return id;
        }
    }
    return RIBOS_VM_INVALID_ID;
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
            &remaining) != RIBOS_VM_STATUS_OK) {
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

static int
advance_to_instruction(
    TestProgram *test,
    const TestContext *context,
    TestArena *arena,
    uint32_t target,
    RibosVmInterpreterSnapshot *snapshot)
{
    if (ribos_vm_interpreter_snapshot_v1(
            test->prepared,
            arena->storage,
            arena->size,
            snapshot) != RIBOS_VM_STATUS_OK) {
        return 0;
    }
    while (snapshot->instruction_id != target &&
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
    return snapshot->instruction_id == target &&
        (snapshot->state == RIBOS_VM_INTERPRETER_READY ||
         snapshot->state == RIBOS_VM_INTERPRETER_RUNNING);
}

static int
test_normal_execution(
    TestProgram *test,
    const TestContext *context)
{
    TestArena arena;
    RibosVmInterpreterSnapshot snapshot;
    RibosVmFaultReceipt receipt;
    uint32_t opcode_counts[RIBOS_BC_TRAP + 1] = {0};
    uint64_t initial_fuel;
    int passed;

    if (!initialize_arena(test, context, &arena)) {
        return expect_true("normal arena", 0);
    }
    passed = expect_status(
        "initial snapshot",
        ribos_vm_interpreter_snapshot_v1(
            test->prepared,
            arena.storage,
            arena.size,
            &snapshot),
        RIBOS_VM_STATUS_OK);
    initial_fuel = snapshot.remaining_instructions;
    passed = passed && expect_true(
        "initial ready and exact fuel",
        snapshot.state == RIBOS_VM_INTERPRETER_READY &&
            initial_fuel ==
                test->report.recomputed_instruction_upper_bound);
    while (passed &&
           (snapshot.state == RIBOS_VM_INTERPRETER_READY ||
            snapshot.state == RIBOS_VM_INTERPRETER_RUNNING)) {
        uint32_t opcode =
            instruction_opcode(test, snapshot.instruction_id);

        if (opcode <= RIBOS_BC_TRAP) {
            ++opcode_counts[opcode];
        }
        passed = expect_status(
            "scalar step",
            ribos_vm_interpreter_step_v1(
                test->prepared,
                &context->value,
                arena.storage,
                arena.size,
                &snapshot),
            RIBOS_VM_STATUS_OK);
    }
    passed = passed && expect_true(
        "scalar/control path reaches unsupported helper",
        snapshot.state == RIBOS_VM_INTERPRETER_FAULTED &&
            snapshot.fault_code == RIBOS_VM_FAULT_INVALID_STATE &&
            instruction_opcode(test, snapshot.instruction_id) ==
                RIBOS_BC_CALL_HELPER &&
            opcode_counts[RIBOS_BC_PARAMETER] == 1 &&
            opcode_counts[RIBOS_BC_CONST_BOOL] != 0 &&
            opcode_counts[RIBOS_BC_CONST_INTEGER] != 0 &&
            opcode_counts[RIBOS_BC_CONST_STRING] != 0 &&
            opcode_counts[RIBOS_BC_CONST_SYMBOL] != 0 &&
            opcode_counts[RIBOS_BC_MOVE] != 0 &&
            opcode_counts[RIBOS_BC_CHECKED_UNARY] != 0 &&
            opcode_counts[RIBOS_BC_CHECKED_BINARY] != 0 &&
            opcode_counts[RIBOS_BC_JUMP] != 0 &&
            opcode_counts[RIBOS_BC_BRANCH] != 0 &&
            opcode_counts[RIBOS_BC_CALL_HELPER] == 1 &&
            snapshot.consumed_instructions ==
                initial_fuel - snapshot.remaining_instructions);
    passed = passed && expect_status(
        "fault receipt",
        ribos_vm_interpreter_fault_v1(
            test->prepared,
            arena.storage,
            arena.size,
            &receipt),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "fault receipt provenance",
        receipt.fault_code == snapshot.fault_code &&
            receipt.function_id == snapshot.function_id &&
            receipt.instruction_id == snapshot.instruction_id &&
            receipt.detail == snapshot.source_map_id &&
            receipt.consumed_instructions ==
                snapshot.consumed_instructions &&
            memcmp(
                receipt.artifact_hash,
                ribos_prepared_program_artifact_view_v1(
                    test->prepared)->artifact_hash,
                RIBOS_VM_DIGEST_BYTES) == 0 &&
            helper_invocations == 0);
    passed = passed && expect_status(
        "terminal step rejected",
        ribos_vm_interpreter_step_v1(
            test->prepared,
            &context->value,
            arena.storage,
            arena.size,
            &snapshot),
        RIBOS_VM_STATUS_ALREADY_CONSUMED);
    release_arena(&arena);
    return passed;
}

static int
test_run_and_context_binding(
    TestProgram *test,
    TestContext *context)
{
    TestArena arena;
    RibosVmInterpreterSnapshot before;
    RibosVmInterpreterSnapshot after;
    int passed;

    if (!initialize_arena(test, context, &arena)) {
        return expect_true("run arena", 0);
    }
    passed = expect_status(
        "run snapshot",
        ribos_vm_interpreter_snapshot_v1(
            test->prepared,
            arena.storage,
            arena.size,
            &before),
        RIBOS_VM_STATUS_OK);
    context->bytes[0] ^= 0xff;
    passed = passed && expect_status(
        "mutated borrowed context rejected",
        ribos_vm_interpreter_step_v1(
            test->prepared,
            &context->value,
            arena.storage,
            arena.size,
            &after),
        RIBOS_VM_STATUS_DIGEST_MISMATCH);
    context->bytes[0] ^= 0xff;
    passed = passed && expect_status(
        "unchanged fuel snapshot",
        ribos_vm_interpreter_snapshot_v1(
            test->prepared,
            arena.storage,
            arena.size,
            &after),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "context rejection consumes no instruction",
        after.remaining_instructions == before.remaining_instructions &&
            after.consumed_instructions ==
                before.consumed_instructions);
    passed = passed && expect_status(
        "bounded run",
        ribos_vm_interpreter_run_v1(
            test->prepared,
            &context->value,
            arena.storage,
            arena.size,
            &after),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "run terminates fail closed at helper boundary",
        after.state == RIBOS_VM_INTERPRETER_FAULTED &&
            after.fault_code == RIBOS_VM_FAULT_INVALID_STATE &&
            instruction_opcode(test, after.instruction_id) ==
                RIBOS_BC_CALL_HELPER);
    release_arena(&arena);
    return passed;
}

static int
test_exact_fuel(
    TestProgram *test,
    const TestContext *context)
{
    TestArena arena;
    RibosVmInterpreterSnapshot snapshot;
    RibosVmFaultReceipt receipt;
    int passed;

    if (!initialize_arena(test, context, &arena)) {
        return expect_true("fuel arena", 0);
    }
    write_u64(
        arena.bytes +
            RIBOS_VM_STORAGE_CONTROL_REMAINING_INSTRUCTIONS_OFFSET_V1,
        0);
    passed = expect_status(
        "zero fuel step",
        ribos_vm_interpreter_step_v1(
            test->prepared,
            &context->value,
            arena.storage,
            arena.size,
            &snapshot),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "fuel checked before dispatch",
        snapshot.state == RIBOS_VM_INTERPRETER_FAULTED &&
            snapshot.fault_code ==
                RIBOS_VM_FAULT_INSTRUCTION_BUDGET &&
            snapshot.remaining_instructions == 0 &&
            snapshot.consumed_instructions == 0);
    passed = passed && expect_status(
        "fuel fault receipt",
        ribos_vm_interpreter_fault_v1(
            test->prepared,
            arena.storage,
            arena.size,
            &receipt),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "fuel receipt counter",
        receipt.fault_code == RIBOS_VM_FAULT_INSTRUCTION_BUDGET &&
            receipt.consumed_instructions == 0);
    release_arena(&arena);
    return passed;
}

static int
store_unsigned_operands(
    TestProgram *test,
    TestArena *arena,
    uint32_t instruction_id,
    uint16_t width,
    uint64_t left,
    uint64_t right)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(test->prepared);
    const RibosArtifactSectionView *instructions =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_INSTRUCTIONS);
    const uint8_t *instruction =
        section_row(instructions, instruction_id);

    return instruction != NULL &&
        ribos_vm_storage_slot_store_unsigned_v1(
            test->prepared,
            arena->storage,
            arena->size,
            view->entry_function,
            0,
            instruction_operand(test, instruction, 0),
            width,
            left) == RIBOS_VM_STATUS_OK &&
        ribos_vm_storage_slot_store_unsigned_v1(
            test->prepared,
            arena->storage,
            arena->size,
            view->entry_function,
            0,
            instruction_operand(test, instruction, 1),
            width,
            right) == RIBOS_VM_STATUS_OK;
}

static int
store_signed_operands(
    TestProgram *test,
    TestArena *arena,
    uint32_t instruction_id,
    uint16_t width,
    int64_t left,
    int64_t right)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(test->prepared);
    const RibosArtifactSectionView *instructions =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_INSTRUCTIONS);
    const uint8_t *instruction =
        section_row(instructions, instruction_id);

    return instruction != NULL &&
        ribos_vm_storage_slot_store_signed_v1(
            test->prepared,
            arena->storage,
            arena->size,
            view->entry_function,
            0,
            instruction_operand(test, instruction, 0),
            width,
            left) == RIBOS_VM_STATUS_OK &&
        ribos_vm_storage_slot_store_signed_v1(
            test->prepared,
            arena->storage,
            arena->size,
            view->entry_function,
            0,
            instruction_operand(test, instruction, 1),
            width,
            right) == RIBOS_VM_STATUS_OK;
}

static int
expect_arithmetic_fault(
    TestProgram *test,
    const TestContext *context,
    uint32_t instruction_id,
    uint16_t kind,
    uint16_t width,
    uint64_t left_raw,
    uint64_t right_raw)
{
    TestArena arena;
    RibosVmInterpreterSnapshot snapshot;
    RibosVmFaultReceipt receipt;
    int stored;
    int passed;

    memset(&arena, 0, sizeof(arena));
    if (instruction_id == RIBOS_VM_INVALID_ID ||
        !initialize_arena(test, context, &arena) ||
        !advance_to_instruction(
            test,
            context,
            &arena,
            instruction_id,
            &snapshot)) {
        release_arena(&arena);
        return expect_true("arithmetic target reachable", 0);
    }
    stored = kind == RIBOS_BC_TYPE_UNSIGNED ?
        store_unsigned_operands(
            test,
            &arena,
            instruction_id,
            width,
            left_raw,
            right_raw) :
        store_signed_operands(
            test,
            &arena,
            instruction_id,
            width,
            (int64_t)left_raw,
            (int64_t)right_raw);
    passed = expect_true("hostile operands stored", stored);
    passed = passed && expect_status(
        "hostile arithmetic step",
        ribos_vm_interpreter_step_v1(
            test->prepared,
            &context->value,
            arena.storage,
            arena.size,
            &snapshot),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "arithmetic traps fail closed",
        snapshot.state == RIBOS_VM_INTERPRETER_FAULTED &&
            snapshot.fault_code == RIBOS_VM_FAULT_ARITHMETIC &&
            snapshot.instruction_id == instruction_id);
    passed = passed && expect_status(
        "arithmetic receipt",
        ribos_vm_interpreter_fault_v1(
            test->prepared,
            arena.storage,
            arena.size,
            &receipt),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "arithmetic receipt location",
        receipt.subject == RIBOS_VM_FAULT_SUBJECT_VALUE &&
            receipt.instruction_id == instruction_id &&
            receipt.consumed_instructions ==
                snapshot.consumed_instructions);
    release_arena(&arena);
    return passed;
}

static int
test_arithmetic_faults(
    TestProgram *test,
    const TestContext *context)
{
    int passed = 1;

    passed = passed && expect_arithmetic_fault(
        test,
        context,
        find_checked_instruction(
            test,
            RIBOS_BC_CHECKED_BINARY,
            RIBOS_BC_CHECK_ADD,
            RIBOS_BC_TYPE_UNSIGNED,
            8),
        RIBOS_BC_TYPE_UNSIGNED,
        8,
        UINT8_MAX,
        1);
    passed = passed && expect_arithmetic_fault(
        test,
        context,
        find_checked_instruction(
            test,
            RIBOS_BC_CHECKED_BINARY,
            RIBOS_BC_CHECK_SUBTRACT,
            RIBOS_BC_TYPE_UNSIGNED,
            8),
        RIBOS_BC_TYPE_UNSIGNED,
        8,
        0,
        1);
    passed = passed && expect_arithmetic_fault(
        test,
        context,
        find_checked_instruction(
            test,
            RIBOS_BC_CHECKED_BINARY,
            RIBOS_BC_CHECK_MULTIPLY,
            RIBOS_BC_TYPE_UNSIGNED,
            8),
        RIBOS_BC_TYPE_UNSIGNED,
        8,
        UINT8_MAX,
        2);
    passed = passed && expect_arithmetic_fault(
        test,
        context,
        find_checked_instruction(
            test,
            RIBOS_BC_CHECKED_BINARY,
            RIBOS_BC_CHECK_DIVIDE,
            RIBOS_BC_TYPE_UNSIGNED,
            32),
        RIBOS_BC_TYPE_UNSIGNED,
        32,
        10,
        0);
    passed = passed && expect_arithmetic_fault(
        test,
        context,
        find_checked_instruction(
            test,
            RIBOS_BC_CHECKED_BINARY,
            RIBOS_BC_CHECK_SHIFT_RIGHT,
            RIBOS_BC_TYPE_UNSIGNED,
            8),
        RIBOS_BC_TYPE_UNSIGNED,
        8,
        1,
        8);
    passed = passed && expect_arithmetic_fault(
        test,
        context,
        find_checked_instruction(
            test,
            RIBOS_BC_CHECKED_BINARY,
            RIBOS_BC_CHECK_DIVIDE,
            RIBOS_BC_TYPE_SIGNED,
            64),
        RIBOS_BC_TYPE_SIGNED,
        64,
        (uint64_t)INT64_MIN,
        (uint64_t)INT64_C(-1));
    passed = passed && expect_arithmetic_fault(
        test,
        context,
        find_checked_instruction(
            test,
            RIBOS_BC_CHECKED_BINARY,
            RIBOS_BC_CHECK_SHIFT_LEFT,
            RIBOS_BC_TYPE_SIGNED,
            64),
        RIBOS_BC_TYPE_SIGNED,
        64,
        1,
        63);
    return passed;
}

static int
test_unary_minimum_fault(
    TestProgram *test,
    const TestContext *context)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(test->prepared);
    const RibosArtifactSectionView *instructions =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_INSTRUCTIONS);
    uint32_t instruction_id = find_checked_instruction(
        test,
        RIBOS_BC_CHECKED_UNARY,
        RIBOS_BC_CHECK_NEGATIVE,
        RIBOS_BC_TYPE_SIGNED,
        8);
    const uint8_t *instruction =
        section_row(instructions, instruction_id);
    TestArena arena;
    RibosVmInterpreterSnapshot snapshot;
    int passed;

    memset(&arena, 0, sizeof(arena));
    if (instruction == NULL ||
        !initialize_arena(test, context, &arena) ||
        !advance_to_instruction(
            test,
            context,
            &arena,
            instruction_id,
            &snapshot)) {
        release_arena(&arena);
        return expect_true("unary target reachable", 0);
    }
    passed = expect_status(
        "store signed minimum",
        ribos_vm_storage_slot_store_signed_v1(
            test->prepared,
            arena.storage,
            arena.size,
            view->entry_function,
            0,
            instruction_operand(test, instruction, 0),
            8,
            INT8_MIN),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "signed minimum negation",
        ribos_vm_interpreter_step_v1(
            test->prepared,
            &context->value,
            arena.storage,
            arena.size,
            &snapshot),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "signed minimum negation faults",
        snapshot.state == RIBOS_VM_INTERPRETER_FAULTED &&
            snapshot.fault_code == RIBOS_VM_FAULT_ARITHMETIC &&
            snapshot.instruction_id == instruction_id);
    release_arena(&arena);
    return passed;
}

static int
test_signed_shift_boundary(
    TestProgram *test,
    const TestContext *context)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(test->prepared);
    const RibosArtifactSectionView *instructions =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_INSTRUCTIONS);
    uint32_t instruction_id = find_checked_instruction(
        test,
        RIBOS_BC_CHECKED_BINARY,
        RIBOS_BC_CHECK_SHIFT_LEFT,
        RIBOS_BC_TYPE_SIGNED,
        64);
    const uint8_t *instruction =
        section_row(instructions, instruction_id);
    TestArena arena;
    RibosVmInterpreterSnapshot snapshot;
    int64_t result = 0;
    int passed;

    memset(&arena, 0, sizeof(arena));
    if (instruction == NULL ||
        !initialize_arena(test, context, &arena) ||
        !advance_to_instruction(
            test,
            context,
            &arena,
            instruction_id,
            &snapshot)) {
        release_arena(&arena);
        return expect_true("signed shift target reachable", 0);
    }
    passed = expect_true(
        "signed shift boundary operands",
        store_signed_operands(
            test,
            &arena,
            instruction_id,
            64,
            -1,
            63));
    passed = passed && expect_status(
        "signed shift boundary step",
        ribos_vm_interpreter_step_v1(
            test->prepared,
            &context->value,
            arena.storage,
            arena.size,
            &snapshot),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "signed shift remains running",
        snapshot.state == RIBOS_VM_INTERPRETER_RUNNING);
    passed = passed && expect_status(
        "signed shift result",
        ribos_vm_storage_slot_load_signed_v1(
            test->prepared,
            arena.storage,
            arena.size,
            view->entry_function,
            0,
            read_u32(instruction + 12),
            64,
            &result),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "negative one shift 63 is signed minimum",
        result == INT64_MIN);
    release_arena(&arena);
    return passed;
}

static int
test_terminal_handlers(
    TestProgram *test,
    const TestContext *context)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(test->prepared);
    const RibosArtifactSectionView *instructions =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_INSTRUCTIONS);
    const RibosArtifactSectionView *slots =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_SLOTS);
    uint32_t return_id =
        find_opcode_instruction(test, RIBOS_BC_RETURN);
    uint32_t trap_id =
        find_opcode_instruction(test, RIBOS_BC_TRAP);
    const uint8_t *return_instruction =
        section_row(instructions, return_id);
    uint32_t return_slot = return_instruction == NULL ?
        RIBOS_VM_INVALID_ID :
        instruction_operand(test, return_instruction, 0);
    const uint8_t *return_slot_row =
        section_row(slots, return_slot);
    TestArena arena;
    RibosVmInterpreterSnapshot snapshot;
    uint8_t *return_bytes = NULL;
    uint32_t return_size = return_slot_row == NULL ?
        0 :
        read_u32(return_slot_row + 16);
    int passed;

    memset(&arena, 0, sizeof(arena));
    if (return_instruction == NULL || return_slot_row == NULL ||
        trap_id == RIBOS_VM_INVALID_ID || return_size == 0 ||
        !initialize_arena(test, context, &arena)) {
        release_arena(&arena);
        return expect_true("terminal instructions available", 0);
    }
    return_bytes = calloc(1, return_size);
    passed = expect_true("return bytes allocated", return_bytes != NULL);
    passed = passed && expect_status(
        "initialize return operand",
        ribos_vm_storage_slot_write_v1(
            test->prepared,
            arena.storage,
            arena.size,
            view->entry_function,
            0,
            return_slot,
            return_bytes,
            return_size),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "position at return",
        reposition_execution(test, &arena, return_id));
    passed = passed && expect_status(
        "return step",
        ribos_vm_interpreter_step_v1(
            test->prepared,
            &context->value,
            arena.storage,
            arena.size,
            &snapshot),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "return terminal state",
        snapshot.state == RIBOS_VM_INTERPRETER_RETURNED &&
            snapshot.return_slot_id == return_slot &&
            snapshot.instruction_id == return_id);
    free(return_bytes);
    release_arena(&arena);

    memset(&arena, 0, sizeof(arena));
    if (!passed || !initialize_arena(test, context, &arena)) {
        release_arena(&arena);
        return expect_true("trap arena", 0);
    }
    passed = expect_true(
        "position at trap",
        reposition_execution(test, &arena, trap_id));
    passed = passed && expect_status(
        "trap step",
        ribos_vm_interpreter_step_v1(
            test->prepared,
            &context->value,
            arena.storage,
            arena.size,
            &snapshot),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "trap terminal fault",
        snapshot.state == RIBOS_VM_INTERPRETER_FAULTED &&
            snapshot.fault_code == RIBOS_VM_FAULT_INVALID_STATE &&
            snapshot.instruction_id == trap_id);
    release_arena(&arena);
    return passed;
}

int
main(int argc, char **argv)
{
    TestProgram test;
    TestContext context;
    int passed;

    if (argc != 2) {
        fprintf(stderr, "usage: %s POLICY.rba\n", argv[0]);
        return 2;
    }
    if (!prepare_program(argv[1], &test) ||
        !initialize_context(&test, &context)) {
        fprintf(stderr, "failed to prepare scalar interpreter fixture\n");
        release_program(&test);
        return 1;
    }
    helper_invocations = 0;
    passed = test_normal_execution(&test, &context);
    passed = test_run_and_context_binding(&test, &context) && passed;
    passed = test_exact_fuel(&test, &context) && passed;
    passed = test_arithmetic_faults(&test, &context) && passed;
    passed = test_unary_minimum_fault(&test, &context) && passed;
    passed = test_signed_shift_boundary(&test, &context) && passed;
    passed = test_terminal_handlers(&test, &context) && passed;
    release_program(&test);
    if (!passed) {
        return 1;
    }
    puts(
        "RIBOS-VM-SCALAR-OK dispatch=switch arithmetic=checked "
        "control=direct fuel=exact evidence=host-interpreter");
    return 0;
}
