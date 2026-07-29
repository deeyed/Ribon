#ifndef RIBOS_PARSER_INTERNAL_H
#define RIBOS_PARSER_INTERNAL_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ribos/parser.h"
#include "tokens.h"

#define MAXSTACK 512
#define UNUSED(value) ((void)(value))
#define D(call) ((void)0)

typedef ptrdiff_t Py_ssize_t;

typedef struct Token {
    int type;
    const char *start;
    size_t length;
    size_t byte_offset;
    uint32_t line;
    uint32_t column;
} Token;

typedef Token *expr_ty;

typedef struct KeywordToken {
    const char *str;
    int type;
} KeywordToken;

typedef struct asdl_seq {
    Py_ssize_t size;
    void *elements[];
} asdl_seq;

#define asdl_seq_SET_UNTYPED(sequence, index, value) \
    ((sequence)->elements[(index)] = (value))

typedef struct RibosArenaAllocation {
    struct RibosArenaAllocation *next;
    max_align_t alignment;
    unsigned char bytes[];
} RibosArenaAllocation;

typedef struct Parser {
    Token *tokens;
    size_t token_count;
    int mark;
    int level;
    int error_indicator;
    int farthest_mark;
    uint32_t max_parser_depth;
    KeywordToken **keywords;
    int n_keyword_lists;
    char **soft_keywords;
    void *arena;
    RibosArenaAllocation *arena_allocations;
    RibosParseSummary result;
    RibosParseStatus failure_status;
} Parser;

RibosParseSummary *ribos_generated_parse(Parser *parser);
RibosParseSummary *ribos_parser_finish(
    Parser *parser,
    const asdl_seq *declarations);
void ribos_parser_runtime_enter(Parser *parser);
void ribos_parser_runtime_leave(void);

Token *_PyPegen_expect_token(Parser *parser, int expected_type);
expr_ty _PyPegen_name_token(Parser *parser);
expr_ty _PyPegen_number_token(Parser *parser);
expr_ty _PyPegen_string_token(Parser *parser);
void *_PyPegen_dummy_name(Parser *parser, ...);
int _PyPegen_lookahead(
    int positive,
    void *(*rule)(Parser *parser),
    Parser *parser);
void *_Py_asdl_generic_seq_new(Py_ssize_t size, void *arena);
void *PyMem_Malloc(size_t size);
void *PyMem_Realloc(void *allocation, size_t size);
void PyMem_Free(void *allocation);
void PyErr_NoMemory(void);
int PyErr_Occurred(void);
void *PyThreadState_Get(void);
int _Py_ReachedRecursionLimitWithMargin(void *thread_state, int margin);
void _Pypegen_stack_overflow(Parser *parser);

RibosParseStatus ribos_lex_source(
    const char *source,
    size_t source_length,
    Token **tokens,
    size_t *token_count,
    RibosDiagnostic *diagnostic);
void ribos_free_tokens(Token *tokens);
const char *ribos_token_name(int token_type);
int ribos_token_is_reserved(const Token *token);

#endif
