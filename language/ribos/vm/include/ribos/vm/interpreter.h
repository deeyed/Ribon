#ifndef RIBOS_VM_INTERPRETER_H
#define RIBOS_VM_INTERPRETER_H

#include <stddef.h>
#include <stdint.h>

#include "ribos/vm/storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file interpreter.h
 * @brief PreparedProgram만 실행하는 architecture-neutral Ribos interpreter v1.
 *
 * v1.3 API는 scalar/control-flow, bounded direct call/loop, heap-free bounded
 * aggregate engine과 verified whole-value ownership transfer를 제공한다. Full
 * policy success나 BootAction을 만들지 않으며 지원 범위 밖 opcode는 fail
 * closed한다.
 */

#define RIBOS_VM_INTERPRETER_V1_MAJOR 1u
#define RIBOS_VM_INTERPRETER_V1_MINOR 3u

/** Caller가 관찰할 수 있는 bounded interpreter 상태다. */
typedef enum RibosVmInterpreterState {
    RIBOS_VM_INTERPRETER_EMPTY = 0,
    RIBOS_VM_INTERPRETER_READY = 1,
    RIBOS_VM_INTERPRETER_RUNNING = 2,
    RIBOS_VM_INTERPRETER_RETURNED = 3,
    RIBOS_VM_INTERPRETER_FAULTED = 4
} RibosVmInterpreterState;

/**
 * Arena control state의 pointer-free snapshot이다.
 *
 * `return_slot_id`는 `RETURNED`에서만 유효하다. `fault_code`는 `FAULTED`에서만
 * 유효하며 상세 receipt는 `ribos_vm_interpreter_fault_v1`로 읽는다.
 */
typedef struct RibosVmInterpreterSnapshot {
    uint32_t size;
    uint16_t interpreter_major;
    uint16_t interpreter_minor;
    uint32_t state;
    uint32_t function_id;
    uint32_t block_id;
    uint32_t instruction_id;
    uint32_t source_map_id;
    uint32_t return_slot_id;
    uint32_t fault_code;
    uint64_t remaining_instructions;
    uint64_t consumed_instructions;
    uint32_t frame_depth;
    uint32_t reserved0;
    uint64_t stack_bytes;
    uint64_t reserved[2];
} RibosVmInterpreterSnapshot;

/**
 * Entry function과 immutable context를 initialized storage에 결박한다.
 *
 * Context type ID와 byte size는 entry parameter slot과 정확히 일치해야 한다.
 * 함수는 entry frame record와 loop counter를 reset하고 첫 verified instruction ID를
 * control region에 봉인한다.
 */
RibosVmStatus ribos_vm_interpreter_initialize_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmContext *context,
    RibosVmStorage *storage,
    size_t arena_size);

/**
 * 정확히 한 instruction을 dispatch한다.
 *
 * Fuel은 opcode 해석 전에 검사하고 감소한다. `RETURN`과 `TRAP`, arithmetic fault,
 * invariant fault 또는 아직 지원하지 않는 opcode에 도달하면 terminal snapshot을
 * 반환하고 이후 step은 거부한다.
 */
RibosVmStatus ribos_vm_interpreter_step_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmContext *context,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmInterpreterSnapshot *snapshot);

/**
 * Entry `RETURNED` 또는 `FAULTED`까지 bounded instruction loop를 실행한다.
 *
 * Direct call은 arena frame stack만 사용하고 loop는 verified row별 trip counter를
 * 집행한다. Aggregate는 verified inline layout 안에서만 실행한다. 이 함수는 helper
 * callback 또는 boot transfer를 실행하지 않는다.
 */
RibosVmStatus ribos_vm_interpreter_run_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmContext *context,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmInterpreterSnapshot *snapshot);

/** Active interpreter control state를 pointer-free snapshot으로 읽는다. */
RibosVmStatus ribos_vm_interpreter_snapshot_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    RibosVmInterpreterSnapshot *snapshot);

/** `FAULTED` storage에 봉인된 fixed receipt를 caller structure로 복사한다. */
RibosVmStatus ribos_vm_interpreter_fault_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    RibosVmFaultReceipt *receipt);

#ifdef __cplusplus
}
#endif

#endif
