#ifndef RIBOS_VM_RUNTIME_STORAGE_INTERNAL_H
#define RIBOS_VM_RUNTIME_STORAGE_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "ribos/vm/storage.h"

#include "ribos/vm/helpers.h"

/*
 * 이 header는 VM runtime 구현 전용이다. Arena control bytes를 해석하는 권한은
 * storage.c에만 남기고 interpreter는 이 typed internal API만 사용한다.
 */

#define RIBOS_VM_STORAGE_CONTROL_REMAINING_INSTRUCTIONS_OFFSET_V1 384u

typedef struct RibosVmStorageExecutionControl {
    uint32_t state;
    uint32_t function_id;
    uint32_t block_id;
    uint32_t instruction_id;
    uint32_t return_slot_id;
    uint64_t frame_base;
    uint64_t stack_cursor;
    uint32_t frame_depth;
    uint64_t consumed_instructions;
    uint64_t context_generation;
    uint32_t context_type_id;
    uint8_t context_digest[RIBOS_VM_DIGEST_BYTES];
} RibosVmStorageExecutionControl;

typedef struct RibosVmStorageCallTarget {
    uint32_t function_id;
    uint32_t entry_block_id;
    uint32_t entry_instruction_id;
    uint32_t frame_size;
    uint64_t frame_base;
} RibosVmStorageCallTarget;

typedef struct RibosVmStorageReturnTarget {
    uint32_t function_id;
    uint32_t block_id;
    uint32_t instruction_id;
    uint32_t return_slot_id;
    uint64_t frame_base;
} RibosVmStorageReturnTarget;

RibosVmStatus ribos_vm_storage_execution_begin_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *control);

RibosVmStatus ribos_vm_storage_execution_load_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    RibosVmStorageExecutionControl *control,
    uint64_t *remaining_instructions);

RibosVmStatus ribos_vm_storage_execution_store_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *control);

RibosVmStatus ribos_vm_storage_call_target_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *caller,
    uint32_t callee_function_id,
    RibosVmStorageCallTarget *target,
    uint32_t *fault_code);

RibosVmStatus ribos_vm_storage_frame_push_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *caller,
    const RibosVmStorageCallTarget *target,
    uint32_t continuation_instruction_id,
    uint32_t return_slot_id,
    RibosVmStorageExecutionControl *callee);

RibosVmStatus ribos_vm_storage_return_target_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *callee,
    RibosVmStorageReturnTarget *target);

RibosVmStatus ribos_vm_storage_frame_pop_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *callee,
    const RibosVmStorageReturnTarget *target,
    RibosVmStorageExecutionControl *caller);

RibosVmStatus ribos_vm_storage_loop_transition_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint32_t source_block_id,
    uint32_t target_block_id,
    uint32_t *violation_loop_id);

RibosVmStatus ribos_vm_storage_consume_instruction_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint64_t *remaining_instructions,
    uint64_t *consumed_instructions);

RibosVmStatus ribos_vm_storage_helper_execution_initialize_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmHelperExecutionSnapshot *snapshot);

RibosVmStatus ribos_vm_storage_helper_execution_load_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHelperExecutionSnapshot *snapshot);

RibosVmStatus ribos_vm_storage_helper_execution_store_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmHelperExecutionSnapshot *snapshot);

RibosVmStatus ribos_vm_storage_consume_helper_call_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t stable_id);

RibosVmStatus ribos_vm_storage_consume_operations_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint64_t count);

RibosVmStatus ribos_vm_storage_consume_polls_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint64_t count);

RibosVmStatus ribos_vm_storage_slot_zero_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id);

RibosVmStatus ribos_vm_storage_slot_copy_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t source_function_id,
    uint64_t source_frame_base,
    uint32_t source_slot_id,
    uint32_t destination_function_id,
    uint64_t destination_frame_base,
    uint32_t destination_slot_id);

RibosVmStatus ribos_vm_storage_slot_slice_read_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    uint32_t offset,
    uint8_t *bytes,
    uint32_t byte_size);

RibosVmStatus ribos_vm_storage_slot_slice_write_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    uint32_t offset,
    const uint8_t *bytes,
    uint32_t byte_size);

RibosVmStatus ribos_vm_storage_slot_slice_zero_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    uint32_t offset,
    uint32_t byte_size);

RibosVmStatus ribos_vm_storage_slot_slice_copy_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t source_function_id,
    uint64_t source_frame_base,
    uint32_t source_slot_id,
    uint32_t source_offset,
    uint32_t destination_function_id,
    uint64_t destination_frame_base,
    uint32_t destination_slot_id,
    uint32_t destination_offset,
    uint32_t byte_size);

RibosVmStatus ribos_vm_storage_slot_slice_move_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    uint32_t source_offset,
    uint32_t destination_offset,
    uint32_t byte_size);

RibosVmStatus ribos_vm_storage_slot_slice_compare_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    uint32_t left_function_id,
    uint64_t left_frame_base,
    uint32_t left_slot_id,
    uint32_t left_offset,
    uint32_t right_function_id,
    uint64_t right_frame_base,
    uint32_t right_slot_id,
    uint32_t right_offset,
    uint32_t byte_size,
    int *comparison);

RibosVmStatus ribos_vm_storage_seal_fault_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmFaultReceipt *receipt);

RibosVmStatus ribos_vm_storage_read_fault_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    RibosVmFaultReceipt *receipt);

#endif
