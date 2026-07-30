#ifndef RIBOS_IR_ANALYSIS_H
#define RIBOS_IR_ANALYSIS_H

#include "ribos/ir/ir.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file analysis.h
 * @brief Policy IR v1 CFG와 실행 자원의 폐쇄 분석 계약.
 */

#define RIBOS_IR_MAX_VALUE_BYTES (1024u * 1024u)
#define RIBOS_IR_MAX_FRAME_BYTES (16u * 1024u * 1024u)
#define RIBOS_IR_MAX_STACK_BYTES (64u * 1024u * 1024u)

/** VM이 collection value를 저장하는 결정론적 representation이다. */
typedef enum RibosIrStorageKind {
    RIBOS_IR_STORAGE_SCALAR = 0,
    RIBOS_IR_STORAGE_OPAQUE,
    RIBOS_IR_STORAGE_INLINE_ARRAY,
    RIBOS_IR_STORAGE_INLINE_LIST,
    RIBOS_IR_STORAGE_SORTED_MAP,
    RIBOS_IR_STORAGE_TAGGED_UNION,
    RIBOS_IR_STORAGE_INLINE_STRUCT
} RibosIrStorageKind;

/** 한 IR type의 host ABI와 독립적인 VM storage layout이다. */
typedef struct RibosIrTypeLayout {
    uint32_t type_id;
    RibosIrStorageKind storage_kind;
    uint32_t byte_size;
    uint32_t alignment;
    uint32_t element_stride;
    uint32_t payload_offset;
    uint32_t capacity;
} RibosIrTypeLayout;

/** 한 virtual slot이 function frame 안에서 차지하는 고정 범위다. */
typedef struct RibosIrSlotLayout {
    uint32_t slot_id;
    uint32_t function_id;
    uint32_t type_id;
    uint32_t frame_offset;
    uint32_t byte_size;
    uint32_t alignment;
} RibosIrSlotLayout;

/** Basic block의 entry 도달 가능성과 terminal closure 분류다. */
typedef struct RibosIrBlockResource {
    uint32_t block_id;
    uint32_t function_id;
    uint64_t execution_upper_bound;
    uint8_t reachable;
} RibosIrBlockResource;

/** Bounded loop의 검증된 실행 상한이다. */
typedef struct RibosIrLoopResource {
    uint32_t loop_id;
    uint32_t function_id;
    uint32_t header_block;
    uint32_t body_block;
    uint32_t exit_block;
    uint32_t latch_block;
    uint32_t trip_count;
    uint8_t reachable;
} RibosIrLoopResource;

/** Function이 도달할 수 있는 terminal action 비트다. */
typedef enum RibosIrTerminalMask {
    RIBOS_IR_TERMINAL_RETURN = 1u << 0,
    RIBOS_IR_TERMINAL_TRAP = 1u << 1
} RibosIrTerminalMask;

/** 한 function에 대해 닫힌 frame, call graph와 실행량 상한이다. */
typedef struct RibosIrFunctionResource {
    uint32_t function_id;
    uint32_t reachable_block_count;
    uint32_t terminal_mask;
    uint32_t maximum_call_depth;
    uint32_t frame_bytes;
    uint32_t aggregate_slot_bytes;
    uint32_t largest_value_bytes;
    uint64_t maximum_stack_bytes;
    uint64_t instruction_upper_bound;
    uint64_t helper_call_upper_bound;
    uint8_t all_paths_terminal;
    uint8_t instruction_budget_satisfied;
    uint8_t helper_budget_satisfied;
} RibosIrFunctionResource;

/** Function과 helper stable ID별 worst-path call 상한이다. */
typedef struct RibosIrHelperBound {
    uint32_t function_id;
    uint32_t helper_stable_id;
    uint64_t call_upper_bound;
} RibosIrHelperBound;

/** Resource closure가 소유한 table 수다. */
typedef struct RibosIrResourceSummary {
    size_t type_layout_count;
    size_t function_count;
    size_t block_count;
    size_t loop_count;
    size_t slot_layout_count;
    size_t helper_bound_count;
} RibosIrResourceSummary;

/** 내부 storage를 숨기는 host resource-closure 결과다. */
typedef struct RibosIrResourceClosure RibosIrResourceClosure;

/**
 * 비어 있는 host resource-closure 결과를 생성한다.
 *
 * Allocator는 closure보다 오래 살아야 한다.
 */
RibosIrResourceClosure *ribos_ir_resource_closure_create(
    const RibosAllocator *allocator);

/** Resource-closure의 compiler-owned storage를 해제한다. */
void ribos_ir_resource_closure_destroy(RibosIrResourceClosure *closure);

/** 기존 분석 결과를 폐기하고 빈 상태로 되돌린다. */
void ribos_ir_resource_closure_reset(RibosIrResourceClosure *closure);

/**
 * Policy IR의 CFG, type storage, call graph와 실행량 상한을 닫는다.
 *
 * 결과는 VM 실행 증거가 아니라 bytecode emitter와 independent verifier가 다시
 * 확인해야 하는 host analysis artifact다.
 */
RibosIrStatus ribos_ir_analyze_resources_v1(
    const RibosIrModule *module,
    RibosIrResourceClosure *closure);

/** 모든 finite `@policy` instruction/helper budget을 분석값과 대조한다. */
RibosIrStatus ribos_ir_enforce_resource_budgets_v1(
    const RibosIrModule *module,
    const RibosIrResourceClosure *closure);

/** Resource-closure table 수를 반환한다. */
RibosIrStatus ribos_ir_resource_summary(
    const RibosIrResourceClosure *closure,
    RibosIrResourceSummary *summary);

/** Stable type ID의 storage layout을 반환한다. */
const RibosIrTypeLayout *ribos_ir_resource_type_layout(
    const RibosIrResourceClosure *closure,
    uint32_t type_id);

/** Stable function ID의 resource closure를 반환한다. */
const RibosIrFunctionResource *ribos_ir_resource_function(
    const RibosIrResourceClosure *closure,
    uint32_t function_id);

/** Stable block ID의 reachability와 실행 상한을 반환한다. */
const RibosIrBlockResource *ribos_ir_resource_block(
    const RibosIrResourceClosure *closure,
    uint32_t block_id);

/** Stable loop ID의 bounded trip record를 반환한다. */
const RibosIrLoopResource *ribos_ir_resource_loop(
    const RibosIrResourceClosure *closure,
    uint32_t loop_id);

/** Stable slot ID의 frame layout을 반환한다. */
const RibosIrSlotLayout *ribos_ir_resource_slot(
    const RibosIrResourceClosure *closure,
    uint32_t slot_id);

/** Function/helper pair table의 index번째 상한을 반환한다. */
const RibosIrHelperBound *ribos_ir_resource_helper_bound(
    const RibosIrResourceClosure *closure,
    size_t index);

/** Pointer identity가 없는 deterministic resource dump를 기록한다. */
RibosIrStatus ribos_ir_dump_resources_v1(
    const RibosIrResourceClosure *closure,
    RibosWriter *output);

#ifdef __cplusplus
}
#endif

#endif
