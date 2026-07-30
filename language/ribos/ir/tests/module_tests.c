#include "ribos/host/allocator.h"
#include "ribos/ir/builder.h"

#include <stdio.h>

static RibosIrModule *
build_minimal_module(int use_unindexed_helper)
{
    RibosIrModule *module =
        ribos_ir_module_create(ribos_host_allocator());
    uint8_t schema_digest[RIBOS_SCHEMA_DIGEST_BYTES] = {1};
    RibosIrType unit_type = {
        .kind = RIBOS_IR_TYPE_UNIT,
    };
    RibosIrFunction function = {
        .return_type = 0,
        .parameter_start = 0,
    };
    RibosIrSourceMap source_map = {
        .ast_node_id = 1,
        .start_byte = 0,
        .end_byte = 1,
        .start_line = 1,
        .start_column = 1,
        .end_line = 1,
        .end_column = 2,
    };
    RibosIrBlock block = {
        .function_id = 0,
    };
    RibosIrSlot slot = {
        .function_id = 0,
        .type_id = 0,
        .source_map_id = 0,
    };
    RibosIrInstruction value = {
        .opcode = use_unindexed_helper ?
            RIBOS_IR_OP_CALL_HELPER : RIBOS_IR_OP_CONST_UNIT,
        .block_id = 0,
        .result_slot = 0,
        .target = use_unindexed_helper ? 1 : RIBOS_IR_INVALID_ID,
        .alternate = RIBOS_IR_INVALID_ID,
        .source_map_id = 0,
    };
    RibosIrInstruction terminal = {
        .opcode = RIBOS_IR_OP_RETURN,
        .block_id = 0,
        .result_slot = RIBOS_IR_INVALID_ID,
        .target = RIBOS_IR_INVALID_ID,
        .alternate = RIBOS_IR_INVALID_ID,
        .source_map_id = 0,
    };
    uint32_t id;
    uint32_t return_operand = 0;

    if (module == NULL ||
        ribos_ir_builder_set_schema_identity(
            module,
            schema_digest) != RIBOS_IR_OK ||
        ribos_ir_builder_add_type(
            module,
            &unit_type,
            &id) != RIBOS_IR_OK ||
        ribos_ir_builder_add_function(
            module,
            &function,
            &id) != RIBOS_IR_OK ||
        ribos_ir_builder_add_source_map(
            module,
            &source_map,
            &id) != RIBOS_IR_OK ||
        ribos_ir_builder_add_block(
            module,
            &block,
            &id) != RIBOS_IR_OK ||
        ribos_ir_builder_add_slot(
            module,
            &slot,
            &id) != RIBOS_IR_OK ||
        ribos_ir_builder_add_instruction(
            module,
            &value,
            NULL,
            0,
            &id) != RIBOS_IR_OK ||
        ribos_ir_builder_add_instruction(
            module,
            &terminal,
            &return_operand,
            1,
            &id) != RIBOS_IR_OK) {
        ribos_ir_module_destroy(module);
        return NULL;
    }
    function = (RibosIrFunction){
        .id = 0,
        .return_type = 0,
        .entry_block = 0,
        .first_block = 0,
        .block_count = 1,
        .first_slot = 0,
        .slot_count = 1,
        .parameter_start = 0,
    };
    if (ribos_ir_builder_update_function(
            module,
            &function) != RIBOS_IR_OK) {
        ribos_ir_module_destroy(module);
        return NULL;
    }
    return module;
}

int
main(void)
{
    RibosIrModule *valid = build_minimal_module(0);
    RibosIrModule *missing_helper_site = build_minimal_module(1);
    int passed = valid != NULL && missing_helper_site != NULL &&
        ribos_ir_validate_v1(valid) == RIBOS_IR_OK &&
        ribos_ir_validate_v1(missing_helper_site) ==
            RIBOS_IR_INVALID_MODULE;

    ribos_ir_module_destroy(valid);
    ribos_ir_module_destroy(missing_helper_site);
    if (!passed) {
        (void)fprintf(stderr, "RIBOS-IR-MODULE-TEST-FAIL\n");
        return 1;
    }
    (void)printf(
        "RIBOS-IR-MODULE-TEST-OK valid=1 rejected-missing-helper-site=1\n");
    return 0;
}
