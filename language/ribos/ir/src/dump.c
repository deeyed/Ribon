#include "ir_internal.h"

#include <inttypes.h>

RibosIrStatus
ribos_ir_dump_v1(const RibosIrModule *module, FILE *output)
{
    size_t index;

    if (module == NULL || output == NULL) {
        return RIBOS_IR_INVALID_ARGUMENT;
    }
    (void)fprintf(
        output,
        "IR-MODULE version=%u.%u schema=",
        module->format_major,
        module->format_minor);
    for (index = 0; index < RIBOS_SCHEMA_DIGEST_BYTES; ++index) {
        (void)fprintf(output, "%02x", module->schema_digest[index]);
    }
    (void)fprintf(
        output,
        " types=%zu shapes=%zu constants=%zu constant-bytes=%zu functions=%zu "
        "blocks=%zu loops=%zu slots=%zu instructions=%zu operands=%zu "
        "source-maps=%zu helper-calls=%zu\n",
        module->type_count,
        module->shape_count,
        module->constant_count,
        module->constant_byte_count,
        module->function_count,
        module->block_count,
        module->loop_count,
        module->slot_count,
        module->instruction_count,
        module->operand_count,
        module->source_map_count,
        module->helper_call_count);
    for (index = 0; index < module->type_count; ++index) {
        const RibosIrType *type = &module->types[index];

        (void)fprintf(
            output,
            "IR-TYPE id=%u kind=%u name=%s first=%u second=%u "
            "bound=%u shape=%u+%u abi=%u/%u bits=%u\n",
            type->id,
            (unsigned)type->kind,
            type->name,
            type->first_type,
            type->second_type,
            type->bound,
            type->shape_start,
            type->shape_count,
            type->abi_size,
            type->abi_alignment,
            type->bits);
    }
    for (index = 0; index < module->shape_count; ++index) {
        const RibosIrShape *shape = &module->shapes[index];

        (void)fprintf(
            output,
            "IR-SHAPE id=%u kind=%u owner=%u tag=%u ordinal=%u "
            "type=%u name=%s\n",
            shape->id,
            (unsigned)shape->kind,
            shape->owner_type,
            shape->variant_tag,
            shape->ordinal,
            shape->value_type,
            shape->name);
    }
    for (index = 0; index < module->constant_count; ++index) {
        const RibosIrConstant *constant = &module->constants[index];
        size_t byte;

        (void)fprintf(
            output,
            "IR-CONSTANT id=c%u kind=%u hash=%016" PRIx64 " bytes=",
            constant->id,
            (unsigned)constant->kind,
            constant->stable_hash);
        for (byte = 0; byte < constant->byte_length; ++byte) {
            (void)fprintf(
                output,
                "%02x",
                module->constant_bytes[
                    constant->byte_offset + byte]);
        }
        (void)fprintf(output, "\n");
    }
    for (index = 0; index < module->function_count; ++index) {
        const RibosIrFunction *function = &module->functions[index];

        (void)fprintf(
            output,
            "IR-FUNCTION id=%u name=%s return=%u entry=b%u "
            "blocks=%u+%u slots=%u+%u params=%u+%u "
            "declared=0x%08x required=0x%08x instruction-budget=%" PRIu64
            " helper-budget=%" PRIu64 " helper-upper=%" PRIu64
            " call-depth=%u flags=0x%x\n",
            function->id,
            function->name,
            function->return_type,
            function->entry_block,
            function->first_block,
            function->block_count,
            function->first_slot,
            function->slot_count,
            function->parameter_start,
            function->parameter_count,
            function->declared_capabilities,
            function->required_capabilities,
            function->declared_instruction_budget,
            function->declared_helper_budget,
            function->helper_call_upper_bound,
            function->maximum_call_depth,
            function->flags);
    }
    for (index = 0; index < module->block_count; ++index) {
        const RibosIrBlock *block = &module->blocks[index];
        uint32_t instruction = block->first_instruction;

        (void)fprintf(
            output,
            "IR-BLOCK id=b%u function=%u params=%u+%u instructions=%u "
            "flags=0x%x\n",
            block->id,
            block->function_id,
            block->parameter_start,
            block->parameter_count,
            block->instruction_count,
            block->flags);
        while (instruction != RIBOS_IR_INVALID_ID) {
            const RibosIrInstruction *value =
                &module->instructions[instruction];
            size_t operand;

            (void)fprintf(
                output,
                "IR-INSTRUCTION id=i%u block=b%u op=%s result=",
                value->id,
                value->block_id,
                ribos_ir_opcode_name(value->opcode));
            if (value->result_slot == RIBOS_IR_INVALID_ID) {
                (void)fprintf(output, "-");
            } else {
                (void)fprintf(output, "s%u", value->result_slot);
            }
            (void)fprintf(output, " operands=[");
            for (operand = 0; operand < value->operand_count; ++operand) {
                if (operand != 0) {
                    (void)fprintf(output, ",");
                }
                (void)fprintf(
                    output,
                    "s%u",
                    module->operands[value->operand_start + operand]);
            }
            (void)fprintf(
                output,
                "] target=%u alternate=%u immediate=%" PRIu64
                " source=m%u\n",
                value->target,
                value->alternate,
                value->immediate,
                value->source_map_id);
            instruction = value->next_in_block;
        }
    }
    for (index = 0; index < module->loop_count; ++index) {
        const RibosIrLoop *loop = &module->loops[index];

        (void)fprintf(
            output,
            "IR-LOOP id=l%u function=%u header=b%u body=b%u exit=b%u "
            "latch=",
            loop->id,
            loop->function_id,
            loop->header_block,
            loop->body_block,
            loop->exit_block);
        if (loop->latch_block == RIBOS_IR_INVALID_ID) {
            (void)fprintf(output, "-");
        } else {
            (void)fprintf(output, "b%u", loop->latch_block);
        }
        (void)fprintf(
            output,
            " trips=%u source=m%u\n",
            loop->trip_count,
            loop->source_map_id);
    }
    for (index = 0; index < module->slot_count; ++index) {
        const RibosIrSlot *slot = &module->slots[index];

        (void)fprintf(
            output,
            "IR-SLOT id=s%u function=%u type=%u source=m%u flags=0x%x\n",
            slot->id,
            slot->function_id,
            slot->type_id,
            slot->source_map_id,
            slot->flags);
    }
    for (index = 0; index < module->source_map_count; ++index) {
        const RibosIrSourceMap *map = &module->source_maps[index];

        (void)fprintf(
            output,
            "IR-SOURCE id=m%u ast=%u bytes=%zu..%zu "
            "start=%u:%u end=%u:%u\n",
            map->id,
            map->ast_node_id,
            map->start_byte,
            map->end_byte,
            map->start_line,
            map->start_column,
            map->end_line,
            map->end_column);
    }
    for (index = 0; index < module->helper_call_count; ++index) {
        const RibosIrHelperCallSite *site = &module->helper_calls[index];

        (void)fprintf(
            output,
            "IR-HELPER id=%u instruction=i%u helper=%u caps=0x%08x "
            "result=%u arguments=%u source=m%u\n",
            site->id,
            site->instruction_id,
            site->helper_stable_id,
            site->capabilities,
            site->result_type,
            site->argument_count,
            site->source_map_id);
    }
    return RIBOS_IR_OK;
}
