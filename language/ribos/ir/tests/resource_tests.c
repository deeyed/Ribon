#include "ribos/host/allocator.h"
#include "ribos/ir/analysis.h"
#include "ribos/ir/builder.h"

#include <stdio.h>

static RibosIrModule *
build_linear_module(uint64_t instruction_budget)
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
        .declared_instruction_budget = instruction_budget,
        .declared_helper_budget = UINT64_MAX,
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
        .opcode = RIBOS_IR_OP_CONST_UNIT,
        .block_id = 0,
        .result_slot = 0,
        .target = RIBOS_IR_INVALID_ID,
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
        .declared_instruction_budget = instruction_budget,
        .declared_helper_budget = UINT64_MAX,
    };
    if (ribos_ir_builder_update_function(
            module,
            &function) != RIBOS_IR_OK) {
        ribos_ir_module_destroy(module);
        return NULL;
    }
    return module;
}

static RibosIrModule *
build_unbounded_cycle(void)
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
        .declared_instruction_budget = UINT64_MAX,
        .declared_helper_budget = UINT64_MAX,
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
    RibosIrInstruction first_jump = {
        .opcode = RIBOS_IR_OP_JUMP,
        .block_id = 0,
        .result_slot = RIBOS_IR_INVALID_ID,
        .target = 1,
        .alternate = RIBOS_IR_INVALID_ID,
        .source_map_id = 0,
    };
    RibosIrInstruction second_jump = {
        .opcode = RIBOS_IR_OP_JUMP,
        .block_id = 1,
        .result_slot = RIBOS_IR_INVALID_ID,
        .target = 0,
        .alternate = RIBOS_IR_INVALID_ID,
        .source_map_id = 0,
    };
    uint32_t id;

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
        ribos_ir_builder_add_block(
            module,
            &block,
            &id) != RIBOS_IR_OK ||
        ribos_ir_builder_add_instruction(
            module,
            &first_jump,
            NULL,
            0,
            &id) != RIBOS_IR_OK ||
        ribos_ir_builder_add_instruction(
            module,
            &second_jump,
            NULL,
            0,
            &id) != RIBOS_IR_OK) {
        ribos_ir_module_destroy(module);
        return NULL;
    }
    function = (RibosIrFunction){
        .id = 0,
        .return_type = 0,
        .entry_block = 0,
        .first_block = 0,
        .block_count = 2,
        .first_slot = 0,
        .parameter_start = 0,
        .declared_instruction_budget = UINT64_MAX,
        .declared_helper_budget = UINT64_MAX,
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
    RibosIrModule *within_budget = build_linear_module(2);
    RibosIrModule *over_budget = build_linear_module(1);
    RibosIrModule *cycle = build_unbounded_cycle();
    RibosIrResourceClosure *resources =
        ribos_ir_resource_closure_create(ribos_host_allocator());
    const RibosIrFunctionResource *function;
    int passed =
        within_budget != NULL && over_budget != NULL &&
        cycle != NULL && resources != NULL;

    if (passed) {
        passed =
            ribos_ir_analyze_resources_v1(
                within_budget,
                resources) == RIBOS_IR_OK;
    }
    function = passed ?
        ribos_ir_resource_function(resources, 0) : NULL;
    passed = passed && function != NULL &&
        function->instruction_upper_bound == 2 &&
        function->frame_bytes == 0 &&
        function->maximum_call_depth == 1 &&
        function->all_paths_terminal &&
        function->terminal_mask == RIBOS_IR_TERMINAL_RETURN &&
        ribos_ir_enforce_resource_budgets_v1(
            within_budget,
            resources) == RIBOS_IR_OK;
    if (passed) {
        passed =
            ribos_ir_analyze_resources_v1(
                over_budget,
                resources) == RIBOS_IR_OK &&
            ribos_ir_enforce_resource_budgets_v1(
                over_budget,
                resources) == RIBOS_IR_BUDGET_EXCEEDED;
    }
    if (passed) {
        passed =
            ribos_ir_analyze_resources_v1(
                cycle,
                resources) ==
                RIBOS_IR_UNBOUNDED_CONTROL_FLOW;
    }
    ribos_ir_resource_closure_destroy(resources);
    ribos_ir_module_destroy(within_budget);
    ribos_ir_module_destroy(over_budget);
    ribos_ir_module_destroy(cycle);
    if (!passed) {
        (void)fprintf(stderr, "RIBOS-IR-RESOURCE-TEST-FAIL\n");
        return 1;
    }
    (void)printf(
        "RIBOS-IR-RESOURCE-TEST-OK "
        "instructions=2 frame=0 cycle-rejected=1 budget-enforced=1\n");
    return 0;
}
