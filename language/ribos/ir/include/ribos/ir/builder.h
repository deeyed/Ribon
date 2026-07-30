#ifndef RIBOS_IR_BUILDER_H
#define RIBOS_IR_BUILDER_H

#include "ribos/ir/ir.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file builder.h
 * @brief Host frontend가 bounded Policy IR v1 module을 구성하는 API.
 */

/** Module에 canonical product schema identity를 봉인한다. */
RibosIrStatus ribos_ir_builder_set_schema_identity(
    RibosIrModule *module,
    const uint8_t digest[RIBOS_SCHEMA_DIGEST_BYTES]);

/** Resolved type record를 stable ID 순서로 추가한다. */
RibosIrStatus ribos_ir_builder_add_type(
    RibosIrModule *module,
    const RibosIrType *type,
    uint32_t *type_id);

/** User-defined struct field 또는 enum variant/payload shape row를 추가한다. */
RibosIrStatus ribos_ir_builder_add_shape(
    RibosIrModule *module,
    const RibosIrShape *shape,
    uint32_t *shape_id);

/** String 또는 symbol byte slice를 deterministic constant pool에 추가한다. */
RibosIrStatus ribos_ir_builder_add_constant(
    RibosIrModule *module,
    RibosIrConstantKind kind,
    const uint8_t *bytes,
    size_t byte_count,
    uint32_t *constant_id);

/** Direct function descriptor를 추가한다. */
RibosIrStatus ribos_ir_builder_add_function(
    RibosIrModule *module,
    const RibosIrFunction *function,
    uint32_t *function_id);

/** Lowering 종료 시 계산된 function range와 budget을 갱신한다. */
RibosIrStatus ribos_ir_builder_update_function(
    RibosIrModule *module,
    const RibosIrFunction *function);

/** 한 function에 explicit basic block을 추가한다. */
RibosIrStatus ribos_ir_builder_add_block(
    RibosIrModule *module,
    const RibosIrBlock *block,
    uint32_t *block_id);

/** Block parameter와 instruction range를 갱신한다. */
RibosIrStatus ribos_ir_builder_update_block(
    RibosIrModule *module,
    const RibosIrBlock *block);

/** Bounded loop의 header, body, exit, latch와 최대 trip count를 추가한다. */
RibosIrStatus ribos_ir_builder_add_loop(
    RibosIrModule *module,
    const RibosIrLoop *loop,
    uint32_t *loop_id);

/** Function-owned typed virtual slot을 추가한다. */
RibosIrStatus ribos_ir_builder_add_slot(
    RibosIrModule *module,
    const RibosIrSlot *slot,
    uint32_t *slot_id);

/** AST source range 하나를 stable source-map table에 추가한다. */
RibosIrStatus ribos_ir_builder_add_source_map(
    RibosIrModule *module,
    const RibosIrSourceMap *source_map,
    uint32_t *source_map_id);

/** Operand slice와 함께 instruction을 해당 basic block에 추가한다. */
RibosIrStatus ribos_ir_builder_add_instruction(
    RibosIrModule *module,
    const RibosIrInstruction *instruction,
    const uint32_t *operands,
    size_t operand_count,
    uint32_t *instruction_id);

/** Helper instruction과 schema stable ID를 call-site table에 결합한다. */
RibosIrStatus ribos_ir_builder_add_helper_call(
    RibosIrModule *module,
    const RibosIrHelperCallSite *call_site,
    uint32_t *call_site_id);

#ifdef __cplusplus
}
#endif

#endif
