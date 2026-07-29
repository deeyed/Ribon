#include "parser_internal.h"

static RibosSourceLocation
ribos_token_end(const Token *token)
{
    return (RibosSourceLocation){
        .byte_offset = token->end_byte_offset,
        .line = token->end_line,
        .column = token->end_column,
    };
}

static RibosSourceLocation
ribos_token_start(const Token *token)
{
    return (RibosSourceLocation){
        .byte_offset = token->byte_offset,
        .line = token->line,
        .column = token->column,
    };
}

void *
ribos_arena_allocate(Parser *parser, size_t size)
{
    RibosArenaAllocation *allocation;

    if (parser == NULL || size == 0 ||
        size > RIBOS_MAX_ARENA_BYTES - parser->arena_bytes ||
        size > SIZE_MAX - sizeof(*allocation)) {
        if (parser != NULL) {
            parser->failure_status = RIBOS_PARSE_LIMIT_EXCEEDED;
            parser->error_indicator = 1;
        }
        return NULL;
    }
    allocation = malloc(sizeof(*allocation) + size);
    if (allocation == NULL) {
        parser->failure_status = RIBOS_PARSE_NO_MEMORY;
        parser->error_indicator = 1;
        return NULL;
    }
    allocation->next = parser->arena_allocations;
    parser->arena_allocations = allocation;
    parser->arena_bytes += size;
    return allocation->bytes;
}

static Token *
ribos_previous_token(Parser *parser)
{
    if (parser == NULL || parser->mark <= 0 ||
        (size_t)(parser->mark - 1) >= parser->token_count) {
        return NULL;
    }
    return &parser->tokens[parser->mark - 1];
}

RibosAstNode *
ribos_ast_make(
    Parser *parser,
    RibosAstKind kind,
    Token *token,
    RibosAstNode *first,
    RibosAstNode *second,
    RibosAstNode *third,
    asdl_seq *items,
    asdl_seq *extra,
    uint32_t flags)
{
    RibosAstNode *node;
    Token *last;

    if (parser == NULL || parser->ast_node_count == RIBOS_MAX_AST_NODES) {
        if (parser != NULL) {
            parser->failure_status = RIBOS_PARSE_LIMIT_EXCEEDED;
            parser->error_indicator = 1;
        }
        return NULL;
    }
    node = ribos_arena_allocate(parser, sizeof(*node));
    if (node == NULL) {
        return NULL;
    }
    memset(node, 0, sizeof(*node));
    node->id = (uint32_t)parser->ast_node_count++;
    node->kind = kind;
    node->token = token;
    node->first = first;
    node->second = second;
    node->third = third;
    node->items = items;
    node->extra = extra;
    node->flags = flags;

    if (token != NULL) {
        node->span.start = ribos_token_start(token);
    } else if (first != NULL) {
        node->span.start = first->span.start;
    } else if (items != NULL && items->size > 0) {
        const RibosAstNode *item = items->elements[0];

        node->span.start = item->span.start;
    } else {
        last = ribos_previous_token(parser);
        if (last != NULL) {
            node->span.start = ribos_token_start(last);
        } else {
            node->span.start.line = 1;
            node->span.start.column = 1;
        }
    }

    last = ribos_previous_token(parser);
    if (last != NULL) {
        node->span.end = ribos_token_end(last);
    } else {
        node->span.end = node->span.start;
    }
    return node;
}

RibosAstNode *
ribos_ast_program(Parser *parser, asdl_seq *declarations)
{
    RibosAstNode *program;

    program = ribos_ast_make(
        parser,
        RIBOS_AST_PROGRAM,
        parser->token_count == 0 ? NULL : &parser->tokens[0],
        NULL,
        NULL,
        NULL,
        declarations,
        NULL,
        0);
    if (program != NULL) {
        parser->root = program;
        parser->result.declaration_count =
            declarations == NULL ? 0 : (size_t)declarations->size;
    }
    return program;
}

RibosAstNode *
ribos_ast_decorated(
    Parser *parser,
    RibosAstNode *declaration,
    asdl_seq *decorators)
{
    UNUSED(parser);
    if (declaration == NULL) {
        return NULL;
    }
    declaration->extra = decorators;
    if (decorators != NULL && decorators->size > 0) {
        const RibosAstNode *first = decorators->elements[0];

        declaration->span.start = first->span.start;
    }
    return declaration;
}

RibosAstNode *
ribos_ast_path(Parser *parser, Token *first, asdl_seq *rest)
{
    return ribos_ast_make(
        parser,
        RIBOS_AST_PATH,
        first,
        NULL,
        NULL,
        NULL,
        rest,
        NULL,
        0);
}

RibosAstNode *
ribos_ast_literal(Parser *parser, RibosAstKind kind, Token *token)
{
    return ribos_ast_make(
        parser,
        kind,
        token,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        0);
}

RibosAstNode *
ribos_ast_binary_tail(
    Parser *parser,
    RibosOperator operation,
    Token *token,
    RibosAstNode *right)
{
    return ribos_ast_make(
        parser,
        RIBOS_AST_BINARY,
        token,
        NULL,
        right,
        NULL,
        NULL,
        NULL,
        (uint32_t)operation);
}

asdl_seq *
ribos_ast_singleton_sequence(Parser *parser, RibosAstNode *node)
{
    asdl_seq *sequence;

    if (node == NULL) {
        return NULL;
    }
    sequence = _Py_asdl_generic_seq_new(1, parser);
    if (sequence != NULL) {
        asdl_seq_SET_UNTYPED(sequence, 0, node);
    }
    return sequence;
}

RibosAstNode *
ribos_ast_fold_binary(
    Parser *parser,
    RibosAstNode *left,
    asdl_seq *tails)
{
    Py_ssize_t index;

    if (left == NULL) {
        return NULL;
    }
    if (tails == NULL) {
        return left;
    }
    for (index = 0; index < tails->size; ++index) {
        RibosAstNode *tail = tails->elements[index];
        RibosAstNode *combined;

        if (tail == NULL || tail->kind != RIBOS_AST_BINARY ||
            tail->second == NULL) {
            parser->failure_status = RIBOS_PARSE_SYNTAX_ERROR;
            parser->error_indicator = 1;
            return NULL;
        }
        combined = ribos_ast_make(
            parser,
            RIBOS_AST_BINARY,
            tail->token,
            left,
            tail->second,
            NULL,
            NULL,
            NULL,
            tail->flags);
        if (combined == NULL) {
            return NULL;
        }
        combined->span.start = left->span.start;
        combined->span.end = tail->second->span.end;
        left = combined;
    }
    return left;
}

RibosAstNode *
ribos_ast_apply_trailers(
    Parser *parser,
    RibosAstNode *base,
    asdl_seq *trailers)
{
    Py_ssize_t index;

    UNUSED(parser);
    if (base == NULL || trailers == NULL) {
        return base;
    }
    for (index = 0; index < trailers->size; ++index) {
        RibosAstNode *trailer = trailers->elements[index];

        if (trailer == NULL) {
            return NULL;
        }
        trailer->first = base;
        trailer->span.start = base->span.start;
        base = trailer;
    }
    return base;
}
