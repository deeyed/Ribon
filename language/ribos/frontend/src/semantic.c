#include "semantic_internal.h"

#include <inttypes.h>

static int
ribos_token_equals(const Token *token, const char *text)
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
ribos_tokens_equal(const Token *left, const Token *right)
{
    return left != NULL && right != NULL &&
        left->length == right->length &&
        memcmp(left->start, right->start, left->length) == 0;
}

static size_t
ribos_path_count(const RibosAstNode *path)
{
    if (path == NULL || path->kind != RIBOS_AST_PATH ||
        path->token == NULL) {
        return 0;
    }
    return 1 + (path->items == NULL ? 0 : (size_t)path->items->size);
}

static Token *
ribos_path_component(const RibosAstNode *path, size_t index)
{
    if (index == 0) {
        return path == NULL ? NULL : path->token;
    }
    if (path == NULL || path->items == NULL ||
        index > (size_t)path->items->size) {
        return NULL;
    }
    return path->items->elements[index - 1];
}

static int
ribos_path_equals(const RibosAstNode *path, const char *text)
{
    size_t component = 0;
    size_t offset = 0;
    size_t length = strlen(text);

    while (offset < length) {
        Token *token = ribos_path_component(path, component);
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
    return component == ribos_path_count(path);
}

static void
ribos_copy_text(
    char *output,
    size_t output_size,
    const char *text,
    size_t length)
{
    if (output_size == 0) {
        return;
    }
    if (length >= output_size) {
        length = output_size - 1;
    }
    if (length != 0) {
        memcpy(output, text, length);
    }
    output[length] = '\0';
}

static RibosCompileStatus
ribos_semantic_fail(
    RibosSemanticContext *context,
    RibosCompileStatus status,
    RibosCompileDiagnosticCode code,
    const RibosAstNode *node,
    const Token *symbol,
    const char *message,
    const char *expected,
    const char *actual)
{
    if (context->status != RIBOS_COMPILE_OK) {
        return context->status;
    }
    context->status = status;
    context->diagnostic->code = code;
    if (node != NULL) {
        context->diagnostic->span = node->span;
    }
    if (symbol != NULL) {
        ribos_copy_text(
            context->diagnostic->symbol,
            sizeof(context->diagnostic->symbol),
            symbol->start,
            symbol->length);
    }
    if (message != NULL) {
        ribos_copy_text(
            context->diagnostic->message,
            sizeof(context->diagnostic->message),
            message,
            strlen(message));
    }
    if (expected != NULL) {
        ribos_copy_text(
            context->diagnostic->expected,
            sizeof(context->diagnostic->expected),
            expected,
            strlen(expected));
    }
    if (actual != NULL) {
        ribos_copy_text(
            context->diagnostic->actual,
            sizeof(context->diagnostic->actual),
            actual,
            strlen(actual));
    }
    return status;
}

static uint32_t
ribos_add_type(
    RibosSemanticContext *context,
    RibosTypeKind kind,
    const char *name,
    size_t name_length,
    uint32_t first,
    uint32_t second,
    uint32_t bound,
    uint8_t bits,
    RibosAstNode *declaration)
{
    size_t index;
    RibosType *type;

    for (index = 0; index < context->type_count; ++index) {
        type = &context->types[index];
        if (type->kind == kind && type->first == first &&
            type->second == second && type->bound == bound &&
            type->bits == bits && type->name_length == name_length &&
            ((name == NULL && type->name == NULL) ||
             (name != NULL && type->name != NULL &&
              memcmp(type->name, name, name_length) == 0))) {
            if (declaration != NULL && type->declaration == NULL) {
                type->declaration = declaration;
            }
            return (uint32_t)index;
        }
    }
    if (context->type_count == RIBOS_MAX_TYPES) {
        (void)ribos_semantic_fail(
            context,
            RIBOS_COMPILE_BOUND_ERROR,
            RIBOS_E_RESOURCE_LIMIT,
            declaration,
            declaration == NULL ? NULL : declaration->token,
            "type table limit exceeded",
            NULL,
            NULL);
        return 0;
    }
    type = &context->types[context->type_count];
    *type = (RibosType){
        .kind = kind,
        .name = name,
        .name_length = name_length,
        .first = first,
        .second = second,
        .bound = bound,
        .bits = bits,
        .declaration = declaration,
    };
    return (uint32_t)context->type_count++;
}

static uint32_t
ribos_find_type_token(
    const RibosSemanticContext *context,
    const Token *name)
{
    size_t index;

    for (index = 0; index < context->type_count; ++index) {
        const RibosType *type = &context->types[index];

        if (type->name != NULL && type->name_length == name->length &&
            memcmp(type->name, name->start, name->length) == 0) {
            return (uint32_t)index;
        }
    }
    return 0;
}

static uint32_t
ribos_find_type_name(
    const RibosSemanticContext *context,
    const char *name)
{
    size_t index;
    size_t length = strlen(name);

    for (index = 0; index < context->type_count; ++index) {
        const RibosType *type = &context->types[index];

        if (type->name != NULL && type->name_length == length &&
            memcmp(type->name, name, length) == 0) {
            return (uint32_t)index;
        }
    }
    return 0;
}

static const char *
ribos_type_name(
    const RibosSemanticContext *context,
    uint32_t type_id,
    char *buffer,
    size_t buffer_size)
{
    const RibosType *type;

    if (type_id >= context->type_count) {
        return "<invalid-type>";
    }
    type = &context->types[type_id];
    if (type->name != NULL) {
        ribos_copy_text(
            buffer,
            buffer_size,
            type->name,
            type->name_length);
        return buffer;
    }
    switch (type->kind) {
    case RIBOS_TYPE_ARRAY:
        (void)snprintf(buffer, buffer_size, "Array[...,%u]", type->bound);
        break;
    case RIBOS_TYPE_LIST:
        (void)snprintf(buffer, buffer_size, "List[...,%u]", type->bound);
        break;
    case RIBOS_TYPE_FROZEN_MAP:
        (void)snprintf(
            buffer,
            buffer_size,
            "FrozenMap[...,%u]",
            type->bound);
        break;
    case RIBOS_TYPE_DICT:
        (void)snprintf(buffer, buffer_size, "Dict[...,%u]", type->bound);
        break;
    case RIBOS_TYPE_OPTION:
        (void)snprintf(buffer, buffer_size, "Option[...]");
        break;
    case RIBOS_TYPE_RESULT:
        (void)snprintf(buffer, buffer_size, "Result[...,...]");
        break;
    case RIBOS_TYPE_STRING_LITERAL:
        (void)snprintf(
            buffer,
            buffer_size,
            "StringLiteral[%u]",
            type->bound);
        break;
    default:
        (void)snprintf(buffer, buffer_size, "<type-%u>", type_id);
        break;
    }
    return buffer;
}

static uint64_t
ribos_saturating_add(uint64_t left, uint64_t right)
{
    return UINT64_MAX - left < right ? UINT64_MAX : left + right;
}

static uint64_t
ribos_saturating_multiply(uint64_t left, uint64_t right)
{
    return left != 0 && right > UINT64_MAX / left ?
        UINT64_MAX : left * right;
}

static RibosStaticCost
ribos_cost_add(RibosStaticCost left, RibosStaticCost right)
{
    size_t index;

    left.helpers = ribos_saturating_add(left.helpers, right.helpers);
    for (index = 0; index < RIBOS_MAX_FUNCTIONS; ++index) {
        left.calls[index] = ribos_saturating_add(
            left.calls[index],
            right.calls[index]);
    }
    return left;
}

static RibosStaticCost
ribos_cost_max(RibosStaticCost left, RibosStaticCost right)
{
    size_t index;

    if (right.helpers > left.helpers) {
        left.helpers = right.helpers;
    }
    for (index = 0; index < RIBOS_MAX_FUNCTIONS; ++index) {
        if (right.calls[index] > left.calls[index]) {
            left.calls[index] = right.calls[index];
        }
    }
    return left;
}

static RibosStaticCost
ribos_cost_multiply(RibosStaticCost value, uint64_t multiplier)
{
    size_t index;

    value.helpers = ribos_saturating_multiply(value.helpers, multiplier);
    for (index = 0; index < RIBOS_MAX_FUNCTIONS; ++index) {
        value.calls[index] = ribos_saturating_multiply(
            value.calls[index],
            multiplier);
    }
    return value;
}

static int
ribos_parse_u64_token(const Token *token, uint64_t *value)
{
    unsigned base = 10;
    size_t index = 0;
    uint64_t result = 0;

    if (token == NULL || token->length == 0) {
        return 0;
    }
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
        unsigned digit;
        unsigned char byte = (unsigned char)token->start[index];

        if (byte == '_') {
            continue;
        }
        if (byte >= '0' && byte <= '9') {
            digit = byte - '0';
        } else if (byte >= 'a' && byte <= 'f') {
            digit = byte - 'a' + 10;
        } else if (byte >= 'A' && byte <= 'F') {
            digit = byte - 'A' + 10;
        } else {
            return 0;
        }
        if (digit >= base || result > (UINT64_MAX - digit) / base) {
            return 0;
        }
        result = result * base + digit;
    }
    *value = result;
    return 1;
}

static uint32_t
ribos_resolve_type_expression(
    RibosSemanticContext *context,
    RibosAstNode *node);

static uint32_t
ribos_resolve_named_type(
    RibosSemanticContext *context,
    RibosAstNode *path,
    RibosAstNode *owner)
{
    Token *name;
    uint32_t type;

    if (ribos_path_count(path) != 1) {
        (void)ribos_semantic_fail(
            context,
            RIBOS_COMPILE_TYPE_ERROR,
            RIBOS_E_UNKNOWN_TYPE,
            owner,
            path == NULL ? NULL : path->token,
            "qualified type is not declared",
            NULL,
            NULL);
        return 0;
    }
    name = path->token;
    type = ribos_find_type_token(context, name);
    if (type == 0) {
        (void)ribos_semantic_fail(
            context,
            RIBOS_COMPILE_TYPE_ERROR,
            RIBOS_E_UNKNOWN_TYPE,
            owner,
            name,
            "type name is not declared by Ribos or the selected schema",
            NULL,
            NULL);
    }
    return type;
}

static uint32_t
ribos_resolve_type_expression(
    RibosSemanticContext *context,
    RibosAstNode *node)
{
    RibosAstNode *path;
    Token *name;
    size_t argument_count;
    uint32_t first = 0;
    uint32_t second = 0;
    uint64_t bound = 0;

    if (node == NULL || node->kind != RIBOS_AST_TYPE ||
        node->first == NULL) {
        (void)ribos_semantic_fail(
            context,
            RIBOS_COMPILE_INTERNAL_ERROR,
            RIBOS_E_INTERNAL,
            node,
            NULL,
            "malformed type AST",
            NULL,
            NULL);
        return 0;
    }
    path = node->first;
    name = path->token;
    argument_count = node->items == NULL ? 0 : (size_t)node->items->size;

    if (argument_count == 0) {
        node->inferred_type = ribos_resolve_named_type(context, path, node);
        return node->inferred_type;
    }
    if (ribos_path_count(path) != 1) {
        return ribos_resolve_named_type(context, path, node);
    }
    if (ribos_token_equals(name, "Option") && argument_count == 1) {
        first = ribos_resolve_type_expression(
            context,
            node->items->elements[0]);
        node->inferred_type = ribos_add_type(
            context,
            RIBOS_TYPE_OPTION,
            NULL,
            0,
            first,
            0,
            0,
            0,
            NULL);
        return node->inferred_type;
    }
    if (ribos_token_equals(name, "Result") && argument_count == 2) {
        first = ribos_resolve_type_expression(
            context,
            node->items->elements[0]);
        second = ribos_resolve_type_expression(
            context,
            node->items->elements[1]);
        node->inferred_type = ribos_add_type(
            context,
            RIBOS_TYPE_RESULT,
            NULL,
            0,
            first,
            second,
            0,
            0,
            NULL);
        return node->inferred_type;
    }
    if ((ribos_token_equals(name, "Array") ||
         ribos_token_equals(name, "List")) &&
        argument_count == 2) {
        RibosAstNode *bound_node = node->items->elements[1];

        first = ribos_resolve_type_expression(
            context,
            node->items->elements[0]);
        if (bound_node->kind != RIBOS_AST_INTEGER ||
            !ribos_parse_u64_token(bound_node->token, &bound) ||
            bound == 0 || bound > UINT32_MAX) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_BOUND_ERROR,
                RIBOS_E_COLLECTION_BOUND_EXCEEDED,
                bound_node,
                bound_node->token,
                "collection capacity must be a non-zero u32 constant",
                NULL,
                NULL);
            return 0;
        }
        node->inferred_type = ribos_add_type(
            context,
            ribos_token_equals(name, "Array") ?
                RIBOS_TYPE_ARRAY : RIBOS_TYPE_LIST,
            NULL,
            0,
            first,
            0,
            (uint32_t)bound,
            0,
            NULL);
        return node->inferred_type;
    }
    if ((ribos_token_equals(name, "FrozenMap") ||
         ribos_token_equals(name, "Dict")) &&
        argument_count == 3) {
        RibosAstNode *bound_node = node->items->elements[2];

        first = ribos_resolve_type_expression(
            context,
            node->items->elements[0]);
        second = ribos_resolve_type_expression(
            context,
            node->items->elements[1]);
        if (bound_node->kind != RIBOS_AST_INTEGER ||
            !ribos_parse_u64_token(bound_node->token, &bound) ||
            bound == 0 || bound > UINT32_MAX) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_BOUND_ERROR,
                RIBOS_E_COLLECTION_BOUND_EXCEEDED,
                bound_node,
                bound_node->token,
                "map capacity must be a non-zero u32 constant",
                NULL,
                NULL);
            return 0;
        }
        node->inferred_type = ribos_add_type(
            context,
            ribos_token_equals(name, "FrozenMap") ?
                RIBOS_TYPE_FROZEN_MAP : RIBOS_TYPE_DICT,
            NULL,
            0,
            first,
            second,
            (uint32_t)bound,
            0,
            NULL);
        return node->inferred_type;
    }
    if (ribos_token_equals(name, "StringLiteral") && argument_count == 1) {
        RibosAstNode *bound_node = node->items->elements[0];

        if (bound_node->kind != RIBOS_AST_INTEGER ||
            !ribos_parse_u64_token(bound_node->token, &bound) ||
            bound > UINT32_MAX) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_BOUND_ERROR,
                RIBOS_E_COLLECTION_BOUND_EXCEEDED,
                bound_node,
                bound_node->token,
                "string bound must be a u32 constant",
                NULL,
                NULL);
            return 0;
        }
        node->inferred_type = ribos_add_type(
            context,
            RIBOS_TYPE_STRING_LITERAL,
            NULL,
            0,
            0,
            0,
            (uint32_t)bound,
            0,
            NULL);
        return node->inferred_type;
    }
    (void)ribos_semantic_fail(
        context,
        RIBOS_COMPILE_TYPE_ERROR,
        RIBOS_E_UNKNOWN_TYPE,
        node,
        name,
        "type constructor or type argument arity is not supported",
        NULL,
        NULL);
    return 0;
}

static RibosFunctionInfo *
ribos_find_function(
    RibosSemanticContext *context,
    const Token *name,
    size_t *index_out)
{
    size_t index;

    for (index = 0; index < context->function_count; ++index) {
        if (ribos_tokens_equal(context->functions[index].name, name)) {
            if (index_out != NULL) {
                *index_out = index;
            }
            return &context->functions[index];
        }
    }
    return NULL;
}

static RibosLocal *
ribos_find_local(RibosSemanticContext *context, const Token *name)
{
    size_t index = context->local_count;

    while (index != 0) {
        --index;
        if (ribos_tokens_equal(context->locals[index].name, name)) {
            return &context->locals[index];
        }
    }
    return NULL;
}

static int
ribos_enter_scope(RibosSemanticContext *context)
{
    if (context->scope_depth == RIBOS_MAX_SCOPE_DEPTH) {
        (void)ribos_semantic_fail(
            context,
            RIBOS_COMPILE_BOUND_ERROR,
            RIBOS_E_RESOURCE_LIMIT,
            context->function == NULL ?
                NULL : context->function->declaration,
            NULL,
            "semantic scope depth limit exceeded",
            NULL,
            NULL);
        return 0;
    }
    ++context->scope_depth;
    if (context->scope_depth > context->max_scope_depth) {
        context->max_scope_depth = context->scope_depth;
    }
    return 1;
}

static void
ribos_leave_scope(RibosSemanticContext *context, size_t local_mark)
{
    context->local_count = local_mark;
    if (context->scope_depth != 0) {
        --context->scope_depth;
    }
}

static int
ribos_add_local(
    RibosSemanticContext *context,
    Token *name,
    uint32_t type,
    int mutable_binding,
    RibosAstNode *owner)
{
    size_t index = context->local_count;

    while (index != 0) {
        --index;
        if (context->locals[index].depth != context->scope_depth) {
            break;
        }
        if (ribos_tokens_equal(context->locals[index].name, name)) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_NAME_ERROR,
                RIBOS_E_DUPLICATE_BINDING,
                owner,
                name,
                "binding is already declared in this scope",
                NULL,
                NULL);
            return 0;
        }
    }
    if (context->local_count == RIBOS_MAX_LOCALS) {
        (void)ribos_semantic_fail(
            context,
            RIBOS_COMPILE_BOUND_ERROR,
            RIBOS_E_RESOURCE_LIMIT,
            owner,
            name,
            "local binding limit exceeded",
            NULL,
            NULL);
        return 0;
    }
    context->locals[context->local_count++] = (RibosLocal){
        .name = name,
        .type = type,
        .depth = context->scope_depth,
        .mutable_binding = mutable_binding != 0,
    };
    return 1;
}

static uint32_t
ribos_struct_field_type(
    RibosSemanticContext *context,
    uint32_t owner_type,
    Token *field)
{
    RibosType *type;
    Py_ssize_t index;

    if (owner_type >= context->type_count) {
        return 0;
    }
    type = &context->types[owner_type];
    if (type->declaration != NULL &&
        type->declaration->kind == RIBOS_AST_STRUCT &&
        type->declaration->items != NULL) {
        for (index = 0; index < type->declaration->items->size; ++index) {
            RibosAstNode *member = type->declaration->items->elements[index];

            if (ribos_tokens_equal(member->token, field)) {
                return member->first->inferred_type;
            }
        }
    }
    if (type->name != NULL) {
        const RibosSchemaMember *member = ribos_schema_find_member(
            context->schema,
            type->name,
            type->name_length,
            field->start,
            field->length);

        if (member != NULL && member->result_type != NULL) {
            return ribos_find_type_name(context, member->result_type);
        }
        if (member != NULL && member->collection_element_type != NULL) {
            return ribos_add_type(
                context,
                RIBOS_TYPE_LIST,
                NULL,
                0,
                ribos_find_type_name(
                    context,
                    member->collection_element_type),
                0,
                member->collection_bound,
                0,
                NULL);
        }
    }
    return 0;
}

static int
ribos_type_is_integer(
    const RibosSemanticContext *context,
    uint32_t type)
{
    return type < context->type_count &&
        (context->types[type].kind == RIBOS_TYPE_UNSIGNED ||
         context->types[type].kind == RIBOS_TYPE_SIGNED);
}

static int
ribos_type_compatible(
    const RibosSemanticContext *context,
    uint32_t expected,
    uint32_t actual)
{
    if (expected == actual) {
        return 1;
    }
    if (expected >= context->type_count || actual >= context->type_count) {
        return 0;
    }
    if (context->types[expected].kind == RIBOS_TYPE_STRING_LITERAL &&
        context->types[actual].kind == RIBOS_TYPE_STRING_LITERAL) {
        return context->types[actual].bound <= context->types[expected].bound;
    }
    return 0;
}

static uint32_t ribos_infer_expression(
    RibosSemanticContext *context,
    RibosAstNode *node,
    uint32_t expected,
    RibosStaticCost *cost);

static uint32_t
ribos_infer_path(
    RibosSemanticContext *context,
    RibosAstNode *node)
{
    Token *first = ribos_path_component(node, 0);
    RibosLocal *local;
    uint32_t type;
    size_t component;

    local = ribos_find_local(context, first);
    if (local != NULL) {
        type = local->type;
        for (component = 1;
             component < ribos_path_count(node);
             ++component) {
            Token *member = ribos_path_component(node, component);
            uint32_t member_type =
                ribos_struct_field_type(context, type, member);

            if (member_type == 0) {
                char actual[96];

                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_TYPE_ERROR,
                    RIBOS_E_UNKNOWN_MEMBER,
                    node,
                    member,
                    "member is not present on the value type",
                    NULL,
                    ribos_type_name(
                        context,
                        type,
                        actual,
                        sizeof(actual)));
                return 0;
            }
            type = member_type;
        }
        node->inferred_type = type;
        return type;
    }
    type = ribos_find_type_token(context, first);
    if (type != 0 && ribos_path_count(node) >= 2) {
        node->inferred_type = type;
        return type;
    }
    if (ribos_token_equals(first, "Unit") &&
        ribos_path_count(node) == 1) {
        type = ribos_find_type_name(context, "Unit");
        node->inferred_type = type;
        return type;
    }
    (void)ribos_semantic_fail(
        context,
        RIBOS_COMPILE_NAME_ERROR,
        RIBOS_E_UNKNOWN_NAME,
        node,
        first,
        "value name is not declared in this scope or schema",
        NULL,
        NULL);
    return 0;
}

static const RibosSchemaHelper *
ribos_find_helper(
    const RibosSemanticContext *context,
    const RibosAstNode *path)
{
    size_t index;

    for (index = 0; index < context->schema->helper_count; ++index) {
        if (ribos_path_equals(path, context->schema->helpers[index].path)) {
            return &context->schema->helpers[index];
        }
    }
    return NULL;
}

static RibosAstNode *
ribos_call_argument_at(
    RibosAstNode *call,
    const char *name,
    size_t position)
{
    Py_ssize_t index;

    if (call->items == NULL) {
        return NULL;
    }
    for (index = 0; index < call->items->size; ++index) {
        RibosAstNode *argument = call->items->elements[index];

        if (argument->flags == 0 && (size_t)index == position) {
            return argument;
        }
        if (argument->flags != 0 && name != NULL &&
            ribos_token_equals(argument->token, name)) {
            return argument;
        }
    }
    return NULL;
}

static uint32_t
ribos_literal_expected_type(
    const RibosSemanticContext *context,
    const RibosAstNode *node,
    uint32_t expected)
{
    if (node == NULL) {
        return 0;
    }
    if (expected >= context->type_count) {
        return 0;
    }
    if (node->kind == RIBOS_AST_INTEGER) {
        return ribos_type_is_integer(context, expected) ? expected : 0;
    }
    if (node->kind == RIBOS_AST_STRING) {
        return context->types[expected].kind == RIBOS_TYPE_STRING_LITERAL ?
            expected : 0;
    }
    if (node->kind == RIBOS_AST_NONE) {
        return context->types[expected].kind == RIBOS_TYPE_OPTION ?
            expected : 0;
    }
    return 0;
}

static uint32_t
ribos_helper_result_type(
    RibosSemanticContext *context,
    const RibosSchemaHelper *helper)
{
    uint32_t value =
        ribos_find_type_name(context, helper->result_type);

    if (helper->error_type == NULL) {
        return value;
    }
    return ribos_add_type(
        context,
        RIBOS_TYPE_RESULT,
        NULL,
        0,
        value,
        ribos_find_type_name(context, helper->error_type),
        0,
        0,
        NULL);
}

static uint32_t
ribos_infer_helper_call(
    RibosSemanticContext *context,
    RibosAstNode *call,
    const RibosSchemaHelper *helper,
    RibosStaticCost *cost)
{
    size_t argument_count =
        call->items == NULL ? 0 : (size_t)call->items->size;
    size_t index;

    if (argument_count != helper->parameter_count) {
        (void)ribos_semantic_fail(
            context,
            RIBOS_COMPILE_TYPE_ERROR,
            RIBOS_E_ARGUMENT_COUNT_MISMATCH,
            call,
            call->first == NULL ? NULL : call->first->token,
            "helper argument count does not match its schema",
            NULL,
            NULL);
        return 0;
    }
    for (index = 0; index < helper->parameter_count; ++index) {
        RibosAstNode *argument = ribos_call_argument_at(
            call,
            helper->parameters[index].name,
            index);
        uint32_t expected = 0;
        uint32_t actual;
        RibosStaticCost argument_cost = {0};

        if (argument == NULL) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_ARGUMENT_COUNT_MISMATCH,
                call,
                NULL,
                "required helper argument is missing",
                helper->parameters[index].name,
                NULL);
            return 0;
        }
        if (strcmp(helper->parameters[index].type, "*") != 0) {
            expected = ribos_find_type_name(
                context,
                helper->parameters[index].type);
        } else if (strcmp(helper->path, "handoff.set") == 0 &&
            index == 1) {
            RibosAstNode *key_argument =
                ribos_call_argument_at(call, "key", 0);
            RibosAstNode *key = key_argument == NULL ?
                NULL : key_argument->first;
            Token *key_name = key == NULL ||
                key->kind != RIBOS_AST_PATH ||
                ribos_path_count(key) != 2 ||
                !ribos_token_equals(
                    ribos_path_component(key, 0),
                    "HandoffKey") ?
                    NULL : ribos_path_component(key, 1);

            const RibosSchemaHandoffField *field =
                key_name == NULL ? NULL :
                ribos_schema_find_handoff_field(
                    context->schema,
                    key_name->start,
                    key_name->length);

            if (field != NULL) {
                expected =
                    ribos_find_type_name(context, field->value_type);
            } else {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_TYPE_ERROR,
                    RIBOS_E_ARGUMENT_TYPE_MISMATCH,
                    key,
                    key_name,
                    "handoff.set key is absent from the selected schema",
                    "known HandoffKey",
                    NULL);
                return 0;
            }
        }
        actual = ribos_infer_expression(
            context,
            argument->first,
            ribos_literal_expected_type(
                context,
                argument->first,
                expected),
            &argument_cost);
        *cost = ribos_cost_add(*cost, argument_cost);
        if (expected != 0 &&
            !ribos_type_compatible(context, expected, actual)) {
            char expected_name[96];
            char actual_name[96];

            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_ARGUMENT_TYPE_MISMATCH,
                argument,
                argument->token,
                "helper argument type does not match its schema",
                ribos_type_name(
                    context,
                    expected,
                    expected_name,
                    sizeof(expected_name)),
                ribos_type_name(
                    context,
                    actual,
                    actual_name,
                    sizeof(actual_name)));
            return 0;
        }
    }
    ++cost->helpers;
    ++context->function->helper_call_sites;
    context->function->direct_capabilities |= helper->capabilities;
    call->inferred_type = ribos_helper_result_type(context, helper);
    return call->inferred_type;
}

static uint32_t
ribos_infer_user_call(
    RibosSemanticContext *context,
    RibosAstNode *call,
    RibosFunctionInfo *function,
    size_t function_index,
    RibosStaticCost *cost)
{
    size_t argument_count =
        call->items == NULL ? 0 : (size_t)call->items->size;
    size_t index;

    if (argument_count != function->parameter_count) {
        (void)ribos_semantic_fail(
            context,
            RIBOS_COMPILE_TYPE_ERROR,
            RIBOS_E_ARGUMENT_COUNT_MISMATCH,
            call,
            function->name,
            "function argument count does not match its declaration",
            NULL,
            NULL);
        return 0;
    }
    for (index = 0; index < function->parameter_count; ++index) {
        RibosAstNode *argument = ribos_call_argument_at(
            call,
            NULL,
            index);
        RibosStaticCost argument_cost = {0};
        uint32_t actual;

        if (argument == NULL || argument->flags != 0) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_ARGUMENT_COUNT_MISMATCH,
                call,
                function->name,
                "user function calls use positional arguments",
                NULL,
                NULL);
            return 0;
        }
        actual = ribos_infer_expression(
            context,
            argument->first,
            function->parameters[index].type,
            &argument_cost);
        *cost = ribos_cost_add(*cost, argument_cost);
        if (!ribos_type_compatible(
                context,
                function->parameters[index].type,
                actual)) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_ARGUMENT_TYPE_MISMATCH,
                argument,
                NULL,
                "function argument type mismatch",
                NULL,
                NULL);
            return 0;
        }
    }
    cost->calls[function_index] =
        ribos_saturating_add(cost->calls[function_index], 1);
    call->inferred_type = function->return_type;
    return call->inferred_type;
}

static uint32_t
ribos_infer_constructor_call(
    RibosSemanticContext *context,
    RibosAstNode *call,
    uint32_t type_id,
    RibosStaticCost *cost)
{
    RibosType *type = &context->types[type_id];
    RibosAstNode *callee = call->first;
    Py_ssize_t index;
    size_t argument_count =
        call->items == NULL ? 0 : (size_t)call->items->size;
    size_t field_count;

    if (type->declaration == NULL) {
        return 0;
    }
    if (type->declaration->kind == RIBOS_AST_ENUM) {
        Token *variant_name = ribos_path_component(callee, 1);

        for (index = 0;
             type->declaration->items != NULL &&
             index < type->declaration->items->size;
             ++index) {
            RibosAstNode *variant =
                type->declaration->items->elements[index];
            size_t payload_count = variant->items == NULL ?
                0 : (size_t)variant->items->size;
            size_t payload_index;

            if (!ribos_tokens_equal(variant->token, variant_name)) {
                continue;
            }
            if (argument_count != payload_count) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_TYPE_ERROR,
                    RIBOS_E_ARGUMENT_COUNT_MISMATCH,
                    call,
                    variant_name,
                    "enum constructor payload arity mismatch",
                    NULL,
                    NULL);
                return 0;
            }
            for (payload_index = 0;
                 payload_index < payload_count;
                 ++payload_index) {
                RibosAstNode *argument = ribos_call_argument_at(
                    call,
                    NULL,
                    payload_index);
                RibosAstNode *payload =
                    variant->items->elements[payload_index];
                RibosStaticCost argument_cost = {0};
                uint32_t actual;

                if (argument == NULL || argument->flags != 0) {
                    (void)ribos_semantic_fail(
                        context,
                        RIBOS_COMPILE_TYPE_ERROR,
                        RIBOS_E_ARGUMENT_COUNT_MISMATCH,
                        call,
                        variant_name,
                        "enum constructor arguments must be positional",
                        NULL,
                        NULL);
                    return 0;
                }
                actual = ribos_infer_expression(
                    context,
                    argument->first,
                    payload->inferred_type,
                    &argument_cost);
                *cost = ribos_cost_add(*cost, argument_cost);
                if (!ribos_type_compatible(
                        context,
                        payload->inferred_type,
                        actual)) {
                    (void)ribos_semantic_fail(
                        context,
                        RIBOS_COMPILE_TYPE_ERROR,
                        RIBOS_E_ARGUMENT_TYPE_MISMATCH,
                        argument,
                        variant_name,
                        "enum constructor payload type mismatch",
                        NULL,
                        NULL);
                    return 0;
                }
            }
            call->inferred_type = type_id;
            return type_id;
        }
        (void)ribos_semantic_fail(
            context,
            RIBOS_COMPILE_TYPE_ERROR,
            RIBOS_E_UNKNOWN_MEMBER,
            call,
            variant_name,
            "enum variant is not declared",
            NULL,
            NULL);
        return 0;
    }
    if (type->declaration->kind != RIBOS_AST_STRUCT) {
        return 0;
    }
    field_count = type->declaration->items == NULL ?
        0 : (size_t)type->declaration->items->size;
    if (argument_count != field_count) {
        (void)ribos_semantic_fail(
            context,
            RIBOS_COMPILE_TYPE_ERROR,
            RIBOS_E_ARGUMENT_COUNT_MISMATCH,
            call,
            type->declaration->token,
            "struct constructor requires every field exactly once",
            NULL,
            NULL);
        return 0;
    }
    for (index = 0; index < type->declaration->items->size; ++index) {
        RibosAstNode *field = type->declaration->items->elements[index];
        RibosAstNode *argument = call->items->elements[index];
        RibosStaticCost argument_cost = {0};
        uint32_t actual;

        if (argument == NULL || argument->flags == 0 ||
            !ribos_tokens_equal(argument->token, field->token)) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_ARGUMENT_COUNT_MISMATCH,
                call,
                field->token,
                "struct constructor arguments must be named in field order",
                NULL,
                NULL);
            return 0;
        }
        actual = ribos_infer_expression(
            context,
            argument->first,
            field->first->inferred_type,
            &argument_cost);
        *cost = ribos_cost_add(*cost, argument_cost);
        if (!ribos_type_compatible(
                context,
                field->first->inferred_type,
                actual)) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_ARGUMENT_TYPE_MISMATCH,
                argument,
                field->token,
                "struct field argument type mismatch",
                NULL,
                NULL);
            return 0;
        }
    }
    call->inferred_type = type_id;
    return type_id;
}

static uint32_t
ribos_infer_call(
    RibosSemanticContext *context,
    RibosAstNode *node,
    uint32_t expected,
    RibosStaticCost *cost)
{
    RibosAstNode *callee = node->first;

    if (callee != NULL && callee->kind == RIBOS_AST_PATH) {
        const RibosSchemaHelper *helper =
            ribos_find_helper(context, callee);
        RibosFunctionInfo *function;
        size_t function_index;
        uint32_t type_id;
        RibosLocal *receiver_local = ribos_find_local(
            context,
            ribos_path_component(callee, 0));

        if (receiver_local != NULL && ribos_path_count(callee) == 2 &&
            ribos_token_equals(ribos_path_component(callee, 1), "get") &&
            receiver_local->type < context->type_count &&
            (context->types[receiver_local->type].kind ==
                 RIBOS_TYPE_FROZEN_MAP ||
             context->types[receiver_local->type].kind ==
                 RIBOS_TYPE_DICT)) {
            RibosType *map_type =
                &context->types[receiver_local->type];
            RibosAstNode *key = ribos_call_argument_at(node, NULL, 0);
            RibosAstNode *fallback = ribos_call_argument_at(
                node,
                "default",
                1);
            RibosStaticCost key_cost = {0};
            RibosStaticCost fallback_cost = {0};
            uint32_t key_type;
            uint32_t value_type;

            if (key == NULL || fallback == NULL ||
                node->items == NULL || node->items->size != 2) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_TYPE_ERROR,
                    RIBOS_E_ARGUMENT_COUNT_MISMATCH,
                    node,
                    ribos_path_component(callee, 1),
                    "map.get requires key and named default",
                    NULL,
                    NULL);
                return 0;
            }
            key_type = ribos_infer_expression(
                context,
                key->first,
                map_type->first,
                &key_cost);
            value_type = ribos_infer_expression(
                context,
                fallback->first,
                map_type->second,
                &fallback_cost);
            *cost = ribos_cost_add(*cost, key_cost);
            *cost = ribos_cost_add(*cost, fallback_cost);
            if (!ribos_type_compatible(
                    context,
                    map_type->first,
                    key_type) ||
                !ribos_type_compatible(
                    context,
                    map_type->second,
                    value_type)) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_TYPE_ERROR,
                    RIBOS_E_ARGUMENT_TYPE_MISMATCH,
                    node,
                    ribos_path_component(callee, 1),
                    "map.get key or default type mismatch",
                    NULL,
                    NULL);
                return 0;
            }
            node->inferred_type = map_type->second;
            return node->inferred_type;
        }

        if (helper != NULL) {
            return ribos_infer_helper_call(context, node, helper, cost);
        }
        if (ribos_path_count(callee) == 1 &&
            expected < context->type_count &&
            ((context->types[expected].kind == RIBOS_TYPE_RESULT &&
              (ribos_token_equals(callee->token, "Ok") ||
               ribos_token_equals(callee->token, "Err"))) ||
             (context->types[expected].kind == RIBOS_TYPE_OPTION &&
              ribos_token_equals(callee->token, "Some")))) {
            RibosAstNode *argument = ribos_call_argument_at(node, NULL, 0);
            RibosStaticCost argument_cost = {0};
            RibosType *sum_type = &context->types[expected];
            uint32_t payload_type =
                sum_type->kind == RIBOS_TYPE_RESULT &&
                ribos_token_equals(callee->token, "Err") ?
                sum_type->second : sum_type->first;
            uint32_t actual;

            if (argument == NULL || node->items->size != 1) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_TYPE_ERROR,
                    RIBOS_E_ARGUMENT_COUNT_MISMATCH,
                    node,
                    callee->token,
                    "sum-type constructor requires one value",
                    NULL,
                    NULL);
                return 0;
            }
            actual = ribos_infer_expression(
                context,
                argument->first,
                payload_type,
                &argument_cost);
            *cost = ribos_cost_add(*cost, argument_cost);
            if (!ribos_type_compatible(
                    context,
                    payload_type,
                    actual)) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_TYPE_ERROR,
                    RIBOS_E_ARGUMENT_TYPE_MISMATCH,
                    argument,
                    NULL,
                    "constructor payload does not match the expected sum type",
                    NULL,
                    NULL);
                return 0;
            }
            node->inferred_type = expected;
            return expected;
        }
        function = ribos_find_function(
            context,
            callee->token,
            &function_index);
        if (function != NULL && ribos_path_count(callee) == 1) {
            return ribos_infer_user_call(
                context,
                node,
                function,
                function_index,
                cost);
        }
        type_id = ribos_find_type_token(context, callee->token);
        if (type_id != 0 &&
            ((context->types[type_id].declaration != NULL &&
              context->types[type_id].declaration->kind ==
                  RIBOS_AST_STRUCT &&
              ribos_path_count(callee) == 1) ||
             (context->types[type_id].declaration != NULL &&
              context->types[type_id].declaration->kind ==
                  RIBOS_AST_ENUM &&
              ribos_path_count(callee) == 2))) {
            return ribos_infer_constructor_call(
                context,
                node,
                type_id,
                cost);
        }
    }
    if (callee != NULL && callee->kind == RIBOS_AST_MEMBER) {
        RibosStaticCost receiver_cost = {0};
        uint32_t receiver_type = ribos_infer_expression(
            context,
            callee->first,
            0,
            &receiver_cost);
        RibosType *type = receiver_type < context->type_count ?
            &context->types[receiver_type] : NULL;

        *cost = ribos_cost_add(*cost, receiver_cost);
        if (type != NULL &&
            (type->kind == RIBOS_TYPE_FROZEN_MAP ||
             type->kind == RIBOS_TYPE_DICT) &&
            ribos_token_equals(callee->token, "get")) {
            RibosAstNode *key = ribos_call_argument_at(node, NULL, 0);
            RibosAstNode *fallback = ribos_call_argument_at(
                node,
                "default",
                1);
            RibosStaticCost key_cost = {0};
            RibosStaticCost fallback_cost = {0};
            uint32_t key_type;
            uint32_t value_type;

            if (key == NULL || fallback == NULL ||
                node->items == NULL || node->items->size != 2) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_TYPE_ERROR,
                    RIBOS_E_ARGUMENT_COUNT_MISMATCH,
                    node,
                    callee->token,
                    "map.get requires key and named default",
                    NULL,
                    NULL);
                return 0;
            }
            key_type = ribos_infer_expression(
                context,
                key->first,
                type->first,
                &key_cost);
            value_type = ribos_infer_expression(
                context,
                fallback->first,
                type->second,
                &fallback_cost);
            *cost = ribos_cost_add(*cost, key_cost);
            *cost = ribos_cost_add(*cost, fallback_cost);
            if (!ribos_type_compatible(context, type->first, key_type) ||
                !ribos_type_compatible(context, type->second, value_type)) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_TYPE_ERROR,
                    RIBOS_E_ARGUMENT_TYPE_MISMATCH,
                    node,
                    callee->token,
                    "map.get key or default type mismatch",
                    NULL,
                    NULL);
                return 0;
            }
            node->inferred_type = type->second;
            return node->inferred_type;
        }
    }
    (void)ribos_semantic_fail(
        context,
        RIBOS_COMPILE_NAME_ERROR,
        RIBOS_E_UNKNOWN_NAME,
        node,
        callee == NULL ? NULL : callee->token,
        "call target is not a helper, function, constructor, or method",
        NULL,
        NULL);
    return 0;
}

static uint32_t
ribos_infer_list(
    RibosSemanticContext *context,
    RibosAstNode *node,
    uint32_t expected,
    RibosStaticCost *cost)
{
    size_t count = node->items == NULL ? 0 : (size_t)node->items->size;
    uint32_t element_type = 0;
    uint32_t bound;
    RibosTypeKind kind = RIBOS_TYPE_ARRAY;
    size_t index;

    if (expected < context->type_count &&
        (context->types[expected].kind == RIBOS_TYPE_ARRAY ||
         context->types[expected].kind == RIBOS_TYPE_LIST)) {
        element_type = context->types[expected].first;
        bound = context->types[expected].bound;
        kind = context->types[expected].kind;
        if (count > bound) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_BOUND_ERROR,
                RIBOS_E_COLLECTION_BOUND_EXCEEDED,
                node,
                NULL,
                "list literal exceeds its declared capacity",
                NULL,
                NULL);
            return 0;
        }
    } else {
        if (count == 0) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_CANNOT_INFER_EMPTY_COLLECTION,
                node,
                NULL,
                "empty list requires an explicit bounded collection type",
                NULL,
                NULL);
            return 0;
        }
        bound = (uint32_t)count;
    }
    for (index = 0; index < count; ++index) {
        RibosStaticCost item_cost = {0};
        uint32_t actual = ribos_infer_expression(
            context,
            node->items->elements[index],
            ribos_literal_expected_type(
                context,
                node->items->elements[index],
                element_type),
            &item_cost);

        *cost = ribos_cost_add(*cost, item_cost);
        if (element_type == 0) {
            element_type = actual;
        } else if (!ribos_type_compatible(context, element_type, actual)) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_COLLECTION_ELEMENT_TYPE_MISMATCH,
                node->items->elements[index],
                NULL,
                "list literal elements must have one type",
                NULL,
                NULL);
            return 0;
        }
    }
    node->inferred_type = expected != 0 ? expected : ribos_add_type(
        context,
        kind,
        NULL,
        0,
        element_type,
        0,
        bound,
        0,
        NULL);
    return node->inferred_type;
}

static uint32_t
ribos_infer_map(
    RibosSemanticContext *context,
    RibosAstNode *node,
    uint32_t expected,
    RibosStaticCost *cost)
{
    size_t count = node->items == NULL ? 0 : (size_t)node->items->size;
    uint32_t key_type = 0;
    uint32_t value_type = 0;
    uint32_t bound;
    RibosTypeKind kind = RIBOS_TYPE_FROZEN_MAP;
    size_t index;

    if (expected < context->type_count &&
        (context->types[expected].kind == RIBOS_TYPE_FROZEN_MAP ||
         context->types[expected].kind == RIBOS_TYPE_DICT)) {
        key_type = context->types[expected].first;
        value_type = context->types[expected].second;
        bound = context->types[expected].bound;
        kind = context->types[expected].kind;
        if (count > bound) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_BOUND_ERROR,
                RIBOS_E_COLLECTION_BOUND_EXCEEDED,
                node,
                NULL,
                "map literal exceeds its declared capacity",
                NULL,
                NULL);
            return 0;
        }
    } else {
        if (count == 0) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_CANNOT_INFER_EMPTY_COLLECTION,
                node,
                NULL,
                "empty map requires an explicit bounded map type",
                NULL,
                NULL);
            return 0;
        }
        bound = (uint32_t)count;
    }
    for (index = 0; index < count; ++index) {
        RibosAstNode *entry = node->items->elements[index];
        RibosStaticCost key_cost = {0};
        RibosStaticCost value_cost = {0};
        uint32_t actual_key = ribos_infer_expression(
            context,
            entry->first,
            key_type,
            &key_cost);
        uint32_t actual_value = ribos_infer_expression(
            context,
            entry->second,
            value_type,
            &value_cost);

        *cost = ribos_cost_add(*cost, key_cost);
        *cost = ribos_cost_add(*cost, value_cost);
        if (key_type == 0) {
            key_type = actual_key;
            value_type = actual_value;
        } else if (!ribos_type_compatible(context, key_type, actual_key) ||
            !ribos_type_compatible(context, value_type, actual_value)) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_COLLECTION_ELEMENT_TYPE_MISMATCH,
                entry,
                NULL,
                "map literal keys and values must each have one type",
                NULL,
                NULL);
            return 0;
        }
    }
    node->inferred_type = expected != 0 ? expected : ribos_add_type(
        context,
        kind,
        NULL,
        0,
        key_type,
        value_type,
        bound,
        0,
        NULL);
    return node->inferred_type;
}

static uint32_t
ribos_infer_expression(
    RibosSemanticContext *context,
    RibosAstNode *node,
    uint32_t expected,
    RibosStaticCost *cost)
{
    uint32_t type = 0;

    if (node == NULL || context->status != RIBOS_COMPILE_OK) {
        return 0;
    }
    switch (node->kind) {
    case RIBOS_AST_INTEGER: {
        uint64_t value;

        if (!ribos_parse_u64_token(node->token, &value)) {
            return 0;
        }
        if (expected != 0 && ribos_type_is_integer(context, expected)) {
            const RibosType *expected_type = &context->types[expected];
            uint64_t maximum = expected_type->bits == 64 ?
                UINT64_MAX : ((UINT64_C(1) << expected_type->bits) - 1);

            if (expected_type->kind == RIBOS_TYPE_SIGNED) {
                maximum >>= 1;
            }
            if (value > maximum) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_TYPE_ERROR,
                    RIBOS_E_TYPE_MISMATCH,
                    node,
                    node->token,
                    "integer literal does not fit the expected type",
                    NULL,
                    NULL);
                return 0;
            }
            type = expected;
        } else {
            type = ribos_find_type_name(
                context,
                value <= UINT32_MAX ? "u32" : "u64");
        }
        break;
    }
    case RIBOS_AST_STRING: {
        uint32_t length = node->token->length >= 2 ?
            (uint32_t)(node->token->length - 2) : 0;

        type = ribos_add_type(
            context,
            RIBOS_TYPE_STRING_LITERAL,
            NULL,
            0,
            0,
            0,
            length,
            0,
            NULL);
        if (expected != 0 &&
            context->types[expected].kind == RIBOS_TYPE_STRING_LITERAL &&
            length <= context->types[expected].bound) {
            type = expected;
        }
        break;
    }
    case RIBOS_AST_BOOLEAN:
        type = ribos_find_type_name(context, "bool");
        break;
    case RIBOS_AST_NONE:
        if (expected == 0 || expected >= context->type_count ||
            context->types[expected].kind != RIBOS_TYPE_OPTION) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_TYPE_MISMATCH,
                node,
                node->token,
                "None requires an expected Option type",
                "Option",
                NULL);
            return 0;
        }
        type = expected;
        break;
    case RIBOS_AST_PATH:
        type = ribos_infer_path(context, node);
        break;
    case RIBOS_AST_LIST:
        type = ribos_infer_list(context, node, expected, cost);
        break;
    case RIBOS_AST_MAP:
        type = ribos_infer_map(context, node, expected, cost);
        break;
    case RIBOS_AST_CALL:
        type = ribos_infer_call(context, node, expected, cost);
        break;
    case RIBOS_AST_MEMBER: {
        RibosStaticCost receiver_cost = {0};
        uint32_t receiver = ribos_infer_expression(
            context,
            node->first,
            0,
            &receiver_cost);

        *cost = ribos_cost_add(*cost, receiver_cost);
        type = ribos_struct_field_type(context, receiver, node->token);
        if (type == 0) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_UNKNOWN_MEMBER,
                node,
                node->token,
                "member is not declared on the receiver type",
                NULL,
                NULL);
            return 0;
        }
        break;
    }
    case RIBOS_AST_INDEX: {
        RibosStaticCost receiver_cost = {0};
        RibosStaticCost index_cost = {0};
        uint32_t receiver = ribos_infer_expression(
            context,
            node->first,
            0,
            &receiver_cost);

        *cost = ribos_cost_add(*cost, receiver_cost);
        if (receiver >= context->type_count) {
            return 0;
        }
        if (context->types[receiver].kind == RIBOS_TYPE_ARRAY ||
            context->types[receiver].kind == RIBOS_TYPE_LIST) {
            (void)ribos_infer_expression(
                context,
                node->second,
                ribos_find_type_name(context, "u32"),
                &index_cost);
            type = context->types[receiver].first;
        } else if (context->types[receiver].kind == RIBOS_TYPE_FROZEN_MAP ||
            context->types[receiver].kind == RIBOS_TYPE_DICT) {
            (void)ribos_infer_expression(
                context,
                node->second,
                context->types[receiver].first,
                &index_cost);
            type = context->types[receiver].second;
        } else {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_TYPE_MISMATCH,
                node,
                NULL,
                "indexing requires a bounded collection",
                NULL,
                NULL);
            return 0;
        }
        *cost = ribos_cost_add(*cost, index_cost);
        break;
    }
    case RIBOS_AST_PROPAGATE: {
        RibosStaticCost inner_cost = {0};
        uint32_t inner = ribos_infer_expression(
            context,
            node->first,
            0,
            &inner_cost);

        *cost = ribos_cost_add(*cost, inner_cost);
        if (inner >= context->type_count ||
            (context->types[inner].kind != RIBOS_TYPE_RESULT &&
             context->types[inner].kind != RIBOS_TYPE_OPTION)) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_TYPE_MISMATCH,
                node,
                NULL,
                "postfix ? requires Result or Option",
                "Result or Option",
                NULL);
            return 0;
        }
        if (context->types[inner].kind == RIBOS_TYPE_RESULT) {
            RibosType *return_type =
                &context->types[context->function->return_type];

            if (return_type->kind != RIBOS_TYPE_RESULT ||
                return_type->second != context->types[inner].second) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_TYPE_ERROR,
                    RIBOS_E_RETURN_TYPE_MISMATCH,
                    node,
                    NULL,
                    "propagated Result error does not match the function return",
                    NULL,
                    NULL);
                return 0;
            }
        }
        type = context->types[inner].first;
        break;
    }
    case RIBOS_AST_UNARY: {
        RibosStaticCost operand_cost = {0};
        uint32_t operand = ribos_infer_expression(
            context,
            node->first,
            0,
            &operand_cost);

        *cost = ribos_cost_add(*cost, operand_cost);
        if (node->flags == RIBOS_OPERATOR_NOT) {
            type = ribos_find_type_name(context, "bool");
            if (operand != type) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_TYPE_ERROR,
                    RIBOS_E_TYPE_MISMATCH,
                    node,
                    node->token,
                    "logical not requires bool",
                    "bool",
                    NULL);
                return 0;
            }
        } else if (!ribos_type_is_integer(context, operand)) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_TYPE_MISMATCH,
                node,
                node->token,
                "numeric unary operator requires integer",
                "integer",
                NULL);
            return 0;
        } else {
            type = operand;
        }
        break;
    }
    case RIBOS_AST_BINARY: {
        RibosStaticCost left_cost = {0};
        RibosStaticCost right_cost = {0};
        uint32_t left = ribos_infer_expression(
            context,
            node->first,
            0,
            &left_cost);
        uint32_t right = ribos_infer_expression(
            context,
            node->second,
            left,
            &right_cost);

        *cost = ribos_cost_add(*cost, left_cost);
        *cost = ribos_cost_add(*cost, right_cost);
        if (node->flags == RIBOS_OPERATOR_AND ||
            node->flags == RIBOS_OPERATOR_OR) {
            type = ribos_find_type_name(context, "bool");
            if (left != type || right != type) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_TYPE_ERROR,
                    RIBOS_E_TYPE_MISMATCH,
                    node,
                    node->token,
                    "logical operators require bool operands",
                    "bool",
                    NULL);
                return 0;
            }
        } else if (node->flags >= RIBOS_OPERATOR_EQUAL &&
            node->flags <= RIBOS_OPERATOR_NOT_IN) {
            if (!ribos_type_compatible(context, left, right) &&
                node->flags != RIBOS_OPERATOR_IN &&
                node->flags != RIBOS_OPERATOR_NOT_IN) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_TYPE_ERROR,
                    RIBOS_E_TYPE_MISMATCH,
                    node,
                    node->token,
                    "comparison operands have incompatible types",
                    NULL,
                    NULL);
                return 0;
            }
            type = ribos_find_type_name(context, "bool");
        } else {
            if (!ribos_type_is_integer(context, left) ||
                !ribos_type_compatible(context, left, right)) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_TYPE_ERROR,
                    RIBOS_E_TYPE_MISMATCH,
                    node,
                    node->token,
                    "arithmetic and bitwise operands must be one integer type",
                    "integer",
                    NULL);
                return 0;
            }
            type = left;
        }
        break;
    }
    case RIBOS_AST_IF_EXPRESSION: {
        RibosStaticCost condition_cost = {0};
        RibosStaticCost consequence_cost = {0};
        RibosStaticCost alternative_cost = {0};
        uint32_t condition = ribos_infer_expression(
            context,
            node->first,
            ribos_find_type_name(context, "bool"),
            &condition_cost);
        uint32_t consequence = ribos_infer_expression(
            context,
            node->second,
            expected,
            &consequence_cost);
        uint32_t alternative = ribos_infer_expression(
            context,
            node->third,
            consequence,
            &alternative_cost);

        *cost = ribos_cost_add(*cost, condition_cost);
        *cost = ribos_cost_add(
            *cost,
            ribos_cost_max(consequence_cost, alternative_cost));
        if (condition != ribos_find_type_name(context, "bool")) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_CONDITION_NOT_BOOL,
                node->first,
                NULL,
                "if expression condition must be bool",
                "bool",
                NULL);
            return 0;
        }
        if (!ribos_type_compatible(context, consequence, alternative)) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_TYPE_MISMATCH,
                node,
                NULL,
                "if expression branches must have one type",
                NULL,
                NULL);
            return 0;
        }
        type = consequence;
        break;
    }
    default:
        (void)ribos_semantic_fail(
            context,
            RIBOS_COMPILE_INTERNAL_ERROR,
            RIBOS_E_INTERNAL,
            node,
            node->token,
            "AST node is not an expression",
            NULL,
            NULL);
        return 0;
    }
    node->inferred_type = type;
    if (expected != 0 &&
        !ribos_type_compatible(context, expected, type)) {
        char expected_name[96];
        char actual_name[96];

        (void)ribos_semantic_fail(
            context,
            RIBOS_COMPILE_TYPE_ERROR,
            RIBOS_E_TYPE_MISMATCH,
            node,
            node->token,
            "expression type does not match the expected type",
            ribos_type_name(
                context,
                expected,
                expected_name,
                sizeof(expected_name)),
            ribos_type_name(
                context,
                type,
                actual_name,
                sizeof(actual_name)));
        return 0;
    }
    return type;
}

static RibosStaticCost
ribos_check_block(
    RibosSemanticContext *context,
    RibosAstNode *block,
    int *always_returns);

static uint64_t
ribos_bind_match_pattern(
    RibosSemanticContext *context,
    RibosAstNode *pattern,
    uint32_t value_type,
    uint64_t *required_coverage)
{
    RibosType *type = value_type < context->type_count ?
        &context->types[value_type] : NULL;
    RibosAstNode *path = pattern == NULL ? NULL : pattern->first;
    Token *variant_name = path == NULL ? NULL :
        ribos_path_component(path, ribos_path_count(path) - 1);
    Py_ssize_t argument_count = pattern == NULL ||
        pattern->items == NULL ? 0 : pattern->items->size;
    Py_ssize_t index;

    if (pattern == NULL || type == NULL) {
        return 0;
    }
    if (pattern->flags == 1) {
        *required_coverage = UINT64_MAX;
        return UINT64_MAX;
    }
    if (pattern->flags == 3) {
        RibosStaticCost cost = {0};
        uint32_t pattern_type = ribos_infer_expression(
            context,
            pattern->first,
            value_type,
            &cost);

        if (!ribos_type_compatible(context, value_type, pattern_type)) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_TYPE_MISMATCH,
                pattern,
                NULL,
                "literal match pattern has the wrong type",
                NULL,
                NULL);
        }
        *required_coverage = UINT64_MAX;
        return 0;
    }
    if (type->kind == RIBOS_TYPE_RESULT) {
        uint32_t payload_type;
        uint64_t coverage;

        *required_coverage = 3;
        if (variant_name != NULL &&
            ribos_token_equals(variant_name, "Ok")) {
            payload_type = type->first;
            coverage = 1;
        } else if (variant_name != NULL &&
            ribos_token_equals(variant_name, "Err")) {
            payload_type = type->second;
            coverage = 2;
        } else {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_TYPE_MISMATCH,
                pattern,
                variant_name,
                "Result pattern must be Ok or Err",
                "Ok or Err",
                NULL);
            return 0;
        }
        if ((pattern->flags == 2 && argument_count != 1) ||
            (pattern->flags != 2 && argument_count != 0)) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_ARGUMENT_COUNT_MISMATCH,
                pattern,
                variant_name,
                "Result variant pattern requires one payload binding",
                "one binding",
                NULL);
            return 0;
        }
        if (argument_count == 1) {
            RibosAstNode *binding = pattern->items->elements[0];

            if (binding->flags != 1) {
                (void)ribos_add_local(
                    context,
                    binding->token,
                    payload_type,
                    0,
                    binding);
            }
        }
        return coverage;
    }
    if (type->kind == RIBOS_TYPE_OPTION) {
        uint64_t coverage;
        int is_some = variant_name != NULL &&
            ribos_token_equals(variant_name, "Some");
        int is_none = variant_name != NULL &&
            ribos_token_equals(variant_name, "None");

        *required_coverage = 3;
        if (is_some) {
            coverage = 1;
        } else if (is_none) {
            coverage = 2;
        } else {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_TYPE_MISMATCH,
                pattern,
                variant_name,
                "Option pattern must be Some or None",
                "Some or None",
                NULL);
            return 0;
        }
        if ((is_some && pattern->flags == 2 && argument_count != 1) ||
            (is_some && pattern->flags != 2 && argument_count != 0) ||
            (is_none && argument_count != 0)) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_ARGUMENT_COUNT_MISMATCH,
                pattern,
                variant_name,
                "Option variant pattern has the wrong payload arity",
                NULL,
                NULL);
            return 0;
        }
        if (argument_count == 1) {
            RibosAstNode *binding = pattern->items->elements[0];

            if (binding->flags != 1) {
                (void)ribos_add_local(
                    context,
                    binding->token,
                    type->first,
                    0,
                    binding);
            }
        }
        return coverage;
    }
    if (type->declaration != NULL &&
        type->declaration->kind == RIBOS_AST_ENUM) {
        uint64_t required = 0;

        if (type->declaration->items == NULL ||
            type->declaration->items->size > 63) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_BOUND_ERROR,
                RIBOS_E_RESOURCE_LIMIT,
                pattern,
                variant_name,
                "enum match coverage limit exceeded",
                NULL,
                NULL);
            return 0;
        }
        required = (UINT64_C(1) <<
            (unsigned)type->declaration->items->size) - 1;
        *required_coverage = required;
        for (index = 0;
             index < type->declaration->items->size;
             ++index) {
            RibosAstNode *variant =
                type->declaration->items->elements[index];
            Py_ssize_t payload_index;
            Py_ssize_t payload_count = variant->items == NULL ?
                0 : variant->items->size;

            if (!ribos_tokens_equal(variant->token, variant_name)) {
                continue;
            }
            if (payload_count != argument_count) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_TYPE_ERROR,
                    RIBOS_E_ARGUMENT_COUNT_MISMATCH,
                    pattern,
                    variant_name,
                    "enum pattern payload arity mismatch",
                    NULL,
                    NULL);
                return 0;
            }
            for (payload_index = 0;
                 payload_index < payload_count;
                 ++payload_index) {
                RibosAstNode *binding =
                    pattern->items->elements[payload_index];
                RibosAstNode *payload_type =
                    variant->items->elements[payload_index];

                if (binding->flags != 1) {
                    (void)ribos_add_local(
                        context,
                        binding->token,
                        payload_type->inferred_type,
                        0,
                        binding);
                }
            }
            return UINT64_C(1) << (unsigned)index;
        }
        (void)ribos_semantic_fail(
            context,
            RIBOS_COMPILE_TYPE_ERROR,
            RIBOS_E_UNKNOWN_MEMBER,
            pattern,
            variant_name,
            "enum variant is not declared",
            NULL,
            NULL);
        return 0;
    }
    (void)ribos_semantic_fail(
        context,
        RIBOS_COMPILE_TYPE_ERROR,
        RIBOS_E_TYPE_MISMATCH,
        pattern,
        variant_name,
        "match requires an enum, Result, or wildcard-compatible value",
        NULL,
        NULL);
    return 0;
}

static RibosStaticCost
ribos_check_statement(
    RibosSemanticContext *context,
    RibosAstNode *statement,
    int *always_returns)
{
    RibosStaticCost cost = {0};

    *always_returns = 0;
    if (statement == NULL || context->status != RIBOS_COMPILE_OK) {
        return cost;
    }
    switch (statement->kind) {
    case RIBOS_AST_LET: {
        uint32_t annotation = statement->first == NULL ?
            0 : ribos_resolve_type_expression(context, statement->first);
        uint32_t value = ribos_infer_expression(
            context,
            statement->second,
            annotation,
            &cost);

        if (context->status == RIBOS_COMPILE_OK) {
            (void)ribos_add_local(
                context,
                statement->token,
                annotation == 0 ? value : annotation,
                statement->flags != 0,
                statement);
        }
        break;
    }
    case RIBOS_AST_ASSIGN:
        if (statement->first == NULL ||
            statement->first->kind != RIBOS_AST_PATH ||
            ribos_path_count(statement->first) != 1) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_INVALID_ASSIGNMENT_TARGET,
                statement,
                NULL,
                "assignment target must be a mutable local binding",
                NULL,
                NULL);
            break;
        } else {
            RibosLocal *local = ribos_find_local(
                context,
                statement->first->token);
            uint32_t actual;

            if (local == NULL) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_NAME_ERROR,
                    RIBOS_E_UNKNOWN_NAME,
                    statement->first,
                    statement->first->token,
                    "assignment target is not declared",
                    NULL,
                    NULL);
                break;
            }
            if (!local->mutable_binding) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_TYPE_ERROR,
                    RIBOS_E_MUTATE_IMMUTABLE_BINDING,
                    statement->first,
                    statement->first->token,
                    "immutable let binding cannot be reassigned",
                    NULL,
                    NULL);
                break;
            }
            actual = ribos_infer_expression(
                context,
                statement->second,
                local->type,
                &cost);
            UNUSED(actual);
        }
        break;
    case RIBOS_AST_RETURN: {
        uint32_t unit = ribos_find_type_name(context, "Unit");
        uint32_t actual = statement->first == NULL ?
            unit : ribos_infer_expression(
                context,
                statement->first,
                context->function->return_type,
                &cost);

        if (!ribos_type_compatible(
                context,
                context->function->return_type,
                actual)) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_RETURN_TYPE_MISMATCH,
                statement,
                statement->token,
                "return value does not match the declared return type",
                NULL,
                NULL);
        }
        *always_returns = 1;
        break;
    }
    case RIBOS_AST_IF: {
        RibosStaticCost condition_cost = {0};
        RibosStaticCost consequence_cost;
        RibosStaticCost alternative_cost = {0};
        int consequence_returns = 0;
        int alternative_returns = 0;
        uint32_t condition = ribos_infer_expression(
            context,
            statement->first,
            ribos_find_type_name(context, "bool"),
            &condition_cost);

        if (condition != ribos_find_type_name(context, "bool")) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_CONDITION_NOT_BOOL,
                statement->first,
                NULL,
                "if condition must be bool",
                "bool",
                NULL);
        }
        consequence_cost = ribos_check_block(
            context,
            statement->second,
            &consequence_returns);
        if (statement->third != NULL) {
            if (statement->third->kind == RIBOS_AST_BLOCK) {
                alternative_cost = ribos_check_block(
                    context,
                    statement->third,
                    &alternative_returns);
            } else {
                alternative_cost = ribos_check_statement(
                    context,
                    statement->third,
                    &alternative_returns);
            }
        }
        cost = ribos_cost_add(
            condition_cost,
            ribos_cost_max(consequence_cost, alternative_cost));
        *always_returns = statement->third != NULL &&
            consequence_returns && alternative_returns;
        break;
    }
    case RIBOS_AST_FOR: {
        RibosStaticCost iterable_cost = {0};
        RibosStaticCost body_cost;
        uint32_t iterable = ribos_infer_expression(
            context,
            statement->first,
            0,
            &iterable_cost);
        uint32_t element;
        uint32_t bound;
        size_t local_mark = context->local_count;
        int body_returns = 0;

        if (iterable >= context->type_count ||
            (context->types[iterable].kind != RIBOS_TYPE_ARRAY &&
             context->types[iterable].kind != RIBOS_TYPE_LIST)) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_BOUND_ERROR,
                RIBOS_E_UNBOUNDED_ITERATION,
                statement->first,
                statement->token,
                "for iteration requires Array or bounded List",
                "Array or List",
                NULL);
            break;
        }
        element = context->types[iterable].first;
        bound = context->types[iterable].bound;
        (void)ribos_enter_scope(context);
        (void)ribos_add_local(
            context,
            statement->token,
            element,
            0,
            statement);
        body_cost = ribos_check_block(
            context,
            statement->second,
            &body_returns);
        ribos_leave_scope(context, local_mark);
        cost = ribos_cost_add(
            iterable_cost,
            ribos_cost_multiply(body_cost, bound));
        UNUSED(body_returns);
        break;
    }
    case RIBOS_AST_MATCH: {
        RibosStaticCost value_cost = {0};
        RibosStaticCost arm_max = {0};
        Py_ssize_t index;
        int all_return = 1;
        uint64_t coverage = 0;
        uint64_t required_coverage = 0;
        uint32_t matched_type;

        matched_type = ribos_infer_expression(
            context,
            statement->first,
            0,
            &value_cost);
        for (index = 0; statement->items != NULL &&
             index < statement->items->size; ++index) {
            RibosAstNode *arm = statement->items->elements[index];
            RibosStaticCost arm_cost;
            int arm_returns = 0;
            size_t local_mark = context->local_count;
            uint64_t arm_required = 0;

            (void)ribos_enter_scope(context);
            coverage |= ribos_bind_match_pattern(
                context,
                arm->first,
                matched_type,
                &arm_required);
            if (required_coverage == 0 ||
                arm_required == UINT64_MAX) {
                required_coverage = arm_required;
            } else if (arm_required != 0 &&
                arm_required != required_coverage) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_TYPE_ERROR,
                    RIBOS_E_TYPE_MISMATCH,
                    arm->first,
                    NULL,
                    "match arms do not target one closed type",
                    NULL,
                    NULL);
            }
            arm_cost = ribos_check_block(
                context,
                arm->second,
                &arm_returns);
            ribos_leave_scope(context, local_mark);
            arm_max = ribos_cost_max(arm_max, arm_cost);
            all_return = all_return && arm_returns;
        }
        if (context->status == RIBOS_COMPILE_OK &&
            required_coverage != UINT64_MAX &&
            coverage != UINT64_MAX &&
            (coverage & required_coverage) != required_coverage) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_NON_EXHAUSTIVE_MATCH,
                statement,
                statement->token,
                "match does not cover every closed variant",
                NULL,
                NULL);
        }
        cost = ribos_cost_add(value_cost, arm_max);
        *always_returns = statement->items != NULL &&
            statement->items->size != 0 && all_return;
        break;
    }
    default: {
        uint32_t result = ribos_infer_expression(
            context,
            statement,
            0,
            &cost);

        if (result < context->type_count &&
            context->types[result].kind == RIBOS_TYPE_RESULT) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_RESULT_MUST_BE_USED,
                statement,
                statement->token,
                "Result-valued expression must be propagated, matched, or bound",
                NULL,
                NULL);
        }
        break;
    }
    }
    return cost;
}

static RibosStaticCost
ribos_check_block(
    RibosSemanticContext *context,
    RibosAstNode *block,
    int *always_returns)
{
    RibosStaticCost cost = {0};
    size_t local_mark = context->local_count;
    Py_ssize_t index;
    int returned = 0;

    *always_returns = 0;
    if (block == NULL || block->kind != RIBOS_AST_BLOCK ||
        !ribos_enter_scope(context)) {
        return cost;
    }
    for (index = 0; block->items != NULL &&
         index < block->items->size; ++index) {
        RibosStaticCost statement_cost;
        int statement_returns = 0;

        statement_cost = ribos_check_statement(
            context,
            block->items->elements[index],
            &statement_returns);
        cost = ribos_cost_add(cost, statement_cost);
        if (statement_returns) {
            returned = 1;
            break;
        }
    }
    ribos_leave_scope(context, local_mark);
    *always_returns = returned;
    return cost;
}

static uint32_t
ribos_capability_from_path(const RibosAstNode *path)
{
    Token *name;

    if (ribos_path_count(path) != 2 ||
        !ribos_token_equals(ribos_path_component(path, 0), "Capability")) {
        return 0;
    }
    name = ribos_path_component(path, 1);
    if (ribos_token_equals(name, "INSPECT")) {
        return RIBOS_CAPABILITY_INSPECT;
    }
    if (ribos_token_equals(name, "DEVICE")) {
        return RIBOS_CAPABILITY_DEVICE;
    }
    if (ribos_token_equals(name, "STATE")) {
        return RIBOS_CAPABILITY_STATE;
    }
    if (ribos_token_equals(name, "NETWORK")) {
        return RIBOS_CAPABILITY_NETWORK;
    }
    if (ribos_token_equals(name, "FLASH")) {
        return RIBOS_CAPABILITY_FLASH;
    }
    if (ribos_token_equals(name, "HANDOFF")) {
        return RIBOS_CAPABILITY_HANDOFF;
    }
    if (ribos_token_equals(name, "BOOT")) {
        return RIBOS_CAPABILITY_BOOT;
    }
    if (ribos_token_equals(name, "DIAGNOSTIC")) {
        return RIBOS_CAPABILITY_DIAGNOSTIC;
    }
    return 0;
}

static RibosAstNode *
ribos_decorator_argument(
    RibosAstNode *decorator,
    const char *name)
{
    Py_ssize_t index;

    for (index = 0; decorator->items != NULL &&
         index < decorator->items->size; ++index) {
        RibosAstNode *argument = decorator->items->elements[index];

        if (ribos_token_equals(argument->token, name)) {
            return argument->first;
        }
    }
    return NULL;
}

static void
ribos_read_function_decorators(
    RibosSemanticContext *context,
    RibosFunctionInfo *function)
{
    Py_ssize_t index;

    function->helper_budget = UINT64_MAX;
    function->instruction_budget = UINT64_MAX;
    for (index = 0; function->declaration->extra != NULL &&
         index < function->declaration->extra->size; ++index) {
        RibosAstNode *decorator =
            function->declaration->extra->elements[index];

        if (ribos_path_equals(decorator->first, "policy")) {
            RibosAstNode *capabilities;
            RibosAstNode *instruction_budget;
            RibosAstNode *budget;
            Py_ssize_t capability_index;

            if (function->is_policy || function->is_pure) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_NAME_ERROR,
                    RIBOS_E_INVALID_DECORATOR,
                    decorator,
                    decorator->first->token,
                    "function entry decorators are mutually exclusive",
                    NULL,
                    NULL);
                return;
            }
            function->is_policy = 1;
            capabilities = ribos_decorator_argument(
                decorator,
                "capabilities");
            if (capabilities != NULL &&
                capabilities->kind == RIBOS_AST_LIST) {
                for (capability_index = 0;
                     capabilities->items != NULL &&
                     capability_index < capabilities->items->size;
                     ++capability_index) {
                    RibosAstNode *value =
                        capabilities->items->elements[capability_index];
                    uint32_t capability =
                        ribos_capability_from_path(value);

                    if (capability == 0) {
                        (void)ribos_semantic_fail(
                            context,
                            RIBOS_COMPILE_CAPABILITY_ERROR,
                            RIBOS_E_INVALID_DECORATOR,
                            value,
                            value->token,
                            "unknown capability in @policy",
                            NULL,
                            NULL);
                        return;
                    }
                    function->declared_capabilities |= capability;
                }
            }
            budget = ribos_decorator_argument(
                decorator,
                "helper_budget");
            if (budget != NULL &&
                (budget->kind != RIBOS_AST_INTEGER ||
                 !ribos_parse_u64_token(
                     budget->token,
                     &function->helper_budget))) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_BOUND_ERROR,
                    RIBOS_E_INVALID_DECORATOR,
                    budget,
                    budget == NULL ? NULL : budget->token,
                    "helper_budget must be an integer constant",
                    NULL,
                    NULL);
                return;
            }
            instruction_budget = ribos_decorator_argument(
                decorator,
                "instruction_budget");
            if (instruction_budget != NULL &&
                (instruction_budget->kind != RIBOS_AST_INTEGER ||
                 !ribos_parse_u64_token(
                     instruction_budget->token,
                     &function->instruction_budget))) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_BOUND_ERROR,
                    RIBOS_E_INVALID_DECORATOR,
                    instruction_budget,
                    instruction_budget == NULL ?
                        NULL : instruction_budget->token,
                    "instruction_budget must be an integer constant",
                    NULL,
                    NULL);
                return;
            }
        } else if (ribos_path_equals(decorator->first, "pure")) {
            if (function->is_policy || function->is_pure ||
                decorator->items != NULL) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_NAME_ERROR,
                    RIBOS_E_INVALID_DECORATOR,
                    decorator,
                    decorator->first->token,
                    "@pure takes no arguments and cannot combine with @policy",
                    NULL,
                    NULL);
                return;
            }
            function->is_pure = 1;
        }
    }
}

static void
ribos_collect_declarations(RibosSemanticContext *context)
{
    Py_ssize_t index;

    for (index = 0; context->parser->root->items != NULL &&
         index < context->parser->root->items->size; ++index) {
        RibosAstNode *declaration =
            context->parser->root->items->elements[index];

        if (declaration->kind == RIBOS_AST_STRUCT ||
            declaration->kind == RIBOS_AST_ENUM) {
            if (ribos_find_type_token(context, declaration->token) != 0) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_NAME_ERROR,
                    RIBOS_E_DUPLICATE_DECLARATION,
                    declaration,
                    declaration->token,
                    "type declaration collides with an existing type",
                    NULL,
                    NULL);
                return;
            }
            (void)ribos_add_type(
                context,
                RIBOS_TYPE_NAMED,
                declaration->token->start,
                declaration->token->length,
                0,
                0,
                0,
                0,
                declaration);
        } else if (declaration->kind == RIBOS_AST_FUNCTION) {
            if (context->function_count == RIBOS_MAX_FUNCTIONS) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_BOUND_ERROR,
                    RIBOS_E_RESOURCE_LIMIT,
                    declaration,
                    declaration->token,
                    "function declaration limit exceeded",
                    NULL,
                    NULL);
                return;
            }
            if (ribos_find_function(
                    context,
                    declaration->token,
                    NULL) != NULL) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_NAME_ERROR,
                    RIBOS_E_DUPLICATE_DECLARATION,
                    declaration,
                    declaration->token,
                    "function is already declared",
                    NULL,
                    NULL);
                return;
            }
            context->functions[context->function_count++] =
                (RibosFunctionInfo){
                    .declaration = declaration,
                    .name = declaration->token,
                };
        }
    }
}

static void
ribos_resolve_declaration_types(RibosSemanticContext *context)
{
    size_t function_index;
    Py_ssize_t declaration_index;

    for (declaration_index = 0;
         context->parser->root->items != NULL &&
         declaration_index < context->parser->root->items->size;
         ++declaration_index) {
        RibosAstNode *declaration =
            context->parser->root->items->elements[declaration_index];
        Py_ssize_t member_index;

        if ((declaration->kind == RIBOS_AST_STRUCT ||
             declaration->kind == RIBOS_AST_ENUM) &&
            declaration->items != NULL) {
            if ((declaration->kind == RIBOS_AST_STRUCT &&
                 declaration->items->size >
                    RIBOS_MAX_AGGREGATE_MEMBERS) ||
                (declaration->kind == RIBOS_AST_ENUM &&
                 declaration->items->size >
                    RIBOS_MAX_ENUM_VARIANTS)) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_BOUND_ERROR,
                    RIBOS_E_RESOURCE_LIMIT,
                    declaration,
                    declaration->token,
                    "aggregate declaration limit exceeded",
                    NULL,
                    NULL);
                return;
            }
            for (member_index = 0;
                 member_index < declaration->items->size;
                 ++member_index) {
                RibosAstNode *member =
                    declaration->items->elements[member_index];
                Py_ssize_t argument_index;

                if (member->kind == RIBOS_AST_FIELD) {
                    (void)ribos_resolve_type_expression(
                        context,
                        member->first);
                } else {
                    if (member->items != NULL &&
                        member->items->size >
                            RIBOS_MAX_AGGREGATE_MEMBERS) {
                        (void)ribos_semantic_fail(
                            context,
                            RIBOS_COMPILE_BOUND_ERROR,
                            RIBOS_E_RESOURCE_LIMIT,
                            member,
                            member->token,
                            "enum payload limit exceeded",
                            NULL,
                            NULL);
                        return;
                    }
                    for (argument_index = 0;
                         member->items != NULL &&
                         argument_index < member->items->size;
                         ++argument_index) {
                        (void)ribos_resolve_type_expression(
                            context,
                            member->items->elements[argument_index]);
                    }
                }
            }
        }
    }
    for (function_index = 0;
         function_index < context->function_count;
         ++function_index) {
        RibosFunctionInfo *function =
            &context->functions[function_index];
        Py_ssize_t parameter_index;

        function->return_type = ribos_resolve_type_expression(
            context,
            function->declaration->first);
        function->parameter_count =
            function->declaration->items == NULL ?
                0 : (size_t)function->declaration->items->size;
        if (function->parameter_count > RIBOS_MAX_PARAMETERS) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_BOUND_ERROR,
                RIBOS_E_RESOURCE_LIMIT,
                function->declaration,
                function->name,
                "function parameter limit exceeded",
                NULL,
                NULL);
            return;
        }
        for (parameter_index = 0;
             parameter_index < (Py_ssize_t)function->parameter_count;
             ++parameter_index) {
            RibosAstNode *parameter =
                function->declaration->items->elements[parameter_index];

            function->parameters[parameter_index] =
                (RibosParameterInfo){
                    .name = parameter->token,
                    .type = ribos_resolve_type_expression(
                        context,
                        parameter->first),
                };
        }
        ribos_read_function_decorators(context, function);
        if (context->status != RIBOS_COMPILE_OK) {
            return;
        }
    }
}

static void
ribos_check_functions(RibosSemanticContext *context)
{
    size_t function_index;

    for (function_index = 0;
         function_index < context->function_count;
         ++function_index) {
        RibosFunctionInfo *function =
            &context->functions[function_index];
        size_t parameter_index;
        int always_returns = 0;

        context->function = function;
        context->local_count = 0;
        context->scope_depth = 0;
        (void)ribos_enter_scope(context);
        for (parameter_index = 0;
             parameter_index < function->parameter_count;
             ++parameter_index) {
            (void)ribos_add_local(
                context,
                function->parameters[parameter_index].name,
                function->parameters[parameter_index].type,
                0,
                function->declaration);
        }
        function->direct_cost = ribos_check_block(
            context,
            function->declaration->second,
            &always_returns);
        if (context->status != RIBOS_COMPILE_OK) {
            return;
        }
        if (!always_returns) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_TYPE_ERROR,
                RIBOS_E_MISSING_RETURN,
                function->declaration,
                function->name,
                "function has a reachable path without return",
                NULL,
                NULL);
            return;
        }
        if (function->is_pure &&
            function->direct_capabilities != 0) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_CAPABILITY_ERROR,
                RIBOS_E_PURE_FUNCTION_HAS_EFFECT,
                function->declaration,
                function->name,
                "@pure function reaches a capability-bearing helper",
                NULL,
                NULL);
            return;
        }
    }
}

static int
ribos_close_function_effects(
    RibosSemanticContext *context,
    size_t function_index)
{
    RibosFunctionInfo *function = &context->functions[function_index];
    size_t callee_index;

    if (function->visited) {
        return 1;
    }
    if (function->visiting) {
        (void)ribos_semantic_fail(
            context,
            RIBOS_COMPILE_BOUND_ERROR,
            RIBOS_E_RECURSIVE_CALL_GRAPH,
            function->declaration,
            function->name,
            "recursive Ribos call graph is not bounded",
            NULL,
            NULL);
        return 0;
    }
    function->visiting = 1;
    function->required_capabilities = function->direct_capabilities;
    function->total_helper_upper_bound = function->direct_cost.helpers;
    function->max_call_depth = 1;
    for (callee_index = 0;
         callee_index < context->function_count;
         ++callee_index) {
        RibosFunctionInfo *callee;
        uint64_t multiplicity = function->direct_cost.calls[callee_index];

        if (multiplicity == 0) {
            continue;
        }
        if (!ribos_close_function_effects(context, callee_index)) {
            return 0;
        }
        callee = &context->functions[callee_index];
        function->required_capabilities |=
            callee->required_capabilities;
        function->total_helper_upper_bound = ribos_saturating_add(
            function->total_helper_upper_bound,
            ribos_saturating_multiply(
                multiplicity,
                callee->total_helper_upper_bound));
        if (callee->max_call_depth + 1 > function->max_call_depth) {
            function->max_call_depth = callee->max_call_depth + 1;
        }
    }
    function->visiting = 0;
    function->visited = 1;
    return 1;
}

static void
ribos_check_policy_effects(RibosSemanticContext *context)
{
    size_t index;
    size_t policy_count = 0;

    for (index = 0; index < context->function_count; ++index) {
        RibosFunctionInfo *function = &context->functions[index];

        if (!ribos_close_function_effects(context, index)) {
            return;
        }
        if (function->is_policy) {
            ++policy_count;
            context->summary->declared_capabilities |=
                function->declared_capabilities;
            context->summary->required_capabilities |=
                function->required_capabilities;
            context->summary->helper_call_upper_bound =
                ribos_saturating_add(
                    context->summary->helper_call_upper_bound,
                    function->total_helper_upper_bound);
            context->summary->declared_instruction_budget =
                function->instruction_budget;
            context->summary->declared_helper_budget =
                function->helper_budget;
            if ((function->required_capabilities &
                 ~function->declared_capabilities) != 0) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_CAPABILITY_ERROR,
                    RIBOS_E_CAPABILITY_NOT_DECLARED,
                    function->declaration,
                    function->name,
                    "reachable helper capability is absent from @policy",
                    NULL,
                    NULL);
                return;
            }
            if (function->total_helper_upper_bound >
                function->helper_budget) {
                (void)ribos_semantic_fail(
                    context,
                    RIBOS_COMPILE_BOUND_ERROR,
                    RIBOS_E_HELPER_BUDGET_EXCEEDED,
                    function->declaration,
                    function->name,
                    "static helper-call upper bound exceeds helper_budget",
                    NULL,
                    NULL);
                return;
            }
        }
        if (function->is_pure &&
            function->required_capabilities != 0) {
            (void)ribos_semantic_fail(
                context,
                RIBOS_COMPILE_CAPABILITY_ERROR,
                RIBOS_E_PURE_FUNCTION_HAS_EFFECT,
                function->declaration,
                function->name,
                "@pure function reaches an effectful user function",
                NULL,
                NULL);
            return;
        }
        context->summary->helper_call_site_count +=
            function->helper_call_sites;
        if (function->max_call_depth >
            context->summary->max_call_depth) {
            context->summary->max_call_depth =
                function->max_call_depth;
        }
    }
    if (policy_count != 1) {
        (void)ribos_semantic_fail(
            context,
            RIBOS_COMPILE_NAME_ERROR,
            RIBOS_E_INVALID_DECORATOR,
            context->parser->root,
            NULL,
            "source unit must declare exactly one @policy entry",
            "one @policy",
            NULL);
    }
}

static void
ribos_initialize_types(RibosSemanticContext *context)
{
    size_t index;
    static const struct {
        const char *name;
        RibosTypeKind kind;
        uint8_t bits;
    } core[] = {
        {"<error>", RIBOS_TYPE_ERROR, 0},
        {"<unknown>", RIBOS_TYPE_UNKNOWN, 0},
        {"Unit", RIBOS_TYPE_UNIT, 0},
        {"bool", RIBOS_TYPE_BOOL, 1},
        {"u8", RIBOS_TYPE_UNSIGNED, 8},
        {"u16", RIBOS_TYPE_UNSIGNED, 16},
        {"u32", RIBOS_TYPE_UNSIGNED, 32},
        {"u64", RIBOS_TYPE_UNSIGNED, 64},
        {"i8", RIBOS_TYPE_SIGNED, 8},
        {"i16", RIBOS_TYPE_SIGNED, 16},
        {"i32", RIBOS_TYPE_SIGNED, 32},
        {"i64", RIBOS_TYPE_SIGNED, 64},
    };

    for (index = 0; index < sizeof(core) / sizeof(core[0]); ++index) {
        (void)ribos_add_type(
            context,
            core[index].kind,
            core[index].name,
            strlen(core[index].name),
            0,
            0,
            0,
            core[index].bits,
            NULL);
    }
    for (index = 0; index < context->schema->type_count; ++index) {
        (void)ribos_add_type(
            context,
            RIBOS_TYPE_NAMED,
            context->schema->types[index].name,
            strlen(context->schema->types[index].name),
            0,
            0,
            0,
            0,
            NULL);
    }
}

static const char *
ribos_type_kind_name(RibosTypeKind kind)
{
    switch (kind) {
    case RIBOS_TYPE_ERROR:
        return "error";
    case RIBOS_TYPE_UNKNOWN:
        return "unknown";
    case RIBOS_TYPE_UNIT:
        return "unit";
    case RIBOS_TYPE_BOOL:
        return "bool";
    case RIBOS_TYPE_UNSIGNED:
        return "unsigned";
    case RIBOS_TYPE_SIGNED:
        return "signed";
    case RIBOS_TYPE_STRING_LITERAL:
        return "string-literal";
    case RIBOS_TYPE_NAMED:
        return "named";
    case RIBOS_TYPE_ARRAY:
        return "array";
    case RIBOS_TYPE_LIST:
        return "list";
    case RIBOS_TYPE_FROZEN_MAP:
        return "frozen-map";
    case RIBOS_TYPE_DICT:
        return "dict";
    case RIBOS_TYPE_OPTION:
        return "option";
    case RIBOS_TYPE_RESULT:
        return "result";
    default:
        return "invalid";
    }
}

static void
ribos_dump_semantic_model(
    const RibosSemanticContext *context,
    FILE *output)
{
    size_t index;

    for (index = 0; index < context->type_count; ++index) {
        const RibosType *type = &context->types[index];

        (void)fprintf(
            output,
            "TYPE id=%zu kind=%s name=%.*s first=%u second=%u "
            "bound=%u bits=%u\n",
            index,
            ribos_type_kind_name(type->kind),
            (int)type->name_length,
            type->name == NULL ? "" : type->name,
            type->first,
            type->second,
            type->bound,
            type->bits);
    }
    for (index = 0; index < context->function_count; ++index) {
        const RibosFunctionInfo *function = &context->functions[index];

        (void)fprintf(
            output,
            "FUNCTION id=%zu name=%.*s policy=%u pure=%u return=%u "
            "declared=0x%08x required=0x%08x helper-sites=%zu "
            "helper-upper=%" PRIu64 " helper-budget=%" PRIu64
            " instruction-budget=%" PRIu64 " call-depth=%u\n",
            index,
            (int)function->name->length,
            function->name->start,
            function->is_policy,
            function->is_pure,
            function->return_type,
            function->declared_capabilities,
            function->required_capabilities,
            function->helper_call_sites,
            function->total_helper_upper_bound,
            function->helper_budget,
            function->instruction_budget,
            function->max_call_depth);
    }
}

static size_t
ribos_count_reachable_ast(const RibosAstNode *node)
{
    size_t count = 1;
    Py_ssize_t index;

    if (node == NULL) {
        return 0;
    }
    count += ribos_count_reachable_ast(node->first);
    count += ribos_count_reachable_ast(node->second);
    count += ribos_count_reachable_ast(node->third);
    if (node->kind != RIBOS_AST_PATH) {
        for (index = 0; node->items != NULL &&
             index < node->items->size; ++index) {
            count += ribos_count_reachable_ast(
                node->items->elements[index]);
        }
    }
    for (index = 0; node->extra != NULL &&
         index < node->extra->size; ++index) {
        count += ribos_count_reachable_ast(
            node->extra->elements[index]);
    }
    return count;
}

RibosCompileStatus
ribos_compile_parser_tree(
    Parser *parser,
    const RibosProductSchema *schema,
    RibosIrModule *ir_module,
    RibosCompileSummary *summary,
    RibosCompileDiagnostic *diagnostic,
    FILE *dump,
    unsigned dump_flags)
{
    RibosSemanticContext context = {
        .parser = parser,
        .schema = schema,
        .ir_module = ir_module,
        .summary = summary,
        .diagnostic = diagnostic,
        .status = RIBOS_COMPILE_OK,
    };

    if (ribos_schema_validate(schema) != RIBOS_SCHEMA_OK ||
        ribos_schema_compute_identity(
            schema,
            summary->schema_digest) != RIBOS_SCHEMA_OK) {
        context.status = RIBOS_COMPILE_SCHEMA_ERROR;
        context.diagnostic->code = RIBOS_E_SCHEMA_INVALID;
        (void)snprintf(
            context.diagnostic->message,
            sizeof(context.diagnostic->message),
            "selected product schema is invalid or cannot be identified");
    }
    if (context.status == RIBOS_COMPILE_OK) {
        ribos_initialize_types(&context);
    }
    if (context.status == RIBOS_COMPILE_OK) {
        ribos_collect_declarations(&context);
    }
    if (context.status == RIBOS_COMPILE_OK) {
        ribos_resolve_declaration_types(&context);
    }
    if (context.status == RIBOS_COMPILE_OK) {
        ribos_check_functions(&context);
    }
    if (context.status == RIBOS_COMPILE_OK) {
        ribos_check_policy_effects(&context);
    }
    if (context.status == RIBOS_COMPILE_OK && ir_module != NULL) {
        context.status = ribos_lower_policy_ir(&context, ir_module);
    }
    summary->ast_node_count = ribos_count_reachable_ast(parser->root);
    summary->ast_reduction_count = parser->ast_node_count;
    summary->parser_arena_bytes = parser->arena_bytes;
    summary->peak_transient_bytes = parser->peak_transient_bytes;
    summary->type_count = context.type_count;
    summary->function_count = context.function_count;
    summary->max_scope_depth = context.max_scope_depth;
    if (context.status == RIBOS_COMPILE_OK &&
        dump != NULL &&
        (dump_flags & RIBOS_DUMP_SEMANTICS) != 0) {
        ribos_dump_semantic_model(&context, dump);
    }
    return context.status;
}
