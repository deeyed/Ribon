#include "ribos/vm/handles.h"
#include "ribos/vm/interpreter.h"

#include "internal.h"

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
    RibosVmHelperBinding bindings[5];
    RibosVmHelperContract contract;
    RibosVmLimits limits;
    RibosVerifierReport report;
    TestAuthority authority;
} TestPrepared;

typedef struct DropProbe {
    uint32_t calls;
    uint32_t fail;
} DropProbe;

typedef struct TestArena {
    uint8_t *allocation;
    uint8_t *arena;
    size_t arena_size;
    RibosVmStorage *storage;
    RibosVmStoragePlan plan;
    RibosVmHandleHostEntry *entries;
    RibosVmHandleHostTable table;
} TestArena;

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

static uint32_t
invoke_helper(void *context, RibosVmHelperCall *call)
{
    (void)context;
    (void)call;
    return RIBOS_VM_HELPER_CALLBACK_OK;
}

static uint32_t
drop_probe(void *context, void *trusted_object)
{
    DropProbe *probe = context;

    if (probe == NULL || trusted_object == NULL) {
        return RIBOS_VM_HANDLE_DROP_FAILED;
    }
    ++probe->calls;
    return probe->fail ?
        RIBOS_VM_HANDLE_DROP_FAILED :
        RIBOS_VM_HANDLE_DROP_COMPLETE;
}

static uint32_t
authorize_for_test(
    void *context,
    const RibosArtifactAuthorizationRequest *request,
    RibosArtifactAuthorizationReceipt *receipt)
{
    TestAuthority *authority = context;

    if (authority == NULL || request == NULL || receipt == NULL ||
        !ribos_vm_digest_is_nonzero(request->artifact_hash) ||
        !ribos_vm_digest_is_nonzero(request->schema_digest)) {
        return RIBOS_VM_STATUS_NOT_AUTHORIZED;
    }
    *receipt = (RibosArtifactAuthorizationReceipt){
        .size = sizeof(*receipt),
        .authorization_major = RIBOS_VM_AUTHORIZATION_V1_MAJOR,
        .authorization_minor = RIBOS_VM_AUTHORIZATION_V1_MINOR,
        .decision = RIBOS_ARTIFACT_AUTHORIZATION_GRANTED,
        .authority_generation = 55,
        .manifest_sequence = 89,
        .rollback_floor = 34,
        .policy_identity_digest = {0x6d},
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

static int
initialize_helper_contract(TestPrepared *test)
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
prepare_test_program(const char *path, TestPrepared *test)
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
        .maximum_instructions = 64,
        .maximum_helper_calls = 4,
        .maximum_stack_bytes = 4096,
        .maximum_arena_bytes = 65536,
        .maximum_input_bytes = 256,
        .maximum_output_bytes = 256,
        .maximum_operations = 16,
        .maximum_polls = 16,
        .maximum_execution_duration_ns = 1000000,
        .maximum_helper_duration_ns = 100000,
        .maximum_call_depth = 4,
        .maximum_handles = 6,
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
release_test_program(TestPrepared *test)
{
    free(test->prepared_workspace);
    free(test->authorized_workspace);
    free(test->artifact);
    memset(test, 0, sizeof(*test));
}

static uint32_t
find_named_type(
    const RibosPreparedProgram *prepared,
    const char *name)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(prepared);
    const RibosArtifactSectionView *types =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_TYPES);
    size_t length = strlen(name);

    if (types == NULL || length > 63) {
        return RIBOS_VM_INVALID_ID;
    }
    for (uint32_t index = 0; index < types->count; ++index) {
        const uint8_t *row = types->bytes +
            (size_t)index * types->row_size;

        if (read_u16(row + 4) == RIBOS_BC_TYPE_NAMED &&
            read_u32(row + 56) == length &&
            memcmp(row + 60, name, length) == 0) {
            return index;
        }
    }
    return RIBOS_VM_INVALID_ID;
}

static uint32_t
find_affine_aggregate_type(
    const RibosPreparedProgram *prepared)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(prepared);
    const RibosArtifactSectionView *types =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_TYPES);

    if (types == NULL) {
        return RIBOS_VM_INVALID_ID;
    }
    for (uint32_t index = 0; index < types->count; ++index) {
        const uint8_t *row = types->bytes +
            (size_t)index * types->row_size;
        uint32_t ownership = RIBOS_VM_INVALID_ID;
        uint32_t type_class = RIBOS_VM_INVALID_ID;

        if (read_u16(row + 4) == RIBOS_BC_TYPE_NAMED ||
            ribos_prepared_program_type_semantics_v1(
                prepared,
                index,
                &ownership,
                &type_class) != RIBOS_VM_STATUS_OK) {
            continue;
        }
        if (ownership == RIBOS_SCHEMA_OWNERSHIP_AFFINE &&
            type_class == RIBOS_VM_INVALID_ID) {
            return index;
        }
    }
    return RIBOS_VM_INVALID_ID;
}

static int
initialize_test_arena(
    const TestPrepared *test,
    TestArena *arena)
{
    size_t required_size = 0;
    uintptr_t aligned;

    memset(arena, 0, sizeof(*arena));
    if (ribos_vm_runtime_size_v1(
            test->prepared,
            &arena->plan,
            &required_size) != RIBOS_VM_STATUS_OK ||
        arena->plan.handle_count !=
            test->limits.maximum_handles) {
        return 0;
    }
    arena->allocation = malloc(
        required_size + RIBOS_VM_STORAGE_ALIGNMENT_V1);
    arena->entries = calloc(
        arena->plan.handle_count,
        sizeof(*arena->entries));
    if (arena->allocation == NULL || arena->entries == NULL) {
        return 0;
    }
    aligned = ((uintptr_t)arena->allocation +
        RIBOS_VM_STORAGE_ALIGNMENT_V1 - 1u) &
        ~(uintptr_t)(RIBOS_VM_STORAGE_ALIGNMENT_V1 - 1u);
    arena->arena = (uint8_t *)aligned;
    arena->arena_size = required_size;
    return ribos_vm_storage_initialize_v1(
            test->prepared,
            &arena->plan,
            arena->arena,
            arena->arena_size,
            RIBOS_VM_STORAGE_INITIALIZE_DIAGNOSTIC_POISON,
            &arena->storage) == RIBOS_VM_STATUS_OK &&
        ribos_vm_handle_host_table_initialize_v1(
            &arena->table,
            arena->entries,
            arena->plan.handle_count) == RIBOS_VM_STATUS_OK;
}

static int
reset_test_arena(
    const TestPrepared *test,
    TestArena *arena)
{
    return ribos_vm_storage_initialize_v1(
            test->prepared,
            &arena->plan,
            arena->arena,
            arena->arena_size,
            RIBOS_VM_STORAGE_INITIALIZE_DIAGNOSTIC_POISON,
            &arena->storage) == RIBOS_VM_STATUS_OK &&
        ribos_vm_handle_host_table_initialize_v1(
            &arena->table,
            arena->entries,
            arena->plan.handle_count) == RIBOS_VM_STATUS_OK;
}

static void
release_test_arena(TestArena *arena)
{
    free(arena->entries);
    free(arena->allocation);
    memset(arena, 0, sizeof(*arena));
}

static int
test_generation_and_typestate(
    const TestPrepared *test,
    TestArena *arena,
    uint32_t image_type,
    uint32_t verified_type,
    uint32_t slot_type)
{
    int object_a = 1;
    int object_b = 2;
    int object_c = 3;
    DropProbe successful = {0};
    DropProbe failing = {.fail = 1};
    RibosVmHandleSnapshot snapshot;
    RibosVmHandleBorrow borrow;
    RibosVmHandleBorrow nested;
    RibosVmHandleConsumeLease lease;
    RibosVmHandleConsumeLease duplicate_lease;
    uint8_t source[8];
    uint8_t moved[8];
    uint8_t stale[8];
    uint8_t transitioned[8];
    uint8_t copy_source[8];
    uint8_t copy_target[8];
    uint32_t index;
    uint32_t generation;
    int passed;

    passed = expect_status(
        "create affine handle",
        ribos_vm_handle_create_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            image_type,
            &object_a,
            drop_probe,
            &failing,
            source),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "decode canonical token",
        ribos_vm_handle_token_decode_v1(
            source,
            &index,
            &generation),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "canonical little endian token",
        read_u32(source) == index &&
            read_u32(source + 4) == generation &&
            generation != 0);
    passed = passed && expect_status(
        "lookup affine handle",
        ribos_vm_handle_lookup_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            source,
            image_type,
            &snapshot),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "reject wrong typestate",
        ribos_vm_handle_lookup_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            source,
            verified_type,
            &snapshot),
        RIBOS_VM_STATUS_INVALID_STATE);
    memcpy(stale, source, sizeof(stale));
    stale[4] ^= 0x40;
    passed = passed && expect_status(
        "reject forged generation",
        ribos_vm_handle_lookup_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            stale,
            image_type,
            &snapshot),
        RIBOS_VM_STATUS_INVALID_STATE);
    passed = passed && expect_status(
        "borrow begin",
        ribos_vm_handle_borrow_begin_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            source,
            image_type,
            &borrow),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "nested borrow rejected",
        ribos_vm_handle_borrow_begin_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            source,
            image_type,
            &nested),
        RIBOS_VM_STATUS_INVALID_STATE);
    passed = passed && expect_status(
        "lookup during borrow rejected",
        ribos_vm_handle_lookup_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            source,
            image_type,
            &snapshot),
        RIBOS_VM_STATUS_INVALID_STATE);
    passed = passed && expect_status(
        "borrow end",
        ribos_vm_handle_borrow_end_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            &borrow),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "affine move",
        ribos_vm_handle_move_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            source,
            image_type,
            moved),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "move rotates generation",
        memcmp(source, moved, sizeof(source)) != 0);
    passed = passed && expect_status(
        "source stale after move",
        ribos_vm_handle_lookup_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            source,
            image_type,
            &snapshot),
        RIBOS_VM_STATUS_INVALID_STATE);
    passed = passed && expect_status(
        "destination available after move",
        ribos_vm_handle_lookup_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            moved,
            image_type,
            &snapshot),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "move count sealed",
        snapshot.move_count == 1);
    passed = passed && expect_status(
        "consume invalidates before callback",
        ribos_vm_handle_consume_begin_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            moved,
            image_type,
            &lease),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "double consume rejected",
        ribos_vm_handle_consume_begin_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            moved,
            image_type,
            &duplicate_lease),
        RIBOS_VM_STATUS_ALREADY_CONSUMED);
    passed = passed && expect_status(
        "failed drop remains fail closed",
        ribos_vm_handle_consume_finish_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            &lease,
            RIBOS_VM_HANDLE_CONSUME_DROP),
        RIBOS_VM_STATUS_EMBEDDER_REJECTED);
    passed = passed && expect_true(
        "drop called at most once",
        failing.calls == 1);
    passed = passed && expect_status(
        "consume lease cannot revive",
        ribos_vm_handle_consume_finish_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            &lease,
            RIBOS_VM_HANDLE_CONSUME_TRANSFERRED),
        RIBOS_VM_STATUS_ALREADY_CONSUMED);
    passed = passed && expect_status(
        "create transition source",
        ribos_vm_handle_create_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            image_type,
            &object_b,
            drop_probe,
            &successful,
            source),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "transition consume begin",
        ribos_vm_handle_consume_begin_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            source,
            image_type,
            &lease),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "null lease rejected",
        ribos_vm_handle_consume_replace_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            NULL,
            verified_type,
            &object_c,
            drop_probe,
            &successful,
            transitioned),
        RIBOS_VM_STATUS_INVALID_ARGUMENT);
    passed = passed && expect_status(
        "typestate replace",
        ribos_vm_handle_consume_replace_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            &lease,
            verified_type,
            &object_c,
            drop_probe,
            &successful,
            transitioned),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "transition source stale",
        ribos_vm_handle_lookup_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            source,
            image_type,
            &snapshot),
        RIBOS_VM_STATUS_INVALID_STATE);
    passed = passed && expect_status(
        "transition target typed",
        ribos_vm_handle_lookup_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            transitioned,
            verified_type,
            &snapshot),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "create copy handle",
        ribos_vm_handle_create_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            slot_type,
            &object_a,
            drop_probe,
            &successful,
            copy_source),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "copy handle move",
        ribos_vm_handle_move_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            copy_source,
            slot_type,
            copy_target),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "copy token remains identical",
        memcmp(copy_source, copy_target, sizeof(copy_source)) == 0);
    return passed;
}

static int
test_capacity_poison_and_cleanup(
    const TestPrepared *test,
    TestArena *arena,
    uint32_t image_type)
{
    int objects[8] = {0};
    DropProbe probes[8] = {{0}};
    uint8_t tokens[8][RIBOS_VM_HANDLE_TOKEN_BYTES_V1] = {{0}};
    RibosVmHandleSnapshot snapshot;
    RibosVmHandleBorrow borrow;
    RibosVmHandleConsumeLease lease;
    RibosVmHandleCleanupReport report;
    uint32_t index = 0;
    uint32_t generation = 0;
    uint8_t *record;
    int passed = reset_test_arena(test, arena);

    for (uint32_t item = 0;
         passed && item < arena->plan.handle_count;
         ++item) {
        objects[item] = (int)item + 1;
        passed = expect_status(
            "fill fixed handle table",
            ribos_vm_handle_create_v1(
                test->prepared,
                arena->storage,
                arena->arena_size,
                &arena->table,
                image_type,
                &objects[item],
                drop_probe,
                &probes[item],
                tokens[item]),
            RIBOS_VM_STATUS_OK);
    }
    passed = passed && expect_status(
        "capacity exhaustion",
        ribos_vm_handle_create_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            image_type,
            &objects[7],
            drop_probe,
            &probes[7],
            tokens[7]),
        RIBOS_VM_STATUS_LIMIT_EXCEEDED);
    passed = passed && expect_status(
        "decode poison target",
        ribos_vm_handle_token_decode_v1(
            tokens[0],
            &index,
            &generation),
        RIBOS_VM_STATUS_OK);
    record = arena->arena +
        arena->plan.regions[RIBOS_VM_STORAGE_REGION_HANDLES].offset +
        (size_t)index *
            arena->plan.regions[
                RIBOS_VM_STORAGE_REGION_HANDLES].stride;
    record[20] = 1;
    passed = passed && expect_status(
        "poisoned record rejected",
        ribos_vm_handle_lookup_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            tokens[0],
            image_type,
            &snapshot),
        RIBOS_VM_STATUS_INVALID_STATE);
    record[20] = 0;
    passed = passed && expect_status(
        "bounded cleanup after capacity",
        ribos_vm_handle_fault_cleanup_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            &report),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "capacity cleanup exact bound",
        report.scanned == arena->plan.handle_count &&
            report.revoked == arena->plan.handle_count &&
            report.drop_calls == arena->plan.handle_count &&
            report.drop_failures == 0);
    passed = passed && reset_test_arena(test, arena);
    probes[0].fail = 1;
    for (uint32_t item = 0; passed && item < 3; ++item) {
        passed = expect_status(
            "create mixed cleanup handle",
            ribos_vm_handle_create_v1(
                test->prepared,
                arena->storage,
                arena->arena_size,
                &arena->table,
                image_type,
                &objects[item],
                drop_probe,
                &probes[item],
                tokens[item]),
            RIBOS_VM_STATUS_OK);
    }
    passed = passed && expect_status(
        "leave borrowed for cleanup",
        ribos_vm_handle_borrow_begin_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            tokens[1],
            image_type,
            &borrow),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "leave in flight for cleanup",
        ribos_vm_handle_consume_begin_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            tokens[2],
            image_type,
            &lease),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "decode corrupt cleanup target",
        ribos_vm_handle_token_decode_v1(
            tokens[0],
            &index,
            &generation),
        RIBOS_VM_STATUS_OK);
    record = arena->arena +
        arena->plan.regions[
            RIBOS_VM_STORAGE_REGION_HANDLES].offset +
        (size_t)index *
            arena->plan.regions[
                RIBOS_VM_STORAGE_REGION_HANDLES].stride;
    record[20] = 1;
    passed = passed && expect_status(
        "cleanup reports callback failure",
        ribos_vm_handle_fault_cleanup_v1(
            test->prepared,
            arena->storage,
            arena->arena_size,
            &arena->table,
            &report),
        RIBOS_VM_STATUS_EMBEDDER_REJECTED);
    passed = passed && expect_true(
        "mixed cleanup bounded and complete",
        report.scanned == arena->plan.handle_count &&
            report.revoked == 3 &&
            report.drop_calls == 3 &&
            report.drop_failures == 1);
    for (uint32_t item = 0; passed && item < 3; ++item) {
        passed = expect_status(
            "cleanup leaves token stale",
            ribos_vm_handle_lookup_v1(
                test->prepared,
                arena->storage,
                arena->arena_size,
                &arena->table,
                tokens[item],
                image_type,
                &snapshot),
            RIBOS_VM_STATUS_ALREADY_CONSUMED);
        passed = passed && expect_true(
            "cleanup clears trusted pointer",
            arena->entries[item].trusted_object == NULL);
    }
    return passed;
}

static int
initialize_interpreter_context(
    const TestPrepared *test,
    RibosVmContext *context,
    uint8_t bytes[256])
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
    const uint8_t *function;
    const uint8_t *slot;
    uint32_t parameter_start;
    uint32_t parameter_count;
    uint32_t byte_size;

    if (functions == NULL || slots == NULL ||
        view->entry_function >= functions->count) {
        return 0;
    }
    function = functions->bytes +
        (size_t)view->entry_function * functions->row_size;
    parameter_start = read_u32(function + 32);
    parameter_count = read_u32(function + 36);
    if (parameter_count == 0 || parameter_start >= slots->count) {
        return 0;
    }
    slot = slots->bytes +
        (size_t)parameter_start * slots->row_size;
    byte_size = read_u32(slot + 16);
    if (byte_size > 256) {
        return 0;
    }
    memset(bytes, 0x71, byte_size);
    *context = (RibosVmContext){
        .size = sizeof(*context),
        .runtime_abi_major = RIBOS_VM_RUNTIME_ABI_V1_MAJOR,
        .runtime_abi_minor = RIBOS_VM_RUNTIME_ABI_V1_MINOR,
        .context_type_id = read_u32(slot + 8),
        .selected_mode = 0,
        .selected_phase = 0,
        .generation = 1,
        .bytes = bytes,
        .byte_size = byte_size,
    };
    ribos_artifact_sha256(
        bytes,
        byte_size,
        context->digest);
    return 1;
}

static uint32_t
find_affine_move_source(
    const TestPrepared *test,
    uint32_t *instruction_id)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(test->prepared);
    const RibosArtifactSectionView *instructions =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_INSTRUCTIONS);
    const RibosArtifactSectionView *operands =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_OPERANDS);
    const RibosArtifactSectionView *slots =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_SLOTS);

    if (instruction_id == NULL || instructions == NULL ||
        operands == NULL || slots == NULL) {
        return RIBOS_VM_INVALID_ID;
    }
    for (uint32_t index = 0; index < instructions->count; ++index) {
        const uint8_t *instruction = instructions->bytes +
            (size_t)index * instructions->row_size;
        uint32_t operand_start;
        uint32_t source;
        uint32_t type_id;
        uint32_t ownership;
        uint32_t type_class;

        if (instruction[0] != RIBOS_BC_MOVE ||
            read_u16(instruction + 2) != 1) {
            continue;
        }
        operand_start = read_u32(instruction + 16);
        if (operand_start >= operands->count) {
            continue;
        }
        source = read_u32(
            operands->bytes +
                (size_t)operand_start * operands->row_size);
        if (source >= slots->count) {
            continue;
        }
        type_id = read_u32(
            slots->bytes + (size_t)source * slots->row_size + 8);
        if (ribos_prepared_program_type_semantics_v1(
                test->prepared,
                type_id,
                &ownership,
                &type_class) == RIBOS_VM_STATUS_OK &&
            ownership == RIBOS_SCHEMA_OWNERSHIP_AFFINE &&
            type_class == RIBOS_VM_INVALID_ID) {
            *instruction_id = index;
            return source;
        }
    }
    return RIBOS_VM_INVALID_ID;
}

static int
test_interpreter_aggregate_ownership(const char *path)
{
    TestPrepared test;
    TestArena arena = {0};
    RibosVmContext context;
    RibosVmInterpreterSnapshot snapshot;
    uint8_t context_bytes[256] = {0};
    uint32_t move_instruction = RIBOS_VM_INVALID_ID;
    uint32_t source_slot;
    uint32_t source_state = RIBOS_VM_SLOT_STORAGE_UNINITIALIZED;
    uint32_t steps = 0;
    int passed;

    if (!prepare_test_program(path, &test)) {
        return expect_true("prepare aggregate ownership fixture", 0);
    }
    source_slot = find_affine_move_source(
        &test,
        &move_instruction);
    passed = expect_true(
        "discover affine aggregate move",
        source_slot != RIBOS_VM_INVALID_ID &&
            move_instruction != RIBOS_VM_INVALID_ID);
    passed = passed && initialize_interpreter_context(
        &test,
        &context,
        context_bytes);
    passed = passed && initialize_test_arena(&test, &arena);
    passed = passed && expect_status(
        "initialize aggregate ownership interpreter",
        ribos_vm_interpreter_initialize_v1(
            test.prepared,
            &context,
            arena.storage,
            arena.arena_size),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "initial aggregate ownership snapshot",
        ribos_vm_interpreter_snapshot_v1(
            test.prepared,
            arena.storage,
            arena.arena_size,
            &snapshot),
        RIBOS_VM_STATUS_OK);
    while (passed && snapshot.instruction_id != move_instruction &&
           steps < 32) {
        passed = expect_status(
            "step toward affine aggregate move",
            ribos_vm_interpreter_step_v1(
                test.prepared,
                &context,
                arena.storage,
                arena.arena_size,
                &snapshot),
            RIBOS_VM_STATUS_OK);
        passed = passed &&
            snapshot.state != RIBOS_VM_INTERPRETER_FAULTED;
        ++steps;
    }
    passed = passed && expect_true(
        "affine aggregate move reached",
        snapshot.instruction_id == move_instruction);
    passed = passed && expect_status(
        "execute affine aggregate move",
        ribos_vm_interpreter_step_v1(
            test.prepared,
            &context,
            arena.storage,
            arena.arena_size,
            &snapshot),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "read affine aggregate source state",
        ribos_vm_storage_slot_state_v1(
            test.prepared,
            arena.storage,
            arena.arena_size,
            source_slot,
            &source_state),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "whole affine aggregate source moved",
        source_state == RIBOS_VM_SLOT_STORAGE_MOVED);
    release_test_arena(&arena);
    release_test_program(&test);
    return passed;
}

int
main(int argc, char **argv)
{
    TestPrepared test;
    TestArena arena = {0};
    uint32_t image_type;
    uint32_t verified_type;
    uint32_t slot_type;
    uint32_t aggregate_type;
    uint32_t ownership = RIBOS_VM_INVALID_ID;
    uint32_t type_class = RIBOS_VM_INVALID_ID;
    int passed;

    if (argc != 3) {
        fprintf(
            stderr,
            "usage: %s HANDLE_POLICY.rba OWNERSHIP_POLICY.rba\n",
            argv[0]);
        return 2;
    }
    if (!prepare_test_program(argv[1], &test)) {
        fprintf(stderr, "cannot prepare handle runtime fixture\n");
        release_test_program(&test);
        return 2;
    }
    image_type = find_named_type(test.prepared, "Image");
    verified_type = find_named_type(test.prepared, "VerifiedImage");
    slot_type = find_named_type(test.prepared, "Slot");
    aggregate_type = find_affine_aggregate_type(test.prepared);
    passed = expect_true(
        "fixture type discovery",
        image_type != RIBOS_VM_INVALID_ID &&
            verified_type != RIBOS_VM_INVALID_ID &&
            slot_type != RIBOS_VM_INVALID_ID &&
            aggregate_type != RIBOS_VM_INVALID_ID);
    passed = passed && expect_status(
        "prepared aggregate ownership",
        ribos_prepared_program_type_semantics_v1(
            test.prepared,
            aggregate_type,
            &ownership,
            &type_class),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "aggregate ownership is whole-value affine",
        ownership == RIBOS_SCHEMA_OWNERSHIP_AFFINE &&
            type_class == RIBOS_VM_INVALID_ID);
    passed = passed && initialize_test_arena(&test, &arena);
    passed = passed && test_generation_and_typestate(
        &test,
        &arena,
        image_type,
        verified_type,
        slot_type);
    passed = passed && test_capacity_poison_and_cleanup(
        &test,
        &arena,
        image_type);
    passed = passed &&
        test_interpreter_aggregate_ownership(argv[2]);
    release_test_arena(&arena);
    release_test_program(&test);
    if (!passed) {
        fprintf(stderr, "RIBOS-VM-HANDLES-FAIL\n");
        return 1;
    }
    puts(
        "RIBOS-VM-HANDLES-OK "
        "token=generation-index ownership=sealed typestate=checked "
        "cleanup=bounded evidence=host-unit");
    return 0;
}
