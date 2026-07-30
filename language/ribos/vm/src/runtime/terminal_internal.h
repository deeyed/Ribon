#ifndef RIBOS_VM_RUNTIME_TERMINAL_INTERNAL_H
#define RIBOS_VM_RUNTIME_TERMINAL_INTERNAL_H

#include "ribos/vm/terminal.h"

typedef struct RibosVmTerminalHelperReceiptInternal {
    uint32_t helper_id;
    uint32_t function_id;
    uint32_t instruction_id;
    uint32_t source_map_id;
    uint32_t result_slot_id;
    uint32_t result_type_id;
    uint32_t result_byte_size;
    uint32_t result_kind;
    uint32_t callback_status;
    uint32_t effect;
    uint32_t durability;
    uint32_t journal_state;
    uint64_t frame_base;
    uint64_t receipt_sequence;
    uint8_t journal_receipt_digest[RIBOS_VM_DIGEST_BYTES];
} RibosVmTerminalHelperReceiptInternal;

RibosVmStatus ribos_vm_terminal_initialize_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmContext *context,
    RibosVmStorage *storage,
    size_t arena_size);

RibosVmStatus ribos_vm_terminal_record_helper_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmContext *context,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmTerminalHelperReceiptInternal *receipt);

RibosVmStatus ribos_vm_terminal_finalize_interpreter_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmHelperEnvironment *environment,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmInterpreterSnapshot *interpreter,
    RibosVmOutcome *outcome);

RibosVmStatus ribos_vm_terminal_close_fault_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmHelperEnvironment *environment,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmOutcome *outcome);

#endif
