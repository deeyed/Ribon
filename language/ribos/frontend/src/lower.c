#include "semantic_internal.h"

#include "ribos/ir/analysis.h"
#include "ribos/ir/builder.h"

#include <stdlib.h>

#define RIBOS_LOWER_MAX_BLOCKS 4096u
#define RIBOS_LOWER_MAX_BINDINGS 512u
#define RIBOS_LOWER_SLOT_PARAMETER (1u << 0)
#define RIBOS_LOWER_SLOT_BINDING (1u << 1)
#define RIBOS_LOWER_SLOT_MUTABLE (1u << 2)
#define RIBOS_LOWER_BLOCK_ENTRY (1u << 0)

typedef struct RibosLowerBinding {
    Token *name;
    uint32_t slot;
    uint32_t type;
    uint32_t depth;
    unsigned mutable_binding : 1;
} RibosLowerBinding;

typedef struct RibosLowerContext {
    RibosSemanticContext *semantic;
    RibosIrModule *module;
    RibosFunctionInfo *function;
    uint32_t function_id;
    uint32_t current_block;
    uint32_t scope_depth;
    RibosLowerBinding bindings[RIBOS_LOWER_MAX_BINDINGS];
    size_t binding_count;
    uint32_t source_maps[RIBOS_MAX_AST_NODES];
    uint8_t terminated[RIBOS_LOWER_MAX_BLOCKS];
    uint32_t next_block_id;
    uint32_t next_slot_id;
    RibosIrStatus ir_status;
} RibosLowerContext;

static int
ribos_lower_token_equals(const Token *token, const char *text)
{
    size_t length;

    if (token == NULL || text == NULL) {
        return 0;
    }
    length = strlen(text);
    return token->length == length &&
        memcmp(token->start, text, length) == 0;
}

static int
ribos_lower_tokens_equal(const Token *left, const Token *right)
{
    return left != NULL && right != NULL &&
        left->length == right->length &&
        memcmp(left->start, right->start, left->length) == 0;
}

static uint32_t
ribos_lower_checked_operator(uint32_t source_operator)
{
    switch ((RibosOperator)source_operator) {
    case RIBOS_OPERATOR_NOT:
        return RIBOS_IR_CHECK_NOT;
    case RIBOS_OPERATOR_EQUAL:
        return RIBOS_IR_CHECK_EQUAL;
    case RIBOS_OPERATOR_NOT_EQUAL:
        return RIBOS_IR_CHECK_NOT_EQUAL;
    case RIBOS_OPERATOR_LESS:
        return RIBOS_IR_CHECK_LESS;
    case RIBOS_OPERATOR_LESS_EQUAL:
        return RIBOS_IR_CHECK_LESS_EQUAL;
    case RIBOS_OPERATOR_GREATER:
        return RIBOS_IR_CHECK_GREATER;
    case RIBOS_OPERATOR_GREATER_EQUAL:
        return RIBOS_IR_CHECK_GREATER_EQUAL;
    case RIBOS_OPERATOR_IN:
        return RIBOS_IR_CHECK_IN;
    case RIBOS_OPERATOR_NOT_IN:
        return RIBOS_IR_CHECK_NOT_IN;
    case RIBOS_OPERATOR_BIT_OR:
        return RIBOS_IR_CHECK_BIT_OR;
    case RIBOS_OPERATOR_BIT_XOR:
        return RIBOS_IR_CHECK_BIT_XOR;
    case RIBOS_OPERATOR_BIT_AND:
        return RIBOS_IR_CHECK_BIT_AND;
    case RIBOS_OPERATOR_SHIFT_LEFT:
        return RIBOS_IR_CHECK_SHIFT_LEFT;
    case RIBOS_OPERATOR_SHIFT_RIGHT:
        return RIBOS_IR_CHECK_SHIFT_RIGHT;
    case RIBOS_OPERATOR_ADD:
        return RIBOS_IR_CHECK_ADD;
    case RIBOS_OPERATOR_SUBTRACT:
        return RIBOS_IR_CHECK_SUBTRACT;
    case RIBOS_OPERATOR_MULTIPLY:
        return RIBOS_IR_CHECK_MULTIPLY;
    case RIBOS_OPERATOR_DIVIDE:
        return RIBOS_IR_CHECK_DIVIDE;
    case RIBOS_OPERATOR_REMAINDER:
        return RIBOS_IR_CHECK_REMAINDER;
    case RIBOS_OPERATOR_POSITIVE:
        return RIBOS_IR_CHECK_POSITIVE;
    case RIBOS_OPERATOR_NEGATIVE:
        return RIBOS_IR_CHECK_NEGATIVE;
    case RIBOS_OPERATOR_BIT_NOT:
        return RIBOS_IR_CHECK_BIT_NOT;
    default:
        return 0;
    }
}

static size_t
ribos_lower_path_count(const RibosAstNode *path)
{
    if (path == NULL || path->kind != RIBOS_AST_PATH ||
        path->token == NULL) {
        return 0;
    }
    return 1 + (path->items == NULL ? 0 : (size_t)path->items->size);
}

static Token *
ribos_lower_path_component(const RibosAstNode *path, size_t index)
{
    if (path == NULL) {
        return NULL;
    }
    if (index == 0) {
        return path->token;
    }
    if (path->items == NULL || index > (size_t)path->items->size) {
        return NULL;
    }
    return path->items->elements[index - 1];
}

static int
ribos_lower_path_equals(const RibosAstNode *path, const char *text)
{
    size_t component = 0;
    size_t offset = 0;
    size_t length = strlen(text);

    while (offset < length) {
        Token *token = ribos_lower_path_component(path, component);
        size_t end = offset;

        while (end < length && text[end] != '.') {
            ++end;
        }
        if (token == NULL || token->length != end - offset ||
            memcmp(token->start, text + offset, end - offset) != 0) {
            return 0;
        }
        ++component;
        offset = end == length ? end : end + 1;
    }
    return component == ribos_lower_path_count(path);
}

static uint64_t
ribos_lower_parse_integer(const Token *token)
{
    unsigned base = 10;
    size_t index = 0;
    uint64_t result = 0;

    if (token->length >= 2 && token->start[0] == '0' &&
        (token->start[1] == 'x' || token->start[1] == 'X')) {
        base = 16;
        index = 2;
    } else if (token->length >= 2 && token->start[0] == '0' &&
        (token->start[1] == 'b' || token->start[1] == 'B')) {
        base = 2;
        index = 2;
    }
    for (; index < token->length; ++index) {
        unsigned char byte = (unsigned char)token->start[index];
        unsigned digit;

        if (byte == '_') {
            continue;
        }
        if (byte >= '0' && byte <= '9') {
            digit = byte - '0';
        } else if (byte >= 'a' && byte <= 'f') {
            digit = byte - 'a' + 10;
        } else {
            digit = byte - 'A' + 10;
        }
        result = result * base + digit;
    }
    return result;
}

static void
ribos_lower_fail(RibosLowerContext *context, const RibosAstNode *node)
{
    if (context->semantic->status != RIBOS_COMPILE_OK) {
        return;
    }
    context->semantic->status = RIBOS_COMPILE_IR_ERROR;
    context->semantic->diagnostic->code = RIBOS_E_IR_LOWERING;
    if (node != NULL) {
        context->semantic->diagnostic->span = node->span;
    }
    (void)snprintf(
        context->semantic->diagnostic->message,
        sizeof(context->semantic->diagnostic->message),
        "typed AST could not be lowered into bounded Policy IR v1");
}

static uint32_t
ribos_lower_source_map(
    RibosLowerContext *context,
    const RibosAstNode *node)
{
    RibosIrSourceMap map;
    uint32_t id;

    if (node == NULL || node->id >= RIBOS_MAX_AST_NODES) {
        ribos_lower_fail(context, node);
        return RIBOS_IR_INVALID_ID;
    }
    if (context->source_maps[node->id] != RIBOS_IR_INVALID_ID) {
        return context->source_maps[node->id];
    }
    map = (RibosIrSourceMap){
        .ast_node_id = node->id,
        .start_byte = node->span.start.byte_offset,
        .end_byte = node->span.end.byte_offset,
        .start_line = node->span.start.line,
        .start_column = node->span.start.column,
        .end_line = node->span.end.line,
        .end_column = node->span.end.column,
    };
    context->ir_status = ribos_ir_builder_add_source_map(
        context->module,
        &map,
        &id);
    if (context->ir_status != RIBOS_IR_OK) {
        ribos_lower_fail(context, node);
        return RIBOS_IR_INVALID_ID;
    }
    context->source_maps[node->id] = id;
    return id;
}

static uint32_t
ribos_lower_new_slot(
    RibosLowerContext *context,
    uint32_t type,
    const RibosAstNode *owner,
    uint32_t flags)
{
    RibosIrSlot slot = {
        .function_id = context->function_id,
        .type_id = type,
        .source_map_id = ribos_lower_source_map(context, owner),
        .flags = flags,
    };
    uint32_t id;

    if (slot.source_map_id == RIBOS_IR_INVALID_ID) {
        return RIBOS_IR_INVALID_ID;
    }
    context->ir_status =
        ribos_ir_builder_add_slot(context->module, &slot, &id);
    if (context->ir_status != RIBOS_IR_OK) {
        ribos_lower_fail(context, owner);
        return RIBOS_IR_INVALID_ID;
    }
    context->next_slot_id = id + 1;
    return id;
}

static uint32_t
ribos_lower_new_block(
    RibosLowerContext *context,
    const RibosAstNode *owner,
    uint32_t flags)
{
    RibosIrBlock block = {
        .function_id = context->function_id,
        .flags = flags,
    };
    uint32_t id;

    UNUSED(owner);
    context->ir_status =
        ribos_ir_builder_add_block(context->module, &block, &id);
    if (context->ir_status != RIBOS_IR_OK ||
        id >= RIBOS_LOWER_MAX_BLOCKS) {
        ribos_lower_fail(context, owner);
        return RIBOS_IR_INVALID_ID;
    }
    context->next_block_id = id + 1;
    return id;
}

static uint32_t
ribos_lower_emit(
    RibosLowerContext *context,
    const RibosAstNode *owner,
    RibosIrOpcode opcode,
    uint32_t result,
    const uint32_t *operands,
    size_t operand_count,
    uint32_t target,
    uint32_t alternate,
    uint64_t immediate)
{
    RibosIrInstruction instruction = {
        .opcode = opcode,
        .block_id = context->current_block,
        .result_slot = result,
        .target = target,
        .alternate = alternate,
        .immediate = immediate,
        .source_map_id = ribos_lower_source_map(context, owner),
    };
    uint32_t id;

    if (context->current_block == RIBOS_IR_INVALID_ID ||
        context->current_block >= RIBOS_LOWER_MAX_BLOCKS ||
        context->terminated[context->current_block] ||
        instruction.source_map_id == RIBOS_IR_INVALID_ID) {
        ribos_lower_fail(context, owner);
        return RIBOS_IR_INVALID_ID;
    }
    context->ir_status = ribos_ir_builder_add_instruction(
        context->module,
        &instruction,
        operands,
        operand_count,
        &id);
    if (context->ir_status != RIBOS_IR_OK) {
        ribos_lower_fail(context, owner);
        return RIBOS_IR_INVALID_ID;
    }
    if (opcode == RIBOS_IR_OP_JUMP ||
        opcode == RIBOS_IR_OP_BRANCH ||
        opcode == RIBOS_IR_OP_RETURN ||
        opcode == RIBOS_IR_OP_TRAP) {
        context->terminated[context->current_block] = 1;
    }
    return id;
}

static uint32_t
ribos_lower_emit_value(
    RibosLowerContext *context,
    const RibosAstNode *owner,
    RibosIrOpcode opcode,
    uint32_t type,
    const uint32_t *operands,
    size_t operand_count,
    uint32_t target,
    uint32_t alternate,
    uint64_t immediate)
{
    uint32_t slot = ribos_lower_new_slot(context, type, owner, 0);

    if (slot == RIBOS_IR_INVALID_ID ||
        ribos_lower_emit(
            context,
            owner,
            opcode,
            slot,
            operands,
            operand_count,
            target,
            alternate,
            immediate) == RIBOS_IR_INVALID_ID) {
        return RIBOS_IR_INVALID_ID;
    }
    return slot;
}

static uint32_t
ribos_lower_emit_value_with_instruction(
    RibosLowerContext *context,
    const RibosAstNode *owner,
    RibosIrOpcode opcode,
    uint32_t type,
    const uint32_t *operands,
    size_t operand_count,
    uint32_t target,
    uint32_t alternate,
    uint64_t immediate,
    uint32_t *instruction_id)
{
    uint32_t slot = ribos_lower_new_slot(context, type, owner, 0);

    if (instruction_id == NULL || slot == RIBOS_IR_INVALID_ID) {
        ribos_lower_fail(context, owner);
        return RIBOS_IR_INVALID_ID;
    }
    *instruction_id = ribos_lower_emit(
        context,
        owner,
        opcode,
        slot,
        operands,
        operand_count,
        target,
        alternate,
        immediate);
    if (*instruction_id == RIBOS_IR_INVALID_ID) {
        return RIBOS_IR_INVALID_ID;
    }
    return slot;
}

static int
ribos_lower_jump(
    RibosLowerContext *context,
    const RibosAstNode *owner,
    uint32_t target)
{
    return ribos_lower_emit(
        context,
        owner,
        RIBOS_IR_OP_JUMP,
        RIBOS_IR_INVALID_ID,
        NULL,
        0,
        target,
        RIBOS_IR_INVALID_ID,
        0) != RIBOS_IR_INVALID_ID;
}

static RibosLowerBinding *
ribos_lower_find_binding(RibosLowerContext *context, const Token *name)
{
    size_t index = context->binding_count;

    while (index != 0) {
        --index;
        if (ribos_lower_tokens_equal(context->bindings[index].name, name)) {
            return &context->bindings[index];
        }
    }
    return NULL;
}

static int
ribos_lower_add_binding(
    RibosLowerContext *context,
    Token *name,
    uint32_t slot,
    uint32_t type,
    int mutable_binding)
{
    if (context->binding_count == RIBOS_LOWER_MAX_BINDINGS) {
        ribos_lower_fail(context, NULL);
        return 0;
    }
    context->bindings[context->binding_count++] = (RibosLowerBinding){
        .name = name,
        .slot = slot,
        .type = type,
        .depth = context->scope_depth,
        .mutable_binding = mutable_binding != 0,
    };
    return 1;
}

static uint32_t
ribos_lower_find_type_name(
    const RibosSemanticContext *semantic,
    const char *name)
{
    size_t index;
    size_t length = strlen(name);

    for (index = 0; index < semantic->type_count; ++index) {
        const RibosType *type = &semantic->types[index];

        if (type->name != NULL && type->name_length == length &&
            memcmp(type->name, name, length) == 0) {
            return (uint32_t)index;
        }
    }
    return 0;
}

static int
ribos_lower_enum_variant(
    const RibosLowerContext *context,
    const RibosAstNode *path,
    uint32_t *type_id,
    uint32_t *tag)
{
    uint32_t candidate;
    RibosType *type;
    Token *variant_name;
    Py_ssize_t index;

    if (path == NULL || ribos_lower_path_count(path) != 2 ||
        type_id == NULL || tag == NULL) {
        return 0;
    }
    for (candidate = 0;
         candidate < context->semantic->type_count;
         ++candidate) {
        RibosType *current = &context->semantic->types[candidate];
        Token *name = ribos_lower_path_component(path, 0);

        if (current->name != NULL &&
            current->name_length == name->length &&
            memcmp(current->name, name->start, name->length) == 0) {
            break;
        }
    }
    if (candidate >= context->semantic->type_count) {
        return 0;
    }
    type = &context->semantic->types[candidate];
    if (type->declaration == NULL ||
        type->declaration->kind != RIBOS_AST_ENUM ||
        type->declaration->items == NULL) {
        return 0;
    }
    variant_name = ribos_lower_path_component(path, 1);
    for (index = 0;
         index < type->declaration->items->size;
         ++index) {
        RibosAstNode *variant =
            type->declaration->items->elements[index];

        if (ribos_lower_tokens_equal(variant->token, variant_name)) {
            *type_id = candidate;
            *tag = (uint32_t)index;
            return 1;
        }
    }
    return 0;
}

static uint32_t
ribos_lower_find_function(
    const RibosSemanticContext *semantic,
    const Token *name)
{
    size_t index;

    for (index = 0; index < semantic->function_count; ++index) {
        if (ribos_lower_tokens_equal(semantic->functions[index].name, name)) {
            return (uint32_t)index;
        }
    }
    return RIBOS_IR_INVALID_ID;
}

static const RibosSchemaHelper *
ribos_lower_find_helper(
    const RibosLowerContext *context,
    const RibosAstNode *path)
{
    size_t index;

    for (index = 0;
         index < context->semantic->schema->helper_count;
         ++index) {
        const RibosSchemaHelper *helper =
            &context->semantic->schema->helpers[index];

        if (ribos_lower_path_equals(path, helper->path)) {
            return helper;
        }
    }
    return NULL;
}

static uint32_t
ribos_lower_add_path_constant(
    RibosLowerContext *context,
    const RibosAstNode *path)
{
    size_t count = ribos_lower_path_count(path);
    size_t length = count == 0 ? 0 : count - 1;
    size_t index;
    size_t offset = 0;
    uint8_t *bytes;
    uint32_t constant;

    for (index = 0; index < count; ++index) {
        length += ribos_lower_path_component(path, index)->length;
    }
    bytes = length == 0 ? NULL : malloc(length);
    if (length != 0 && bytes == NULL) {
        ribos_lower_fail(context, path);
        return RIBOS_IR_INVALID_ID;
    }
    for (index = 0; index < count; ++index) {
        Token *token = ribos_lower_path_component(path, index);

        if (index != 0) {
            bytes[offset++] = '.';
        }
        memcpy(bytes + offset, token->start, token->length);
        offset += token->length;
    }
    context->ir_status = ribos_ir_builder_add_constant(
        context->module,
        RIBOS_IR_CONSTANT_SYMBOL,
        bytes,
        length,
        &constant);
    free(bytes);
    if (context->ir_status != RIBOS_IR_OK) {
        ribos_lower_fail(context, path);
        return RIBOS_IR_INVALID_ID;
    }
    return constant;
}

static uint32_t
ribos_lower_add_token_constant(
    RibosLowerContext *context,
    const RibosAstNode *owner,
    const Token *token)
{
    uint32_t constant;

    if (token == NULL) {
        ribos_lower_fail(context, owner);
        return RIBOS_IR_INVALID_ID;
    }
    context->ir_status = ribos_ir_builder_add_constant(
        context->module,
        RIBOS_IR_CONSTANT_SYMBOL,
        (const uint8_t *)token->start,
        token->length,
        &constant);
    if (context->ir_status != RIBOS_IR_OK) {
        ribos_lower_fail(context, owner);
        return RIBOS_IR_INVALID_ID;
    }
    return constant;
}

static uint32_t
ribos_lower_add_string_constant(
    RibosLowerContext *context,
    const RibosAstNode *node)
{
    const Token *token = node->token;
    size_t input = 1;
    size_t output = 0;
    uint8_t *bytes;
    uint32_t constant;

    bytes = malloc(token->length);
    if (bytes == NULL) {
        ribos_lower_fail(context, node);
        return RIBOS_IR_INVALID_ID;
    }
    while (input + 1 < token->length) {
        unsigned char byte = (unsigned char)token->start[input++];

        if (byte != '\\') {
            bytes[output++] = byte;
            continue;
        }
        byte = (unsigned char)token->start[input++];
        if (byte == 'n') {
            bytes[output++] = '\n';
        } else if (byte == 'r') {
            bytes[output++] = '\r';
        } else if (byte == 't') {
            bytes[output++] = '\t';
        } else if (byte == '0') {
            bytes[output++] = '\0';
        } else if (byte == 'x') {
            unsigned high =
                (unsigned char)token->start[input++] <= '9' ?
                (unsigned char)token->start[input - 1] - '0' :
                ((unsigned char)token->start[input - 1] | 32u) - 'a' + 10u;
            unsigned low =
                (unsigned char)token->start[input++] <= '9' ?
                (unsigned char)token->start[input - 1] - '0' :
                ((unsigned char)token->start[input - 1] | 32u) - 'a' + 10u;

            bytes[output++] = (uint8_t)((high << 4) | low);
        } else {
            bytes[output++] = byte;
        }
    }
    context->ir_status = ribos_ir_builder_add_constant(
        context->module,
        RIBOS_IR_CONSTANT_STRING,
        bytes,
        output,
        &constant);
    free(bytes);
    if (context->ir_status != RIBOS_IR_OK) {
        ribos_lower_fail(context, node);
        return RIBOS_IR_INVALID_ID;
    }
    return constant;
}

static uint32_t ribos_lower_expression(
    RibosLowerContext *context,
    RibosAstNode *node);

static uint32_t
ribos_lower_argument_expression(
    RibosLowerContext *context,
    RibosAstNode *argument)
{
    return ribos_lower_expression(
        context,
        argument == NULL ? NULL : argument->first);
}

static uint32_t
ribos_lower_call(RibosLowerContext *context, RibosAstNode *node)
{
    RibosAstNode *callee = node->first;
    uint32_t operands[64];
    size_t operand_count = 0;
    Py_ssize_t index;

    if (callee == NULL) {
        ribos_lower_fail(context, node);
        return RIBOS_IR_INVALID_ID;
    }
    if (callee->kind == RIBOS_AST_PATH) {
        const RibosSchemaHelper *helper =
            ribos_lower_find_helper(context, callee);
        RibosLowerBinding *receiver =
            ribos_lower_find_binding(context, callee->token);
        uint32_t function_id =
            ribos_lower_find_function(context->semantic, callee->token);
        uint32_t enum_type;
        uint32_t enum_tag;

        if (receiver != NULL &&
            ribos_lower_path_count(callee) == 2 &&
            ribos_lower_token_equals(
                ribos_lower_path_component(callee, 1),
                "get")) {
            operands[operand_count++] = receiver->slot;
            for (index = 0; node->items != NULL &&
                 index < node->items->size; ++index) {
                operands[operand_count++] =
                    ribos_lower_argument_expression(
                        context,
                        node->items->elements[index]);
            }
            return ribos_lower_emit_value(
                context,
                node,
                RIBOS_IR_OP_INDEX,
                node->inferred_type,
                operands,
                operand_count,
                1,
                RIBOS_IR_INVALID_ID,
                0);
        }
        for (index = 0; node->items != NULL &&
             index < node->items->size; ++index) {
            operands[operand_count++] = ribos_lower_argument_expression(
                context,
                node->items->elements[index]);
        }
        if (helper != NULL) {
            uint32_t instruction_id;
            uint32_t call_site_id;
            uint32_t result = ribos_lower_emit_value_with_instruction(
                context,
                node,
                RIBOS_IR_OP_CALL_HELPER,
                node->inferred_type,
                operands,
                operand_count,
                helper->stable_id,
                RIBOS_IR_INVALID_ID,
                0,
                &instruction_id);
            RibosIrHelperCallSite call_site = {
                .instruction_id = instruction_id,
                .helper_stable_id = helper->stable_id,
                .capabilities = helper->capabilities,
                .result_type = node->inferred_type,
                .argument_count = (uint32_t)operand_count,
                .source_map_id = ribos_lower_source_map(context, node),
            };

            if (result == RIBOS_IR_INVALID_ID) {
                return result;
            }
            context->ir_status = ribos_ir_builder_add_helper_call(
                context->module,
                &call_site,
                &call_site_id);
            if (context->ir_status != RIBOS_IR_OK) {
                ribos_lower_fail(context, node);
                return RIBOS_IR_INVALID_ID;
            }
            return result;
        }
        if (ribos_lower_path_count(callee) == 1 &&
            (ribos_lower_token_equals(callee->token, "Ok") ||
             ribos_lower_token_equals(callee->token, "Err") ||
             ribos_lower_token_equals(callee->token, "Some"))) {
            uint32_t tag =
                ribos_lower_token_equals(callee->token, "Err") ? 1u : 0u;

            return ribos_lower_emit_value(
                context,
                node,
                RIBOS_IR_OP_BUILD_VARIANT,
                node->inferred_type,
                operands,
                operand_count,
                tag,
                RIBOS_IR_INVALID_ID,
                0);
        }
        if (ribos_lower_enum_variant(
                context,
                callee,
                &enum_type,
                &enum_tag)) {
            return ribos_lower_emit_value(
                context,
                node,
                RIBOS_IR_OP_BUILD_VARIANT,
                enum_type,
                operands,
                operand_count,
                enum_tag,
                RIBOS_IR_INVALID_ID,
                0);
        }
        if (function_id != RIBOS_IR_INVALID_ID &&
            ribos_lower_path_count(callee) == 1) {
            return ribos_lower_emit_value(
                context,
                node,
                RIBOS_IR_OP_CALL_DIRECT,
                node->inferred_type,
                operands,
                operand_count,
                function_id,
                RIBOS_IR_INVALID_ID,
                0);
        }
        if (ribos_lower_path_count(callee) == 1) {
            return ribos_lower_emit_value(
                context,
                node,
                RIBOS_IR_OP_BUILD_STRUCT,
                node->inferred_type,
                operands,
                operand_count,
                node->inferred_type,
                RIBOS_IR_INVALID_ID,
                0);
        }
    }
    if (callee->kind == RIBOS_AST_MEMBER) {
        operands[operand_count++] =
            ribos_lower_expression(context, callee->first);
        for (index = 0; node->items != NULL &&
             index < node->items->size; ++index) {
            operands[operand_count++] = ribos_lower_argument_expression(
                context,
                node->items->elements[index]);
        }
        return ribos_lower_emit_value(
            context,
            node,
            RIBOS_IR_OP_INDEX,
            node->inferred_type,
            operands,
            operand_count,
            1,
            RIBOS_IR_INVALID_ID,
            0);
    }
    ribos_lower_fail(context, node);
    return RIBOS_IR_INVALID_ID;
}

static uint32_t
ribos_lower_logical_expression(
    RibosLowerContext *context,
    RibosAstNode *node)
{
    uint32_t result =
        ribos_lower_new_slot(context, node->inferred_type, node, 0);
    uint32_t left = ribos_lower_expression(context, node->first);
    uint32_t right_block = ribos_lower_new_block(context, node, 0);
    uint32_t short_block = ribos_lower_new_block(context, node, 0);
    uint32_t merge_block = ribos_lower_new_block(context, node, 0);
    uint32_t branch_operands[1] = {left};
    uint32_t short_value;
    uint32_t move_operands[1];

    if (node->flags == RIBOS_OPERATOR_AND) {
        (void)ribos_lower_emit(
            context,
            node,
            RIBOS_IR_OP_BRANCH,
            RIBOS_IR_INVALID_ID,
            branch_operands,
            1,
            right_block,
            short_block,
            0);
    } else {
        (void)ribos_lower_emit(
            context,
            node,
            RIBOS_IR_OP_BRANCH,
            RIBOS_IR_INVALID_ID,
            branch_operands,
            1,
            short_block,
            right_block,
            0);
    }
    context->current_block = short_block;
    short_value = ribos_lower_emit_value(
        context,
        node,
        RIBOS_IR_OP_CONST_BOOL,
        node->inferred_type,
        NULL,
        0,
        RIBOS_IR_INVALID_ID,
        RIBOS_IR_INVALID_ID,
        node->flags == RIBOS_OPERATOR_OR);
    move_operands[0] = short_value;
    (void)ribos_lower_emit(
        context,
        node,
        RIBOS_IR_OP_MOVE,
        result,
        move_operands,
        1,
        RIBOS_IR_INVALID_ID,
        RIBOS_IR_INVALID_ID,
        0);
    (void)ribos_lower_jump(context, node, merge_block);

    context->current_block = right_block;
    move_operands[0] = ribos_lower_expression(context, node->second);
    (void)ribos_lower_emit(
        context,
        node,
        RIBOS_IR_OP_MOVE,
        result,
        move_operands,
        1,
        RIBOS_IR_INVALID_ID,
        RIBOS_IR_INVALID_ID,
        0);
    (void)ribos_lower_jump(context, node, merge_block);
    context->current_block = merge_block;
    return result;
}

static uint32_t
ribos_lower_if_expression(
    RibosLowerContext *context,
    RibosAstNode *node)
{
    uint32_t result =
        ribos_lower_new_slot(context, node->inferred_type, node, 0);
    uint32_t condition = ribos_lower_expression(context, node->first);
    uint32_t then_block = ribos_lower_new_block(context, node->second, 0);
    uint32_t else_block = ribos_lower_new_block(context, node->third, 0);
    uint32_t merge_block = ribos_lower_new_block(context, node, 0);
    uint32_t branch_operands[1] = {condition};
    uint32_t move_operands[1];

    (void)ribos_lower_emit(
        context,
        node,
        RIBOS_IR_OP_BRANCH,
        RIBOS_IR_INVALID_ID,
        branch_operands,
        1,
        then_block,
        else_block,
        0);
    context->current_block = then_block;
    move_operands[0] = ribos_lower_expression(context, node->second);
    (void)ribos_lower_emit(
        context,
        node->second,
        RIBOS_IR_OP_MOVE,
        result,
        move_operands,
        1,
        RIBOS_IR_INVALID_ID,
        RIBOS_IR_INVALID_ID,
        0);
    (void)ribos_lower_jump(context, node->second, merge_block);
    context->current_block = else_block;
    move_operands[0] = ribos_lower_expression(context, node->third);
    (void)ribos_lower_emit(
        context,
        node->third,
        RIBOS_IR_OP_MOVE,
        result,
        move_operands,
        1,
        RIBOS_IR_INVALID_ID,
        RIBOS_IR_INVALID_ID,
        0);
    (void)ribos_lower_jump(context, node->third, merge_block);
    context->current_block = merge_block;
    return result;
}

static uint32_t
ribos_lower_propagate(
    RibosLowerContext *context,
    RibosAstNode *node)
{
    uint32_t inner = ribos_lower_expression(context, node->first);
    RibosType *inner_type = &context->semantic->types[node->first->inferred_type];
    uint32_t tag_type =
        ribos_lower_find_type_name(context->semantic, "u32");
    uint32_t bool_type =
        ribos_lower_find_type_name(context->semantic, "bool");
    uint32_t tag = ribos_lower_emit_value(
        context,
        node,
        RIBOS_IR_OP_VARIANT_TAG,
        tag_type,
        &inner,
        1,
        RIBOS_IR_INVALID_ID,
        RIBOS_IR_INVALID_ID,
        0);
    uint32_t zero = ribos_lower_emit_value(
        context,
        node,
        RIBOS_IR_OP_CONST_INTEGER,
        tag_type,
        NULL,
        0,
        RIBOS_IR_INVALID_ID,
        RIBOS_IR_INVALID_ID,
        0);
    uint32_t compare_operands[2] = {tag, zero};
    uint32_t success;
    uint32_t success_block = ribos_lower_new_block(context, node, 0);
    uint32_t failure_block = ribos_lower_new_block(context, node, 0);
    uint32_t branch_operands[1];

    success = ribos_lower_emit_value(
        context,
        node,
        RIBOS_IR_OP_CHECKED_BINARY,
        bool_type,
        compare_operands,
        2,
        RIBOS_IR_CHECK_EQUAL,
        RIBOS_IR_INVALID_ID,
        0);
    branch_operands[0] = success;
    (void)ribos_lower_emit(
        context,
        node,
        RIBOS_IR_OP_BRANCH,
        RIBOS_IR_INVALID_ID,
        branch_operands,
        1,
        success_block,
        failure_block,
        0);

    context->current_block = failure_block;
    if (inner_type->kind == RIBOS_TYPE_RESULT) {
        uint32_t error = ribos_lower_emit_value(
            context,
            node,
            RIBOS_IR_OP_VARIANT_PAYLOAD,
            inner_type->second,
            &inner,
            1,
            1,
            RIBOS_IR_INVALID_ID,
            0);
        uint32_t returned = ribos_lower_emit_value(
            context,
            node,
            RIBOS_IR_OP_BUILD_VARIANT,
            context->function->return_type,
            &error,
            1,
            1,
            RIBOS_IR_INVALID_ID,
            0);

        (void)ribos_lower_emit(
            context,
            node,
            RIBOS_IR_OP_RETURN,
            RIBOS_IR_INVALID_ID,
            &returned,
            1,
            RIBOS_IR_INVALID_ID,
            RIBOS_IR_INVALID_ID,
            0);
    } else {
        (void)ribos_lower_emit(
            context,
            node,
            RIBOS_IR_OP_RETURN,
            RIBOS_IR_INVALID_ID,
            &inner,
            1,
            RIBOS_IR_INVALID_ID,
            RIBOS_IR_INVALID_ID,
            0);
    }
    context->current_block = success_block;
    return ribos_lower_emit_value(
        context,
        node,
        RIBOS_IR_OP_VARIANT_PAYLOAD,
        inner_type->first,
        &inner,
        1,
        0,
        RIBOS_IR_INVALID_ID,
        0);
}

static uint32_t
ribos_lower_expression(RibosLowerContext *context, RibosAstNode *node)
{
    uint32_t operands[16];
    size_t operand_count = 0;
    Py_ssize_t index;

    if (node == NULL || context->semantic->status != RIBOS_COMPILE_OK) {
        ribos_lower_fail(context, node);
        return RIBOS_IR_INVALID_ID;
    }
    switch (node->kind) {
    case RIBOS_AST_INTEGER:
        return ribos_lower_emit_value(
            context,
            node,
            RIBOS_IR_OP_CONST_INTEGER,
            node->inferred_type,
            NULL,
            0,
            RIBOS_IR_INVALID_ID,
            RIBOS_IR_INVALID_ID,
            ribos_lower_parse_integer(node->token));
    case RIBOS_AST_STRING:
        return ribos_lower_emit_value(
            context,
            node,
            RIBOS_IR_OP_CONST_STRING,
            node->inferred_type,
            NULL,
            0,
            ribos_lower_add_string_constant(context, node),
            RIBOS_IR_INVALID_ID,
            0);
    case RIBOS_AST_BOOLEAN:
        return ribos_lower_emit_value(
            context,
            node,
            RIBOS_IR_OP_CONST_BOOL,
            node->inferred_type,
            NULL,
            0,
            RIBOS_IR_INVALID_ID,
            RIBOS_IR_INVALID_ID,
            ribos_lower_token_equals(node->token, "True"));
    case RIBOS_AST_NONE:
        return ribos_lower_emit_value(
            context,
            node,
            RIBOS_IR_OP_BUILD_VARIANT,
            node->inferred_type,
            NULL,
            0,
            1,
            RIBOS_IR_INVALID_ID,
            0);
    case RIBOS_AST_PATH: {
        RibosLowerBinding *binding =
            ribos_lower_find_binding(context, node->token);
        uint32_t enum_type;
        uint32_t enum_tag;

        if (binding != NULL && ribos_lower_path_count(node) == 1) {
            return binding->slot;
        }
        if (binding != NULL) {
            operands[0] = binding->slot;
            return ribos_lower_emit_value(
                context,
                node,
                RIBOS_IR_OP_MEMBER,
                node->inferred_type,
                operands,
                1,
                ribos_lower_add_path_constant(context, node),
                RIBOS_IR_INVALID_ID,
                0);
        }
        if (ribos_lower_token_equals(node->token, "Unit") &&
            ribos_lower_path_count(node) == 1) {
            return ribos_lower_emit_value(
                context,
                node,
                RIBOS_IR_OP_CONST_UNIT,
                node->inferred_type,
                NULL,
                0,
                RIBOS_IR_INVALID_ID,
                RIBOS_IR_INVALID_ID,
                0);
        }
        if (ribos_lower_enum_variant(
                context,
                node,
                &enum_type,
                &enum_tag)) {
            return ribos_lower_emit_value(
                context,
                node,
                RIBOS_IR_OP_BUILD_VARIANT,
                enum_type,
                NULL,
                0,
                enum_tag,
                RIBOS_IR_INVALID_ID,
                0);
        }
        return ribos_lower_emit_value(
            context,
            node,
            RIBOS_IR_OP_CONST_SYMBOL,
            node->inferred_type,
            NULL,
            0,
            ribos_lower_add_path_constant(context, node),
            RIBOS_IR_INVALID_ID,
            0);
    }
    case RIBOS_AST_LIST: {
        size_t item_count =
            node->items == NULL ? 0 : (size_t)node->items->size;
        uint32_t *items =
            item_count == 0 ? NULL : calloc(item_count, sizeof(*items));
        uint32_t result;

        if (item_count != 0 && items == NULL) {
            ribos_lower_fail(context, node);
            return RIBOS_IR_INVALID_ID;
        }
        for (index = 0; node->items != NULL &&
             index < node->items->size; ++index) {
            items[operand_count++] = ribos_lower_expression(
                context,
                node->items->elements[index]);
        }
        result = ribos_lower_emit_value(
            context,
            node,
            RIBOS_IR_OP_BUILD_LIST,
            node->inferred_type,
            items,
            operand_count,
            context->semantic->types[node->inferred_type].bound,
            RIBOS_IR_INVALID_ID,
            0);
        free(items);
        return result;
    }
    case RIBOS_AST_MAP: {
        size_t entry_count =
            node->items == NULL ? 0 : (size_t)node->items->size;
        size_t item_count;
        uint32_t *items;
        uint32_t result;

        if (entry_count > SIZE_MAX / 2) {
            ribos_lower_fail(context, node);
            return RIBOS_IR_INVALID_ID;
        }
        item_count = entry_count * 2;
        items =
            item_count == 0 ? NULL : calloc(item_count, sizeof(*items));
        if (item_count != 0 && items == NULL) {
            ribos_lower_fail(context, node);
            return RIBOS_IR_INVALID_ID;
        }
        for (index = 0; node->items != NULL &&
             index < node->items->size; ++index) {
            RibosAstNode *entry = node->items->elements[index];

            items[operand_count++] =
                ribos_lower_expression(context, entry->first);
            items[operand_count++] =
                ribos_lower_expression(context, entry->second);
        }
        result = ribos_lower_emit_value(
            context,
            node,
            RIBOS_IR_OP_BUILD_MAP,
            node->inferred_type,
            items,
            operand_count,
            context->semantic->types[node->inferred_type].bound,
            RIBOS_IR_INVALID_ID,
            0);
        free(items);
        return result;
    }
    case RIBOS_AST_CALL:
        return ribos_lower_call(context, node);
    case RIBOS_AST_MEMBER:
        operands[0] = ribos_lower_expression(context, node->first);
        return ribos_lower_emit_value(
            context,
            node,
            RIBOS_IR_OP_MEMBER,
            node->inferred_type,
            operands,
            1,
            ribos_lower_add_token_constant(context, node, node->token),
            RIBOS_IR_INVALID_ID,
            0);
    case RIBOS_AST_INDEX:
        operands[0] = ribos_lower_expression(context, node->first);
        operands[1] = ribos_lower_expression(context, node->second);
        return ribos_lower_emit_value(
            context,
            node,
            RIBOS_IR_OP_INDEX,
            node->inferred_type,
            operands,
            2,
            0,
            RIBOS_IR_INVALID_ID,
            0);
    case RIBOS_AST_PROPAGATE:
        return ribos_lower_propagate(context, node);
    case RIBOS_AST_UNARY:
        operands[0] = ribos_lower_expression(context, node->first);
        return ribos_lower_emit_value(
            context,
            node,
            RIBOS_IR_OP_CHECKED_UNARY,
            node->inferred_type,
            operands,
            1,
            ribos_lower_checked_operator(node->flags),
            RIBOS_IR_INVALID_ID,
            0);
    case RIBOS_AST_BINARY:
        if (node->flags == RIBOS_OPERATOR_AND ||
            node->flags == RIBOS_OPERATOR_OR) {
            return ribos_lower_logical_expression(context, node);
        }
        operands[0] = ribos_lower_expression(context, node->first);
        operands[1] = ribos_lower_expression(context, node->second);
        return ribos_lower_emit_value(
            context,
            node,
            RIBOS_IR_OP_CHECKED_BINARY,
            node->inferred_type,
            operands,
            2,
            ribos_lower_checked_operator(node->flags),
            RIBOS_IR_INVALID_ID,
            0);
    case RIBOS_AST_IF_EXPRESSION:
        return ribos_lower_if_expression(context, node);
    default:
        ribos_lower_fail(context, node);
        return RIBOS_IR_INVALID_ID;
    }
}

static void ribos_lower_statement(
    RibosLowerContext *context,
    RibosAstNode *statement);

static void
ribos_lower_block(RibosLowerContext *context, RibosAstNode *block)
{
    size_t binding_mark = context->binding_count;
    Py_ssize_t index;

    ++context->scope_depth;
    for (index = 0; block != NULL && block->items != NULL &&
         index < block->items->size; ++index) {
        if (context->terminated[context->current_block]) {
            break;
        }
        ribos_lower_statement(context, block->items->elements[index]);
    }
    context->binding_count = binding_mark;
    --context->scope_depth;
}

static void
ribos_lower_if_statement(
    RibosLowerContext *context,
    RibosAstNode *statement)
{
    uint32_t condition =
        ribos_lower_expression(context, statement->first);
    uint32_t then_block =
        ribos_lower_new_block(context, statement->second, 0);
    uint32_t else_block =
        ribos_lower_new_block(context, statement, 0);
    uint32_t merge_block =
        ribos_lower_new_block(context, statement, 0);
    uint32_t operands[1] = {condition};
    int then_terminated;
    int else_terminated;

    (void)ribos_lower_emit(
        context,
        statement,
        RIBOS_IR_OP_BRANCH,
        RIBOS_IR_INVALID_ID,
        operands,
        1,
        then_block,
        else_block,
        0);
    context->current_block = then_block;
    ribos_lower_block(context, statement->second);
    then_terminated = context->terminated[context->current_block];
    if (!then_terminated) {
        (void)ribos_lower_jump(context, statement, merge_block);
    }
    context->current_block = else_block;
    if (statement->third != NULL) {
        if (statement->third->kind == RIBOS_AST_BLOCK) {
            ribos_lower_block(context, statement->third);
        } else {
            ribos_lower_statement(context, statement->third);
        }
    }
    else_terminated = context->terminated[context->current_block];
    if (!else_terminated) {
        (void)ribos_lower_jump(context, statement, merge_block);
    }
    context->current_block = merge_block;
    if (then_terminated && else_terminated) {
        (void)ribos_lower_emit(
            context,
            statement,
            RIBOS_IR_OP_TRAP,
            RIBOS_IR_INVALID_ID,
            NULL,
            0,
            1,
            RIBOS_IR_INVALID_ID,
            0);
    }
}

static void
ribos_lower_for_statement(
    RibosLowerContext *context,
    RibosAstNode *statement)
{
    uint32_t iterable =
        ribos_lower_expression(context, statement->first);
    RibosType *iterable_type =
        &context->semantic->types[statement->first->inferred_type];
    uint32_t u32_type =
        ribos_lower_find_type_name(context->semantic, "u32");
    uint32_t bool_type =
        ribos_lower_find_type_name(context->semantic, "bool");
    uint32_t index_slot =
        ribos_lower_new_slot(context, u32_type, statement, RIBOS_LOWER_SLOT_BINDING);
    uint32_t zero = ribos_lower_emit_value(
        context,
        statement,
        RIBOS_IR_OP_CONST_INTEGER,
        u32_type,
        NULL,
        0,
        RIBOS_IR_INVALID_ID,
        RIBOS_IR_INVALID_ID,
        0);
    uint32_t move[1] = {zero};
    uint32_t condition_block;
    uint32_t body_block;
    uint32_t exit_block;
    uint32_t length;
    uint32_t compare;
    uint32_t compare_operands[2];
    uint32_t branch_operand[1];
    uint32_t latch_block = RIBOS_IR_INVALID_ID;
    uint32_t loop_id;
    size_t binding_mark;

    (void)ribos_lower_emit(
        context,
        statement,
        RIBOS_IR_OP_MOVE,
        index_slot,
        move,
        1,
        RIBOS_IR_INVALID_ID,
        RIBOS_IR_INVALID_ID,
        0);
    condition_block = ribos_lower_new_block(context, statement, 0);
    body_block = ribos_lower_new_block(context, statement->second, 0);
    exit_block = ribos_lower_new_block(context, statement, 0);
    (void)ribos_lower_jump(context, statement, condition_block);
    context->current_block = condition_block;
    length = ribos_lower_emit_value(
        context,
        statement,
        RIBOS_IR_OP_COLLECTION_LENGTH,
        u32_type,
        &iterable,
        1,
        iterable_type->bound,
        RIBOS_IR_INVALID_ID,
        0);
    compare_operands[0] = index_slot;
    compare_operands[1] = length;
    compare = ribos_lower_emit_value(
        context,
        statement,
        RIBOS_IR_OP_CHECKED_BINARY,
        bool_type,
        compare_operands,
        2,
        RIBOS_IR_CHECK_LESS,
        RIBOS_IR_INVALID_ID,
        0);
    branch_operand[0] = compare;
    (void)ribos_lower_emit(
        context,
        statement,
        RIBOS_IR_OP_BRANCH,
        RIBOS_IR_INVALID_ID,
        branch_operand,
        1,
        body_block,
        exit_block,
        iterable_type->bound);

    context->current_block = body_block;
    binding_mark = context->binding_count;
    ++context->scope_depth;
    {
        uint32_t index_operands[2] = {iterable, index_slot};
        uint32_t item = ribos_lower_emit_value(
            context,
            statement,
            RIBOS_IR_OP_INDEX,
            iterable_type->first,
            index_operands,
            2,
            0,
            RIBOS_IR_INVALID_ID,
            0);

        (void)ribos_lower_add_binding(
            context,
            statement->token,
            item,
            iterable_type->first,
            0);
    }
    ribos_lower_block(context, statement->second);
    context->binding_count = binding_mark;
    --context->scope_depth;
    if (!context->terminated[context->current_block]) {
        uint32_t one = ribos_lower_emit_value(
            context,
            statement,
            RIBOS_IR_OP_CONST_INTEGER,
            u32_type,
            NULL,
            0,
            RIBOS_IR_INVALID_ID,
            RIBOS_IR_INVALID_ID,
            1);
        uint32_t add_operands[2] = {index_slot, one};
        uint32_t next = ribos_lower_emit_value(
            context,
            statement,
            RIBOS_IR_OP_CHECKED_BINARY,
            u32_type,
            add_operands,
            2,
            RIBOS_IR_CHECK_ADD,
            RIBOS_IR_INVALID_ID,
            0);

        move[0] = next;
        (void)ribos_lower_emit(
            context,
            statement,
            RIBOS_IR_OP_MOVE,
            index_slot,
            move,
            1,
            RIBOS_IR_INVALID_ID,
            RIBOS_IR_INVALID_ID,
            0);
        latch_block = context->current_block;
        (void)ribos_lower_jump(context, statement, condition_block);
    }
    {
        RibosIrLoop loop = {
            .function_id = context->function_id,
            .header_block = condition_block,
            .body_block = body_block,
            .exit_block = exit_block,
            .latch_block = latch_block,
            .trip_count = iterable_type->bound,
            .source_map_id = ribos_lower_source_map(context, statement),
        };

        context->ir_status = ribos_ir_builder_add_loop(
            context->module,
            &loop,
            &loop_id);
        if (context->ir_status != RIBOS_IR_OK) {
            ribos_lower_fail(context, statement);
        }
    }
    context->current_block = exit_block;
}

static uint32_t
ribos_lower_pattern_tag(
    const RibosLowerContext *context,
    const RibosAstNode *pattern,
    const RibosType *matched_type)
{
    RibosAstNode *path = pattern->first;
    Token *name = ribos_lower_path_component(
        path,
        ribos_lower_path_count(path) - 1);
    Py_ssize_t index;

    if (matched_type->kind == RIBOS_TYPE_RESULT) {
        return ribos_lower_token_equals(name, "Ok") ? 0u : 1u;
    }
    if (matched_type->kind == RIBOS_TYPE_OPTION) {
        return ribos_lower_token_equals(name, "Some") ? 0u : 1u;
    }
    if (matched_type->declaration != NULL &&
        matched_type->declaration->items != NULL) {
        for (index = 0;
             index < matched_type->declaration->items->size;
             ++index) {
            RibosAstNode *variant =
                matched_type->declaration->items->elements[index];

            if (ribos_lower_tokens_equal(variant->token, name)) {
                return (uint32_t)index;
            }
        }
    }
    UNUSED(context);
    return 0;
}

static uint32_t
ribos_lower_pattern_payload_type(
    const RibosType *matched_type,
    uint32_t tag,
    size_t payload_index)
{
    if (matched_type->kind == RIBOS_TYPE_RESULT) {
        return tag == 0 ? matched_type->first : matched_type->second;
    }
    if (matched_type->kind == RIBOS_TYPE_OPTION) {
        return matched_type->first;
    }
    if (matched_type->declaration != NULL &&
        matched_type->declaration->items != NULL &&
        tag < (uint32_t)matched_type->declaration->items->size) {
        RibosAstNode *variant =
            matched_type->declaration->items->elements[tag];

        if (variant->items != NULL &&
            payload_index < (size_t)variant->items->size) {
            RibosAstNode *payload =
                variant->items->elements[payload_index];

            return payload->inferred_type;
        }
    }
    return 0;
}

static void
ribos_lower_match_statement(
    RibosLowerContext *context,
    RibosAstNode *statement)
{
    uint32_t value =
        ribos_lower_expression(context, statement->first);
    RibosType *matched_type =
        &context->semantic->types[statement->first->inferred_type];
    uint32_t tag_type =
        ribos_lower_find_type_name(context->semantic, "u32");
    uint32_t bool_type =
        ribos_lower_find_type_name(context->semantic, "bool");
    uint32_t tag = RIBOS_IR_INVALID_ID;
    uint32_t merge_block =
        ribos_lower_new_block(context, statement, 0);
    int every_arm_terminated = 1;
    Py_ssize_t index;

    for (index = 0; statement->items != NULL &&
         index < statement->items->size; ++index) {
        RibosAstNode *arm = statement->items->elements[index];
        uint32_t arm_block = ribos_lower_new_block(context, arm, 0);
        uint32_t next_test =
            ribos_lower_new_block(context, statement, 0);
        uint32_t tag_value = 0;
        size_t binding_mark;

        if (arm->first->flags == 1) {
            (void)ribos_lower_jump(context, arm, arm_block);
        } else {
            uint32_t compare_operands[2];
            uint32_t compare;
            uint32_t branch_operand[1];

            if (arm->first->flags == 3) {
                compare_operands[0] = value;
                compare_operands[1] =
                    ribos_lower_expression(context, arm->first->first);
            } else {
                uint32_t tag_constant;

                if (tag == RIBOS_IR_INVALID_ID) {
                    tag = ribos_lower_emit_value(
                        context,
                        statement,
                        RIBOS_IR_OP_VARIANT_TAG,
                        tag_type,
                        &value,
                        1,
                        RIBOS_IR_INVALID_ID,
                        RIBOS_IR_INVALID_ID,
                        0);
                }
                tag_value = ribos_lower_pattern_tag(
                    context,
                    arm->first,
                    matched_type);
                tag_constant = ribos_lower_emit_value(
                    context,
                    arm->first,
                    RIBOS_IR_OP_CONST_INTEGER,
                    tag_type,
                    NULL,
                    0,
                    RIBOS_IR_INVALID_ID,
                    RIBOS_IR_INVALID_ID,
                    tag_value);
                compare_operands[0] = tag;
                compare_operands[1] = tag_constant;
            }
            compare = ribos_lower_emit_value(
                context,
                arm->first,
                RIBOS_IR_OP_CHECKED_BINARY,
                bool_type,
                compare_operands,
                2,
                RIBOS_IR_CHECK_EQUAL,
                RIBOS_IR_INVALID_ID,
                0);
            branch_operand[0] = compare;
            (void)ribos_lower_emit(
                context,
                arm,
                RIBOS_IR_OP_BRANCH,
                RIBOS_IR_INVALID_ID,
                branch_operand,
                1,
                arm_block,
                next_test,
                0);
        }
        context->current_block = arm_block;
        binding_mark = context->binding_count;
        ++context->scope_depth;
        if (arm->first->items != NULL) {
            Py_ssize_t payload_index;

            for (payload_index = 0;
                 payload_index < arm->first->items->size;
                 ++payload_index) {
                RibosAstNode *binding =
                    arm->first->items->elements[payload_index];

                if (binding->flags != 1) {
                    uint32_t payload_type =
                        ribos_lower_pattern_payload_type(
                            matched_type,
                            tag_value,
                            (size_t)payload_index);
                uint32_t payload = ribos_lower_emit_value(
                    context,
                    arm->first,
                    RIBOS_IR_OP_VARIANT_PAYLOAD,
                    payload_type,
                    &value,
                    1,
                    tag_value,
                    (uint32_t)payload_index,
                    0);

                    (void)ribos_lower_add_binding(
                        context,
                        binding->token,
                        payload,
                        payload_type,
                        0);
                }
            }
        }
        ribos_lower_block(context, arm->second);
        context->binding_count = binding_mark;
        --context->scope_depth;
        if (!context->terminated[context->current_block]) {
            every_arm_terminated = 0;
            (void)ribos_lower_jump(context, arm, merge_block);
        }
        context->current_block = next_test;
    }
    (void)ribos_lower_emit(
        context,
        statement,
        RIBOS_IR_OP_TRAP,
        RIBOS_IR_INVALID_ID,
        NULL,
        0,
        2,
        RIBOS_IR_INVALID_ID,
        0);
    context->current_block = merge_block;
    if (every_arm_terminated) {
        (void)ribos_lower_emit(
            context,
            statement,
            RIBOS_IR_OP_TRAP,
            RIBOS_IR_INVALID_ID,
            NULL,
            0,
            3,
            RIBOS_IR_INVALID_ID,
            0);
    }
}

static void
ribos_lower_statement(
    RibosLowerContext *context,
    RibosAstNode *statement)
{
    if (statement == NULL ||
        context->semantic->status != RIBOS_COMPILE_OK) {
        return;
    }
    switch (statement->kind) {
    case RIBOS_AST_LET: {
        uint32_t value =
            ribos_lower_expression(context, statement->second);
        uint32_t binding = ribos_lower_new_slot(
            context,
            statement->second->inferred_type,
            statement,
            RIBOS_LOWER_SLOT_BINDING |
                (statement->flags != 0 ?
                    RIBOS_LOWER_SLOT_MUTABLE : 0));
        uint32_t operands[1] = {value};

        (void)ribos_lower_emit(
            context,
            statement,
            RIBOS_IR_OP_MOVE,
            binding,
            operands,
            1,
            RIBOS_IR_INVALID_ID,
            RIBOS_IR_INVALID_ID,
            0);
        (void)ribos_lower_add_binding(
            context,
            statement->token,
            binding,
            statement->second->inferred_type,
            statement->flags != 0);
        break;
    }
    case RIBOS_AST_ASSIGN: {
        RibosLowerBinding *binding =
            ribos_lower_find_binding(context, statement->first->token);
        uint32_t value =
            ribos_lower_expression(context, statement->second);
        uint32_t operands[1] = {value};

        if (binding == NULL) {
            ribos_lower_fail(context, statement);
            break;
        }
        (void)ribos_lower_emit(
            context,
            statement,
            RIBOS_IR_OP_MOVE,
            binding->slot,
            operands,
            1,
            RIBOS_IR_INVALID_ID,
            RIBOS_IR_INVALID_ID,
            0);
        break;
    }
    case RIBOS_AST_RETURN: {
        uint32_t value;

        if (statement->first == NULL) {
            value = ribos_lower_emit_value(
                context,
                statement,
                RIBOS_IR_OP_CONST_UNIT,
                context->function->return_type,
                NULL,
                0,
                RIBOS_IR_INVALID_ID,
                RIBOS_IR_INVALID_ID,
                0);
        } else {
            value = ribos_lower_expression(context, statement->first);
        }
        (void)ribos_lower_emit(
            context,
            statement,
            RIBOS_IR_OP_RETURN,
            RIBOS_IR_INVALID_ID,
            &value,
            1,
            RIBOS_IR_INVALID_ID,
            RIBOS_IR_INVALID_ID,
            0);
        break;
    }
    case RIBOS_AST_IF:
        ribos_lower_if_statement(context, statement);
        break;
    case RIBOS_AST_FOR:
        ribos_lower_for_statement(context, statement);
        break;
    case RIBOS_AST_MATCH:
        ribos_lower_match_statement(context, statement);
        break;
    default:
        (void)ribos_lower_expression(context, statement);
        break;
    }
}

static void
ribos_lower_copy_text(
    char *output,
    size_t output_size,
    const char *input,
    size_t input_size)
{
    if (input_size >= output_size) {
        input_size = output_size - 1;
    }
    if (input_size != 0) {
        memcpy(output, input, input_size);
    }
    output[input_size] = '\0';
}

static int
ribos_lower_types(RibosLowerContext *context)
{
    size_t index;
    uint32_t shape_offset = 0;

    for (index = 0;
         index < context->semantic->type_count;
         ++index) {
        const RibosType *source = &context->semantic->types[index];
        RibosIrType target = {
            .kind = (RibosIrTypeKind)source->kind,
            .first_type = source->first,
            .second_type = source->second,
            .bound = source->bound,
            .bits = source->bits,
            .shape_start = shape_offset,
        };
        const RibosSchemaType *schema_type = NULL;
        uint32_t id;

        if (source->declaration != NULL &&
            source->declaration->kind == RIBOS_AST_STRUCT) {
            target.kind = RIBOS_IR_TYPE_STRUCT;
            target.shape_count = source->declaration->items == NULL ?
                0 : (uint32_t)source->declaration->items->size;
        } else if (source->declaration != NULL &&
            source->declaration->kind == RIBOS_AST_ENUM) {
            Py_ssize_t variant_index;

            target.kind = RIBOS_IR_TYPE_ENUM;
            for (variant_index = 0;
                 source->declaration->items != NULL &&
                 variant_index < source->declaration->items->size;
                 ++variant_index) {
                RibosAstNode *variant =
                    source->declaration->items->elements[variant_index];

                ++target.shape_count;
                if (variant->items != NULL) {
                    target.shape_count +=
                        (uint32_t)variant->items->size;
                }
            }
        }
        if (target.kind == RIBOS_IR_TYPE_NAMED &&
            source->name != NULL) {
            schema_type = ribos_schema_find_type(
                context->semantic->schema,
                source->name,
                source->name_length);
            if (schema_type == NULL) {
                ribos_lower_fail(context, source->declaration);
                return 0;
            }
            if (schema_type->type_class == RIBOS_SCHEMA_TYPE_ENUM) {
                target.abi_size = 4;
                target.abi_alignment = 4;
            } else {
                target.abi_size = 8;
                target.abi_alignment = 8;
            }
        }
        if (target.shape_count > UINT32_MAX - shape_offset) {
            ribos_lower_fail(context, source->declaration);
            return 0;
        }
        shape_offset += target.shape_count;
        if (source->name != NULL) {
            ribos_lower_copy_text(
                target.name,
                sizeof(target.name),
                source->name,
                source->name_length);
        }
        context->ir_status = ribos_ir_builder_add_type(
            context->module,
            &target,
            &id);
        if (context->ir_status != RIBOS_IR_OK || id != index) {
            ribos_lower_fail(context, source->declaration);
            return 0;
        }
    }
    return 1;
}

static int
ribos_lower_shapes(RibosLowerContext *context)
{
    size_t type_index;

    for (type_index = 0;
         type_index < context->semantic->type_count;
         ++type_index) {
        RibosType *type = &context->semantic->types[type_index];
        Py_ssize_t member_index;

        if (type->declaration == NULL ||
            type->declaration->items == NULL) {
            continue;
        }
        if (type->declaration->kind == RIBOS_AST_STRUCT) {
            for (member_index = 0;
                 member_index < type->declaration->items->size;
                 ++member_index) {
                RibosAstNode *field =
                    type->declaration->items->elements[member_index];
                RibosIrShape shape = {
                    .kind = RIBOS_IR_SHAPE_STRUCT_FIELD,
                    .owner_type = (uint32_t)type_index,
                    .variant_tag = RIBOS_IR_INVALID_ID,
                    .ordinal = (uint32_t)member_index,
                    .value_type = field->first->inferred_type,
                };
                uint32_t shape_id;

                ribos_lower_copy_text(
                    shape.name,
                    sizeof(shape.name),
                    field->token->start,
                    field->token->length);
                context->ir_status = ribos_ir_builder_add_shape(
                    context->module,
                    &shape,
                    &shape_id);
                if (context->ir_status != RIBOS_IR_OK) {
                    ribos_lower_fail(context, field);
                    return 0;
                }
            }
        } else if (type->declaration->kind == RIBOS_AST_ENUM) {
            for (member_index = 0;
                 member_index < type->declaration->items->size;
                 ++member_index) {
                RibosAstNode *variant =
                    type->declaration->items->elements[member_index];
                RibosIrShape variant_shape = {
                    .kind = RIBOS_IR_SHAPE_ENUM_VARIANT,
                    .owner_type = (uint32_t)type_index,
                    .variant_tag = (uint32_t)member_index,
                    .ordinal = (uint32_t)member_index,
                    .value_type = RIBOS_IR_INVALID_ID,
                };
                uint32_t shape_id;
                Py_ssize_t payload_index;

                ribos_lower_copy_text(
                    variant_shape.name,
                    sizeof(variant_shape.name),
                    variant->token->start,
                    variant->token->length);
                context->ir_status = ribos_ir_builder_add_shape(
                    context->module,
                    &variant_shape,
                    &shape_id);
                if (context->ir_status != RIBOS_IR_OK) {
                    ribos_lower_fail(context, variant);
                    return 0;
                }
                for (payload_index = 0;
                     variant->items != NULL &&
                     payload_index < variant->items->size;
                     ++payload_index) {
                    RibosAstNode *payload =
                        variant->items->elements[payload_index];
                    RibosIrShape payload_shape = {
                        .kind = RIBOS_IR_SHAPE_ENUM_PAYLOAD,
                        .owner_type = (uint32_t)type_index,
                        .variant_tag = (uint32_t)member_index,
                        .ordinal = (uint32_t)payload_index,
                        .value_type = payload->inferred_type,
                    };

                    context->ir_status = ribos_ir_builder_add_shape(
                        context->module,
                        &payload_shape,
                        &shape_id);
                    if (context->ir_status != RIBOS_IR_OK) {
                        ribos_lower_fail(context, payload);
                        return 0;
                    }
                }
            }
        }
    }
    return 1;
}

static int
ribos_lower_declare_functions(RibosLowerContext *context)
{
    size_t index;

    for (index = 0;
         index < context->semantic->function_count;
         ++index) {
        RibosFunctionInfo *source = &context->semantic->functions[index];
        RibosIrFunction target = {
            .return_type = source->return_type,
            .entry_block = RIBOS_IR_INVALID_ID,
            .declared_capabilities = source->declared_capabilities,
            .required_capabilities = source->required_capabilities,
            .declared_instruction_budget = source->instruction_budget,
            .declared_helper_budget = source->helper_budget,
            .helper_call_upper_bound = source->total_helper_upper_bound,
            .maximum_call_depth = source->max_call_depth,
            .flags =
                (source->is_policy ? RIBOS_IR_FUNCTION_POLICY : 0) |
                (source->is_pure ? RIBOS_IR_FUNCTION_PURE : 0),
        };
        uint32_t id;

        ribos_lower_copy_text(
            target.name,
            sizeof(target.name),
            source->name->start,
            source->name->length);
        context->ir_status = ribos_ir_builder_add_function(
            context->module,
            &target,
            &id);
        if (context->ir_status != RIBOS_IR_OK || id != index) {
            ribos_lower_fail(context, source->declaration);
            return 0;
        }
    }
    return 1;
}

static int
ribos_lower_function(
    RibosLowerContext *context,
    size_t function_index)
{
    RibosFunctionInfo *function =
        &context->semantic->functions[function_index];
    RibosAstNode *declaration = function->declaration;
    RibosIrFunction ir_function = {
        .id = (uint32_t)function_index,
        .return_type = function->return_type,
        .first_block = context->next_block_id,
        .first_slot = context->next_slot_id,
        .parameter_start = context->next_slot_id,
        .parameter_count = (uint32_t)function->parameter_count,
        .declared_capabilities = function->declared_capabilities,
        .required_capabilities = function->required_capabilities,
        .declared_instruction_budget = function->instruction_budget,
        .declared_helper_budget = function->helper_budget,
        .helper_call_upper_bound = function->total_helper_upper_bound,
        .maximum_call_depth = function->max_call_depth,
        .flags =
            (function->is_policy ? RIBOS_IR_FUNCTION_POLICY : 0) |
            (function->is_pure ? RIBOS_IR_FUNCTION_PURE : 0),
    };
    uint32_t entry;
    size_t index;

    ribos_lower_copy_text(
        ir_function.name,
        sizeof(ir_function.name),
        function->name->start,
        function->name->length);
    context->function = function;
    context->function_id = (uint32_t)function_index;
    context->binding_count = 0;
    context->scope_depth = 0;
    entry = ribos_lower_new_block(
        context,
        declaration,
        RIBOS_LOWER_BLOCK_ENTRY);
    context->current_block = entry;
    ir_function.entry_block = entry;
    for (index = 0; index < function->parameter_count; ++index) {
        RibosAstNode *parameter =
            declaration->items->elements[index];
        uint32_t slot = ribos_lower_new_slot(
            context,
            function->parameters[index].type,
            parameter,
            RIBOS_LOWER_SLOT_PARAMETER | RIBOS_LOWER_SLOT_BINDING);

        (void)ribos_lower_emit(
            context,
            parameter,
            RIBOS_IR_OP_PARAMETER,
            slot,
            NULL,
            0,
            (uint32_t)index,
            RIBOS_IR_INVALID_ID,
            0);
        (void)ribos_lower_add_binding(
            context,
            function->parameters[index].name,
            slot,
            function->parameters[index].type,
            0);
    }
    ribos_lower_block(context, declaration->second);
    if (context->semantic->status != RIBOS_COMPILE_OK) {
        return 0;
    }
    if (!context->terminated[context->current_block]) {
        ribos_lower_fail(context, declaration);
        return 0;
    }
    ir_function.block_count =
        context->next_block_id - ir_function.first_block;
    ir_function.slot_count =
        context->next_slot_id - ir_function.first_slot;
    context->ir_status = ribos_ir_builder_update_function(
        context->module,
        &ir_function);
    if (context->ir_status != RIBOS_IR_OK) {
        ribos_lower_fail(context, declaration);
        return 0;
    }
    return 1;
}

RibosCompileStatus
ribos_lower_policy_ir(
    RibosSemanticContext *semantic,
    RibosIrModule *module)
{
    RibosLowerContext *context;
    RibosIrResourceClosure *resources = NULL;
    size_t index;

    if (semantic == NULL || module == NULL) {
        return RIBOS_COMPILE_INVALID_ARGUMENT;
    }
    context = calloc(1, sizeof(*context));
    if (context == NULL) {
        semantic->diagnostic->code = RIBOS_E_RESOURCE_LIMIT;
        (void)snprintf(
            semantic->diagnostic->message,
            sizeof(semantic->diagnostic->message),
            "Policy IR lowering context allocation failed");
        return RIBOS_COMPILE_NO_MEMORY;
    }
    context->semantic = semantic;
    context->module = module;
    for (index = 0; index < RIBOS_MAX_AST_NODES; ++index) {
        context->source_maps[index] = RIBOS_IR_INVALID_ID;
    }
    if (ribos_ir_builder_set_schema_identity(
            module,
            semantic->summary->schema_digest) != RIBOS_IR_OK ||
        !ribos_lower_types(context) ||
        !ribos_lower_shapes(context) ||
        !ribos_lower_declare_functions(context)) {
        free(context);
        return semantic->status;
    }
    for (index = 0;
         index < semantic->function_count &&
         semantic->status == RIBOS_COMPILE_OK;
         ++index) {
        (void)ribos_lower_function(context, index);
    }
    if (semantic->status == RIBOS_COMPILE_OK &&
        ribos_ir_validate_v1(module) != RIBOS_IR_OK) {
        ribos_lower_fail(context, semantic->parser->root);
    }
    if (semantic->status == RIBOS_COMPILE_OK) {
        resources = ribos_ir_resource_closure_create();
        if (resources == NULL) {
            semantic->status = RIBOS_COMPILE_NO_MEMORY;
            semantic->diagnostic->code = RIBOS_E_RESOURCE_LIMIT;
            (void)snprintf(
                semantic->diagnostic->message,
                sizeof(semantic->diagnostic->message),
                "Policy IR resource-closure allocation failed");
        } else {
            RibosIrStatus resource_status =
                ribos_ir_analyze_resources_v1(module, resources);

            if (resource_status == RIBOS_IR_OK) {
                resource_status = ribos_ir_enforce_resource_budgets_v1(
                    module,
                    resources);
            }
            if (resource_status == RIBOS_IR_BUDGET_EXCEEDED) {
                semantic->status = RIBOS_COMPILE_BOUND_ERROR;
                semantic->diagnostic->code =
                    RIBOS_E_INSTRUCTION_BUDGET_EXCEEDED;
                (void)snprintf(
                    semantic->diagnostic->message,
                    sizeof(semantic->diagnostic->message),
                    "Policy IR worst-case execution exceeds a declared budget");
            } else if (resource_status != RIBOS_IR_OK) {
                ribos_lower_fail(context, semantic->parser->root);
                (void)snprintf(
                    semantic->diagnostic->message,
                    sizeof(semantic->diagnostic->message),
                    "Policy IR resource closure failed with status %u",
                    (unsigned)resource_status);
            }
        }
    }
    if (semantic->status == RIBOS_COMPILE_OK && resources != NULL) {
        for (index = 0; index < semantic->function_count; ++index) {
            const RibosIrFunctionResource *resource =
                ribos_ir_resource_function(resources, (uint32_t)index);

            if (resource != NULL &&
                semantic->functions[index].is_policy) {
                semantic->summary->instruction_upper_bound =
                    resource->instruction_upper_bound;
                semantic->summary->helper_call_upper_bound =
                    resource->helper_call_upper_bound;
                semantic->summary->maximum_stack_bytes =
                    resource->maximum_stack_bytes;
                semantic->summary->frame_byte_upper_bound =
                    resource->frame_bytes;
                semantic->summary->aggregate_storage_upper_bound =
                    resource->aggregate_slot_bytes;
                semantic->summary->reachable_block_count =
                    resource->reachable_block_count;
            }
        }
        {
            RibosIrResourceSummary resource_summary;

            if (ribos_ir_resource_summary(
                    resources,
                    &resource_summary) == RIBOS_IR_OK) {
                semantic->summary->bounded_loop_count =
                    (uint32_t)resource_summary.loop_count;
            }
        }
    }
    ribos_ir_resource_closure_destroy(resources);
    free(context);
    return semantic->status;
}
