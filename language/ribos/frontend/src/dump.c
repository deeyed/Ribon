#include "parser_internal.h"

static const char *
ribos_trivia_kind_name(RibosTriviaKind kind)
{
    switch (kind) {
    case RIBOS_TRIVIA_SPACE:
        return "space";
    case RIBOS_TRIVIA_COMMENT:
        return "comment";
    case RIBOS_TRIVIA_NEWLINE:
        return "newline";
    default:
        return "unknown";
    }
}

static const char *
ribos_ast_kind_name(RibosAstKind kind)
{
    static const char *const names[] = {
        "program",
        "decorator",
        "attribute-argument",
        "function",
        "parameter",
        "struct",
        "field",
        "enum",
        "variant",
        "block",
        "let",
        "return",
        "assign",
        "if",
        "for",
        "match",
        "match-arm",
        "pattern",
        "if-expression",
        "unary",
        "binary",
        "call",
        "argument",
        "member",
        "index",
        "propagate",
        "path",
        "integer",
        "string",
        "boolean",
        "none",
        "list",
        "map",
        "map-entry",
        "type",
    };

    if ((size_t)kind >= sizeof(names) / sizeof(names[0])) {
        return "unknown";
    }
    return names[kind];
}

static void
ribos_dump_escaped(FILE *output, const char *text, size_t length)
{
    size_t index;

    (void)fputc('"', output);
    for (index = 0; index < length; ++index) {
        unsigned char byte = (unsigned char)text[index];

        switch (byte) {
        case '\\':
            (void)fputs("\\\\", output);
            break;
        case '"':
            (void)fputs("\\\"", output);
            break;
        case '\n':
            (void)fputs("\\n", output);
            break;
        case '\r':
            (void)fputs("\\r", output);
            break;
        case '\t':
            (void)fputs("\\t", output);
            break;
        default:
            if (byte < 0x20u || byte == 0x7fu) {
                (void)fprintf(output, "\\x%02x", byte);
            } else {
                (void)fputc(byte, output);
            }
            break;
        }
    }
    (void)fputc('"', output);
}

static void
ribos_dump_tokens(const Parser *parser, FILE *output)
{
    size_t index;

    for (index = 0; index < parser->trivia_count; ++index) {
        const RibosTrivia *trivia = &parser->trivia[index];

        (void)fprintf(
            output,
            "TRIVIA id=%zu kind=%s span=%zu:%u:%u-%zu:%u:%u text=",
            index,
            ribos_trivia_kind_name(trivia->kind),
            trivia->span.start.byte_offset,
            trivia->span.start.line,
            trivia->span.start.column,
            trivia->span.end.byte_offset,
            trivia->span.end.line,
            trivia->span.end.column);
        ribos_dump_escaped(output, trivia->start, trivia->length);
        (void)fputc('\n', output);
    }
    for (index = 0; index < parser->token_count; ++index) {
        const Token *token = &parser->tokens[index];

        (void)fprintf(
            output,
            "TOKEN id=%zu kind=%s span=%zu:%u:%u-%zu:%u:%u "
            "leading=%zu+%zu text=",
            index,
            ribos_token_name(token->type),
            token->byte_offset,
            token->line,
            token->column,
            token->end_byte_offset,
            token->end_line,
            token->end_column,
            token->leading_trivia_index,
            token->leading_trivia_count);
        ribos_dump_escaped(output, token->start, token->length);
        (void)fputc('\n', output);
    }
}

static void
ribos_dump_node_reference(FILE *output, const RibosAstNode *node)
{
    if (node == NULL) {
        (void)fputc('-', output);
    } else {
        (void)fprintf(output, "%u", node->id);
    }
}

static void
ribos_dump_node_sequence(
    FILE *output,
    const asdl_seq *sequence,
    int token_sequence)
{
    Py_ssize_t index;

    (void)fputc('[', output);
    for (index = 0; sequence != NULL && index < sequence->size; ++index) {
        if (index != 0) {
            (void)fputc(',', output);
        }
        if (token_sequence) {
            const Token *token = sequence->elements[index];

            ribos_dump_escaped(output, token->start, token->length);
        } else {
            ribos_dump_node_reference(output, sequence->elements[index]);
        }
    }
    (void)fputc(']', output);
}

static void
ribos_dump_ast_node(
    const RibosAstNode *node,
    FILE *output,
    unsigned char *visited)
{
    Py_ssize_t index;

    if (node == NULL || node->id >= RIBOS_MAX_AST_NODES ||
        visited[node->id] != 0) {
        return;
    }
    visited[node->id] = 1;
    (void)fprintf(
        output,
        "AST id=%u kind=%s span=%zu:%u:%u-%zu:%u:%u type=%u flags=%u "
        "token=",
        node->id,
        ribos_ast_kind_name(node->kind),
        node->span.start.byte_offset,
        node->span.start.line,
        node->span.start.column,
        node->span.end.byte_offset,
        node->span.end.line,
        node->span.end.column,
        node->inferred_type,
        node->flags);
    if (node->token == NULL) {
        (void)fputc('-', output);
    } else {
        ribos_dump_escaped(output, node->token->start, node->token->length);
    }
    (void)fputs(" first=", output);
    ribos_dump_node_reference(output, node->first);
    (void)fputs(" second=", output);
    ribos_dump_node_reference(output, node->second);
    (void)fputs(" third=", output);
    ribos_dump_node_reference(output, node->third);
    (void)fputs(" items=", output);
    ribos_dump_node_sequence(
        output,
        node->items,
        node->kind == RIBOS_AST_PATH);
    (void)fputs(" extra=", output);
    ribos_dump_node_sequence(output, node->extra, 0);
    (void)fputc('\n', output);

    ribos_dump_ast_node(node->first, output, visited);
    ribos_dump_ast_node(node->second, output, visited);
    ribos_dump_ast_node(node->third, output, visited);
    if (node->kind != RIBOS_AST_PATH) {
        for (index = 0; node->items != NULL &&
             index < node->items->size; ++index) {
            ribos_dump_ast_node(
                node->items->elements[index],
                output,
                visited);
        }
    }
    for (index = 0; node->extra != NULL &&
         index < node->extra->size; ++index) {
        ribos_dump_ast_node(
            node->extra->elements[index],
            output,
            visited);
    }
}

void
ribos_dump_parser_model(
    const Parser *parser,
    FILE *output,
    unsigned dump_flags)
{
    unsigned char *visited;

    if ((dump_flags & RIBOS_DUMP_TOKENS) != 0) {
        ribos_dump_tokens(parser, output);
    }
    if ((dump_flags & (RIBOS_DUMP_AST | RIBOS_DUMP_SEMANTICS)) == 0) {
        return;
    }
    visited = calloc(RIBOS_MAX_AST_NODES, sizeof(*visited));
    if (visited == NULL) {
        (void)fputs("RIBOS-DUMP-FAIL reason=no-memory\n", output);
        return;
    }
    ribos_dump_ast_node(parser->root, output, visited);
    free(visited);
}
