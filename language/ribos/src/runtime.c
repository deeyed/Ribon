#include "parser_internal.h"

static _Thread_local Parser *ribos_active_parser;

static int
ribos_token_matches(const Token *token, const char *spelling)
{
    size_t spelling_length;

    if (token == NULL || spelling == NULL) {
        return 0;
    }
    spelling_length = strlen(spelling);
    return token->length == spelling_length &&
        memcmp(token->start, spelling, spelling_length) == 0;
}

static int
ribos_keyword_type(const Parser *parser, const Token *token)
{
    KeywordToken *keywords;
    size_t index;

    if (parser == NULL || token == NULL || token->type != NAME ||
        token->length >= (size_t)parser->n_keyword_lists) {
        return -1;
    }

    keywords = parser->keywords[token->length];
    for (index = 0; keywords[index].str != NULL; ++index) {
        if (ribos_token_matches(token, keywords[index].str)) {
            return keywords[index].type;
        }
    }
    return -1;
}

static void
ribos_note_progress(Parser *parser)
{
    if (parser->mark > parser->farthest_mark) {
        parser->farthest_mark = parser->mark;
    }
    if ((uint32_t)parser->level > parser->max_parser_depth) {
        parser->max_parser_depth = (uint32_t)parser->level;
    }
}

Token *
_PyPegen_expect_token(Parser *parser, int expected_type)
{
    Token *token;
    int actual_keyword;

    ribos_note_progress(parser);
    if (parser->mark < 0 ||
        (size_t)parser->mark >= parser->token_count) {
        return NULL;
    }

    token = &parser->tokens[parser->mark];
    if (expected_type >= 500) {
        actual_keyword = ribos_keyword_type(parser, token);
        if (actual_keyword != expected_type) {
            return NULL;
        }
    } else if (token->type != expected_type) {
        return NULL;
    }

    ++parser->mark;
    ribos_note_progress(parser);
    return token;
}

expr_ty
_PyPegen_name_token(Parser *parser)
{
    Token *token;

    ribos_note_progress(parser);
    if (parser->mark < 0 ||
        (size_t)parser->mark >= parser->token_count) {
        return NULL;
    }
    token = &parser->tokens[parser->mark];
    if (token->type != NAME || ribos_keyword_type(parser, token) >= 0) {
        return NULL;
    }

    ++parser->mark;
    ribos_note_progress(parser);
    return token;
}

expr_ty
_PyPegen_number_token(Parser *parser)
{
    return _PyPegen_expect_token(parser, NUMBER);
}

expr_ty
_PyPegen_string_token(Parser *parser)
{
    return _PyPegen_expect_token(parser, STRING);
}

void *
_PyPegen_dummy_name(Parser *parser, ...)
{
    static const unsigned char sentinel = 1;

    UNUSED(parser);
    return (void *)&sentinel;
}

int
_PyPegen_lookahead(
    int positive,
    void *(*rule)(Parser *parser),
    Parser *parser)
{
    int mark = parser->mark;
    int farthest_mark = parser->farthest_mark;
    void *result = rule(parser);

    parser->mark = mark;
    parser->farthest_mark = farthest_mark;
    return (result != NULL) == positive;
}

void
ribos_parser_runtime_enter(Parser *parser)
{
    ribos_active_parser = parser;
}

void
ribos_parser_runtime_leave(void)
{
    ribos_active_parser = NULL;
}

void *
_Py_asdl_generic_seq_new(Py_ssize_t size, void *arena)
{
    Parser *parser = arena;
    RibosArenaAllocation *allocation;
    asdl_seq *sequence;
    size_t payload_size;

    if (parser == NULL || size < 0 ||
        (size_t)size > (SIZE_MAX - sizeof(*sequence)) / sizeof(void *)) {
        if (parser != NULL) {
            parser->failure_status = RIBOS_PARSE_NO_MEMORY;
        }
        return NULL;
    }
    payload_size = sizeof(*sequence) + (size_t)size * sizeof(void *);
    if (payload_size > SIZE_MAX - sizeof(*allocation)) {
        parser->failure_status = RIBOS_PARSE_NO_MEMORY;
        return NULL;
    }

    allocation = malloc(sizeof(*allocation) + payload_size);
    if (allocation == NULL) {
        parser->failure_status = RIBOS_PARSE_NO_MEMORY;
        return NULL;
    }
    allocation->next = parser->arena_allocations;
    parser->arena_allocations = allocation;
    sequence = (asdl_seq *)allocation->bytes;
    sequence->size = size;
    return sequence;
}

void *
PyMem_Malloc(size_t size)
{
    void *allocation = malloc(size);

    if (allocation == NULL && ribos_active_parser != NULL) {
        ribos_active_parser->failure_status = RIBOS_PARSE_NO_MEMORY;
    }
    return allocation;
}

void *
PyMem_Realloc(void *allocation, size_t size)
{
    void *replacement = realloc(allocation, size);

    if (replacement == NULL && ribos_active_parser != NULL) {
        ribos_active_parser->failure_status = RIBOS_PARSE_NO_MEMORY;
    }
    return replacement;
}

void
PyMem_Free(void *allocation)
{
    free(allocation);
}

void
PyErr_NoMemory(void)
{
}

int
PyErr_Occurred(void)
{
    return 0;
}

void *
PyThreadState_Get(void)
{
    return NULL;
}

int
_Py_ReachedRecursionLimitWithMargin(void *thread_state, int margin)
{
    UNUSED(thread_state);
    UNUSED(margin);
    return 0;
}

void
_Pypegen_stack_overflow(Parser *parser)
{
    parser->error_indicator = 1;
    parser->failure_status = RIBOS_PARSE_LIMIT_EXCEEDED;
}

RibosParseSummary *
ribos_parser_finish(Parser *parser, const asdl_seq *declarations)
{
    if (parser == NULL || declarations == NULL) {
        return NULL;
    }
    parser->result.declaration_count = (size_t)declarations->size;
    return &parser->result;
}
