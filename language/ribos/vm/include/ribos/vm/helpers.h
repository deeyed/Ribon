#ifndef RIBOS_VM_HELPERS_H
#define RIBOS_VM_HELPERS_H

#include <stddef.h>
#include <stdint.h>

#include "ribos/vm/handles.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file helpers.h
 * @brief Typed synchronous helper dispatch와 bounded receipt 계약.
 *
 * Artifact는 stable helper ID만 가진다. Runtime은 PreparedProgram이 복사한
 * descriptor/callback과 이 environment를 대조한 뒤에만 callback을 호출한다.
 */

#define RIBOS_VM_HELPERS_V1_MAJOR 1u
#define RIBOS_VM_HELPERS_V1_MINOR 1u

/** Arena helper state의 stable lifecycle다. */
typedef enum RibosVmHelperExecutionState {
    RIBOS_VM_HELPER_EXECUTION_EMPTY = 0,
    RIBOS_VM_HELPER_EXECUTION_READY = 1,
    RIBOS_VM_HELPER_EXECUTION_CALLBACK_ACTIVE = 2,
    RIBOS_VM_HELPER_EXECUTION_FAULTED = 3
} RibosVmHelperExecutionState;

/** Callback이 채운 typed result의 stable class다. */
typedef enum RibosVmHelperResultKind {
    RIBOS_VM_HELPER_RESULT_NONE = 0,
    RIBOS_VM_HELPER_RESULT_SUCCESS_VALUE = 1,
    RIBOS_VM_HELPER_RESULT_SUCCESS_HANDLE = 2,
    RIBOS_VM_HELPER_RESULT_POLICY_ERROR = 3
} RibosVmHelperResultKind;

/**
 * 한 execution에 필요한 process-local embedder와 handle authority다.
 *
 * Pointer는 wire나 arena에 저장되지 않는다. Environment와 pointee는 helper-aware
 * interpreter가 반환할 때까지 유효해야 한다.
 */
typedef struct RibosVmHelperEnvironment {
    uint32_t size;
    uint16_t helpers_major;
    uint16_t helpers_minor;
    uint32_t flags;
    uint32_t reserved0;
    const RibosVmEmbedder *embedder;
    RibosVmHandleHostTable *handle_table;
    uint64_t reserved[4];
} RibosVmHelperEnvironment;

/** Callback이 읽는 한 verified argument의 pointer-free metadata다. */
typedef struct RibosVmHelperArgumentInfo {
    uint32_t size;
    uint16_t helpers_major;
    uint16_t helpers_minor;
    uint32_t ordinal;
    uint32_t type_id;
    uint32_t byte_size;
    uint32_t ownership;
    uint32_t schema_type_class;
    uint32_t parameter_mode;
    uint32_t reserved0;
    uint64_t reserved[2];
} RibosVmHelperArgumentInfo;

/** Callback-local helper call의 immutable metadata다. */
typedef struct RibosVmHelperCallInfo {
    uint32_t size;
    uint16_t helpers_major;
    uint16_t helpers_minor;
    uint32_t stable_id;
    uint32_t function_id;
    uint32_t instruction_id;
    uint32_t source_map_id;
    uint32_t argument_count;
    uint32_t result_type_id;
    uint32_t effect;
    uint32_t durability;
    uint32_t handle_transition;
    uint32_t transition_parameter;
    uint64_t deadline_ns;
    uint64_t reserved[2];
} RibosVmHelperCallInfo;

/**
 * Outcome region에 field-wise encoding되는 pointer-free execution snapshot이다.
 *
 * 마지막 call receipt는 effect/durability와 실제 callback-local I/O/operation/poll
 * count를 보존한다. Journal receipt chain과 terminal action seal은 terminal
 * 계층에서 별도로 field-wise encoding한다.
 */
typedef struct RibosVmHelperExecutionSnapshot {
    uint32_t size;
    uint16_t helpers_major;
    uint16_t helpers_minor;
    uint32_t state;
    uint32_t callback_active;
    uint32_t selected_mode;
    uint32_t selected_phase;
    uint32_t granted_capabilities;
    uint32_t active_helper_id;
    uint64_t execution_start_ns;
    uint64_t execution_deadline_ns;
    uint64_t last_now_ns;
    uint64_t consumed_helper_calls;
    uint64_t consumed_input_bytes;
    uint64_t consumed_output_bytes;
    uint64_t consumed_operations;
    uint64_t consumed_polls;
    uint64_t receipt_sequence;
    uint32_t last_helper_id;
    uint32_t last_callback_status;
    uint32_t last_effect;
    uint32_t last_durability;
    uint32_t last_handle_transition;
    uint32_t last_result_kind;
    uint64_t last_input_bytes;
    uint64_t last_output_bytes;
    uint64_t last_operations;
    uint64_t last_polls;
    uint64_t last_duration_ns;
    uint64_t context_generation;
    uint8_t context_digest[RIBOS_VM_DIGEST_BYTES];
    uint8_t helper_execution_digest[RIBOS_VM_DIGEST_BYTES];
    uint64_t reserved[2];
} RibosVmHelperExecutionSnapshot;

/** Environment의 ABI, embedder와 handle-table capacity를 검사한다. */
RibosVmStatus ribos_vm_helper_environment_validate_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmContext *context,
    const RibosVmHelperEnvironment *environment);

/**
 * Interpreter initialization 뒤 helper deadline/counter receipt를 시작한다.
 *
 * Monotonic clock 실패, capability/mode/phase 또는 helper-table mismatch는 callback
 * 진입 전에 거부한다.
 */
RibosVmStatus ribos_vm_helper_execution_initialize_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmContext *context,
    const RibosVmHelperEnvironment *environment,
    RibosVmStorage *storage,
    size_t arena_size);

/** Arena에 봉인된 helper execution과 마지막 call receipt를 읽는다. */
RibosVmStatus ribos_vm_helper_execution_snapshot_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHelperExecutionSnapshot *snapshot);

/** Callback-local immutable call metadata를 복사한다. */
RibosVmStatus ribos_vm_helper_call_info_v1(
    const RibosVmHelperCall *call,
    RibosVmHelperCallInfo *info);

/** Verified argument의 type, ownership과 parameter mode를 복사한다. */
RibosVmStatus ribos_vm_helper_call_argument_info_v1(
    const RibosVmHelperCall *call,
    uint32_t ordinal,
    RibosVmHelperArgumentInfo *info);

/**
 * Copy 또는 borrowed value argument를 caller buffer로 복사한다.
 *
 * `required_size`는 exact verified type size다. Opaque handle은 trusted pointer가
 * 아니라 token bytes만 복사되므로 object 사용에는 handle accessor를 써야 한다.
 */
RibosVmStatus ribos_vm_helper_call_argument_copy_v1(
    const RibosVmHelperCall *call,
    uint32_t ordinal,
    uint32_t expected_type_id,
    uint8_t *output,
    size_t output_capacity,
    size_t *required_size);

/**
 * Opaque borrow/consume argument의 callback-local trusted object를 반환한다.
 *
 * Pointer는 callback 반환과 동시에 invalid다. Runtime은 accessor를 callback 밖에서
 * 호출하거나 closed lease를 다시 읽는 시도를 거부한다.
 */
RibosVmStatus ribos_vm_helper_call_argument_handle_v1(
    const RibosVmHelperCall *call,
    uint32_t ordinal,
    uint32_t expected_type_id,
    void **trusted_object);

/** Callback-local operation budget을 실제 external operation 전에 소비한다. */
RibosVmStatus ribos_vm_helper_call_consume_operations_v1(
    RibosVmHelperCall *call,
    uint64_t count);

/** Bounded poll 전에 poll budget과 monotonic deadline을 함께 검사한다. */
RibosVmStatus ribos_vm_helper_call_consume_polls_v1(
    RibosVmHelperCall *call,
    uint64_t count);

/** Copy success payload를 exact verified success type으로 기록한다. */
RibosVmStatus ribos_vm_helper_call_set_success_value_v1(
    RibosVmHelperCall *call,
    uint32_t type_id,
    const uint8_t *bytes,
    size_t byte_size);

/** Opaque success가 반환할 trusted object를 callback-local로 설정한다. */
RibosVmStatus ribos_vm_helper_call_set_success_handle_v1(
    RibosVmHelperCall *call,
    uint32_t type_id,
    void *trusted_object,
    RibosVmHandleDropFn drop,
    void *drop_context);

/** Result helper의 typed policy-error payload를 기록한다. */
RibosVmStatus ribos_vm_helper_call_set_policy_error_v1(
    RibosVmHelperCall *call,
    uint32_t error_type_id,
    const uint8_t *bytes,
    size_t byte_size);

/**
 * Journaled helper가 남긴 product-owned durable receipt identity를 기록한다.
 *
 * `journal_state`는 `RibosVmJournalReceiptState` registry 값이다. Digest는 secret이나
 * pointer를 포함하지 않는 product receipt의 canonical identity여야 한다.
 * Journaled helper만 호출할 수 있고 한 callback에서 정확히 한 번만 설정한다.
 */
RibosVmStatus ribos_vm_helper_call_set_journal_receipt_v1(
    RibosVmHelperCall *call,
    uint32_t journal_state,
    const uint8_t receipt_digest[RIBOS_VM_DIGEST_BYTES]);

/**
 * Consume callback이 source object authority를 외부로 이전했음을 표시한다.
 *
 * 표시하지 않은 consume은 callback 종료에서 bounded drop/revoke된다.
 */
RibosVmStatus ribos_vm_helper_call_mark_consumed_transferred_v1(
    RibosVmHelperCall *call);

#ifdef __cplusplus
}
#endif

#endif
