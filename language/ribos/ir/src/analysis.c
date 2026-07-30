#include "ribos/ir/analysis.h"

#include "ir_internal.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

typedef struct RibosPathBound {
    uint64_t stop;
    uint64_t returned;
    uint64_t trapped;
    uint8_t has_stop;
    uint8_t has_return;
    uint8_t has_trap;
} RibosPathBound;

typedef enum RibosMetricKind {
    RIBOS_METRIC_INSTRUCTION = 0,
    RIBOS_METRIC_HELPER_TOTAL,
    RIBOS_METRIC_HELPER_ID
} RibosMetricKind;

struct RibosIrResourceClosure {
    RibosIrTypeLayout *types;
    size_t type_count;
    RibosIrFunctionResource *functions;
    size_t function_count;
    RibosIrBlockResource *blocks;
    size_t block_count;
    RibosIrLoopResource *loops;
    size_t loop_count;
    RibosIrSlotLayout *slots;
    size_t slot_count;
    uint32_t *helper_ids;
    size_t helper_id_count;
    uint64_t *helper_matrix;
    RibosIrHelperBound *helper_bounds;
    size_t helper_bound_count;
    uint8_t complete;
};

typedef struct RibosPathContext {
    const RibosIrModule *module;
    const RibosIrResourceClosure *closure;
    uint32_t function_id;
    uint32_t stop_block;
    RibosMetricKind metric;
    size_t helper_index;
    uint8_t *state;
    RibosPathBound *memo;
    int failed;
} RibosPathContext;

typedef struct RibosFunctionAnalysis {
    const RibosIrModule *module;
    RibosIrResourceClosure *closure;
    uint8_t *state;
} RibosFunctionAnalysis;

static int
ribos_u64_add(uint64_t left, uint64_t right, uint64_t *result)
{
    if (result == NULL || right > UINT64_MAX - left) {
        return 0;
    }
    *result = left + right;
    return 1;
}

static int
ribos_u64_multiply(uint64_t left, uint64_t right, uint64_t *result)
{
    if (result == NULL ||
        (left != 0 && right > UINT64_MAX / left)) {
        return 0;
    }
    *result = left * right;
    return 1;
}

static int
ribos_u32_add(uint32_t left, uint32_t right, uint32_t *result)
{
    uint64_t value = (uint64_t)left + right;

    if (result == NULL || value > UINT32_MAX) {
        return 0;
    }
    *result = (uint32_t)value;
    return 1;
}

static int
ribos_u32_multiply(uint32_t left, uint32_t right, uint32_t *result)
{
    uint64_t value = (uint64_t)left * right;

    if (result == NULL || value > UINT32_MAX) {
        return 0;
    }
    *result = (uint32_t)value;
    return 1;
}

static int
ribos_align_u32(uint32_t value, uint32_t alignment, uint32_t *result)
{
    uint32_t mask;

    if (alignment == 0 ||
        (alignment & (alignment - 1)) != 0) {
        return 0;
    }
    mask = alignment - 1;
    if (value > UINT32_MAX - mask) {
        return 0;
    }
    *result = (value + mask) & ~mask;
    return 1;
}

static uint64_t
ribos_path_max(const RibosPathBound *bound)
{
    uint64_t result = 0;

    if (bound->has_return && bound->returned > result) {
        result = bound->returned;
    }
    if (bound->has_trap && bound->trapped > result) {
        result = bound->trapped;
    }
    return result;
}

static void
ribos_path_merge(RibosPathBound *target, const RibosPathBound *source)
{
    if (source->has_stop &&
        (!target->has_stop || source->stop > target->stop)) {
        target->stop = source->stop;
        target->has_stop = 1;
    }
    if (source->has_return &&
        (!target->has_return || source->returned > target->returned)) {
        target->returned = source->returned;
        target->has_return = 1;
    }
    if (source->has_trap &&
        (!target->has_trap || source->trapped > target->trapped)) {
        target->trapped = source->trapped;
        target->has_trap = 1;
    }
}

static int
ribos_path_prepend(RibosPathBound *bound, uint64_t prefix)
{
    if (bound->has_stop &&
        !ribos_u64_add(prefix, bound->stop, &bound->stop)) {
        return 0;
    }
    if (bound->has_return &&
        !ribos_u64_add(prefix, bound->returned, &bound->returned)) {
        return 0;
    }
    if (bound->has_trap &&
        !ribos_u64_add(prefix, bound->trapped, &bound->trapped)) {
        return 0;
    }
    return 1;
}

static const RibosIrLoop *
ribos_loop_for_header(const RibosIrModule *module, uint32_t block_id)
{
    size_t index;

    for (index = 0; index < module->loop_count; ++index) {
        if (module->loops[index].header_block == block_id) {
            return &module->loops[index];
        }
    }
    return NULL;
}

static int
ribos_type_layout(
    const RibosIrModule *module,
    RibosIrResourceClosure *closure,
    uint32_t type_id,
    uint8_t *state)
{
    const RibosIrType *type;
    RibosIrTypeLayout *layout;
    uint32_t size = 0;
    uint32_t alignment = 1;

    if (type_id >= module->type_count) {
        return 0;
    }
    if (state[type_id] == 2) {
        return 1;
    }
    if (state[type_id] == 1) {
        return 0;
    }
    state[type_id] = 1;
    type = &module->types[type_id];
    layout = &closure->types[type_id];
    *layout = (RibosIrTypeLayout){
        .type_id = type_id,
        .storage_kind = RIBOS_IR_STORAGE_SCALAR,
        .alignment = 1,
        .capacity = type->bound,
    };
    switch (type->kind) {
    case RIBOS_IR_TYPE_ERROR:
    case RIBOS_IR_TYPE_UNKNOWN:
    case RIBOS_IR_TYPE_UNIT:
        break;
    case RIBOS_IR_TYPE_BOOL:
        size = 1;
        break;
    case RIBOS_IR_TYPE_UNSIGNED:
    case RIBOS_IR_TYPE_SIGNED:
        if (type->bits != 8 && type->bits != 16 &&
            type->bits != 32 && type->bits != 64) {
            return 0;
        }
        size = type->bits / 8;
        alignment = size > 8 ? 8 : size;
        break;
    case RIBOS_IR_TYPE_STRING_LITERAL:
        size = 8;
        alignment = 4;
        layout->storage_kind = RIBOS_IR_STORAGE_OPAQUE;
        break;
    case RIBOS_IR_TYPE_NAMED:
        size = type->abi_size;
        alignment = type->abi_alignment;
        layout->storage_kind = RIBOS_IR_STORAGE_OPAQUE;
        break;
    case RIBOS_IR_TYPE_ARRAY:
    case RIBOS_IR_TYPE_LIST: {
        const RibosIrTypeLayout *element;
        uint32_t stride;
        uint32_t bytes;
        uint32_t payload = 0;

        if (!ribos_type_layout(
                module,
                closure,
                type->first_type,
                state)) {
            return 0;
        }
        element = &closure->types[type->first_type];
        if (!ribos_align_u32(
                element->byte_size,
                element->alignment,
                &stride)) {
            return 0;
        }
        alignment = element->alignment;
        if (type->kind == RIBOS_IR_TYPE_LIST) {
            if (alignment < 4) {
                alignment = 4;
            }
            if (!ribos_align_u32(4, element->alignment, &payload)) {
                return 0;
            }
            layout->storage_kind = RIBOS_IR_STORAGE_INLINE_LIST;
        } else {
            layout->storage_kind = RIBOS_IR_STORAGE_INLINE_ARRAY;
        }
        if (!ribos_u32_multiply(stride, type->bound, &bytes) ||
            !ribos_u32_add(payload, bytes, &size) ||
            !ribos_align_u32(size, alignment, &size)) {
            return 0;
        }
        layout->element_stride = stride;
        layout->payload_offset = payload;
        break;
    }
    case RIBOS_IR_TYPE_FROZEN_MAP:
    case RIBOS_IR_TYPE_DICT: {
        const RibosIrTypeLayout *key;
        const RibosIrTypeLayout *value;
        uint32_t value_offset;
        uint32_t entry_alignment;
        uint32_t entry_size;
        uint32_t entry_stride;
        uint32_t payload;
        uint32_t entries;

        if (!ribos_type_layout(
                module,
                closure,
                type->first_type,
                state) ||
            !ribos_type_layout(
                module,
                closure,
                type->second_type,
                state)) {
            return 0;
        }
        key = &closure->types[type->first_type];
        value = &closure->types[type->second_type];
        entry_alignment =
            key->alignment > value->alignment ?
                key->alignment : value->alignment;
        if (!ribos_align_u32(
                key->byte_size,
                value->alignment,
                &value_offset) ||
            !ribos_u32_add(
                value_offset,
                value->byte_size,
                &entry_size) ||
            !ribos_align_u32(
                entry_size,
                entry_alignment,
                &entry_stride)) {
            return 0;
        }
        alignment = entry_alignment < 4 ? 4 : entry_alignment;
        if (!ribos_align_u32(4, entry_alignment, &payload) ||
            !ribos_u32_multiply(
                entry_stride,
                type->bound,
                &entries) ||
            !ribos_u32_add(payload, entries, &size) ||
            !ribos_align_u32(size, alignment, &size)) {
            return 0;
        }
        layout->storage_kind = RIBOS_IR_STORAGE_SORTED_MAP;
        layout->element_stride = entry_stride;
        layout->payload_offset = payload;
        break;
    }
    case RIBOS_IR_TYPE_OPTION:
    case RIBOS_IR_TYPE_RESULT: {
        const RibosIrTypeLayout *first;
        const RibosIrTypeLayout *second = NULL;
        uint32_t payload_size;
        uint32_t payload;

        if (!ribos_type_layout(
                module,
                closure,
                type->first_type,
                state)) {
            return 0;
        }
        first = &closure->types[type->first_type];
        alignment = first->alignment;
        payload_size = first->byte_size;
        if (type->kind == RIBOS_IR_TYPE_RESULT) {
            if (!ribos_type_layout(
                    module,
                    closure,
                    type->second_type,
                    state)) {
                return 0;
            }
            second = &closure->types[type->second_type];
            if (second->alignment > alignment) {
                alignment = second->alignment;
            }
            if (second->byte_size > payload_size) {
                payload_size = second->byte_size;
            }
        }
        if (!ribos_align_u32(1, alignment, &payload) ||
            !ribos_u32_add(payload, payload_size, &size) ||
            !ribos_align_u32(size, alignment, &size)) {
            return 0;
        }
        layout->storage_kind = RIBOS_IR_STORAGE_TAGGED_UNION;
        layout->payload_offset = payload;
        break;
    }
    case RIBOS_IR_TYPE_STRUCT: {
        size_t shape;

        layout->storage_kind = RIBOS_IR_STORAGE_INLINE_STRUCT;
        for (shape = type->shape_start;
             shape < type->shape_start + type->shape_count;
             ++shape) {
            const RibosIrShape *field = &module->shapes[shape];
            const RibosIrTypeLayout *field_layout;

            if (field->kind != RIBOS_IR_SHAPE_STRUCT_FIELD ||
                !ribos_type_layout(
                    module,
                    closure,
                    field->value_type,
                    state)) {
                return 0;
            }
            field_layout = &closure->types[field->value_type];
            if (!ribos_align_u32(
                    size,
                    field_layout->alignment,
                    &size) ||
                !ribos_u32_add(
                    size,
                    field_layout->byte_size,
                    &size)) {
                return 0;
            }
            if (field_layout->alignment > alignment) {
                alignment = field_layout->alignment;
            }
        }
        if (!ribos_align_u32(size, alignment, &size)) {
            return 0;
        }
        break;
    }
    case RIBOS_IR_TYPE_ENUM: {
        size_t shape;
        uint32_t maximum_payload = 0;
        uint32_t maximum_alignment = 1;

        for (shape = type->shape_start;
             shape < type->shape_start + type->shape_count;
             ++shape) {
            const RibosIrShape *variant = &module->shapes[shape];
            uint32_t variant_size = 0;
            uint32_t variant_alignment = 1;
            size_t payload;

            if (variant->kind != RIBOS_IR_SHAPE_ENUM_VARIANT) {
                continue;
            }
            for (payload = type->shape_start;
                 payload < type->shape_start + type->shape_count;
                 ++payload) {
                const RibosIrShape *field = &module->shapes[payload];
                const RibosIrTypeLayout *field_layout;

                if (field->kind != RIBOS_IR_SHAPE_ENUM_PAYLOAD ||
                    field->variant_tag != variant->variant_tag) {
                    continue;
                }
                if (!ribos_type_layout(
                        module,
                        closure,
                        field->value_type,
                        state)) {
                    return 0;
                }
                field_layout = &closure->types[field->value_type];
                if (!ribos_align_u32(
                        variant_size,
                        field_layout->alignment,
                        &variant_size) ||
                    !ribos_u32_add(
                        variant_size,
                        field_layout->byte_size,
                        &variant_size)) {
                    return 0;
                }
                if (field_layout->alignment > variant_alignment) {
                    variant_alignment = field_layout->alignment;
                }
            }
            if (!ribos_align_u32(
                    variant_size,
                    variant_alignment,
                    &variant_size)) {
                return 0;
            }
            if (variant_size > maximum_payload) {
                maximum_payload = variant_size;
            }
            if (variant_alignment > maximum_alignment) {
                maximum_alignment = variant_alignment;
            }
        }
        alignment = maximum_alignment;
        if (!ribos_align_u32(
                1,
                maximum_alignment,
                &layout->payload_offset) ||
            !ribos_u32_add(
                layout->payload_offset,
                maximum_payload,
                &size) ||
            !ribos_align_u32(size, alignment, &size)) {
            return 0;
        }
        layout->storage_kind = RIBOS_IR_STORAGE_TAGGED_UNION;
        break;
    }
    default:
        return 0;
    }
    if (size > RIBOS_IR_MAX_VALUE_BYTES ||
        alignment == 0 || alignment > 8) {
        return 0;
    }
    layout->byte_size = size;
    layout->alignment = alignment;
    state[type_id] = 2;
    return 1;
}

static int
ribos_layout_all_types(
    const RibosIrModule *module,
    RibosIrResourceClosure *closure)
{
    uint8_t *state = module->type_count == 0 ? NULL :
        calloc(module->type_count, sizeof(*state));
    size_t index;
    int valid = module->type_count == 0 || state != NULL;

    for (index = 0; valid && index < module->type_count; ++index) {
        valid = ribos_type_layout(
            module,
            closure,
            (uint32_t)index,
            state);
    }
    free(state);
    return valid;
}

static int
ribos_layout_slots(
    const RibosIrModule *module,
    RibosIrResourceClosure *closure)
{
    size_t function_index;

    for (function_index = 0;
         function_index < module->function_count;
         ++function_index) {
        const RibosIrFunction *function =
            &module->functions[function_index];
        RibosIrFunctionResource *resource =
            &closure->functions[function_index];
        uint32_t offset = 0;
        uint32_t frame_alignment = 1;
        uint32_t aggregate_bytes = 0;
        size_t local;

        resource->function_id = (uint32_t)function_index;
        for (local = 0; local < function->slot_count; ++local) {
            uint32_t slot_id = function->first_slot + (uint32_t)local;
            const RibosIrSlot *slot = &module->slots[slot_id];
            const RibosIrTypeLayout *type =
                &closure->types[slot->type_id];
            RibosIrSlotLayout *layout = &closure->slots[slot_id];

            if (!ribos_align_u32(offset, type->alignment, &offset)) {
                return 0;
            }
            *layout = (RibosIrSlotLayout){
                .slot_id = slot_id,
                .function_id = (uint32_t)function_index,
                .type_id = slot->type_id,
                .frame_offset = offset,
                .byte_size = type->byte_size,
                .alignment = type->alignment,
            };
            if (!ribos_u32_add(offset, type->byte_size, &offset)) {
                return 0;
            }
            if (type->alignment > frame_alignment) {
                frame_alignment = type->alignment;
            }
            if (type->storage_kind != RIBOS_IR_STORAGE_SCALAR &&
                type->storage_kind != RIBOS_IR_STORAGE_OPAQUE &&
                !ribos_u32_add(
                    aggregate_bytes,
                    type->byte_size,
                    &aggregate_bytes)) {
                return 0;
            }
            if (type->byte_size > resource->largest_value_bytes) {
                resource->largest_value_bytes = type->byte_size;
            }
        }
        if (!ribos_align_u32(offset, frame_alignment, &offset) ||
            offset > RIBOS_IR_MAX_FRAME_BYTES) {
            return 0;
        }
        resource->frame_bytes = offset;
        resource->aggregate_slot_bytes = aggregate_bytes;
    }
    return 1;
}

static int
ribos_mark_function_reachable(
    const RibosIrModule *module,
    RibosIrResourceClosure *closure,
    uint32_t function_id)
{
    const RibosIrFunction *function = &module->functions[function_id];
    size_t stack_capacity =
        (size_t)function->block_count * 2 + 1;
    uint32_t *stack = calloc(stack_capacity, sizeof(*stack));
    size_t stack_size = 0;

    if (stack == NULL) {
        return 0;
    }
    stack[stack_size++] = function->entry_block;
    while (stack_size != 0) {
        uint32_t block_id = stack[--stack_size];
        RibosIrBlockResource *resource = &closure->blocks[block_id];
        const RibosIrBlock *block;
        const RibosIrInstruction *terminal;

        if (resource->reachable) {
            continue;
        }
        resource->reachable = 1;
        resource->execution_upper_bound = 1;
        ++closure->functions[function_id].reachable_block_count;
        block = &module->blocks[block_id];
        terminal = &module->instructions[block->last_instruction];
        if (terminal->opcode == RIBOS_IR_OP_JUMP) {
            if (stack_size == stack_capacity) {
                free(stack);
                return 0;
            }
            stack[stack_size++] = terminal->target;
        } else if (terminal->opcode == RIBOS_IR_OP_BRANCH) {
            if (stack_size > stack_capacity - 2) {
                free(stack);
                return 0;
            }
            stack[stack_size++] = terminal->target;
            stack[stack_size++] = terminal->alternate;
        }
    }
    free(stack);
    return 1;
}

static int
ribos_multiply_loop_region(
    const RibosIrModule *module,
    RibosIrResourceClosure *closure,
    const RibosIrLoop *loop)
{
    uint8_t *seen = calloc(module->block_count, sizeof(*seen));
    size_t stack_capacity = module->block_count * 2 + 1;
    uint32_t *stack = calloc(stack_capacity, sizeof(*stack));
    size_t stack_size = 0;
    uint64_t header_factor = (uint64_t)loop->trip_count + 1;

    if (seen == NULL || stack == NULL) {
        free(seen);
        free(stack);
        return 0;
    }
    if (!ribos_u64_multiply(
            closure->blocks[loop->header_block].execution_upper_bound,
            loop->latch_block == RIBOS_IR_INVALID_ID ? 1 : header_factor,
            &closure->blocks[loop->header_block].execution_upper_bound)) {
        free(seen);
        free(stack);
        return 0;
    }
    stack[stack_size++] = loop->body_block;
    while (stack_size != 0) {
        uint32_t block_id = stack[--stack_size];
        const RibosIrBlock *block;
        const RibosIrInstruction *terminal;

        if (block_id == loop->header_block ||
            block_id == loop->exit_block ||
            seen[block_id]) {
            continue;
        }
        seen[block_id] = 1;
        if (!ribos_u64_multiply(
                closure->blocks[block_id].execution_upper_bound,
                loop->latch_block == RIBOS_IR_INVALID_ID ?
                    1 : loop->trip_count,
                &closure->blocks[block_id].execution_upper_bound)) {
            free(seen);
            free(stack);
            return 0;
        }
        block = &module->blocks[block_id];
        terminal = &module->instructions[block->last_instruction];
        if (terminal->opcode == RIBOS_IR_OP_JUMP) {
            if (stack_size == stack_capacity) {
                free(seen);
                free(stack);
                return 0;
            }
            stack[stack_size++] = terminal->target;
        } else if (terminal->opcode == RIBOS_IR_OP_BRANCH) {
            if (stack_size > stack_capacity - 2) {
                free(seen);
                free(stack);
                return 0;
            }
            stack[stack_size++] = terminal->target;
            stack[stack_size++] = terminal->alternate;
        }
    }
    free(seen);
    free(stack);
    return 1;
}

static int
ribos_compare_u32(const void *left, const void *right)
{
    uint32_t a = *(const uint32_t *)left;
    uint32_t b = *(const uint32_t *)right;

    return a < b ? -1 : a > b;
}

static int
ribos_collect_helper_ids(
    const RibosIrModule *module,
    RibosIrResourceClosure *closure)
{
    size_t index;
    size_t unique = 0;

    if (module->helper_call_count == 0) {
        return 1;
    }
    closure->helper_ids = calloc(
        module->helper_call_count,
        sizeof(*closure->helper_ids));
    if (closure->helper_ids == NULL) {
        return 0;
    }
    for (index = 0; index < module->helper_call_count; ++index) {
        closure->helper_ids[index] =
            module->helper_calls[index].helper_stable_id;
    }
    qsort(
        closure->helper_ids,
        module->helper_call_count,
        sizeof(*closure->helper_ids),
        ribos_compare_u32);
    for (index = 0; index < module->helper_call_count; ++index) {
        if (unique == 0 ||
            closure->helper_ids[index] !=
                closure->helper_ids[unique - 1]) {
            closure->helper_ids[unique++] = closure->helper_ids[index];
        }
    }
    closure->helper_id_count = unique;
    if (module->function_count > SIZE_MAX / unique) {
        return 0;
    }
    closure->helper_matrix = calloc(
        module->function_count * unique,
        sizeof(*closure->helper_matrix));
    return closure->helper_matrix != NULL;
}

static uint64_t
ribos_block_weight(
    RibosPathContext *context,
    uint32_t block_id)
{
    const RibosIrBlock *block = &context->module->blocks[block_id];
    uint32_t instruction_id = block->first_instruction;
    uint64_t weight = 0;

    while (instruction_id != RIBOS_IR_INVALID_ID) {
        const RibosIrInstruction *instruction =
            &context->module->instructions[instruction_id];
        uint64_t addition = 0;

        if (context->metric == RIBOS_METRIC_INSTRUCTION) {
            addition = 1;
            if (instruction->opcode == RIBOS_IR_OP_CALL_DIRECT) {
                if (!ribos_u64_add(
                        addition,
                        context->closure->functions[
                            instruction->target
                        ].instruction_upper_bound,
                        &addition)) {
                    context->failed = 1;
                    return 0;
                }
            }
        } else if (context->metric == RIBOS_METRIC_HELPER_TOTAL) {
            if (instruction->opcode == RIBOS_IR_OP_CALL_HELPER) {
                addition = 1;
            } else if (instruction->opcode == RIBOS_IR_OP_CALL_DIRECT) {
                addition = context->closure->functions[
                    instruction->target
                ].helper_call_upper_bound;
            }
        } else if (instruction->opcode == RIBOS_IR_OP_CALL_HELPER) {
            addition = instruction->target ==
                context->closure->helper_ids[context->helper_index];
        } else if (instruction->opcode == RIBOS_IR_OP_CALL_DIRECT) {
            addition = context->closure->helper_matrix[
                (size_t)instruction->target *
                    context->closure->helper_id_count +
                context->helper_index
            ];
        }
        if (!ribos_u64_add(weight, addition, &weight)) {
            context->failed = 1;
            return 0;
        }
        instruction_id = instruction->next_in_block;
    }
    return weight;
}

static RibosPathBound ribos_analyze_node(
    RibosPathContext *context,
    uint32_t block_id);

static RibosPathBound
ribos_analyze_region(
    const RibosIrModule *module,
    const RibosIrResourceClosure *closure,
    uint32_t function_id,
    uint32_t start_block,
    uint32_t stop_block,
    RibosMetricKind metric,
    size_t helper_index,
    int *failed)
{
    RibosPathContext context = {
        .module = module,
        .closure = closure,
        .function_id = function_id,
        .stop_block = stop_block,
        .metric = metric,
        .helper_index = helper_index,
    };
    RibosPathBound result = {0};

    context.state = calloc(module->block_count, sizeof(*context.state));
    context.memo = calloc(module->block_count, sizeof(*context.memo));
    if (context.state == NULL || context.memo == NULL) {
        context.failed = 1;
    } else {
        result = ribos_analyze_node(&context, start_block);
    }
    free(context.state);
    free(context.memo);
    if (failed != NULL && context.failed) {
        *failed = 1;
    }
    return result;
}

static RibosPathBound
ribos_analyze_loop(
    RibosPathContext *context,
    const RibosIrLoop *loop)
{
    RibosPathBound result = {0};
    RibosPathBound body;
    RibosPathBound exit;
    uint64_t header_weight =
        ribos_block_weight(context, loop->header_block);
    uint64_t cycle = 0;
    uint64_t repeat_prefix = 0;
    int failed = 0;

    body = ribos_analyze_region(
        context->module,
        context->closure,
        context->function_id,
        loop->body_block,
        loop->header_block,
        context->metric,
        context->helper_index,
        &failed);
    exit = ribos_analyze_node(context, loop->exit_block);
    if (failed || context->failed) {
        context->failed = 1;
        return result;
    }
    if (body.has_stop) {
        RibosPathBound normal = exit;
        uint64_t repeated;

        if (!ribos_u64_add(
                header_weight,
                body.stop,
                &cycle) ||
            !ribos_u64_multiply(
                cycle,
                loop->trip_count,
                &repeated) ||
            !ribos_u64_add(
                repeated,
                header_weight,
                &repeated) ||
            !ribos_path_prepend(&normal, repeated)) {
            context->failed = 1;
            return result;
        }
        ribos_path_merge(&result, &normal);
        if (loop->trip_count > 0 &&
            !ribos_u64_multiply(
                cycle,
                loop->trip_count - 1,
                &repeat_prefix)) {
            context->failed = 1;
            return result;
        }
    } else {
        RibosPathBound zero_iteration = exit;

        if (!ribos_path_prepend(
                &zero_iteration,
                header_weight)) {
            context->failed = 1;
            return result;
        }
        ribos_path_merge(&result, &zero_iteration);
    }
    if (body.has_return) {
        RibosPathBound terminal = {
            .returned = body.returned,
            .has_return = 1,
        };
        uint64_t prefix;

        if (!ribos_u64_add(
                repeat_prefix,
                header_weight,
                &prefix) ||
            !ribos_path_prepend(&terminal, prefix)) {
            context->failed = 1;
            return result;
        }
        ribos_path_merge(&result, &terminal);
    }
    if (body.has_trap) {
        RibosPathBound terminal = {
            .trapped = body.trapped,
            .has_trap = 1,
        };
        uint64_t prefix;

        if (!ribos_u64_add(
                repeat_prefix,
                header_weight,
                &prefix) ||
            !ribos_path_prepend(&terminal, prefix)) {
            context->failed = 1;
            return result;
        }
        ribos_path_merge(&result, &terminal);
    }
    return result;
}

static RibosPathBound
ribos_analyze_node(RibosPathContext *context, uint32_t block_id)
{
    RibosPathBound result = {0};
    const RibosIrBlock *block;
    const RibosIrInstruction *terminal;
    const RibosIrLoop *loop;
    uint64_t weight;

    if (block_id == context->stop_block) {
        result.has_stop = 1;
        return result;
    }
    if (block_id >= context->module->block_count ||
        context->module->blocks[block_id].function_id !=
            context->function_id) {
        context->failed = 1;
        return result;
    }
    if (context->state[block_id] == 2) {
        return context->memo[block_id];
    }
    if (context->state[block_id] == 1) {
        context->failed = 1;
        return result;
    }
    context->state[block_id] = 1;
    loop = ribos_loop_for_header(context->module, block_id);
    if (loop != NULL) {
        result = ribos_analyze_loop(context, loop);
    } else {
        block = &context->module->blocks[block_id];
        terminal =
            &context->module->instructions[block->last_instruction];
        weight = ribos_block_weight(context, block_id);
        if (terminal->opcode == RIBOS_IR_OP_RETURN) {
            result.has_return = 1;
            result.returned = weight;
        } else if (terminal->opcode == RIBOS_IR_OP_TRAP) {
            result.has_trap = 1;
            result.trapped = weight;
        } else if (terminal->opcode == RIBOS_IR_OP_JUMP) {
            result = ribos_analyze_node(context, terminal->target);
            if (!ribos_path_prepend(&result, weight)) {
                context->failed = 1;
            }
        } else if (terminal->opcode == RIBOS_IR_OP_BRANCH) {
            RibosPathBound left =
                ribos_analyze_node(context, terminal->target);
            RibosPathBound right =
                ribos_analyze_node(context, terminal->alternate);

            ribos_path_merge(&result, &left);
            ribos_path_merge(&result, &right);
            if (!ribos_path_prepend(&result, weight)) {
                context->failed = 1;
            }
        } else {
            context->failed = 1;
        }
    }
    context->state[block_id] = context->failed ? 0 : 2;
    if (!context->failed) {
        context->memo[block_id] = result;
    }
    return result;
}

static int
ribos_analyze_function(
    RibosFunctionAnalysis *analysis,
    uint32_t function_id)
{
    const RibosIrModule *module = analysis->module;
    RibosIrResourceClosure *closure = analysis->closure;
    const RibosIrFunction *function = &module->functions[function_id];
    RibosIrFunctionResource *resource =
        &closure->functions[function_id];
    uint32_t maximum_depth = 1;
    uint64_t maximum_stack = resource->frame_bytes;
    uint32_t terminal_mask = 0;
    size_t block_offset;
    size_t helper_index;
    int failed = 0;
    RibosPathBound instruction;
    RibosPathBound helpers;

    if (analysis->state[function_id] == 2) {
        return 1;
    }
    if (analysis->state[function_id] == 1) {
        return 0;
    }
    analysis->state[function_id] = 1;
    for (block_offset = 0;
         block_offset < function->block_count;
         ++block_offset) {
        uint32_t block_id =
            function->first_block + (uint32_t)block_offset;
        const RibosIrBlock *block;
        uint32_t instruction_id;

        if (!closure->blocks[block_id].reachable) {
            continue;
        }
        block = &module->blocks[block_id];
        instruction_id = block->first_instruction;
        while (instruction_id != RIBOS_IR_INVALID_ID) {
            const RibosIrInstruction *value =
                &module->instructions[instruction_id];

            if (value->opcode == RIBOS_IR_OP_CALL_DIRECT) {
                const RibosIrFunctionResource *callee;
                uint32_t depth;
                uint64_t stack;

                if (!ribos_analyze_function(
                        analysis,
                        value->target)) {
                    return 0;
                }
                callee = &closure->functions[value->target];
                if (!ribos_u32_add(
                        callee->maximum_call_depth,
                        1,
                        &depth) ||
                    !ribos_u64_add(
                        resource->frame_bytes,
                        callee->maximum_stack_bytes,
                        &stack)) {
                    return 0;
                }
                if (depth > maximum_depth) {
                    maximum_depth = depth;
                }
                if (stack > maximum_stack) {
                    maximum_stack = stack;
                }
                if ((callee->terminal_mask &
                     RIBOS_IR_TERMINAL_TRAP) != 0) {
                    terminal_mask |= RIBOS_IR_TERMINAL_TRAP;
                }
            }
            instruction_id = value->next_in_block;
        }
    }
    instruction = ribos_analyze_region(
        module,
        closure,
        function_id,
        function->entry_block,
        RIBOS_IR_INVALID_ID,
        RIBOS_METRIC_INSTRUCTION,
        0,
        &failed);
    helpers = ribos_analyze_region(
        module,
        closure,
        function_id,
        function->entry_block,
        RIBOS_IR_INVALID_ID,
        RIBOS_METRIC_HELPER_TOTAL,
        0,
        &failed);
    if (failed || instruction.has_stop || helpers.has_stop ||
        (!instruction.has_return && !instruction.has_trap)) {
        return 0;
    }
    resource->instruction_upper_bound =
        ribos_path_max(&instruction);
    resource->helper_call_upper_bound =
        ribos_path_max(&helpers);
    if (instruction.has_return) {
        terminal_mask |= RIBOS_IR_TERMINAL_RETURN;
    }
    if (instruction.has_trap) {
        terminal_mask |= RIBOS_IR_TERMINAL_TRAP;
    }
    resource->terminal_mask = terminal_mask;
    resource->maximum_call_depth = maximum_depth;
    resource->maximum_stack_bytes = maximum_stack;
    resource->all_paths_terminal = 1;
    resource->instruction_budget_satisfied =
        function->declared_instruction_budget == UINT64_MAX ||
        resource->instruction_upper_bound <=
            function->declared_instruction_budget;
    resource->helper_budget_satisfied =
        function->declared_helper_budget == UINT64_MAX ||
        resource->helper_call_upper_bound <=
            function->declared_helper_budget;
    if (maximum_stack > RIBOS_IR_MAX_STACK_BYTES) {
        return 0;
    }
    for (helper_index = 0;
         helper_index < closure->helper_id_count;
         ++helper_index) {
        RibosPathBound helper = ribos_analyze_region(
            module,
            closure,
            function_id,
            function->entry_block,
            RIBOS_IR_INVALID_ID,
            RIBOS_METRIC_HELPER_ID,
            helper_index,
            &failed);

        if (failed || helper.has_stop) {
            return 0;
        }
        closure->helper_matrix[
            (size_t)function_id * closure->helper_id_count +
            helper_index
        ] = ribos_path_max(&helper);
    }
    analysis->state[function_id] = 2;
    return 1;
}

RibosIrResourceClosure *
ribos_ir_resource_closure_create(void)
{
    return calloc(1, sizeof(RibosIrResourceClosure));
}

void
ribos_ir_resource_closure_reset(RibosIrResourceClosure *closure)
{
    if (closure == NULL) {
        return;
    }
    free(closure->types);
    free(closure->functions);
    free(closure->blocks);
    free(closure->loops);
    free(closure->slots);
    free(closure->helper_ids);
    free(closure->helper_matrix);
    free(closure->helper_bounds);
    memset(closure, 0, sizeof(*closure));
}

void
ribos_ir_resource_closure_destroy(RibosIrResourceClosure *closure)
{
    ribos_ir_resource_closure_reset(closure);
    free(closure);
}

RibosIrStatus
ribos_ir_analyze_resources_v1(
    const RibosIrModule *module,
    RibosIrResourceClosure *closure)
{
    RibosFunctionAnalysis function_analysis;
    uint8_t *function_state = NULL;
    size_t index;
    size_t helper_rows = 0;

    if (module == NULL || closure == NULL) {
        return RIBOS_IR_INVALID_ARGUMENT;
    }
    ribos_ir_resource_closure_reset(closure);
    if (ribos_ir_validate_v1(module) != RIBOS_IR_OK) {
        return RIBOS_IR_INVALID_MODULE;
    }
    closure->types = module->type_count == 0 ? NULL :
        calloc(module->type_count, sizeof(*closure->types));
    closure->functions = module->function_count == 0 ? NULL :
        calloc(module->function_count, sizeof(*closure->functions));
    closure->blocks = module->block_count == 0 ? NULL :
        calloc(module->block_count, sizeof(*closure->blocks));
    closure->loops = module->loop_count == 0 ? NULL :
        calloc(module->loop_count, sizeof(*closure->loops));
    closure->slots = module->slot_count == 0 ? NULL :
        calloc(module->slot_count, sizeof(*closure->slots));
    if ((module->type_count != 0 && closure->types == NULL) ||
        (module->function_count != 0 && closure->functions == NULL) ||
        (module->block_count != 0 && closure->blocks == NULL) ||
        (module->loop_count != 0 && closure->loops == NULL) ||
        (module->slot_count != 0 && closure->slots == NULL)) {
        ribos_ir_resource_closure_reset(closure);
        return RIBOS_IR_RESOURCE_EXCEEDED;
    }
    closure->type_count = module->type_count;
    closure->function_count = module->function_count;
    closure->block_count = module->block_count;
    closure->loop_count = module->loop_count;
    closure->slot_count = module->slot_count;
    for (index = 0; index < module->block_count; ++index) {
        closure->blocks[index].block_id = (uint32_t)index;
        closure->blocks[index].function_id =
            module->blocks[index].function_id;
    }
    if (!ribos_layout_all_types(module, closure) ||
        !ribos_layout_slots(module, closure)) {
        ribos_ir_resource_closure_reset(closure);
        return RIBOS_IR_RESOURCE_EXCEEDED;
    }
    for (index = 0; index < module->function_count; ++index) {
        if (!ribos_mark_function_reachable(
                module,
                closure,
                (uint32_t)index)) {
            ribos_ir_resource_closure_reset(closure);
            return RIBOS_IR_RESOURCE_EXCEEDED;
        }
    }
    for (index = 0; index < module->loop_count; ++index) {
        const RibosIrLoop *loop = &module->loops[index];
        RibosIrLoopResource *resource = &closure->loops[index];

        *resource = (RibosIrLoopResource){
            .loop_id = (uint32_t)index,
            .function_id = loop->function_id,
            .header_block = loop->header_block,
            .body_block = loop->body_block,
            .exit_block = loop->exit_block,
            .latch_block = loop->latch_block,
            .trip_count = loop->trip_count,
            .reachable = closure->blocks[loop->header_block].reachable,
        };
        if (resource->reachable &&
            !ribos_multiply_loop_region(module, closure, loop)) {
            ribos_ir_resource_closure_reset(closure);
            return RIBOS_IR_RESOURCE_EXCEEDED;
        }
    }
    if (!ribos_collect_helper_ids(module, closure)) {
        ribos_ir_resource_closure_reset(closure);
        return RIBOS_IR_RESOURCE_EXCEEDED;
    }
    function_state = module->function_count == 0 ? NULL :
        calloc(module->function_count, sizeof(*function_state));
    if (module->function_count != 0 && function_state == NULL) {
        ribos_ir_resource_closure_reset(closure);
        return RIBOS_IR_RESOURCE_EXCEEDED;
    }
    function_analysis = (RibosFunctionAnalysis){
        .module = module,
        .closure = closure,
        .state = function_state,
    };
    for (index = 0; index < module->function_count; ++index) {
        if (!ribos_analyze_function(
                &function_analysis,
                (uint32_t)index)) {
            free(function_state);
            ribos_ir_resource_closure_reset(closure);
            return RIBOS_IR_UNBOUNDED_CONTROL_FLOW;
        }
    }
    free(function_state);
    for (index = 0;
         index < module->function_count * closure->helper_id_count;
         ++index) {
        if (closure->helper_matrix[index] != 0) {
            ++helper_rows;
        }
    }
    if (helper_rows != 0) {
        size_t row = 0;
        size_t function;

        closure->helper_bounds =
            calloc(helper_rows, sizeof(*closure->helper_bounds));
        if (closure->helper_bounds == NULL) {
            ribos_ir_resource_closure_reset(closure);
            return RIBOS_IR_RESOURCE_EXCEEDED;
        }
        for (function = 0;
             function < module->function_count;
             ++function) {
            size_t helper;

            for (helper = 0;
                 helper < closure->helper_id_count;
                 ++helper) {
                uint64_t bound = closure->helper_matrix[
                    function * closure->helper_id_count + helper
                ];

                if (bound == 0) {
                    continue;
                }
                closure->helper_bounds[row++] = (RibosIrHelperBound){
                    .function_id = (uint32_t)function,
                    .helper_stable_id = closure->helper_ids[helper],
                    .call_upper_bound = bound,
                };
            }
        }
    }
    closure->helper_bound_count = helper_rows;
    closure->complete = 1;
    return RIBOS_IR_OK;
}

RibosIrStatus
ribos_ir_enforce_resource_budgets_v1(
    const RibosIrModule *module,
    const RibosIrResourceClosure *closure)
{
    size_t index;

    if (module == NULL || closure == NULL || !closure->complete ||
        closure->function_count != module->function_count) {
        return RIBOS_IR_INVALID_ARGUMENT;
    }
    for (index = 0; index < module->function_count; ++index) {
        const RibosIrFunctionResource *resource =
            &closure->functions[index];

        if (!resource->instruction_budget_satisfied ||
            !resource->helper_budget_satisfied) {
            return RIBOS_IR_BUDGET_EXCEEDED;
        }
    }
    return RIBOS_IR_OK;
}

RibosIrStatus
ribos_ir_resource_summary(
    const RibosIrResourceClosure *closure,
    RibosIrResourceSummary *summary)
{
    if (closure == NULL || summary == NULL || !closure->complete) {
        return RIBOS_IR_INVALID_ARGUMENT;
    }
    *summary = (RibosIrResourceSummary){
        .type_layout_count = closure->type_count,
        .function_count = closure->function_count,
        .block_count = closure->block_count,
        .loop_count = closure->loop_count,
        .slot_layout_count = closure->slot_count,
        .helper_bound_count = closure->helper_bound_count,
    };
    return RIBOS_IR_OK;
}

const RibosIrTypeLayout *
ribos_ir_resource_type_layout(
    const RibosIrResourceClosure *closure,
    uint32_t type_id)
{
    return closure != NULL && closure->complete &&
        type_id < closure->type_count ?
            &closure->types[type_id] : NULL;
}

const RibosIrFunctionResource *
ribos_ir_resource_function(
    const RibosIrResourceClosure *closure,
    uint32_t function_id)
{
    return closure != NULL && closure->complete &&
        function_id < closure->function_count ?
            &closure->functions[function_id] : NULL;
}

const RibosIrBlockResource *
ribos_ir_resource_block(
    const RibosIrResourceClosure *closure,
    uint32_t block_id)
{
    return closure != NULL && closure->complete &&
        block_id < closure->block_count ?
            &closure->blocks[block_id] : NULL;
}

const RibosIrLoopResource *
ribos_ir_resource_loop(
    const RibosIrResourceClosure *closure,
    uint32_t loop_id)
{
    return closure != NULL && closure->complete &&
        loop_id < closure->loop_count ?
            &closure->loops[loop_id] : NULL;
}

const RibosIrSlotLayout *
ribos_ir_resource_slot(
    const RibosIrResourceClosure *closure,
    uint32_t slot_id)
{
    return closure != NULL && closure->complete &&
        slot_id < closure->slot_count ?
            &closure->slots[slot_id] : NULL;
}

const RibosIrHelperBound *
ribos_ir_resource_helper_bound(
    const RibosIrResourceClosure *closure,
    size_t index)
{
    return closure != NULL && closure->complete &&
        index < closure->helper_bound_count ?
            &closure->helper_bounds[index] : NULL;
}

RibosIrStatus
ribos_ir_dump_resources_v1(
    const RibosIrResourceClosure *closure,
    FILE *output)
{
    size_t index;

    if (closure == NULL || output == NULL || !closure->complete) {
        return RIBOS_IR_INVALID_ARGUMENT;
    }
    (void)fprintf(
        output,
        "IR-RESOURCE-CLOSURE version=%u.%u types=%zu functions=%zu "
        "blocks=%zu loops=%zu slots=%zu helper-bounds=%zu\n",
        RIBOS_IR_V1_MAJOR,
        RIBOS_IR_V1_MINOR,
        closure->type_count,
        closure->function_count,
        closure->block_count,
        closure->loop_count,
        closure->slot_count,
        closure->helper_bound_count);
    for (index = 0; index < closure->type_count; ++index) {
        const RibosIrTypeLayout *type = &closure->types[index];

        (void)fprintf(
            output,
            "IR-RESOURCE-TYPE id=%u storage=%u bytes=%u align=%u "
            "stride=%u payload=%u capacity=%u\n",
            type->type_id,
            (unsigned)type->storage_kind,
            type->byte_size,
            type->alignment,
            type->element_stride,
            type->payload_offset,
            type->capacity);
    }
    for (index = 0; index < closure->function_count; ++index) {
        const RibosIrFunctionResource *function =
            &closure->functions[index];

        (void)fprintf(
            output,
            "IR-RESOURCE-FUNCTION id=%u reachable=%u terminal=0x%x "
            "closed=%u frame=%u aggregate=%u largest=%u stack=%" PRIu64
            " call-depth=%u instructions=%" PRIu64 " helpers=%" PRIu64
            " instruction-budget=%u helper-budget=%u\n",
            function->function_id,
            function->reachable_block_count,
            function->terminal_mask,
            function->all_paths_terminal,
            function->frame_bytes,
            function->aggregate_slot_bytes,
            function->largest_value_bytes,
            function->maximum_stack_bytes,
            function->maximum_call_depth,
            function->instruction_upper_bound,
            function->helper_call_upper_bound,
            function->instruction_budget_satisfied,
            function->helper_budget_satisfied);
    }
    for (index = 0; index < closure->block_count; ++index) {
        const RibosIrBlockResource *block = &closure->blocks[index];

        (void)fprintf(
            output,
            "IR-RESOURCE-BLOCK id=b%u function=%u reachable=%u "
            "executions=%" PRIu64 "\n",
            block->block_id,
            block->function_id,
            block->reachable,
            block->execution_upper_bound);
    }
    for (index = 0; index < closure->loop_count; ++index) {
        const RibosIrLoopResource *loop = &closure->loops[index];

        (void)fprintf(
            output,
            "IR-RESOURCE-LOOP id=l%u function=%u header=b%u body=b%u "
            "exit=b%u latch=",
            loop->loop_id,
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
            " trips=%u reachable=%u\n",
            loop->trip_count,
            loop->reachable);
    }
    for (index = 0; index < closure->slot_count; ++index) {
        const RibosIrSlotLayout *slot = &closure->slots[index];

        (void)fprintf(
            output,
            "IR-RESOURCE-SLOT id=s%u function=%u type=%u offset=%u "
            "bytes=%u align=%u\n",
            slot->slot_id,
            slot->function_id,
            slot->type_id,
            slot->frame_offset,
            slot->byte_size,
            slot->alignment);
    }
    for (index = 0; index < closure->helper_bound_count; ++index) {
        const RibosIrHelperBound *helper =
            &closure->helper_bounds[index];

        (void)fprintf(
            output,
            "IR-RESOURCE-HELPER function=%u helper=%u upper=%" PRIu64
            "\n",
            helper->function_id,
            helper->helper_stable_id,
            helper->call_upper_bound);
    }
    return RIBOS_IR_OK;
}
