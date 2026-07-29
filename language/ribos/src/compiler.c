#include "parser_internal.h"

static RibosCompileStatus
ribos_compile_status_from_parse(RibosParseStatus status)
{
    switch (status) {
    case RIBOS_PARSE_NO_MEMORY:
        return RIBOS_COMPILE_NO_MEMORY;
    case RIBOS_PARSE_LIMIT_EXCEEDED:
        return RIBOS_COMPILE_BOUND_ERROR;
    case RIBOS_PARSE_OK:
        return RIBOS_COMPILE_OK;
    default:
        return RIBOS_COMPILE_PARSE_ERROR;
    }
}

RibosCompileStatus
ribos_compile_source_with_dump(
    const char *source,
    size_t source_length,
    RibosCompileSummary *summary,
    RibosCompileDiagnostic *diagnostic,
    FILE *dump,
    unsigned dump_flags)
{
    Parser parser = {
        .farthest_mark = 0,
        .failure_status = RIBOS_PARSE_SYNTAX_ERROR,
    };
    RibosAstNode *root;
    RibosParseStatus parse_status;
    RibosCompileStatus status;

    if (source == NULL || summary == NULL || diagnostic == NULL ||
        (dump_flags != 0 && dump == NULL)) {
        return RIBOS_COMPILE_INVALID_ARGUMENT;
    }
    memset(summary, 0, sizeof(*summary));
    memset(diagnostic, 0, sizeof(*diagnostic));
    diagnostic->parse.location.line = 1;
    diagnostic->parse.location.column = 1;
    diagnostic->span.start.line = 1;
    diagnostic->span.start.column = 1;
    diagnostic->span.end = diagnostic->span.start;

    parse_status = ribos_lex_source(
        source,
        source_length,
        &parser.tokens,
        &parser.token_count,
        &parser.trivia,
        &parser.trivia_count,
        &diagnostic->parse);
    if (parse_status != RIBOS_PARSE_OK) {
        return ribos_compile_status_from_parse(parse_status);
    }
    parser.arena = &parser;
    parser.result.source_bytes = source_length;
    parser.result.token_count = parser.token_count;
    ribos_parser_runtime_enter(&parser);
    root = ribos_generated_parse(&parser);
    ribos_parser_runtime_leave();
    parser.result.max_parser_depth = parser.max_parser_depth;
    if (root == NULL || parser.error_indicator != 0) {
        parse_status = parser.failure_status;
        ribos_parser_set_syntax_diagnostic(&parser, &diagnostic->parse);
        status = ribos_compile_status_from_parse(parse_status);
        ribos_parser_release(&parser);
        return status;
    }

    summary->syntax = parser.result;
    status = ribos_compile_parser_tree(
        &parser,
        summary,
        diagnostic,
        dump,
        dump_flags);
    if (status == RIBOS_COMPILE_OK && dump_flags != 0) {
        ribos_dump_parser_model(&parser, dump, dump_flags);
    }
    ribos_parser_release(&parser);
    return status;
}

RibosCompileStatus
ribos_compile_source(
    const char *source,
    size_t source_length,
    RibosCompileSummary *summary,
    RibosCompileDiagnostic *diagnostic)
{
    return ribos_compile_source_with_dump(
        source,
        source_length,
        summary,
        diagnostic,
        NULL,
        0);
}

const char *
ribos_compile_status_name(RibosCompileStatus status)
{
    switch (status) {
    case RIBOS_COMPILE_OK:
        return "ok";
    case RIBOS_COMPILE_INVALID_ARGUMENT:
        return "invalid-argument";
    case RIBOS_COMPILE_PARSE_ERROR:
        return "parse-error";
    case RIBOS_COMPILE_NAME_ERROR:
        return "name-error";
    case RIBOS_COMPILE_TYPE_ERROR:
        return "type-error";
    case RIBOS_COMPILE_CAPABILITY_ERROR:
        return "capability-error";
    case RIBOS_COMPILE_BOUND_ERROR:
        return "bound-error";
    case RIBOS_COMPILE_NO_MEMORY:
        return "no-memory";
    case RIBOS_COMPILE_INTERNAL_ERROR:
        return "internal-error";
    default:
        return "unknown";
    }
}

const char *
ribos_compile_diagnostic_code_name(RibosCompileDiagnosticCode code)
{
    switch (code) {
    case RIBOS_E_NONE:
        return "OK";
    case RIBOS_E_DUPLICATE_DECLARATION:
        return "E_DUPLICATE_DECLARATION";
    case RIBOS_E_DUPLICATE_BINDING:
        return "E_DUPLICATE_BINDING";
    case RIBOS_E_UNKNOWN_NAME:
        return "E_UNKNOWN_NAME";
    case RIBOS_E_UNKNOWN_TYPE:
        return "E_UNKNOWN_TYPE";
    case RIBOS_E_UNKNOWN_MEMBER:
        return "E_UNKNOWN_MEMBER";
    case RIBOS_E_INVALID_DECORATOR:
        return "E_INVALID_DECORATOR";
    case RIBOS_E_TYPE_MISMATCH:
        return "E_TYPE_MISMATCH";
    case RIBOS_E_ARGUMENT_COUNT_MISMATCH:
        return "E_ARGUMENT_COUNT_MISMATCH";
    case RIBOS_E_ARGUMENT_TYPE_MISMATCH:
        return "E_ARGUMENT_TYPE_MISMATCH";
    case RIBOS_E_CONDITION_NOT_BOOL:
        return "E_CONDITION_NOT_BOOL";
    case RIBOS_E_RETURN_TYPE_MISMATCH:
        return "E_RETURN_TYPE_MISMATCH";
    case RIBOS_E_MISSING_RETURN:
        return "E_MISSING_RETURN";
    case RIBOS_E_NON_EXHAUSTIVE_MATCH:
        return "E_NON_EXHAUSTIVE_MATCH";
    case RIBOS_E_MUTATE_IMMUTABLE_BINDING:
        return "E_MUTATE_IMMUTABLE_BINDING";
    case RIBOS_E_INVALID_ASSIGNMENT_TARGET:
        return "E_INVALID_ASSIGNMENT_TARGET";
    case RIBOS_E_CANNOT_INFER_EMPTY_COLLECTION:
        return "E_CANNOT_INFER_EMPTY_COLLECTION";
    case RIBOS_E_COLLECTION_ELEMENT_TYPE_MISMATCH:
        return "E_COLLECTION_ELEMENT_TYPE_MISMATCH";
    case RIBOS_E_COLLECTION_BOUND_EXCEEDED:
        return "E_COLLECTION_BOUND_EXCEEDED";
    case RIBOS_E_UNBOUNDED_ITERATION:
        return "E_UNBOUNDED_ITERATION";
    case RIBOS_E_RESULT_MUST_BE_USED:
        return "E_RESULT_MUST_BE_USED";
    case RIBOS_E_CAPABILITY_NOT_DECLARED:
        return "E_CAPABILITY_NOT_DECLARED";
    case RIBOS_E_PURE_FUNCTION_HAS_EFFECT:
        return "E_PURE_FUNCTION_HAS_EFFECT";
    case RIBOS_E_HELPER_BUDGET_EXCEEDED:
        return "E_HELPER_BUDGET_EXCEEDED";
    case RIBOS_E_RECURSIVE_CALL_GRAPH:
        return "E_RECURSIVE_CALL_GRAPH";
    case RIBOS_E_RESOURCE_LIMIT:
        return "E_RESOURCE_LIMIT";
    case RIBOS_E_INTERNAL:
        return "E_INTERNAL";
    default:
        return "E_UNKNOWN";
    }
}
