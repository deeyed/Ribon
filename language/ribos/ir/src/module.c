#include "ir_internal.h"

#include <stdlib.h>
#include <string.h>

static int
ribos_ir_is_terminator(RibosIrOpcode opcode)
{
    return opcode == RIBOS_IR_OP_JUMP ||
        opcode == RIBOS_IR_OP_BRANCH ||
        opcode == RIBOS_IR_OP_RETURN ||
        opcode == RIBOS_IR_OP_TRAP;
}

RibosIrModule *
ribos_ir_module_create(void)
{
    RibosIrModule *module = calloc(1, sizeof(*module));

    if (module != NULL) {
        module->format_major = RIBOS_IR_V1_MAJOR;
        module->format_minor = RIBOS_IR_V1_MINOR;
    }
    return module;
}

void
ribos_ir_module_destroy(RibosIrModule *module)
{
    free(module);
}

void
ribos_ir_module_reset(RibosIrModule *module)
{
    if (module == NULL) {
        return;
    }
    memset(module, 0, sizeof(*module));
    module->format_major = RIBOS_IR_V1_MAJOR;
    module->format_minor = RIBOS_IR_V1_MINOR;
}

RibosIrStatus
ribos_ir_module_summary(
    const RibosIrModule *module,
    RibosIrSummary *summary)
{
    if (module == NULL || summary == NULL) {
        return RIBOS_IR_INVALID_ARGUMENT;
    }
    *summary = (RibosIrSummary){
        .type_count = module->type_count,
        .shape_count = module->shape_count,
        .constant_count = module->constant_count,
        .constant_byte_count = module->constant_byte_count,
        .function_count = module->function_count,
        .block_count = module->block_count,
        .loop_count = module->loop_count,
        .slot_count = module->slot_count,
        .instruction_count = module->instruction_count,
        .operand_count = module->operand_count,
        .source_map_count = module->source_map_count,
        .helper_call_count = module->helper_call_count,
    };
    return RIBOS_IR_OK;
}

RibosIrStatus
ribos_ir_module_view(
    const RibosIrModule *module,
    RibosIrModuleView *view)
{
    if (module == NULL || view == NULL) {
        return RIBOS_IR_INVALID_ARGUMENT;
    }
    *view = (RibosIrModuleView){
        .format_major = module->format_major,
        .format_minor = module->format_minor,
        .schema_digest = module->schema_digest,
        .types = module->types,
        .type_count = module->type_count,
        .shapes = module->shapes,
        .shape_count = module->shape_count,
        .constants = module->constants,
        .constant_count = module->constant_count,
        .constant_bytes = module->constant_bytes,
        .constant_byte_count = module->constant_byte_count,
        .functions = module->functions,
        .function_count = module->function_count,
        .blocks = module->blocks,
        .block_count = module->block_count,
        .loops = module->loops,
        .loop_count = module->loop_count,
        .slots = module->slots,
        .slot_count = module->slot_count,
        .instructions = module->instructions,
        .instruction_count = module->instruction_count,
        .operands = module->operands,
        .operand_count = module->operand_count,
        .source_maps = module->source_maps,
        .source_map_count = module->source_map_count,
        .helper_calls = module->helper_calls,
        .helper_call_count = module->helper_call_count,
    };
    return RIBOS_IR_OK;
}

static uint64_t
ribos_ir_hash_bytes(const uint8_t *bytes, size_t byte_count)
{
    uint64_t value = UINT64_C(1469598103934665603);
    size_t index;

    for (index = 0; index < byte_count; ++index) {
        value ^= bytes[index];
        value *= UINT64_C(1099511628211);
    }
    return value;
}

RibosIrStatus
ribos_ir_builder_add_constant(
    RibosIrModule *module,
    RibosIrConstantKind kind,
    const uint8_t *bytes,
    size_t byte_count,
    uint32_t *constant_id)
{
    RibosIrConstant value;
    size_t index;

    if (module == NULL || constant_id == NULL ||
        (byte_count != 0 && bytes == NULL) ||
        byte_count > UINT32_MAX) {
        return RIBOS_IR_INVALID_ARGUMENT;
    }
    for (index = 0; index < module->constant_count; ++index) {
        const RibosIrConstant *existing = &module->constants[index];

        if (existing->kind == kind &&
            existing->byte_length == byte_count &&
            memcmp(
                module->constant_bytes + existing->byte_offset,
                bytes,
                byte_count) == 0) {
            *constant_id = existing->id;
            return RIBOS_IR_OK;
        }
    }
    if (module->constant_count == RIBOS_IR_MAX_CONSTANTS ||
        byte_count >
            RIBOS_IR_MAX_CONSTANT_BYTES - module->constant_byte_count) {
        return RIBOS_IR_CAPACITY_EXCEEDED;
    }
    value = (RibosIrConstant){
        .id = (uint32_t)module->constant_count,
        .kind = kind,
        .byte_offset = (uint32_t)module->constant_byte_count,
        .byte_length = (uint32_t)byte_count,
        .stable_hash = ribos_ir_hash_bytes(bytes, byte_count),
    };
    if (byte_count != 0) {
        memcpy(
            module->constant_bytes + module->constant_byte_count,
            bytes,
            byte_count);
    }
    module->constant_byte_count += byte_count;
    module->constants[module->constant_count++] = value;
    *constant_id = value.id;
    return RIBOS_IR_OK;
}

RibosIrStatus
ribos_ir_builder_set_schema_identity(
    RibosIrModule *module,
    const uint8_t digest[RIBOS_SCHEMA_DIGEST_BYTES])
{
    if (module == NULL || digest == NULL) {
        return RIBOS_IR_INVALID_ARGUMENT;
    }
    memcpy(module->schema_digest, digest, RIBOS_SCHEMA_DIGEST_BYTES);
    return RIBOS_IR_OK;
}

RibosIrStatus
ribos_ir_builder_add_type(
    RibosIrModule *module,
    const RibosIrType *type,
    uint32_t *type_id)
{
    RibosIrType value;

    if (module == NULL || type == NULL || type_id == NULL) {
        return RIBOS_IR_INVALID_ARGUMENT;
    }
    if (module->type_count == RIBOS_IR_MAX_TYPES) {
        return RIBOS_IR_CAPACITY_EXCEEDED;
    }
    value = *type;
    value.id = (uint32_t)module->type_count;
    module->types[module->type_count++] = value;
    *type_id = value.id;
    return RIBOS_IR_OK;
}

RibosIrStatus
ribos_ir_builder_add_shape(
    RibosIrModule *module,
    const RibosIrShape *shape,
    uint32_t *shape_id)
{
    RibosIrShape value;

    if (module == NULL || shape == NULL || shape_id == NULL ||
        shape->owner_type >= module->type_count ||
        (shape->kind != RIBOS_IR_SHAPE_ENUM_VARIANT &&
         shape->value_type >= module->type_count)) {
        return RIBOS_IR_INVALID_ARGUMENT;
    }
    if (module->shape_count == RIBOS_IR_MAX_SHAPES) {
        return RIBOS_IR_CAPACITY_EXCEEDED;
    }
    value = *shape;
    value.id = (uint32_t)module->shape_count;
    module->shapes[module->shape_count++] = value;
    *shape_id = value.id;
    return RIBOS_IR_OK;
}

RibosIrStatus
ribos_ir_builder_add_function(
    RibosIrModule *module,
    const RibosIrFunction *function,
    uint32_t *function_id)
{
    RibosIrFunction value;

    if (module == NULL || function == NULL || function_id == NULL) {
        return RIBOS_IR_INVALID_ARGUMENT;
    }
    if (module->function_count == RIBOS_IR_MAX_FUNCTIONS) {
        return RIBOS_IR_CAPACITY_EXCEEDED;
    }
    value = *function;
    value.id = (uint32_t)module->function_count;
    module->functions[module->function_count++] = value;
    *function_id = value.id;
    return RIBOS_IR_OK;
}

RibosIrStatus
ribos_ir_builder_update_function(
    RibosIrModule *module,
    const RibosIrFunction *function)
{
    if (module == NULL || function == NULL ||
        function->id >= module->function_count) {
        return RIBOS_IR_INVALID_ARGUMENT;
    }
    module->functions[function->id] = *function;
    return RIBOS_IR_OK;
}

RibosIrStatus
ribos_ir_builder_add_block(
    RibosIrModule *module,
    const RibosIrBlock *block,
    uint32_t *block_id)
{
    RibosIrBlock value;

    if (module == NULL || block == NULL || block_id == NULL ||
        block->function_id >= module->function_count) {
        return RIBOS_IR_INVALID_ARGUMENT;
    }
    if (module->block_count == RIBOS_IR_MAX_BLOCKS) {
        return RIBOS_IR_CAPACITY_EXCEEDED;
    }
    value = *block;
    value.id = (uint32_t)module->block_count;
    value.first_instruction = RIBOS_IR_INVALID_ID;
    value.last_instruction = RIBOS_IR_INVALID_ID;
    value.instruction_count = 0;
    module->blocks[module->block_count++] = value;
    *block_id = value.id;
    return RIBOS_IR_OK;
}

RibosIrStatus
ribos_ir_builder_update_block(
    RibosIrModule *module,
    const RibosIrBlock *block)
{
    RibosIrBlock *existing;

    if (module == NULL || block == NULL ||
        block->id >= module->block_count) {
        return RIBOS_IR_INVALID_ARGUMENT;
    }
    existing = &module->blocks[block->id];
    if (block->first_instruction != existing->first_instruction ||
        block->last_instruction != existing->last_instruction ||
        block->instruction_count != existing->instruction_count) {
        return RIBOS_IR_INVALID_ARGUMENT;
    }
    *existing = *block;
    return RIBOS_IR_OK;
}

RibosIrStatus
ribos_ir_builder_add_loop(
    RibosIrModule *module,
    const RibosIrLoop *loop,
    uint32_t *loop_id)
{
    RibosIrLoop value;

    if (module == NULL || loop == NULL || loop_id == NULL ||
        loop->function_id >= module->function_count ||
        loop->header_block >= module->block_count ||
        loop->body_block >= module->block_count ||
        loop->exit_block >= module->block_count ||
        (loop->latch_block != RIBOS_IR_INVALID_ID &&
         loop->latch_block >= module->block_count) ||
        loop->trip_count == 0 ||
        loop->source_map_id >= module->source_map_count) {
        return RIBOS_IR_INVALID_ARGUMENT;
    }
    if (module->loop_count == RIBOS_IR_MAX_LOOPS) {
        return RIBOS_IR_CAPACITY_EXCEEDED;
    }
    value = *loop;
    value.id = (uint32_t)module->loop_count;
    module->loops[module->loop_count++] = value;
    *loop_id = value.id;
    return RIBOS_IR_OK;
}

RibosIrStatus
ribos_ir_builder_add_slot(
    RibosIrModule *module,
    const RibosIrSlot *slot,
    uint32_t *slot_id)
{
    RibosIrSlot value;

    if (module == NULL || slot == NULL || slot_id == NULL ||
        slot->function_id >= module->function_count ||
        slot->type_id >= module->type_count) {
        return RIBOS_IR_INVALID_ARGUMENT;
    }
    if (module->slot_count == RIBOS_IR_MAX_SLOTS) {
        return RIBOS_IR_CAPACITY_EXCEEDED;
    }
    value = *slot;
    value.id = (uint32_t)module->slot_count;
    module->slots[module->slot_count++] = value;
    *slot_id = value.id;
    return RIBOS_IR_OK;
}

RibosIrStatus
ribos_ir_builder_add_source_map(
    RibosIrModule *module,
    const RibosIrSourceMap *source_map,
    uint32_t *source_map_id)
{
    RibosIrSourceMap value;

    if (module == NULL || source_map == NULL ||
        source_map_id == NULL) {
        return RIBOS_IR_INVALID_ARGUMENT;
    }
    if (module->source_map_count == RIBOS_IR_MAX_SOURCE_MAPS) {
        return RIBOS_IR_CAPACITY_EXCEEDED;
    }
    value = *source_map;
    value.id = (uint32_t)module->source_map_count;
    module->source_maps[module->source_map_count++] = value;
    *source_map_id = value.id;
    return RIBOS_IR_OK;
}

RibosIrStatus
ribos_ir_builder_add_instruction(
    RibosIrModule *module,
    const RibosIrInstruction *instruction,
    const uint32_t *operands,
    size_t operand_count,
    uint32_t *instruction_id)
{
    RibosIrInstruction value;
    RibosIrBlock *block;
    size_t index;

    if (module == NULL || instruction == NULL ||
        instruction_id == NULL ||
        instruction->block_id >= module->block_count ||
        (instruction->result_slot != RIBOS_IR_INVALID_ID &&
         instruction->result_slot >= module->slot_count) ||
        (operand_count != 0 && operands == NULL)) {
        return RIBOS_IR_INVALID_ARGUMENT;
    }
    if (module->instruction_count == RIBOS_IR_MAX_INSTRUCTIONS ||
        operand_count > RIBOS_IR_MAX_OPERANDS - module->operand_count) {
        return RIBOS_IR_CAPACITY_EXCEEDED;
    }
    for (index = 0; index < operand_count; ++index) {
        if (operands[index] >= module->slot_count) {
            return RIBOS_IR_INVALID_ARGUMENT;
        }
    }
    block = &module->blocks[instruction->block_id];
    if (block->last_instruction != RIBOS_IR_INVALID_ID &&
        ribos_ir_is_terminator(
            module->instructions[block->last_instruction].opcode)) {
        return RIBOS_IR_INVALID_MODULE;
    }
    value = *instruction;
    value.id = (uint32_t)module->instruction_count;
    value.operand_start = (uint32_t)module->operand_count;
    value.operand_count = (uint32_t)operand_count;
    value.next_in_block = RIBOS_IR_INVALID_ID;
    for (index = 0; index < operand_count; ++index) {
        module->operands[module->operand_count++] = operands[index];
    }
    module->instructions[module->instruction_count++] = value;
    if (block->first_instruction == RIBOS_IR_INVALID_ID) {
        block->first_instruction = value.id;
    } else {
        module->instructions[block->last_instruction].next_in_block =
            value.id;
    }
    block->last_instruction = value.id;
    ++block->instruction_count;
    *instruction_id = value.id;
    return RIBOS_IR_OK;
}

RibosIrStatus
ribos_ir_builder_add_helper_call(
    RibosIrModule *module,
    const RibosIrHelperCallSite *call_site,
    uint32_t *call_site_id)
{
    RibosIrHelperCallSite value;

    if (module == NULL || call_site == NULL ||
        call_site_id == NULL ||
        call_site->instruction_id >= module->instruction_count) {
        return RIBOS_IR_INVALID_ARGUMENT;
    }
    if (module->helper_call_count == RIBOS_IR_MAX_HELPER_CALLS) {
        return RIBOS_IR_CAPACITY_EXCEEDED;
    }
    value = *call_site;
    value.id = (uint32_t)module->helper_call_count;
    module->helper_calls[module->helper_call_count++] = value;
    *call_site_id = value.id;
    return RIBOS_IR_OK;
}

static int
ribos_ir_variant_payload_count(
    const RibosIrModule *module,
    uint32_t type_id,
    uint32_t tag,
    uint32_t *payload_count)
{
    const RibosIrType *type;
    size_t index;
    int variant_found = 0;
    uint32_t count = 0;

    if (type_id >= module->type_count || payload_count == NULL) {
        return 0;
    }
    type = &module->types[type_id];
    if (type->kind == RIBOS_IR_TYPE_RESULT) {
        if (tag > 1) {
            return 0;
        }
        *payload_count = 1;
        return 1;
    }
    if (type->kind == RIBOS_IR_TYPE_OPTION) {
        if (tag > 1) {
            return 0;
        }
        *payload_count = tag == 0 ? 1 : 0;
        return 1;
    }
    if (type->kind != RIBOS_IR_TYPE_ENUM) {
        return 0;
    }
    for (index = type->shape_start;
         index < type->shape_start + type->shape_count;
         ++index) {
        const RibosIrShape *shape = &module->shapes[index];

        if (shape->kind == RIBOS_IR_SHAPE_ENUM_VARIANT &&
            shape->variant_tag == tag) {
            variant_found = 1;
        } else if (shape->kind == RIBOS_IR_SHAPE_ENUM_PAYLOAD &&
            shape->variant_tag == tag) {
            ++count;
        }
    }
    *payload_count = count;
    return variant_found;
}

RibosIrStatus
ribos_ir_validate_v1(const RibosIrModule *module)
{
    size_t index;
    size_t traversed_instructions = 0;
    size_t helper_instruction_count = 0;
    uint8_t zero_digest[RIBOS_SCHEMA_DIGEST_BYTES] = {0};

    if (module == NULL ||
        module->format_major != RIBOS_IR_V1_MAJOR ||
        module->format_minor != RIBOS_IR_V1_MINOR ||
        module->type_count == 0 || module->function_count == 0 ||
        memcmp(
            module->schema_digest,
            zero_digest,
            sizeof(zero_digest)) == 0) {
        return RIBOS_IR_INVALID_MODULE;
    }
    for (index = 0; index < module->type_count; ++index) {
        const RibosIrType *type = &module->types[index];
        size_t shape;

        if (type->id != index ||
            type->shape_start > module->shape_count ||
            type->shape_count >
                module->shape_count - type->shape_start ||
            ((type->kind != RIBOS_IR_TYPE_STRUCT &&
              type->kind != RIBOS_IR_TYPE_ENUM) &&
             type->shape_count != 0) ||
            ((type->kind == RIBOS_IR_TYPE_ARRAY ||
              type->kind == RIBOS_IR_TYPE_LIST ||
              type->kind == RIBOS_IR_TYPE_OPTION) &&
             type->first_type >= module->type_count) ||
            ((type->kind == RIBOS_IR_TYPE_FROZEN_MAP ||
              type->kind == RIBOS_IR_TYPE_DICT ||
              type->kind == RIBOS_IR_TYPE_RESULT) &&
             (type->first_type >= module->type_count ||
              type->second_type >= module->type_count)) ||
            (type->kind == RIBOS_IR_TYPE_NAMED &&
             (type->abi_size == 0 ||
              type->abi_alignment == 0 ||
              type->abi_alignment > 8 ||
              (type->abi_alignment &
               (type->abi_alignment - 1)) != 0)) ||
            (type->kind != RIBOS_IR_TYPE_NAMED &&
             (type->abi_size != 0 ||
              type->abi_alignment != 0))) {
            return RIBOS_IR_INVALID_MODULE;
        }
        for (shape = type->shape_start;
             shape < type->shape_start + type->shape_count;
             ++shape) {
            if (module->shapes[shape].owner_type != type->id) {
                return RIBOS_IR_INVALID_MODULE;
            }
        }
    }
    for (index = 0; index < module->shape_count; ++index) {
        const RibosIrShape *shape = &module->shapes[index];
        const RibosIrType *owner;

        if (shape->id != index ||
            shape->owner_type >= module->type_count ||
            (shape->kind != RIBOS_IR_SHAPE_ENUM_VARIANT &&
             shape->value_type >= module->type_count)) {
            return RIBOS_IR_INVALID_MODULE;
        }
        owner = &module->types[shape->owner_type];
        if (index < owner->shape_start ||
            index >= owner->shape_start + owner->shape_count ||
            (shape->kind == RIBOS_IR_SHAPE_STRUCT_FIELD &&
             owner->kind != RIBOS_IR_TYPE_STRUCT) ||
            (shape->kind != RIBOS_IR_SHAPE_STRUCT_FIELD &&
             owner->kind != RIBOS_IR_TYPE_ENUM)) {
            return RIBOS_IR_INVALID_MODULE;
        }
    }
    for (index = 0; index < module->constant_count; ++index) {
        const RibosIrConstant *constant = &module->constants[index];

        if (constant->id != index ||
            constant->byte_offset > module->constant_byte_count ||
            constant->byte_length >
                module->constant_byte_count - constant->byte_offset) {
            return RIBOS_IR_INVALID_MODULE;
        }
    }
    for (index = 0; index < module->function_count; ++index) {
        const RibosIrFunction *function = &module->functions[index];
        size_t owned;

        if (function->id != index ||
            function->entry_block >= module->block_count ||
            function->first_block > module->block_count ||
            function->block_count >
                module->block_count - function->first_block ||
            function->first_slot > module->slot_count ||
            function->slot_count >
                module->slot_count - function->first_slot ||
            function->parameter_count > function->slot_count ||
            function->parameter_start != function->first_slot ||
            function->return_type >= module->type_count) {
            return RIBOS_IR_INVALID_MODULE;
        }
        if (function->entry_block < function->first_block ||
            function->entry_block >=
                function->first_block + function->block_count) {
            return RIBOS_IR_INVALID_MODULE;
        }
        for (owned = 0; owned < function->block_count; ++owned) {
            if (module->blocks[function->first_block + owned].function_id !=
                function->id) {
                return RIBOS_IR_INVALID_MODULE;
            }
        }
        for (owned = 0; owned < function->slot_count; ++owned) {
            if (module->slots[function->first_slot + owned].function_id !=
                function->id) {
                return RIBOS_IR_INVALID_MODULE;
            }
        }
    }
    for (index = 0; index < module->slot_count; ++index) {
        const RibosIrSlot *slot = &module->slots[index];

        if (slot->id != index ||
            slot->function_id >= module->function_count ||
            slot->type_id >= module->type_count ||
            slot->source_map_id >= module->source_map_count) {
            return RIBOS_IR_INVALID_MODULE;
        }
    }
    for (index = 0; index < module->source_map_count; ++index) {
        const RibosIrSourceMap *source_map = &module->source_maps[index];

        if (source_map->id != index ||
            source_map->start_byte > source_map->end_byte ||
            source_map->start_line == 0 ||
            source_map->start_column == 0 ||
            source_map->end_line == 0 ||
            source_map->end_column == 0) {
            return RIBOS_IR_INVALID_MODULE;
        }
    }
    for (index = 0; index < module->loop_count; ++index) {
        const RibosIrLoop *loop = &module->loops[index];
        const RibosIrBlock *header;
        const RibosIrInstruction *branch;
        size_t duplicate;

        if (loop->id != index ||
            loop->function_id >= module->function_count ||
            loop->header_block >= module->block_count ||
            loop->body_block >= module->block_count ||
            loop->exit_block >= module->block_count ||
            (loop->latch_block != RIBOS_IR_INVALID_ID &&
             loop->latch_block >= module->block_count) ||
            loop->trip_count == 0 ||
            loop->source_map_id >= module->source_map_count ||
            module->blocks[loop->header_block].function_id !=
                loop->function_id ||
            module->blocks[loop->body_block].function_id !=
                loop->function_id ||
            module->blocks[loop->exit_block].function_id !=
                loop->function_id ||
            (loop->latch_block != RIBOS_IR_INVALID_ID &&
             module->blocks[loop->latch_block].function_id !=
                loop->function_id)) {
            return RIBOS_IR_INVALID_MODULE;
        }
        for (duplicate = 0; duplicate < index; ++duplicate) {
            if (module->loops[duplicate].header_block ==
                loop->header_block) {
                return RIBOS_IR_INVALID_MODULE;
            }
        }
        header = &module->blocks[loop->header_block];
        if (header->last_instruction >= module->instruction_count) {
            return RIBOS_IR_INVALID_MODULE;
        }
        branch = &module->instructions[header->last_instruction];
        if (branch->opcode != RIBOS_IR_OP_BRANCH ||
            branch->target != loop->body_block ||
            branch->alternate != loop->exit_block ||
            branch->immediate != loop->trip_count) {
            return RIBOS_IR_INVALID_MODULE;
        }
        if (loop->latch_block != RIBOS_IR_INVALID_ID) {
            const RibosIrBlock *latch =
                &module->blocks[loop->latch_block];
            const RibosIrInstruction *jump;

            if (latch->last_instruction >= module->instruction_count) {
                return RIBOS_IR_INVALID_MODULE;
            }
            jump = &module->instructions[latch->last_instruction];
            if (jump->opcode != RIBOS_IR_OP_JUMP ||
                jump->target != loop->header_block) {
                return RIBOS_IR_INVALID_MODULE;
            }
        }
    }
    for (index = 0; index < module->block_count; ++index) {
        const RibosIrBlock *block = &module->blocks[index];
        uint32_t instruction = block->first_instruction;
        uint32_t traversed = 0;

        if (block->id != index ||
            block->function_id >= module->function_count ||
            block->instruction_count == 0 ||
            block->last_instruction >= module->instruction_count ||
            (block->parameter_count != 0 &&
             (block->parameter_start >= module->slot_count ||
              block->parameter_count >
                  module->slot_count - block->parameter_start))) {
            return RIBOS_IR_INVALID_MODULE;
        }
        while (instruction != RIBOS_IR_INVALID_ID) {
            const RibosIrInstruction *value;
            size_t operand;

            if (instruction >= module->instruction_count ||
                traversed++ >= block->instruction_count) {
                return RIBOS_IR_INVALID_MODULE;
            }
            value = &module->instructions[instruction];
            if (value->id != instruction ||
                value->block_id != block->id ||
                value->operand_start > module->operand_count ||
                value->operand_count >
                    module->operand_count - value->operand_start ||
                value->source_map_id >= module->source_map_count ||
                (value->result_slot != RIBOS_IR_INVALID_ID &&
                 (value->result_slot >= module->slot_count ||
                  module->slots[value->result_slot].function_id !=
                      block->function_id))) {
                return RIBOS_IR_INVALID_MODULE;
            }
            for (operand = 0; operand < value->operand_count; ++operand) {
                uint32_t slot =
                    module->operands[value->operand_start + operand];

                if (slot >= module->slot_count ||
                    module->slots[slot].function_id != block->function_id) {
                    return RIBOS_IR_INVALID_MODULE;
                }
            }
            if ((value->opcode == RIBOS_IR_OP_JUMP &&
                 (value->target >= module->block_count ||
                  module->blocks[value->target].function_id !=
                      block->function_id)) ||
                (value->opcode == RIBOS_IR_OP_BRANCH &&
                 (value->target >= module->block_count ||
                  value->alternate >= module->block_count ||
                  module->blocks[value->target].function_id !=
                      block->function_id ||
                  module->blocks[value->alternate].function_id !=
                      block->function_id)) ||
                (value->opcode == RIBOS_IR_OP_CALL_DIRECT &&
                 value->target >= module->function_count) ||
                ((value->opcode == RIBOS_IR_OP_CONST_STRING ||
                  value->opcode == RIBOS_IR_OP_CONST_SYMBOL ||
                  value->opcode == RIBOS_IR_OP_MEMBER) &&
                 value->target >= module->constant_count) ||
                (value->opcode == RIBOS_IR_OP_BUILD_STRUCT &&
                 value->target >= module->type_count)) {
                return RIBOS_IR_INVALID_MODULE;
            }
            if (value->opcode == RIBOS_IR_OP_MOVE) {
                if (value->result_slot == RIBOS_IR_INVALID_ID ||
                    value->operand_count != 1 ||
                    module->slots[value->result_slot].type_id !=
                        module->slots[
                            module->operands[value->operand_start]].type_id) {
                    return RIBOS_IR_INVALID_MODULE;
                }
            } else if (value->opcode == RIBOS_IR_OP_CHECKED_UNARY ||
                value->opcode == RIBOS_IR_OP_CHECKED_BINARY) {
                size_t expected_operands =
                    value->opcode == RIBOS_IR_OP_CHECKED_UNARY ? 1 : 2;

                if (value->result_slot == RIBOS_IR_INVALID_ID ||
                    value->operand_count != expected_operands ||
                    value->target < RIBOS_IR_CHECK_NOT ||
                    value->target > RIBOS_IR_CHECK_BIT_NOT) {
                    return RIBOS_IR_INVALID_MODULE;
                }
            } else if (value->opcode == RIBOS_IR_OP_BUILD_STRUCT) {
                const RibosIrType *type;

                if (value->result_slot == RIBOS_IR_INVALID_ID ||
                    value->target >= module->type_count) {
                    return RIBOS_IR_INVALID_MODULE;
                }
                type = &module->types[value->target];
                if (type->kind != RIBOS_IR_TYPE_STRUCT ||
                    module->slots[value->result_slot].type_id !=
                        value->target ||
                    value->operand_count != type->shape_count) {
                    return RIBOS_IR_INVALID_MODULE;
                }
            } else if (value->opcode == RIBOS_IR_OP_BUILD_VARIANT) {
                uint32_t result_type;
                uint32_t payload_count;

                if (value->result_slot == RIBOS_IR_INVALID_ID) {
                    return RIBOS_IR_INVALID_MODULE;
                }
                result_type =
                    module->slots[value->result_slot].type_id;
                if (!ribos_ir_variant_payload_count(
                        module,
                        result_type,
                        value->target,
                        &payload_count) ||
                    value->operand_count != payload_count) {
                    return RIBOS_IR_INVALID_MODULE;
                }
            } else if (value->opcode == RIBOS_IR_OP_CALL_DIRECT) {
                if (value->result_slot == RIBOS_IR_INVALID_ID ||
                    module->slots[value->result_slot].type_id !=
                        module->functions[value->target].return_type) {
                    return RIBOS_IR_INVALID_MODULE;
                }
            } else if (value->opcode == RIBOS_IR_OP_BRANCH) {
                if (value->result_slot != RIBOS_IR_INVALID_ID ||
                    value->operand_count != 1 ||
                    module->types[
                        module->slots[
                            module->operands[value->operand_start]].type_id
                    ].kind != RIBOS_IR_TYPE_BOOL) {
                    return RIBOS_IR_INVALID_MODULE;
                }
            } else if (value->opcode == RIBOS_IR_OP_JUMP ||
                value->opcode == RIBOS_IR_OP_TRAP) {
                if (value->result_slot != RIBOS_IR_INVALID_ID ||
                    value->operand_count != 0) {
                    return RIBOS_IR_INVALID_MODULE;
                }
            } else if (value->opcode == RIBOS_IR_OP_RETURN) {
                uint32_t return_type =
                    module->functions[block->function_id].return_type;

                if (value->result_slot != RIBOS_IR_INVALID_ID ||
                    value->operand_count != 1 ||
                    module->slots[
                        module->operands[value->operand_start]].type_id !=
                        return_type) {
                    return RIBOS_IR_INVALID_MODULE;
                }
            }
            if ((size_t)value->opcode >
                (size_t)RIBOS_IR_OP_TRAP) {
                return RIBOS_IR_INVALID_MODULE;
            }
            if (value->opcode == RIBOS_IR_OP_CALL_HELPER) {
                if (value->target == 0) {
                    return RIBOS_IR_INVALID_MODULE;
                }
                ++helper_instruction_count;
            }
            instruction = value->next_in_block;
        }
        traversed_instructions += traversed;
        if (traversed != block->instruction_count ||
            !ribos_ir_is_terminator(
                module->instructions[block->last_instruction].opcode)) {
            return RIBOS_IR_INVALID_MODULE;
        }
    }
    if (traversed_instructions != module->instruction_count ||
        helper_instruction_count != module->helper_call_count) {
        return RIBOS_IR_INVALID_MODULE;
    }
    for (index = 0; index < module->helper_call_count; ++index) {
        const RibosIrHelperCallSite *site = &module->helper_calls[index];
        const RibosIrInstruction *instruction;

        if (site->id != index ||
            site->instruction_id >= module->instruction_count ||
            (index != 0 &&
             site->instruction_id <=
                module->helper_calls[index - 1].instruction_id)) {
            return RIBOS_IR_INVALID_MODULE;
        }
        instruction = &module->instructions[site->instruction_id];
        if (instruction->opcode != RIBOS_IR_OP_CALL_HELPER ||
            instruction->target != site->helper_stable_id ||
            instruction->operand_count != site->argument_count ||
            instruction->source_map_id != site->source_map_id ||
            instruction->result_slot == RIBOS_IR_INVALID_ID ||
            module->slots[instruction->result_slot].type_id !=
                site->result_type) {
            return RIBOS_IR_INVALID_MODULE;
        }
    }
    return RIBOS_IR_OK;
}

const char *
ribos_ir_opcode_name(RibosIrOpcode opcode)
{
    static const char *const names[] = {
        "parameter", "const-unit", "const-bool", "const-integer",
        "const-string", "const-symbol", "move", "checked-unary",
        "checked-binary", "build-list", "build-map", "build-struct",
        "build-variant", "member", "index", "collection-length",
        "variant-tag", "variant-payload", "call-direct", "call-helper",
        "jump", "branch", "return", "trap",
    };

    if ((size_t)opcode >= sizeof(names) / sizeof(names[0])) {
        return "invalid";
    }
    return names[opcode];
}
