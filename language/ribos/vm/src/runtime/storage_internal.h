#ifndef RIBOS_VM_RUNTIME_STORAGE_INTERNAL_H
#define RIBOS_VM_RUNTIME_STORAGE_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "ribos/vm/storage.h"

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
    uint64_t consumed_instructions;
    uint64_t context_generation;
    uint32_t context_type_id;
    uint8_t context_digest[RIBOS_VM_DIGEST_BYTES];
} RibosVmStorageExecutionControl;

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

RibosVmStatus ribos_vm_storage_consume_instruction_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint64_t *remaining_instructions,
    uint64_t *consumed_instructions);

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
