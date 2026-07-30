#ifndef RIBOS_VM_RUNTIME_HELPERS_INTERNAL_H
#define RIBOS_VM_RUNTIME_HELPERS_INTERNAL_H

#include "ribos/vm/helpers.h"

typedef struct RibosVmHelperArgumentInternal {
    uint32_t slot_id;
    uint32_t type_id;
    uint32_t byte_size;
    uint32_t ownership;
    uint32_t schema_type_class;
} RibosVmHelperArgumentInternal;

typedef struct RibosVmHelperResultInternal {
    uint32_t slot_id;
    uint32_t type_id;
    uint32_t byte_size;
    uint32_t storage_kind;
    uint32_t payload_offset;
    uint32_t success_type_id;
    uint32_t success_byte_size;
    uint32_t error_type_id;
    uint32_t error_byte_size;
} RibosVmHelperResultInternal;

typedef struct RibosVmHelperDispatchRequest {
    uint32_t function_id;
    uint32_t instruction_id;
    uint32_t source_map_id;
    uint32_t stable_id;
    uint32_t argument_count;
    uint32_t reserved0;
    uint64_t frame_base;
    RibosVmHelperArgumentInternal
        arguments[RIBOS_SCHEMA_MAX_PARAMETERS];
    RibosVmHelperResultInternal result;
} RibosVmHelperDispatchRequest;

typedef struct RibosVmHelperDispatchResult {
    uint32_t fault_code;
    uint32_t fault_subject;
    uint32_t fault_detail;
    uint32_t helper_id;
    uint32_t last_effect;
    uint32_t last_durability;
    uint64_t consumed_helper_calls;
    uint64_t consumed_input_bytes;
    uint64_t consumed_output_bytes;
    uint64_t consumed_operations;
    uint64_t consumed_polls;
    uint64_t elapsed_ns;
} RibosVmHelperDispatchResult;

RibosVmStatus ribos_vm_helper_dispatch_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmContext *context,
    const RibosVmHelperEnvironment *environment,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmHelperDispatchRequest *request,
    RibosVmHelperDispatchResult *result);

#endif
