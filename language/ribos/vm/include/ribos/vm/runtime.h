#ifndef RIBOS_VM_RUNTIME_H
#define RIBOS_VM_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "ribos/schema/schema.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file runtime.h
 * @brief Architecture-neutral Ribos VM runtime와 embedder 계약.
 *
 * 이 header의 structure는 process-local C ABI다. Wire format이나 packed memory
 * image가 아니며 pointer는 명시된 호출 수명 안에서만 유효하다.
 */

#define RIBOS_VM_RUNTIME_ABI_V1_MAJOR 1u
#define RIBOS_VM_RUNTIME_ABI_V1_MINOR 0u
#define RIBOS_VM_HELPER_EXECUTION_V1_MAJOR 1u
#define RIBOS_VM_HELPER_EXECUTION_V1_MINOR 0u
#define RIBOS_VM_MAX_HELPER_BINDINGS 256u
#define RIBOS_VM_INVALID_ID UINT32_MAX
#define RIBOS_VM_DIGEST_BYTES RIBOS_SCHEMA_DIGEST_BYTES

/** Runtime API의 stable fail-closed 상태다. */
typedef enum RibosVmStatus {
    RIBOS_VM_STATUS_OK = 0,
    RIBOS_VM_STATUS_INVALID_ARGUMENT = 1,
    RIBOS_VM_STATUS_UNSUPPORTED_RUNTIME_ABI = 2,
    RIBOS_VM_STATUS_UNSUPPORTED_HELPER_ABI = 3,
    RIBOS_VM_STATUS_INVALID_SIZE = 4,
    RIBOS_VM_STATUS_RESERVED_NONZERO = 5,
    RIBOS_VM_STATUS_INVALID_DESCRIPTOR = 6,
    RIBOS_VM_STATUS_INVALID_STATE = 7,
    RIBOS_VM_STATUS_DIGEST_MISMATCH = 8,
    RIBOS_VM_STATUS_LIMIT_EXCEEDED = 9,
    RIBOS_VM_STATUS_NOT_AUTHORIZED = 10,
    RIBOS_VM_STATUS_NOT_PREPARED = 11,
    RIBOS_VM_STATUS_ARENA_TOO_SMALL = 12,
    RIBOS_VM_STATUS_EMBEDDER_REJECTED = 13,
    RIBOS_VM_STATUS_ALREADY_CONSUMED = 14,
    RIBOS_VM_STATUS_INTERNAL_ERROR = 15
} RibosVmStatus;

/** VM이 정상 return 대신 봉인하는 stable fault code다. */
typedef enum RibosVmFaultCode {
    RIBOS_VM_FAULT_NONE = 0,
    RIBOS_VM_FAULT_INTERNAL = 1,
    RIBOS_VM_FAULT_INVALID_STATE = 2,
    RIBOS_VM_FAULT_INSTRUCTION_BUDGET = 3,
    RIBOS_VM_FAULT_HELPER_BUDGET = 4,
    RIBOS_VM_FAULT_OPERATION_BUDGET = 5,
    RIBOS_VM_FAULT_POLL_BUDGET = 6,
    RIBOS_VM_FAULT_DEADLINE = 7,
    RIBOS_VM_FAULT_STACK_BOUNDS = 8,
    RIBOS_VM_FAULT_CALL_DEPTH = 9,
    RIBOS_VM_FAULT_LOOP_BOUND = 10,
    RIBOS_VM_FAULT_ARITHMETIC = 11,
    RIBOS_VM_FAULT_INVALID_VALUE = 12,
    RIBOS_VM_FAULT_HANDLE_VIOLATION = 13,
    RIBOS_VM_FAULT_CAPABILITY = 14,
    RIBOS_VM_FAULT_MODE_PHASE = 15,
    RIBOS_VM_FAULT_HELPER_CONTRACT = 16,
    RIBOS_VM_FAULT_EMBEDDER = 17,
    RIBOS_VM_FAULT_TERMINAL_ACTION = 18,
    RIBOS_VM_FAULT_RECOVERY = 19
} RibosVmFaultCode;

/** Fault receipt가 가리키는 stable runtime subject다. */
typedef enum RibosVmFaultSubject {
    RIBOS_VM_FAULT_SUBJECT_RUNTIME = 1,
    RIBOS_VM_FAULT_SUBJECT_PROGRAM = 2,
    RIBOS_VM_FAULT_SUBJECT_FUNCTION = 3,
    RIBOS_VM_FAULT_SUBJECT_INSTRUCTION = 4,
    RIBOS_VM_FAULT_SUBJECT_HELPER = 5,
    RIBOS_VM_FAULT_SUBJECT_VALUE = 6,
    RIBOS_VM_FAULT_SUBJECT_TERMINAL = 7,
    RIBOS_VM_FAULT_SUBJECT_RECOVERY = 8
} RibosVmFaultSubject;

/** Successful execute가 만드는 세 가지 terminal outcome class다. */
typedef enum RibosVmOutcomeKind {
    RIBOS_VM_OUTCOME_BOOT_ACTION = 1,
    RIBOS_VM_OUTCOME_POLICY_ERROR = 2,
    RIBOS_VM_OUTCOME_VM_FAULT = 3
} RibosVmOutcomeKind;

/** Helper가 외부 상태에 미치는 effect class다. */
typedef enum RibosVmHelperEffect {
    RIBOS_VM_HELPER_EFFECT_NONE = 0,
    RIBOS_VM_HELPER_EFFECT_PURE = 1,
    RIBOS_VM_HELPER_EFFECT_EPHEMERAL = 2,
    RIBOS_VM_HELPER_EFFECT_JOURNALED = 3,
    RIBOS_VM_HELPER_EFFECT_TERMINAL = 4
} RibosVmHelperEffect;

/** Runtime v1이 허용하는 helper 호출 모델이다. */
typedef enum RibosVmHelperExecutionMode {
    RIBOS_VM_HELPER_EXECUTION_SYNCHRONOUS = 1
} RibosVmHelperExecutionMode;

/** Helper effect가 남기는 durability evidence class다. */
typedef enum RibosVmHelperDurability {
    RIBOS_VM_HELPER_DURABILITY_NONE = 0,
    RIBOS_VM_HELPER_DURABILITY_VOLATILE = 1,
    RIBOS_VM_HELPER_DURABILITY_JOURNAL_RECEIPT = 2,
    RIBOS_VM_HELPER_DURABILITY_SEALED_INTENT = 3
} RibosVmHelperDurability;

/** Helper callback 전후의 opaque handle table transition이다. */
typedef enum RibosVmHandleTransition {
    RIBOS_VM_HANDLE_TRANSITION_NONE = 0,
    RIBOS_VM_HANDLE_TRANSITION_CREATE = 1,
    RIBOS_VM_HANDLE_TRANSITION_CONSUME = 2,
    RIBOS_VM_HANDLE_TRANSITION_REPLACE = 3,
    RIBOS_VM_HANDLE_TRANSITION_TERMINAL_CONSUME = 4
} RibosVmHandleTransition;

/** Embedder helper callback이 반환하는 세 가지 결과 class다. */
typedef enum RibosVmHelperCallbackStatus {
    RIBOS_VM_HELPER_CALLBACK_OK = 0,
    RIBOS_VM_HELPER_CALLBACK_POLICY_ERROR = 1,
    RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT = 2
} RibosVmHelperCallbackStatus;

/**
 * Runtime이 봉인하고 실행 중 집행하는 effective limit다.
 *
 * 모든 byte와 count는 VM 전체 실행 상한이다. `maximum_helper_duration_ns`만 한 helper
 * callback의 elapsed-time 상한이다.
 */
typedef struct RibosVmLimits {
    uint32_t size;
    uint16_t runtime_abi_major;
    uint16_t runtime_abi_minor;
    uint32_t flags;
    uint32_t reserved0;
    uint64_t maximum_instructions;
    uint64_t maximum_helper_calls;
    uint64_t maximum_stack_bytes;
    uint64_t maximum_arena_bytes;
    uint64_t maximum_input_bytes;
    uint64_t maximum_output_bytes;
    uint64_t maximum_operations;
    uint64_t maximum_polls;
    uint64_t maximum_execution_duration_ns;
    uint64_t maximum_helper_duration_ns;
    uint32_t maximum_call_depth;
    uint32_t maximum_handles;
    uint32_t maximum_trace_records;
    uint32_t reserved1;
    uint64_t reserved[4];
} RibosVmLimits;

/**
 * Product가 제공하는 immutable policy-context value view다.
 *
 * `bytes`는 C structure가 아니라 verified artifact type table의 VM value encoding이다.
 * VM은 execute가 반환할 때까지만 이 pointer를 borrow한다.
 */
typedef struct RibosVmContext {
    uint32_t size;
    uint16_t runtime_abi_major;
    uint16_t runtime_abi_minor;
    uint32_t flags;
    uint32_t context_type_id;
    uint32_t selected_mode;
    uint32_t selected_phase;
    uint64_t generation;
    const uint8_t *bytes;
    uint64_t byte_size;
    uint8_t digest[RIBOS_VM_DIGEST_BYTES];
    uint64_t reserved[4];
} RibosVmContext;

/**
 * Semantic schema와 분리된 helper의 target execution 계약이다.
 *
 * Stable ID, capability와 handle transition은 selected product schema와 다시
 * 일치해야 한다. Callback pointer와 context는 canonical execution digest에 포함되지
 * 않는다.
 */
typedef struct RibosVmHelperExecutionDescriptor {
    uint32_t size;
    uint16_t contract_major;
    uint16_t contract_minor;
    uint32_t stable_id;
    uint32_t flags;
    uint32_t required_capabilities;
    uint32_t effect;
    uint32_t execution_mode;
    uint32_t durability;
    uint32_t handle_transition;
    uint32_t transition_parameter;
    uint32_t reserved0;
    uint64_t allowed_mode_mask;
    uint64_t allowed_phase_mask;
    uint64_t maximum_input_bytes;
    uint64_t maximum_output_bytes;
    uint64_t maximum_operations;
    uint64_t maximum_polls;
    uint64_t maximum_duration_ns;
    uint64_t reserved[4];
} RibosVmHelperExecutionDescriptor;

typedef struct RibosVmHelperCall RibosVmHelperCall;
typedef struct RibosPreparedProgram RibosPreparedProgram;
typedef struct RibosVmFaultReceipt RibosVmFaultReceipt;

/**
 * 한 helper를 실행하는 synchronous embedder callback이다.
 *
 * `call`은 callback 동안만 유효하고 보존하거나 VM에 재진입할 수 없다. 반환값은
 * `RibosVmHelperCallbackStatus`의 fixed registry 값이어야 한다.
 */
typedef uint32_t (*RibosVmHelperInvokeFn)(
    void *embedder_context,
    RibosVmHelperCall *call);

/** Monotonic nanosecond snapshot을 제공하는 allocation-free callback이다. */
typedef uint32_t (*RibosVmMonotonicNowNsFn)(
    void *embedder_context,
    uint64_t *now_ns);

/**
 * VM fault를 product-owned factory recovery authority에 알리는 callback이다.
 *
 * Callback은 fault outcome을 바꾸거나 VM에 재진입할 수 없고 receipt pointer를 보존할
 * 수 없다. Runtime은 sealed fault마다 최대 한 번 호출한다.
 */
typedef void (*RibosVmFactoryRecoveryFn)(
    void *embedder_context,
    const RibosVmFaultReceipt *receipt);

/** Canonical descriptor와 process-local callback을 결합한 한 helper binding이다. */
typedef struct RibosVmHelperBinding {
    RibosVmHelperExecutionDescriptor execution;
    RibosVmHelperInvokeFn invoke;
} RibosVmHelperBinding;

/**
 * Stable-ID 순서로 정렬된 product-generated helper execution table이다.
 *
 * `digest`는 callback address를 제외한 execution descriptor의 canonical identity다.
 */
typedef struct RibosVmHelperContract {
    uint32_t size;
    uint16_t contract_major;
    uint16_t contract_minor;
    uint32_t flags;
    uint32_t binding_count;
    const RibosVmHelperBinding *bindings;
    uint8_t digest[RIBOS_VM_DIGEST_BYTES];
    uint64_t reserved[4];
} RibosVmHelperContract;

/**
 * Target에서 helper, clock와 recovery authority를 제공하는 embedder descriptor다.
 *
 * `embedder_context`는 VM이 역참조하지 않는 process-local pointer다. Ribon service
 * descriptor나 native service handle을 이 ABI에 직접 넣어서는 안 된다.
 */
typedef struct RibosVmEmbedder {
    uint32_t size;
    uint16_t runtime_abi_major;
    uint16_t runtime_abi_minor;
    uint32_t flags;
    uint32_t selected_mode;
    uint32_t selected_phase;
    uint32_t granted_capabilities;
    uint32_t reserved0;
    const RibosVmHelperContract *helper_contract;
    void *embedder_context;
    RibosVmMonotonicNowNsFn monotonic_now_ns;
    RibosVmFactoryRecoveryFn factory_recovery;
    uint64_t reserved[4];
} RibosVmEmbedder;

/**
 * VM이 생성한 single-consume boot intent다.
 *
 * Payload는 VM arena에 속하며 outcome이 consume되거나 runtime이 reset될 때까지만
 * borrow할 수 있다. 이 값은 OS entry jump나 durable commit 자체가 아니다.
 */
typedef struct RibosVmBootAction {
    uint32_t terminal_helper_id;
    uint32_t action_type_id;
    uint64_t generation;
    const uint8_t *payload;
    uint64_t payload_size;
    uint8_t receipt_digest[RIBOS_VM_DIGEST_BYTES];
    uint64_t reserved[2];
} RibosVmBootAction;

/** Policy가 명시적으로 반환한 typed error의 borrowed payload다. */
typedef struct RibosVmPolicyError {
    uint32_t stable_code;
    uint32_t error_type_id;
    uint32_t source_map_id;
    uint32_t reserved0;
    const uint8_t *payload;
    uint64_t payload_size;
    uint64_t reserved[2];
} RibosVmPolicyError;

/**
 * Pointer와 secret을 포함하지 않는 fixed-size VM fault receipt다.
 *
 * Optional trace가 없으면 `trace_digest`는 zero다. Artifact hash는 실행 provenance를
 * 식별하며 factory recovery callback과 최종 outcome이 같은 값의 view를 공유한다.
 */
struct RibosVmFaultReceipt {
    uint32_t fault_code;
    uint32_t subject;
    uint32_t function_id;
    uint32_t instruction_id;
    uint32_t helper_id;
    uint32_t detail;
    uint32_t last_effect;
    uint32_t last_durability;
    uint64_t consumed_instructions;
    uint64_t consumed_helper_calls;
    uint64_t consumed_input_bytes;
    uint64_t consumed_output_bytes;
    uint64_t consumed_operations;
    uint64_t consumed_polls;
    uint64_t elapsed_ns;
    uint8_t artifact_hash[RIBOS_VM_DIGEST_BYTES];
    uint8_t trace_digest[RIBOS_VM_DIGEST_BYTES];
    uint64_t reserved[2];
};

/** Execute가 exactly one terminal class로 채우는 process-local tagged outcome다. */
typedef struct RibosVmOutcome {
    uint32_t size;
    uint16_t runtime_abi_major;
    uint16_t runtime_abi_minor;
    uint32_t kind;
    uint32_t flags;
    union {
        RibosVmBootAction boot_action;
        RibosVmPolicyError policy_error;
        RibosVmFaultReceipt vm_fault;
    } value;
    uint64_t reserved[4];
} RibosVmOutcome;

/** Runtime ABI version이 이 header의 exact v1 contract인지 반환한다. */
static inline int
ribos_vm_runtime_version_is_supported(uint16_t major, uint16_t minor)
{
    return major == RIBOS_VM_RUNTIME_ABI_V1_MAJOR &&
           minor == RIBOS_VM_RUNTIME_ABI_V1_MINOR;
}

/** Fixed-size reserved word가 모두 zero인지 검사한다. */
static inline int
ribos_vm_reserved_words_are_zero(
    const uint64_t *words,
    uint32_t word_count)
{
    uint32_t index;

    if (words == NULL) {
        return 0;
    }
    for (index = 0; index < word_count; ++index) {
        if (words[index] != 0) {
            return 0;
        }
    }
    return 1;
}

/** Digest가 zero sentinel이 아닌지 검사한다. */
static inline int
ribos_vm_digest_is_nonzero(const uint8_t digest[RIBOS_VM_DIGEST_BYTES])
{
    uint32_t index;
    uint8_t value = 0;

    if (digest == NULL) {
        return 0;
    }
    for (index = 0; index < RIBOS_VM_DIGEST_BYTES; ++index) {
        value |= digest[index];
    }
    return value != 0;
}

/** Effective runtime limit의 ABI, reserved field와 내부 상한을 검사한다. */
static inline RibosVmStatus
ribos_vm_limits_validate_v1(const RibosVmLimits *limits)
{
    if (limits == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    if (!ribos_vm_runtime_version_is_supported(
            limits->runtime_abi_major,
            limits->runtime_abi_minor)) {
        return RIBOS_VM_STATUS_UNSUPPORTED_RUNTIME_ABI;
    }
    if (limits->size != (uint32_t)sizeof(*limits)) {
        return RIBOS_VM_STATUS_INVALID_SIZE;
    }
    if (limits->flags != 0 || limits->reserved0 != 0 ||
        limits->reserved1 != 0 ||
        !ribos_vm_reserved_words_are_zero(limits->reserved, 4)) {
        return RIBOS_VM_STATUS_RESERVED_NONZERO;
    }
    if (limits->maximum_instructions == 0 ||
        limits->maximum_helper_calls == 0 ||
        limits->maximum_stack_bytes == 0 ||
        limits->maximum_arena_bytes < limits->maximum_stack_bytes ||
        limits->maximum_operations < limits->maximum_helper_calls ||
        limits->maximum_execution_duration_ns == 0 ||
        limits->maximum_helper_duration_ns == 0 ||
        limits->maximum_helper_duration_ns >
            limits->maximum_execution_duration_ns ||
        limits->maximum_call_depth == 0) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    return RIBOS_VM_STATUS_OK;
}

/** Immutable policy context의 ABI, byte range와 digest를 검사한다. */
static inline RibosVmStatus
ribos_vm_context_validate_v1(
    const RibosVmContext *context,
    const RibosVmLimits *limits)
{
    RibosVmStatus status = ribos_vm_limits_validate_v1(limits);

    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (context == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    if (!ribos_vm_runtime_version_is_supported(
            context->runtime_abi_major,
            context->runtime_abi_minor)) {
        return RIBOS_VM_STATUS_UNSUPPORTED_RUNTIME_ABI;
    }
    if (context->size != (uint32_t)sizeof(*context)) {
        return RIBOS_VM_STATUS_INVALID_SIZE;
    }
    if (context->flags != 0 ||
        !ribos_vm_reserved_words_are_zero(context->reserved, 4)) {
        return RIBOS_VM_STATUS_RESERVED_NONZERO;
    }
    if (context->context_type_id == RIBOS_VM_INVALID_ID ||
        context->selected_mode >= 64 ||
        context->selected_phase >= 64 ||
        context->generation == 0 ||
        context->byte_size > limits->maximum_input_bytes ||
        (context->byte_size != 0 && context->bytes == NULL) ||
        !ribos_vm_digest_is_nonzero(context->digest)) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    return RIBOS_VM_STATUS_OK;
}

/** Helper execution descriptor의 version, bound와 transition을 검사한다. */
static inline RibosVmStatus
ribos_vm_helper_execution_validate_v1(
    const RibosVmHelperExecutionDescriptor *execution)
{
    uint32_t expected_durability;

    if (execution == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    if (execution->contract_major != RIBOS_VM_HELPER_EXECUTION_V1_MAJOR ||
        execution->contract_minor != RIBOS_VM_HELPER_EXECUTION_V1_MINOR) {
        return RIBOS_VM_STATUS_UNSUPPORTED_HELPER_ABI;
    }
    if (execution->size != (uint32_t)sizeof(*execution)) {
        return RIBOS_VM_STATUS_INVALID_SIZE;
    }
    if (execution->flags != 0 || execution->reserved0 != 0 ||
        !ribos_vm_reserved_words_are_zero(execution->reserved, 4)) {
        return RIBOS_VM_STATUS_RESERVED_NONZERO;
    }
    if (execution->stable_id == RIBOS_VM_INVALID_ID ||
        execution->required_capabilities == 0 ||
        execution->effect < RIBOS_VM_HELPER_EFFECT_PURE ||
        execution->effect > RIBOS_VM_HELPER_EFFECT_TERMINAL ||
        execution->execution_mode !=
            RIBOS_VM_HELPER_EXECUTION_SYNCHRONOUS ||
        execution->allowed_mode_mask == 0 ||
        execution->allowed_phase_mask == 0 ||
        execution->maximum_operations == 0 ||
        execution->maximum_duration_ns == 0 ||
        execution->handle_transition >
            RIBOS_VM_HANDLE_TRANSITION_TERMINAL_CONSUME) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }

    expected_durability = RIBOS_VM_HELPER_DURABILITY_NONE;
    if (execution->effect == RIBOS_VM_HELPER_EFFECT_EPHEMERAL) {
        expected_durability = RIBOS_VM_HELPER_DURABILITY_VOLATILE;
    } else if (execution->effect == RIBOS_VM_HELPER_EFFECT_JOURNALED) {
        expected_durability =
            RIBOS_VM_HELPER_DURABILITY_JOURNAL_RECEIPT;
    } else if (execution->effect == RIBOS_VM_HELPER_EFFECT_TERMINAL) {
        expected_durability =
            RIBOS_VM_HELPER_DURABILITY_SEALED_INTENT;
    }
    if (execution->durability != expected_durability) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }

    if (execution->handle_transition ==
            RIBOS_VM_HANDLE_TRANSITION_NONE ||
        execution->handle_transition ==
            RIBOS_VM_HANDLE_TRANSITION_CREATE) {
        if (execution->transition_parameter != RIBOS_VM_INVALID_ID) {
            return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
        }
    } else if (execution->transition_parameter >=
               RIBOS_SCHEMA_MAX_PARAMETERS) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    return RIBOS_VM_STATUS_OK;
}

/** Product-generated helper table의 ordering, callback과 digest를 검사한다. */
static inline RibosVmStatus
ribos_vm_helper_contract_validate_v1(
    const RibosVmHelperContract *contract)
{
    uint32_t index;

    if (contract == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    if (contract->contract_major != RIBOS_VM_HELPER_EXECUTION_V1_MAJOR ||
        contract->contract_minor != RIBOS_VM_HELPER_EXECUTION_V1_MINOR) {
        return RIBOS_VM_STATUS_UNSUPPORTED_HELPER_ABI;
    }
    if (contract->size != (uint32_t)sizeof(*contract)) {
        return RIBOS_VM_STATUS_INVALID_SIZE;
    }
    if (contract->flags != 0 ||
        !ribos_vm_reserved_words_are_zero(contract->reserved, 4)) {
        return RIBOS_VM_STATUS_RESERVED_NONZERO;
    }
    if (contract->binding_count == 0 ||
        contract->binding_count > RIBOS_VM_MAX_HELPER_BINDINGS ||
        contract->bindings == NULL ||
        !ribos_vm_digest_is_nonzero(contract->digest)) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    for (index = 0; index < contract->binding_count; ++index) {
        RibosVmStatus status =
            ribos_vm_helper_execution_validate_v1(
                &contract->bindings[index].execution);

        if (status != RIBOS_VM_STATUS_OK) {
            return status;
        }
        if (contract->bindings[index].invoke == NULL ||
            (index != 0 &&
             contract->bindings[index - 1].execution.stable_id >=
                 contract->bindings[index].execution.stable_id)) {
            return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
        }
    }
    return RIBOS_VM_STATUS_OK;
}

/** Embedder의 runtime ABI와 callback authority를 검사한다. */
static inline RibosVmStatus
ribos_vm_embedder_validate_v1(const RibosVmEmbedder *embedder)
{
    RibosVmStatus status;

    if (embedder == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    if (!ribos_vm_runtime_version_is_supported(
            embedder->runtime_abi_major,
            embedder->runtime_abi_minor)) {
        return RIBOS_VM_STATUS_UNSUPPORTED_RUNTIME_ABI;
    }
    if (embedder->size != (uint32_t)sizeof(*embedder)) {
        return RIBOS_VM_STATUS_INVALID_SIZE;
    }
    if (embedder->flags != 0 || embedder->reserved0 != 0 ||
        !ribos_vm_reserved_words_are_zero(embedder->reserved, 4)) {
        return RIBOS_VM_STATUS_RESERVED_NONZERO;
    }
    if (embedder->selected_mode >= 64 ||
        embedder->selected_phase >= 64 ||
        embedder->monotonic_now_ns == NULL ||
        embedder->factory_recovery == NULL) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    status =
        ribos_vm_helper_contract_validate_v1(embedder->helper_contract);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    return RIBOS_VM_STATUS_OK;
}

/** Terminal outcome의 ABI, tag와 payload lifetime marker를 검사한다. */
static inline RibosVmStatus
ribos_vm_outcome_validate_v1(const RibosVmOutcome *outcome)
{
    if (outcome == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    if (!ribos_vm_runtime_version_is_supported(
            outcome->runtime_abi_major,
            outcome->runtime_abi_minor)) {
        return RIBOS_VM_STATUS_UNSUPPORTED_RUNTIME_ABI;
    }
    if (outcome->size != (uint32_t)sizeof(*outcome)) {
        return RIBOS_VM_STATUS_INVALID_SIZE;
    }
    if (outcome->flags != 0 ||
        !ribos_vm_reserved_words_are_zero(outcome->reserved, 4)) {
        return RIBOS_VM_STATUS_RESERVED_NONZERO;
    }
    if (outcome->kind == RIBOS_VM_OUTCOME_BOOT_ACTION) {
        const RibosVmBootAction *action = &outcome->value.boot_action;

        if (action->terminal_helper_id == RIBOS_VM_INVALID_ID ||
            action->action_type_id == RIBOS_VM_INVALID_ID ||
            action->generation == 0 ||
            (action->payload_size != 0 && action->payload == NULL) ||
            !ribos_vm_digest_is_nonzero(action->receipt_digest) ||
            !ribos_vm_reserved_words_are_zero(action->reserved, 2)) {
            return RIBOS_VM_STATUS_INVALID_STATE;
        }
        return RIBOS_VM_STATUS_OK;
    }
    if (outcome->kind == RIBOS_VM_OUTCOME_POLICY_ERROR) {
        const RibosVmPolicyError *error = &outcome->value.policy_error;

        if (error->error_type_id == RIBOS_VM_INVALID_ID ||
            error->reserved0 != 0 ||
            (error->payload_size != 0 && error->payload == NULL) ||
            !ribos_vm_reserved_words_are_zero(error->reserved, 2)) {
            return RIBOS_VM_STATUS_INVALID_STATE;
        }
        return RIBOS_VM_STATUS_OK;
    }
    if (outcome->kind == RIBOS_VM_OUTCOME_VM_FAULT) {
        const RibosVmFaultReceipt *fault = &outcome->value.vm_fault;

        if (fault->fault_code < RIBOS_VM_FAULT_INTERNAL ||
            fault->fault_code > RIBOS_VM_FAULT_RECOVERY ||
            fault->subject < RIBOS_VM_FAULT_SUBJECT_RUNTIME ||
            fault->subject > RIBOS_VM_FAULT_SUBJECT_RECOVERY ||
            fault->last_effect > RIBOS_VM_HELPER_EFFECT_TERMINAL ||
            fault->last_durability >
                RIBOS_VM_HELPER_DURABILITY_SEALED_INTENT ||
            !ribos_vm_digest_is_nonzero(fault->artifact_hash) ||
            !ribos_vm_reserved_words_are_zero(fault->reserved, 2)) {
            return RIBOS_VM_STATUS_INVALID_STATE;
        }
        return RIBOS_VM_STATUS_OK;
    }
    return RIBOS_VM_STATUS_INVALID_STATE;
}

/** Stable runtime status의 ASCII spelling을 반환한다. */
static inline const char *
ribos_vm_status_name(RibosVmStatus status)
{
    switch (status) {
    case RIBOS_VM_STATUS_OK:
        return "ok";
    case RIBOS_VM_STATUS_INVALID_ARGUMENT:
        return "invalid-argument";
    case RIBOS_VM_STATUS_UNSUPPORTED_RUNTIME_ABI:
        return "unsupported-runtime-abi";
    case RIBOS_VM_STATUS_UNSUPPORTED_HELPER_ABI:
        return "unsupported-helper-abi";
    case RIBOS_VM_STATUS_INVALID_SIZE:
        return "invalid-size";
    case RIBOS_VM_STATUS_RESERVED_NONZERO:
        return "reserved-nonzero";
    case RIBOS_VM_STATUS_INVALID_DESCRIPTOR:
        return "invalid-descriptor";
    case RIBOS_VM_STATUS_INVALID_STATE:
        return "invalid-state";
    case RIBOS_VM_STATUS_DIGEST_MISMATCH:
        return "digest-mismatch";
    case RIBOS_VM_STATUS_LIMIT_EXCEEDED:
        return "limit-exceeded";
    case RIBOS_VM_STATUS_NOT_AUTHORIZED:
        return "not-authorized";
    case RIBOS_VM_STATUS_NOT_PREPARED:
        return "not-prepared";
    case RIBOS_VM_STATUS_ARENA_TOO_SMALL:
        return "arena-too-small";
    case RIBOS_VM_STATUS_EMBEDDER_REJECTED:
        return "embedder-rejected";
    case RIBOS_VM_STATUS_ALREADY_CONSUMED:
        return "already-consumed";
    case RIBOS_VM_STATUS_INTERNAL_ERROR:
        return "internal-error";
    default:
        return "unknown";
    }
}

/** Stable fault code의 ASCII spelling을 반환한다. */
static inline const char *
ribos_vm_fault_code_name(uint32_t code)
{
    switch (code) {
    case RIBOS_VM_FAULT_NONE:
        return "none";
    case RIBOS_VM_FAULT_INTERNAL:
        return "internal";
    case RIBOS_VM_FAULT_INVALID_STATE:
        return "invalid-state";
    case RIBOS_VM_FAULT_INSTRUCTION_BUDGET:
        return "instruction-budget";
    case RIBOS_VM_FAULT_HELPER_BUDGET:
        return "helper-budget";
    case RIBOS_VM_FAULT_OPERATION_BUDGET:
        return "operation-budget";
    case RIBOS_VM_FAULT_POLL_BUDGET:
        return "poll-budget";
    case RIBOS_VM_FAULT_DEADLINE:
        return "deadline";
    case RIBOS_VM_FAULT_STACK_BOUNDS:
        return "stack-bounds";
    case RIBOS_VM_FAULT_CALL_DEPTH:
        return "call-depth";
    case RIBOS_VM_FAULT_LOOP_BOUND:
        return "loop-bound";
    case RIBOS_VM_FAULT_ARITHMETIC:
        return "arithmetic";
    case RIBOS_VM_FAULT_INVALID_VALUE:
        return "invalid-value";
    case RIBOS_VM_FAULT_HANDLE_VIOLATION:
        return "handle-violation";
    case RIBOS_VM_FAULT_CAPABILITY:
        return "capability";
    case RIBOS_VM_FAULT_MODE_PHASE:
        return "mode-phase";
    case RIBOS_VM_FAULT_HELPER_CONTRACT:
        return "helper-contract";
    case RIBOS_VM_FAULT_EMBEDDER:
        return "embedder";
    case RIBOS_VM_FAULT_TERMINAL_ACTION:
        return "terminal-action";
    case RIBOS_VM_FAULT_RECOVERY:
        return "recovery";
    default:
        return "unknown";
    }
}

/** Stable outcome kind의 ASCII spelling을 반환한다. */
static inline const char *
ribos_vm_outcome_kind_name(uint32_t kind)
{
    switch (kind) {
    case RIBOS_VM_OUTCOME_BOOT_ACTION:
        return "boot-action";
    case RIBOS_VM_OUTCOME_POLICY_ERROR:
        return "policy-error";
    case RIBOS_VM_OUTCOME_VM_FAULT:
        return "vm-fault";
    default:
        return "unknown";
    }
}

#ifdef __cplusplus
}
#endif

#endif
