#include "parser_internal.h"

static _Thread_local Parser *ribos_active_parser;

typedef struct RibosTransientAllocation {
    Parser *owner;
    size_t size;
    max_align_t alignment;
    unsigned char bytes[];
} RibosTransientAllocation;

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
    sequence = ribos_arena_allocate(parser, payload_size);
    if (sequence == NULL) {
        return NULL;
    }
    sequence->size = size;
    return sequence;
}

asdl_seq *
_PyPegen_seq_insert_in_front(
    Parser *parser,
    void *element,
    asdl_seq *sequence)
{
    asdl_seq *result;
    Py_ssize_t index;
    Py_ssize_t tail_size = sequence == NULL ? 0 : sequence->size;

    if (element == NULL || tail_size < 0) {
        return NULL;
    }
    result = _Py_asdl_generic_seq_new(tail_size + 1, parser);
    if (result == NULL) {
        return NULL;
    }
    asdl_seq_SET_UNTYPED(result, 0, element);
    for (index = 0; index < tail_size; ++index) {
        asdl_seq_SET_UNTYPED(result, index + 1, sequence->elements[index]);
    }
    return result;
}

void *
PyMem_Malloc(size_t size)
{
    RibosTransientAllocation *allocation;
    Parser *parser = ribos_active_parser;

    if (parser == NULL || size > SIZE_MAX - sizeof(*allocation) ||
        size > RIBOS_MAX_TRANSIENT_BYTES - parser->transient_bytes) {
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
    allocation->owner = parser;
    allocation->size = size;
    parser->transient_bytes += size;
    if (parser->transient_bytes > parser->peak_transient_bytes) {
        parser->peak_transient_bytes = parser->transient_bytes;
    }
    return allocation->bytes;
}

void *
PyMem_Realloc(void *allocation, size_t size)
{
    RibosTransientAllocation *header;
    RibosTransientAllocation *replacement;
    Parser *parser;
    size_t retained_bytes;

    if (allocation == NULL) {
        return PyMem_Malloc(size);
    }
    header = (RibosTransientAllocation *)(
        (unsigned char *)allocation -
        offsetof(RibosTransientAllocation, bytes));
    parser = header->owner;
    retained_bytes = parser->transient_bytes - header->size;
    if (size > SIZE_MAX - sizeof(*header) ||
        size > RIBOS_MAX_TRANSIENT_BYTES - retained_bytes) {
        parser->failure_status = RIBOS_PARSE_LIMIT_EXCEEDED;
        parser->error_indicator = 1;
        return NULL;
    }
    replacement = realloc(header, sizeof(*replacement) + size);
    if (replacement == NULL) {
        parser->failure_status = RIBOS_PARSE_NO_MEMORY;
        parser->error_indicator = 1;
        return NULL;
    }
    replacement->owner = parser;
    replacement->size = size;
    parser->transient_bytes = retained_bytes + size;
    if (parser->transient_bytes > parser->peak_transient_bytes) {
        parser->peak_transient_bytes = parser->transient_bytes;
    }
    return replacement->bytes;
}

void
PyMem_Free(void *allocation)
{
    RibosTransientAllocation *header;

    if (allocation == NULL) {
        return;
    }
    header = (RibosTransientAllocation *)(
        (unsigned char *)allocation -
        offsetof(RibosTransientAllocation, bytes));
    if (header->owner != NULL &&
        header->owner->transient_bytes >= header->size) {
        header->owner->transient_bytes -= header->size;
    }
    free(header);
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

RibosAstNode *
ribos_parser_finish(Parser *parser, const asdl_seq *declarations)
{
    if (parser == NULL || declarations == NULL) {
        return NULL;
    }
    return ribos_ast_program(parser, (asdl_seq *)declarations);
}
