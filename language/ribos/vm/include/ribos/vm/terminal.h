#ifndef RIBOS_VM_TERMINAL_H
#define RIBOS_VM_TERMINAL_H

#include <stddef.h>
#include <stdint.h>

#include "ribos/vm/helpers.h"
#include "ribos/vm/interpreter.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file terminal.h
 * @brief Ribos policy terminal outcome와 fail-closed recovery 계약.
 *
 * 이 계층은 verified bytecode interpreter 위에서 entry Result를 다시 분류하고
 * single-consume BootAction intent를 봉인한다. BootAction은 실제 control transfer,
 * device quiesce 또는 durable update commit이 아니다.
 */

#define RIBOS_VM_TERMINAL_V1_MAJOR 1u
#define RIBOS_VM_TERMINAL_V1_MINOR 0u

/** Arena에 field-wise encoding되는 terminal lifecycle다. */
typedef enum RibosVmTerminalState {
    RIBOS_VM_TERMINAL_EMPTY = 0,
    RIBOS_VM_TERMINAL_EXECUTING = 1,
    RIBOS_VM_TERMINAL_ACTION_PENDING = 2,
    RIBOS_VM_TERMINAL_ACTION_SEALED = 3,
    RIBOS_VM_TERMINAL_POLICY_ERROR = 4,
    RIBOS_VM_TERMINAL_VM_FAULT = 5,
    RIBOS_VM_TERMINAL_ACTION_CONSUMED = 6
} RibosVmTerminalState;

/**
 * Journaled helper가 보고한 durable external-effect 상태다.
 *
 * PARTIAL과 UNCERTAIN은 VM local state를 되돌려도 외부 effect가 사라졌다고 주장할
 * 수 없음을 뜻한다.
 */
typedef enum RibosVmJournalReceiptState {
    RIBOS_VM_JOURNAL_RECEIPT_NONE = 0,
    RIBOS_VM_JOURNAL_RECEIPT_COMMITTED = 1,
    RIBOS_VM_JOURNAL_RECEIPT_PARTIAL = 2,
    RIBOS_VM_JOURNAL_RECEIPT_UNCERTAIN = 3
} RibosVmJournalReceiptState;

/**
 * Outcome region에 field-wise encoding되는 pointer-free terminal snapshot이다.
 *
 * Digest는 native structure byte가 아니라 stable little-endian field stream에서
 * 계산한다. `journal_chain_digest`는 receipt chain이지 rollback 증명이 아니다.
 */
typedef struct RibosVmTerminalSnapshot {
    uint32_t size;
    uint16_t terminal_major;
    uint16_t terminal_minor;
    uint32_t state;
    uint32_t outcome_kind;
    uint32_t terminal_helper_id;
    uint32_t action_type_id;
    uint32_t error_type_id;
    uint32_t stable_error_code;
    uint32_t source_map_id;
    uint32_t flags;
    uint32_t action_consumed;
    uint32_t recovery_notified;
    uint32_t authority_revoked;
    uint32_t journal_state;
    uint64_t payload_size;
    uint64_t context_generation;
    uint64_t journal_sequence;
    uint64_t journal_count;
    uint32_t last_journal_helper_id;
    uint32_t last_journal_callback_status;
    uint8_t binding_digest[RIBOS_VM_DIGEST_BYTES];
    uint8_t context_digest[RIBOS_VM_DIGEST_BYTES];
    uint8_t action_receipt_digest[RIBOS_VM_DIGEST_BYTES];
    uint8_t journal_chain_digest[RIBOS_VM_DIGEST_BYTES];
    uint8_t trace_digest[RIBOS_VM_DIGEST_BYTES];
} RibosVmTerminalSnapshot;

/**
 * Prepared policy를 exactly one terminal outcome까지 실행한다.
 *
 * Storage는 같은 PreparedProgram으로 새로 initialize된 상태여야 한다. 성공 반환은
 * `outcome` 자체가 BootAction, PolicyError 또는 VmFault 중 정확히 하나임을 뜻한다.
 * VmFault에서는 VM-side handle/callback authority를 폐쇄한 뒤 factory recovery를
 * 최대 한 번 통지한다.
 */
RibosVmStatus ribos_vm_policy_execute_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmContext *context,
    const RibosVmHelperEnvironment *environment,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmOutcome *outcome);

/** Arena에 봉인된 pointer-free terminal receipt를 복사한다. */
RibosVmStatus ribos_vm_terminal_snapshot_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    RibosVmTerminalSnapshot *snapshot);

/**
 * 봉인된 BootAction intent를 정확히 한 번 소비한다.
 *
 * 함수는 action payload, generation과 receipt digest를 arena seal과 다시 대조한다.
 * 성공 시 borrowed payload를 zeroize하고 같은 action의 재소비를 거부한다. 실제
 * control transfer는 이 API 밖의 Ribon Core authority가 소유한다.
 */
RibosVmStatus ribos_vm_boot_action_consume_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmBootAction *action);

#ifdef __cplusplus
}
#endif

#endif
