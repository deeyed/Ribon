#include "parser_internal.h"

static void
ribos_clear_outputs(
    RibosParseSummary *summary,
    RibosDiagnostic *diagnostic)
{
    memset(summary, 0, sizeof(*summary));
    memset(diagnostic, 0, sizeof(*diagnostic));
    diagnostic->location.line = 1;
    diagnostic->location.column = 1;
}

static void
ribos_copy_token_spelling(
    const Token *token,
    char *output,
    size_t output_size)
{
    size_t length;

    if (output_size == 0) {
        return;
    }
    length = token->length;
    if (length == 0) {
        length = strlen(ribos_token_name(token->type));
        if (length >= output_size) {
            length = output_size - 1;
        }
        memcpy(output, ribos_token_name(token->type), length);
    } else {
        if (length >= output_size) {
            length = output_size - 1;
        }
        memcpy(output, token->start, length);
    }
    output[length] = '\0';
}

void
ribos_parser_set_syntax_diagnostic(
    const Parser *parser,
    RibosDiagnostic *diagnostic)
{
    size_t token_index;
    const Token *token;

    token_index = parser->farthest_mark < 0 ?
        0 : (size_t)parser->farthest_mark;
    if (token_index >= parser->token_count) {
        token_index = parser->token_count - 1;
    }
    token = &parser->tokens[token_index];
    diagnostic->location.byte_offset = token->byte_offset;
    diagnostic->location.line = token->line;
    diagnostic->location.column = token->column;
    ribos_copy_token_spelling(
        token,
        diagnostic->token,
        sizeof(diagnostic->token));
    if (ribos_token_is_reserved(token)) {
        diagnostic->kind = RIBOS_DIAGNOSTIC_RESERVED_FEATURE;
        (void)ribos_host_snprintf(
            diagnostic->message,
            sizeof(diagnostic->message),
            "reserved feature is not part of Ribos");
    } else if (parser->failure_status == RIBOS_PARSE_LIMIT_EXCEEDED) {
        diagnostic->kind = RIBOS_DIAGNOSTIC_RESOURCE_LIMIT;
        (void)ribos_host_snprintf(
            diagnostic->message,
            sizeof(diagnostic->message),
            "parser nesting limit exceeded");
    } else if (parser->failure_status == RIBOS_PARSE_NO_MEMORY) {
        diagnostic->kind = RIBOS_DIAGNOSTIC_RESOURCE_LIMIT;
        (void)ribos_host_snprintf(
            diagnostic->message,
            sizeof(diagnostic->message),
            "host parser allocation failed");
    } else {
        diagnostic->kind = RIBOS_DIAGNOSTIC_SYNTAX;
        (void)ribos_host_snprintf(
            diagnostic->message,
            sizeof(diagnostic->message),
            "unexpected token in Ribos source");
    }
}

void
ribos_parser_release(Parser *parser)
{
    RibosArenaAllocation *allocation = parser->arena_allocations;

    while (allocation != NULL) {
        RibosArenaAllocation *next = allocation->next;

        ribos_allocator_release(
            parser->allocator,
            allocation,
            sizeof(*allocation) + allocation->size,
            _Alignof(RibosArenaAllocation));
        allocation = next;
    }
    ribos_free_token_stream(
        parser->allocator,
        parser->tokens,
        parser->token_capacity,
        parser->trivia,
        parser->trivia_capacity);
    memset(parser, 0, sizeof(*parser));
}

RibosParseStatus
ribos_parse_source(
    const RibosAllocator *allocator,
    const char *source,
    size_t source_length,
    RibosParseSummary *summary,
    RibosDiagnostic *diagnostic)
{
    Parser parser = {
        .allocator = allocator,
        .farthest_mark = 0,
        .failure_status = RIBOS_PARSE_SYNTAX_ERROR,
    };
    RibosAstNode *generated_result;
    RibosParseStatus status;

    if (allocator == NULL || source == NULL ||
        summary == NULL || diagnostic == NULL) {
        return RIBOS_PARSE_INVALID_ARGUMENT;
    }
    ribos_clear_outputs(summary, diagnostic);
    status = ribos_lex_source(
        allocator,
        source,
        source_length,
        &parser.tokens,
        &parser.token_count,
        &parser.token_capacity,
        &parser.trivia,
        &parser.trivia_count,
        &parser.trivia_capacity,
        diagnostic);
    if (status != RIBOS_PARSE_OK) {
        return status;
    }

    parser.arena = &parser;
    parser.result.source_bytes = source_length;
    parser.result.token_count = parser.token_count;
    ribos_parser_runtime_enter(&parser);
    generated_result = ribos_generated_parse(&parser);
    ribos_parser_runtime_leave();
    parser.result.max_parser_depth = parser.max_parser_depth;
    if (generated_result == NULL || parser.error_indicator != 0) {
        status = parser.failure_status;
        ribos_parser_set_syntax_diagnostic(&parser, diagnostic);
    } else {
        *summary = parser.result;
        status = RIBOS_PARSE_OK;
    }

    ribos_parser_release(&parser);
    return status;
}

const char *
ribos_parse_status_name(RibosParseStatus status)
{
    switch (status) {
    case RIBOS_PARSE_OK:
        return "ok";
    case RIBOS_PARSE_INVALID_ARGUMENT:
        return "invalid-argument";
    case RIBOS_PARSE_INVALID_UTF8:
        return "invalid-utf8";
    case RIBOS_PARSE_LEXICAL_ERROR:
        return "lexical-error";
    case RIBOS_PARSE_SYNTAX_ERROR:
        return "syntax-error";
    case RIBOS_PARSE_NO_MEMORY:
        return "no-memory";
    case RIBOS_PARSE_LIMIT_EXCEEDED:
        return "limit-exceeded";
    default:
        return "unknown";
    }
}

const char *
ribos_diagnostic_kind_name(RibosDiagnosticKind kind)
{
    switch (kind) {
    case RIBOS_DIAGNOSTIC_NONE:
        return "none";
    case RIBOS_DIAGNOSTIC_INVALID_UTF8:
        return "invalid-utf8";
    case RIBOS_DIAGNOSTIC_INVALID_CHARACTER:
        return "invalid-character";
    case RIBOS_DIAGNOSTIC_INVALID_NUMBER:
        return "invalid-number";
    case RIBOS_DIAGNOSTIC_INVALID_STRING:
        return "invalid-string";
    case RIBOS_DIAGNOSTIC_RESERVED_FEATURE:
        return "reserved-feature";
    case RIBOS_DIAGNOSTIC_SYNTAX:
        return "syntax";
    case RIBOS_DIAGNOSTIC_RESOURCE_LIMIT:
        return "resource-limit";
    default:
        return "unknown";
    }
}
