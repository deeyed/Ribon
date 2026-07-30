#include "ribos/vm/runtime.h"

#include <stdio.h>
#include <string.h>

static uint32_t
invoke_helper(void *context, RibosVmHelperCall *call)
{
    (void)context;
    (void)call;
    return RIBOS_VM_HELPER_CALLBACK_OK;
}

static uint32_t
monotonic_now_ns(void *context, uint64_t *now_ns)
{
    (void)context;
    if (now_ns == NULL) {
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    *now_ns = 1;
    return RIBOS_VM_HELPER_CALLBACK_OK;
}

static void
factory_recovery(
    void *context,
    const RibosVmFaultReceipt *receipt)
{
    (void)context;
    (void)receipt;
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

static RibosVmLimits
valid_limits(void)
{
    return (RibosVmLimits){
        .size = sizeof(RibosVmLimits),
        .runtime_abi_major = RIBOS_VM_RUNTIME_ABI_V1_MAJOR,
        .runtime_abi_minor = RIBOS_VM_RUNTIME_ABI_V1_MINOR,
        .maximum_instructions = 8192,
        .maximum_helper_calls = 64,
        .maximum_stack_bytes = 4096,
        .maximum_arena_bytes = 16384,
        .maximum_input_bytes = 1024,
        .maximum_output_bytes = 1024,
        .maximum_operations = 128,
        .maximum_polls = 128,
        .maximum_execution_duration_ns = 1000000,
        .maximum_helper_duration_ns = 100000,
        .maximum_call_depth = 8,
        .maximum_handles = 16,
    };
}

static RibosVmHelperExecutionDescriptor
valid_execution(void)
{
    return (RibosVmHelperExecutionDescriptor){
        .size = sizeof(RibosVmHelperExecutionDescriptor),
        .contract_major = RIBOS_VM_HELPER_EXECUTION_V1_MAJOR,
        .contract_minor = RIBOS_VM_HELPER_EXECUTION_V1_MINOR,
        .stable_id = 7,
        .required_capabilities = RIBOS_CAPABILITY_INSPECT,
        .effect = RIBOS_VM_HELPER_EFFECT_PURE,
        .execution_mode = RIBOS_VM_HELPER_EXECUTION_SYNCHRONOUS,
        .durability = RIBOS_VM_HELPER_DURABILITY_NONE,
        .handle_transition = RIBOS_VM_HANDLE_TRANSITION_NONE,
        .transition_parameter = RIBOS_VM_INVALID_ID,
        .allowed_mode_mask = UINT64_C(1),
        .allowed_phase_mask = UINT64_C(1),
        .maximum_input_bytes = 64,
        .maximum_output_bytes = 64,
        .maximum_operations = 1,
        .maximum_duration_ns = 1000,
    };
}

static int
test_versions_and_reserved_fields(void)
{
    RibosVmLimits limits = valid_limits();
    RibosVmHelperExecutionDescriptor execution = valid_execution();

    if (!expect_status(
            "valid limits",
            ribos_vm_limits_validate_v1(&limits),
            RIBOS_VM_STATUS_OK) ||
        !expect_status(
            "valid helper execution",
            ribos_vm_helper_execution_validate_v1(&execution),
            RIBOS_VM_STATUS_OK)) {
        return 0;
    }

    limits.runtime_abi_major = 2;
    if (!expect_status(
            "runtime major",
            ribos_vm_limits_validate_v1(&limits),
            RIBOS_VM_STATUS_UNSUPPORTED_RUNTIME_ABI)) {
        return 0;
    }
    limits = valid_limits();
    limits.runtime_abi_minor = 1;
    if (!expect_status(
            "runtime minor",
            ribos_vm_limits_validate_v1(&limits),
            RIBOS_VM_STATUS_UNSUPPORTED_RUNTIME_ABI)) {
        return 0;
    }
    limits = valid_limits();
    limits.size -= 1;
    if (!expect_status(
            "runtime size",
            ribos_vm_limits_validate_v1(&limits),
            RIBOS_VM_STATUS_INVALID_SIZE)) {
        return 0;
    }
    limits = valid_limits();
    limits.reserved[2] = 1;
    if (!expect_status(
            "runtime reserved",
            ribos_vm_limits_validate_v1(&limits),
            RIBOS_VM_STATUS_RESERVED_NONZERO)) {
        return 0;
    }

    execution.contract_minor = 1;
    if (!expect_status(
            "helper minor",
            ribos_vm_helper_execution_validate_v1(&execution),
            RIBOS_VM_STATUS_UNSUPPORTED_HELPER_ABI)) {
        return 0;
    }
    execution = valid_execution();
    execution.transition_parameter = 0;
    return expect_status(
        "helper transition",
        ribos_vm_helper_execution_validate_v1(&execution),
        RIBOS_VM_STATUS_INVALID_DESCRIPTOR);
}

static int
test_embedder_and_context(void)
{
    static const uint8_t context_bytes[] = {1, 2, 3, 4};
    RibosVmLimits limits = valid_limits();
    RibosVmHelperBinding binding = {
        .execution = valid_execution(),
        .invoke = invoke_helper,
    };
    RibosVmHelperContract contract = {
        .size = sizeof(RibosVmHelperContract),
        .contract_major = RIBOS_VM_HELPER_EXECUTION_V1_MAJOR,
        .contract_minor = RIBOS_VM_HELPER_EXECUTION_V1_MINOR,
        .binding_count = 1,
        .bindings = &binding,
        .digest = {1},
    };
    RibosVmEmbedder embedder = {
        .size = sizeof(RibosVmEmbedder),
        .runtime_abi_major = RIBOS_VM_RUNTIME_ABI_V1_MAJOR,
        .runtime_abi_minor = RIBOS_VM_RUNTIME_ABI_V1_MINOR,
        .selected_mode = 0,
        .selected_phase = 0,
        .granted_capabilities = RIBOS_CAPABILITY_INSPECT,
        .helper_contract = &contract,
        .monotonic_now_ns = monotonic_now_ns,
        .factory_recovery = factory_recovery,
    };
    RibosVmContext context = {
        .size = sizeof(RibosVmContext),
        .runtime_abi_major = RIBOS_VM_RUNTIME_ABI_V1_MAJOR,
        .runtime_abi_minor = RIBOS_VM_RUNTIME_ABI_V1_MINOR,
        .context_type_id = 1,
        .selected_mode = 0,
        .selected_phase = 0,
        .generation = 1,
        .bytes = context_bytes,
        .byte_size = sizeof(context_bytes),
        .digest = {1},
    };

    if (!expect_status(
            "valid helper contract",
            ribos_vm_helper_contract_validate_v1(&contract),
            RIBOS_VM_STATUS_OK) ||
        !expect_status(
            "valid embedder",
            ribos_vm_embedder_validate_v1(&embedder),
            RIBOS_VM_STATUS_OK) ||
        !expect_status(
            "valid context",
            ribos_vm_context_validate_v1(&context, &limits),
            RIBOS_VM_STATUS_OK)) {
        return 0;
    }

    embedder.reserved0 = 1;
    if (!expect_status(
            "embedder reserved",
            ribos_vm_embedder_validate_v1(&embedder),
            RIBOS_VM_STATUS_RESERVED_NONZERO)) {
        return 0;
    }
    context.byte_size = limits.maximum_input_bytes + 1;
    return expect_status(
        "context limit",
        ribos_vm_context_validate_v1(&context, &limits),
        RIBOS_VM_STATUS_INVALID_DESCRIPTOR);
}

static int
test_outcomes_and_names(void)
{
    RibosVmOutcome outcome = {
        .size = sizeof(RibosVmOutcome),
        .runtime_abi_major = RIBOS_VM_RUNTIME_ABI_V1_MAJOR,
        .runtime_abi_minor = RIBOS_VM_RUNTIME_ABI_V1_MINOR,
        .kind = RIBOS_VM_OUTCOME_BOOT_ACTION,
        .value.boot_action = {
            .terminal_helper_id = 21,
            .action_type_id = 2,
            .generation = 1,
            .receipt_digest = {1},
        },
    };

    if (!expect_status(
            "boot action",
            ribos_vm_outcome_validate_v1(&outcome),
            RIBOS_VM_STATUS_OK)) {
        return 0;
    }
    outcome.kind = RIBOS_VM_OUTCOME_POLICY_ERROR;
    outcome.value.policy_error = (RibosVmPolicyError){
        .stable_code = 4,
        .error_type_id = 3,
        .source_map_id = RIBOS_VM_INVALID_ID,
    };
    if (!expect_status(
            "policy error",
            ribos_vm_outcome_validate_v1(&outcome),
            RIBOS_VM_STATUS_OK)) {
        return 0;
    }
    outcome.kind = RIBOS_VM_OUTCOME_VM_FAULT;
    outcome.value.vm_fault = (RibosVmFaultReceipt){
        .fault_code = RIBOS_VM_FAULT_ARITHMETIC,
        .subject = RIBOS_VM_FAULT_SUBJECT_INSTRUCTION,
        .function_id = 0,
        .instruction_id = 3,
        .helper_id = RIBOS_VM_INVALID_ID,
        .artifact_hash = {1},
    };
    if (!expect_status(
            "vm fault",
            ribos_vm_outcome_validate_v1(&outcome),
            RIBOS_VM_STATUS_OK)) {
        return 0;
    }
    outcome.kind = 0;
    if (!expect_status(
            "unknown outcome",
            ribos_vm_outcome_validate_v1(&outcome),
            RIBOS_VM_STATUS_INVALID_STATE)) {
        return 0;
    }

    return strcmp(
               ribos_vm_status_name(
                   RIBOS_VM_STATUS_UNSUPPORTED_RUNTIME_ABI),
               "unsupported-runtime-abi") == 0 &&
           strcmp(
               ribos_vm_fault_code_name(RIBOS_VM_FAULT_DEADLINE),
               "deadline") == 0 &&
           strcmp(
               ribos_vm_outcome_kind_name(
                   RIBOS_VM_OUTCOME_BOOT_ACTION),
               "boot-action") == 0 &&
           strcmp(ribos_vm_outcome_kind_name(0), "unknown") == 0;
}

int
main(void)
{
    const RibosPreparedProgram *prepared_program = NULL;

    (void)prepared_program;
    if (!test_versions_and_reserved_fields() ||
        !test_embedder_and_context() ||
        !test_outcomes_and_names()) {
        return 1;
    }
    puts(
        "RIBOS-RUNTIME-CONTRACT-OK "
        "abi=1.0 outcomes=3 helper-execution=1.0");
    return 0;
}
