#include "ribos/base/checked.h"
#include "ribos/vm/storage.h"

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

static uint32_t
invoke_helper(void *context, RibosVmHelperCall *call)
{
    (void)context;
    (void)call;
    return RIBOS_VM_HELPER_CALLBACK_OK;
}

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
        .authority_generation = 21,
        .manifest_sequence = 34,
        .rollback_floor = 13,
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
find_entry_slot(
    const RibosPreparedProgram *prepared,
    uint16_t type_kind,
    uint16_t bit_width)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(prepared);
    const RibosArtifactSectionView *functions =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_FUNCTIONS);
    const RibosArtifactSectionView *slots =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_SLOTS);
    const RibosArtifactSectionView *types =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_TYPES);
    const uint8_t *function;
    uint32_t first_slot;
    uint32_t slot_count;
    uint32_t local;

    if (functions == NULL || slots == NULL || types == NULL ||
        view->entry_function >= functions->count) {
        return RIBOS_VM_INVALID_ID;
    }
    function = functions->bytes +
        (size_t)view->entry_function * functions->row_size;
    first_slot = read_u32(function + 24);
    slot_count = read_u32(function + 28);
    for (local = 0; local < slot_count; ++local) {
        uint32_t slot_id = first_slot + local;
        const uint8_t *slot = slots->bytes +
            (size_t)slot_id * slots->row_size;
        uint32_t type_id = read_u32(slot + 8);
        const uint8_t *type;

        if (type_id >= types->count) {
            return RIBOS_VM_INVALID_ID;
        }
        type = types->bytes + (size_t)type_id * types->row_size;
        if (read_u16(type + 4) == type_kind &&
            read_u16(type + 6) == bit_width) {
            return slot_id;
        }
    }
    return RIBOS_VM_INVALID_ID;
}

static int
test_checked_arithmetic(void)
{
    size_t size_result = 0;
    uint64_t u64_result = 0;

    return expect_true(
            "checked size add overflow",
            !ribos_checked_size_add(SIZE_MAX, 1, &size_result)) &&
        expect_true(
            "checked size multiply overflow",
            !ribos_checked_size_multiply(
                SIZE_MAX,
                2,
                &size_result)) &&
        expect_true(
            "checked size align overflow",
            !ribos_checked_size_align(
                SIZE_MAX,
                8,
                &size_result)) &&
        expect_true(
            "checked size range",
            ribos_checked_size_range(8, 4, 4) &&
                !ribos_checked_size_range(8, 5, 4)) &&
        expect_true(
            "checked u64 add overflow",
            !ribos_checked_u64_add(
                UINT64_MAX,
                1,
                &u64_result)) &&
        expect_true(
            "checked u64 multiply overflow",
            !ribos_checked_u64_multiply(
                UINT64_MAX,
                2,
                &u64_result)) &&
        expect_true(
            "checked u64 align overflow",
            !ribos_checked_u64_align(
                UINT64_MAX,
                8,
                &u64_result));
}

static int
test_product_arena_limit(
    TestPrepared *test,
    const RibosVmStoragePlan *plan)
{
    RibosVmLimits smaller = test->limits;
    RibosVerifierReport report;
    const RibosPreparedProgram *prepared = NULL;
    RibosVmStoragePlan rejected_plan;
    void *workspace;
    size_t workspace_size = 0;
    size_t required_size = 0;
    int passed;

    if (plan->required_bytes <= test->report.recomputed_stack_bytes) {
        return expect_true("runtime overhead exists", 0);
    }
    smaller.maximum_stack_bytes =
        test->report.recomputed_stack_bytes;
    smaller.maximum_call_depth =
        test->report.recomputed_call_depth;
    smaller.maximum_arena_bytes = plan->required_bytes - 1;
    if (ribos_prepared_program_workspace_size_v1(
            test->authorized,
            &test->contract,
            &workspace_size) != RIBOS_VM_STATUS_OK) {
        return 0;
    }
    workspace = malloc(workspace_size);
    if (workspace == NULL) {
        return 0;
    }
    passed = expect_status(
        "prepare smaller product cap",
        ribos_prepare_program_v1(
            test->authorized,
            ribos_schema_reference_v1(),
            &test->contract,
            &smaller,
            workspace,
            workspace_size,
            &report,
            &prepared),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "runtime product cap override",
        ribos_vm_runtime_size_v1(
            prepared,
            &rejected_plan,
            &required_size),
        RIBOS_VM_STATUS_LIMIT_EXCEEDED);
    free(workspace);
    return passed;
}

static int
test_runtime_storage(TestPrepared *test)
{
    RibosVmStoragePlan plan;
    RibosVmStoragePlan repeated;
    RibosVmStoragePlan modified;
    RibosVmStorage *storage = NULL;
    size_t required_size = 0;
    size_t repeated_size = 0;
    uint8_t *raw_arena;
    uint8_t *arena;
    uint32_t u64_slot;
    uint32_t u8_slot;
    uint32_t i16_slot;
    uint32_t bool_slot;
    uint32_t state = 0;
    uint32_t bool_value = 0;
    uint64_t unsigned_value = 0;
    int64_t signed_value = 0;
    uint8_t scalar_bytes[8] = {0};
    uint32_t index;
    int passed;

    passed = expect_status(
        "runtime size",
        ribos_vm_runtime_size_v1(
            test->prepared,
            &plan,
            &required_size),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "runtime size repeated",
        ribos_vm_runtime_size_v1(
            test->prepared,
            &repeated,
            &repeated_size),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "deterministic required bytes",
        required_size == repeated_size &&
            plan.required_bytes == required_size &&
            plan.maximum_value_bytes ==
                repeated.maximum_value_bytes &&
            plan.entry_function == repeated.entry_function &&
            plan.slot_count == repeated.slot_count &&
            plan.loop_count == repeated.loop_count &&
            plan.helper_count == repeated.helper_count &&
            plan.effective_arena_limit ==
                test->limits.maximum_arena_bytes &&
            plan.effective_arena_limit <
                RIBOS_VM_RUNTIME_MAX_ARENA_BYTES_V1);
    for (index = 0;
         passed && index < RIBOS_VM_STORAGE_REGION_COUNT_V1;
         ++index) {
        const RibosVmStorageRegion *region =
            &plan.regions[index];

        passed = expect_true(
            "region alignment and bounds",
            (region->offset &
                (RIBOS_VM_STORAGE_ALIGNMENT_V1 - 1u)) == 0 &&
                region->offset ==
                    repeated.regions[index].offset &&
                region->byte_size ==
                    repeated.regions[index].byte_size &&
                region->count ==
                    repeated.regions[index].count &&
                region->stride ==
                    repeated.regions[index].stride &&
                region->offset <= plan.required_bytes &&
                region->byte_size <=
                    plan.required_bytes - region->offset &&
                (index == 0 ||
                 region->offset >=
                    plan.regions[index - 1].offset +
                    plan.regions[index - 1].byte_size));
    }
    raw_arena = passed ?
        malloc(required_size + RIBOS_VM_STORAGE_ALIGNMENT_V1) :
        NULL;
    arena = raw_arena != NULL ?
        (uint8_t *)(((uintptr_t)raw_arena +
            RIBOS_VM_STORAGE_ALIGNMENT_V1 - 1u) &
            ~(uintptr_t)(RIBOS_VM_STORAGE_ALIGNMENT_V1 - 1u)) :
        NULL;
    passed = passed && expect_true(
        "aligned arena allocation",
        arena != NULL);
    if (!passed) {
        free(raw_arena);
        return 0;
    }
    passed = expect_status(
        "one-byte-small arena",
        ribos_vm_storage_initialize_v1(
            test->prepared,
            &plan,
            arena,
            required_size - 1,
            0,
            &storage),
        RIBOS_VM_STATUS_ARENA_TOO_SMALL);
    passed = passed && expect_status(
        "misaligned arena",
        ribos_vm_storage_initialize_v1(
            test->prepared,
            &plan,
            arena + 1,
            required_size,
            0,
            &storage),
        RIBOS_VM_STATUS_INVALID_ARGUMENT);
    modified = plan;
    modified.regions[RIBOS_VM_STORAGE_REGION_FRAMES].offset += 8;
    passed = passed && expect_status(
        "modified plan rejected",
        ribos_vm_storage_initialize_v1(
            test->prepared,
            &modified,
            arena,
            required_size,
            0,
            &storage),
        RIBOS_VM_STATUS_INVALID_DESCRIPTOR);
    passed = passed && expect_status(
        "exact arena initialization",
        ribos_vm_storage_initialize_v1(
            test->prepared,
            &plan,
            arena,
            required_size,
            RIBOS_VM_STORAGE_INITIALIZE_DIAGNOSTIC_POISON,
            &storage),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "storage validate",
        ribos_vm_storage_validate_v1(
            test->prepared,
            storage,
            required_size),
        RIBOS_VM_STATUS_OK);
    if (passed) {
        arena[0] ^= 0xff;
        passed = expect_status(
            "storage header mutation",
            ribos_vm_storage_validate_v1(
                test->prepared,
                storage,
                required_size),
            RIBOS_VM_STATUS_DIGEST_MISMATCH);
        arena[0] ^= 0xff;
    }
    passed = passed && expect_status(
        "entry frame reset",
        ribos_vm_storage_reset_frame_v1(
            test->prepared,
            storage,
            required_size,
            plan.entry_function,
            0),
        RIBOS_VM_STATUS_OK);

    u64_slot = find_entry_slot(
        test->prepared,
        RIBOS_BC_TYPE_UNSIGNED,
        64);
    u8_slot = find_entry_slot(
        test->prepared,
        RIBOS_BC_TYPE_UNSIGNED,
        8);
    i16_slot = find_entry_slot(
        test->prepared,
        RIBOS_BC_TYPE_SIGNED,
        16);
    bool_slot = find_entry_slot(
        test->prepared,
        RIBOS_BC_TYPE_BOOL,
        1);
    passed = passed && expect_true(
        "typed scalar slots",
        u64_slot != RIBOS_VM_INVALID_ID &&
            u8_slot != RIBOS_VM_INVALID_ID &&
            i16_slot != RIBOS_VM_INVALID_ID &&
            bool_slot != RIBOS_VM_INVALID_ID);
    passed = passed && expect_status(
        "slot starts uninitialized",
        ribos_vm_storage_slot_state_v1(
            test->prepared,
            storage,
            required_size,
            u64_slot,
            &state),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "uninitialized state value",
        state == RIBOS_VM_SLOT_STORAGE_UNINITIALIZED);
    passed = passed && expect_status(
        "uninitialized load rejected",
        ribos_vm_storage_slot_load_unsigned_v1(
            test->prepared,
            storage,
            required_size,
            plan.entry_function,
            0,
            u64_slot,
            64,
            &unsigned_value),
        RIBOS_VM_STATUS_INVALID_STATE);
    passed = passed && expect_status(
        "store u64",
        ribos_vm_storage_slot_store_unsigned_v1(
            test->prepared,
            storage,
            required_size,
            plan.entry_function,
            0,
            u64_slot,
            64,
            UINT64_C(0x0102030405060708)),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "load u64",
        ribos_vm_storage_slot_load_unsigned_v1(
            test->prepared,
            storage,
            required_size,
            plan.entry_function,
            0,
            u64_slot,
            64,
            &unsigned_value),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "u64 round trip",
        unsigned_value == UINT64_C(0x0102030405060708));
    passed = passed && expect_status(
        "read canonical scalar bytes",
        ribos_vm_storage_slot_read_v1(
            test->prepared,
            storage,
            required_size,
            plan.entry_function,
            0,
            u64_slot,
            scalar_bytes,
            sizeof(scalar_bytes)),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "little-endian scalar bytes",
        scalar_bytes[0] == 0x08 &&
            scalar_bytes[1] == 0x07 &&
            scalar_bytes[2] == 0x06 &&
            scalar_bytes[3] == 0x05 &&
            scalar_bytes[4] == 0x04 &&
            scalar_bytes[5] == 0x03 &&
            scalar_bytes[6] == 0x02 &&
            scalar_bytes[7] == 0x01);
    passed = passed && expect_status(
        "u8 overflow rejected",
        ribos_vm_storage_slot_store_unsigned_v1(
            test->prepared,
            storage,
            required_size,
            plan.entry_function,
            0,
            u8_slot,
            8,
            256),
        RIBOS_VM_STATUS_LIMIT_EXCEEDED);
    passed = passed && expect_status(
        "store signed",
        ribos_vm_storage_slot_store_signed_v1(
            test->prepared,
            storage,
            required_size,
            plan.entry_function,
            0,
            i16_slot,
            16,
            -1234),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "load signed",
        ribos_vm_storage_slot_load_signed_v1(
            test->prepared,
            storage,
            required_size,
            plan.entry_function,
            0,
            i16_slot,
            16,
            &signed_value),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "signed round trip",
        signed_value == -1234);
    passed = passed && expect_status(
        "store bool",
        ribos_vm_storage_slot_store_bool_v1(
            test->prepared,
            storage,
            required_size,
            plan.entry_function,
            0,
            bool_slot,
            1),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "load bool",
        ribos_vm_storage_slot_load_bool_v1(
            test->prepared,
            storage,
            required_size,
            plan.entry_function,
            0,
            bool_slot,
            &bool_value),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "bool round trip",
        bool_value == 1);
    passed = passed && expect_status(
        "scalar type mismatch",
        ribos_vm_storage_slot_store_unsigned_v1(
            test->prepared,
            storage,
            required_size,
            plan.entry_function,
            0,
            bool_slot,
            8,
            1),
        RIBOS_VM_STATUS_INVALID_DESCRIPTOR);
    passed = passed && expect_status(
        "mark moved",
        ribos_vm_storage_slot_mark_moved_v1(
            test->prepared,
            storage,
            required_size,
            plan.entry_function,
            0,
            u64_slot),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_status(
        "moved load rejected",
        ribos_vm_storage_slot_load_unsigned_v1(
            test->prepared,
            storage,
            required_size,
            plan.entry_function,
            0,
            u64_slot,
            64,
            &unsigned_value),
        RIBOS_VM_STATUS_INVALID_STATE);
    passed = passed && expect_status(
        "moved state",
        ribos_vm_storage_slot_state_v1(
            test->prepared,
            storage,
            required_size,
            u64_slot,
            &state),
        RIBOS_VM_STATUS_OK);
    passed = passed && expect_true(
        "moved state value",
        state == RIBOS_VM_SLOT_STORAGE_MOVED);
    passed = passed && test_product_arena_limit(test, &plan);
    free(raw_arena);
    return passed;
}

int
main(int argc, char **argv)
{
    TestPrepared test;
    int passed;

    if (argc != 2) {
        fprintf(stderr, "usage: %s POLICY.rba\n", argv[0]);
        return 2;
    }
    if (!prepare_test_program(argv[1], &test)) {
        fprintf(stderr, "cannot prepare runtime storage fixture\n");
        release_test_program(&test);
        return 2;
    }
    passed = test_checked_arithmetic() &&
        test_runtime_storage(&test);
    release_test_program(&test);
    if (!passed) {
        fprintf(stderr, "RIBOS-RUNTIME-STORAGE-FAIL\n");
        return 1;
    }
    puts(
        "RIBOS-RUNTIME-STORAGE-OK "
        "arena=caller-owned layout=exact values=little-endian "
        "product-cap=intersected evidence=host-unit");
    return 0;
}
