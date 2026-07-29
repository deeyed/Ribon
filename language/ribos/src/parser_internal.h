#ifndef RIBOS_PARSER_INTERNAL_H
#define RIBOS_PARSER_INTERNAL_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ribos/parser.h"
#include "ribos/compiler.h"
#include "tokens.h"

#define MAXSTACK 512
#define UNUSED(value) ((void)(value))
#define D(call) ((void)0)
#define RIBOS_MAX_AST_NODES 65536u
#define RIBOS_MAX_ARENA_BYTES (16u * 1024u * 1024u)
#define RIBOS_MAX_TRANSIENT_BYTES (8u * 1024u * 1024u)
#define RIBOS_DUMP_TOKENS (1u << 0)
#define RIBOS_DUMP_AST (1u << 1)
#define RIBOS_DUMP_SEMANTICS (1u << 2)

typedef ptrdiff_t Py_ssize_t;

typedef enum RibosTriviaKind {
    RIBOS_TRIVIA_SPACE = 0,
    RIBOS_TRIVIA_COMMENT,
    RIBOS_TRIVIA_NEWLINE
} RibosTriviaKind;

typedef struct RibosTrivia {
    RibosTriviaKind kind;
    const char *start;
    size_t length;
    RibosSourceSpan span;
} RibosTrivia;

typedef struct Token {
    int type;
    const char *start;
    size_t length;
    size_t byte_offset;
    size_t end_byte_offset;
    uint32_t line;
    uint32_t column;
    uint32_t end_line;
    uint32_t end_column;
    size_t leading_trivia_index;
    size_t leading_trivia_count;
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

typedef enum RibosAstKind {
    RIBOS_AST_PROGRAM = 0,
    RIBOS_AST_DECORATOR,
    RIBOS_AST_ATTRIBUTE_ARGUMENT,
    RIBOS_AST_FUNCTION,
    RIBOS_AST_PARAMETER,
    RIBOS_AST_STRUCT,
    RIBOS_AST_FIELD,
    RIBOS_AST_ENUM,
    RIBOS_AST_VARIANT,
    RIBOS_AST_BLOCK,
    RIBOS_AST_LET,
    RIBOS_AST_RETURN,
    RIBOS_AST_ASSIGN,
    RIBOS_AST_IF,
    RIBOS_AST_FOR,
    RIBOS_AST_MATCH,
    RIBOS_AST_MATCH_ARM,
    RIBOS_AST_PATTERN,
    RIBOS_AST_IF_EXPRESSION,
    RIBOS_AST_UNARY,
    RIBOS_AST_BINARY,
    RIBOS_AST_CALL,
    RIBOS_AST_ARGUMENT,
    RIBOS_AST_MEMBER,
    RIBOS_AST_INDEX,
    RIBOS_AST_PROPAGATE,
    RIBOS_AST_PATH,
    RIBOS_AST_INTEGER,
    RIBOS_AST_STRING,
    RIBOS_AST_BOOLEAN,
    RIBOS_AST_NONE,
    RIBOS_AST_LIST,
    RIBOS_AST_MAP,
    RIBOS_AST_MAP_ENTRY,
    RIBOS_AST_TYPE
} RibosAstKind;

typedef enum RibosOperator {
    RIBOS_OPERATOR_NONE = 0,
    RIBOS_OPERATOR_OR,
    RIBOS_OPERATOR_AND,
    RIBOS_OPERATOR_NOT,
    RIBOS_OPERATOR_EQUAL,
    RIBOS_OPERATOR_NOT_EQUAL,
    RIBOS_OPERATOR_LESS,
    RIBOS_OPERATOR_LESS_EQUAL,
    RIBOS_OPERATOR_GREATER,
    RIBOS_OPERATOR_GREATER_EQUAL,
    RIBOS_OPERATOR_IN,
    RIBOS_OPERATOR_NOT_IN,
    RIBOS_OPERATOR_BIT_OR,
    RIBOS_OPERATOR_BIT_XOR,
    RIBOS_OPERATOR_BIT_AND,
    RIBOS_OPERATOR_SHIFT_LEFT,
    RIBOS_OPERATOR_SHIFT_RIGHT,
    RIBOS_OPERATOR_ADD,
    RIBOS_OPERATOR_SUBTRACT,
    RIBOS_OPERATOR_MULTIPLY,
    RIBOS_OPERATOR_DIVIDE,
    RIBOS_OPERATOR_REMAINDER,
    RIBOS_OPERATOR_POSITIVE,
    RIBOS_OPERATOR_NEGATIVE,
    RIBOS_OPERATOR_BIT_NOT
} RibosOperator;

typedef struct RibosAstNode {
    uint32_t id;
    RibosAstKind kind;
    RibosSourceSpan span;
    Token *token;
    struct RibosAstNode *first;
    struct RibosAstNode *second;
    struct RibosAstNode *third;
    asdl_seq *items;
    asdl_seq *extra;
    uint32_t flags;
    uint32_t inferred_type;
} RibosAstNode;

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
    size_t arena_bytes;
    size_t transient_bytes;
    size_t peak_transient_bytes;
    size_t ast_node_count;
    RibosTrivia *trivia;
    size_t trivia_count;
    RibosAstNode *root;
    RibosParseSummary result;
    RibosParseStatus failure_status;
} Parser;

RibosAstNode *ribos_generated_parse(Parser *parser);
RibosAstNode *ribos_parser_finish(
    Parser *parser,
    const asdl_seq *declarations);
void ribos_parser_runtime_enter(Parser *parser);
void ribos_parser_runtime_leave(void);
void ribos_parser_set_syntax_diagnostic(
    const Parser *parser,
    RibosDiagnostic *diagnostic);
void ribos_parser_release(Parser *parser);

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
asdl_seq *_PyPegen_seq_insert_in_front(
    Parser *parser,
    void *element,
    asdl_seq *sequence);
void *PyMem_Malloc(size_t size);
void *PyMem_Realloc(void *allocation, size_t size);
void PyMem_Free(void *allocation);
void PyErr_NoMemory(void);
int PyErr_Occurred(void);
void *PyThreadState_Get(void);
int _Py_ReachedRecursionLimitWithMargin(void *thread_state, int margin);
void _Pypegen_stack_overflow(Parser *parser);
void *ribos_arena_allocate(Parser *parser, size_t size);

RibosAstNode *ribos_ast_make(
    Parser *parser,
    RibosAstKind kind,
    Token *token,
    RibosAstNode *first,
    RibosAstNode *second,
    RibosAstNode *third,
    asdl_seq *items,
    asdl_seq *extra,
    uint32_t flags);
RibosAstNode *ribos_ast_program(Parser *parser, asdl_seq *declarations);
RibosAstNode *ribos_ast_decorated(
    Parser *parser,
    RibosAstNode *declaration,
    asdl_seq *decorators);
RibosAstNode *ribos_ast_path(
    Parser *parser,
    Token *first,
    asdl_seq *rest);
RibosAstNode *ribos_ast_literal(
    Parser *parser,
    RibosAstKind kind,
    Token *token);
RibosAstNode *ribos_ast_binary_tail(
    Parser *parser,
    RibosOperator operation,
    Token *token,
    RibosAstNode *right);
asdl_seq *ribos_ast_singleton_sequence(
    Parser *parser,
    RibosAstNode *node);
RibosAstNode *ribos_ast_fold_binary(
    Parser *parser,
    RibosAstNode *left,
    asdl_seq *tails);
RibosAstNode *ribos_ast_apply_trailers(
    Parser *parser,
    RibosAstNode *base,
    asdl_seq *trailers);

RibosCompileStatus ribos_compile_parser_tree(
    Parser *parser,
    RibosCompileSummary *summary,
    RibosCompileDiagnostic *diagnostic,
    FILE *dump,
    unsigned dump_flags);
RibosCompileStatus ribos_compile_source_with_dump(
    const char *source,
    size_t source_length,
    RibosCompileSummary *summary,
    RibosCompileDiagnostic *diagnostic,
    FILE *dump,
    unsigned dump_flags);
void ribos_dump_parser_model(
    const Parser *parser,
    FILE *output,
    unsigned dump_flags);

RibosParseStatus ribos_lex_source(
    const char *source,
    size_t source_length,
    Token **tokens,
    size_t *token_count,
    RibosTrivia **trivia,
    size_t *trivia_count,
    RibosDiagnostic *diagnostic);
void ribos_free_token_stream(Token *tokens, RibosTrivia *trivia);
const char *ribos_token_name(int token_type);
int ribos_token_is_reserved(const Token *token);

#endif
