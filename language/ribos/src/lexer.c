#include "parser_internal.h"

#include <ctype.h>

#define RIBOS_MAX_SOURCE_BYTES (1024u * 1024u)
#define RIBOS_MAX_TOKENS 65536u
#define RIBOS_MAX_TRIVIA 131072u

typedef struct RibosTokenBuffer {
    Token *tokens;
    size_t count;
    size_t capacity;
    RibosTrivia *trivia;
    size_t trivia_count;
    size_t trivia_capacity;
    size_t next_token_trivia_index;
} RibosTokenBuffer;

typedef struct RibosLexer {
    const char *source;
    size_t length;
    size_t offset;
    uint32_t line;
    uint32_t column;
    unsigned paren_depth;
    unsigned bracket_depth;
    int line_has_token;
    RibosTokenBuffer output;
    RibosDiagnostic *diagnostic;
} RibosLexer;

typedef struct RibosPunctuator {
    const char *spelling;
    size_t length;
    int type;
} RibosPunctuator;

static const RibosPunctuator ribos_punctuators[] = {
    {"==", 2, EQEQUAL},
    {"!=", 2, NOTEQUAL},
    {"<=", 2, LESSEQUAL},
    {">=", 2, GREATEREQUAL},
    {"<<", 2, LEFTSHIFT},
    {">>", 2, RIGHTSHIFT},
    {"->", 2, RARROW},
    {"=>", 2, FATARROW},
    {"(", 1, LPAR},
    {")", 1, RPAR},
    {"[", 1, LSQB},
    {"]", 1, RSQB},
    {":", 1, COLON},
    {",", 1, COMMA},
    {"+", 1, PLUS},
    {"-", 1, MINUS},
    {"*", 1, STAR},
    {"/", 1, SLASH},
    {"|", 1, VBAR},
    {"&", 1, AMPER},
    {"<", 1, LESS},
    {">", 1, GREATER},
    {"=", 1, EQUAL},
    {".", 1, DOT},
    {"%", 1, PERCENT},
    {"{", 1, LBRACE},
    {"}", 1, RBRACE},
    {"~", 1, TILDE},
    {"^", 1, CIRCUMFLEX},
    {"@", 1, AT},
    {"?", 1, QUESTION},
};

static void
ribos_set_diagnostic(
    RibosLexer *lexer,
    RibosDiagnosticKind kind,
    const char *token,
    const char *message)
{
    size_t token_length;

    if (lexer->diagnostic == NULL) {
        return;
    }
    lexer->diagnostic->kind = kind;
    lexer->diagnostic->location.byte_offset = lexer->offset;
    lexer->diagnostic->location.line = lexer->line;
    lexer->diagnostic->location.column = lexer->column;
    token_length = token == NULL ? 0 : strlen(token);
    if (token_length >= sizeof(lexer->diagnostic->token)) {
        token_length = sizeof(lexer->diagnostic->token) - 1;
    }
    if (token_length != 0) {
        memcpy(lexer->diagnostic->token, token, token_length);
    }
    lexer->diagnostic->token[token_length] = '\0';
    (void)snprintf(
        lexer->diagnostic->message,
        sizeof(lexer->diagnostic->message),
        "%s",
        message);
}

static int
ribos_is_ascii_letter(unsigned char byte)
{
    return (byte >= (unsigned char)'a' && byte <= (unsigned char)'z') ||
        (byte >= (unsigned char)'A' && byte <= (unsigned char)'Z');
}

static int
ribos_is_identifier_continue(unsigned char byte)
{
    return ribos_is_ascii_letter(byte) || isdigit(byte) || byte == '_';
}

static int
ribos_utf8_sequence_length(
    const unsigned char *bytes,
    size_t remaining,
    size_t *sequence_length)
{
    unsigned char first;
    uint32_t codepoint;
    size_t length;
    size_t index;

    if (remaining == 0) {
        return 0;
    }
    first = bytes[0];
    if (first < 0x80u) {
        *sequence_length = 1;
        return 1;
    }
    if (first >= 0xc2u && first <= 0xdfu) {
        length = 2;
        codepoint = first & 0x1fu;
    } else if (first >= 0xe0u && first <= 0xefu) {
        length = 3;
        codepoint = first & 0x0fu;
    } else if (first >= 0xf0u && first <= 0xf4u) {
        length = 4;
        codepoint = first & 0x07u;
    } else {
        return 0;
    }
    if (remaining < length) {
        return 0;
    }
    for (index = 1; index < length; ++index) {
        if ((bytes[index] & 0xc0u) != 0x80u) {
            return 0;
        }
        codepoint = (codepoint << 6) | (bytes[index] & 0x3fu);
    }
    if ((length == 3 && codepoint < 0x800u) ||
        (length == 4 && codepoint < 0x10000u) ||
        codepoint > 0x10ffffu ||
        (codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
        return 0;
    }
    *sequence_length = length;
    return 1;
}

static RibosParseStatus
ribos_validate_utf8(RibosLexer *lexer)
{
    size_t offset = 0;
    size_t sequence_length;
    uint32_t line = 1;
    uint32_t column = 1;

    if (lexer->length >= 3 &&
        (unsigned char)lexer->source[0] == 0xefu &&
        (unsigned char)lexer->source[1] == 0xbbu &&
        (unsigned char)lexer->source[2] == 0xbfu) {
        ribos_set_diagnostic(
            lexer,
            RIBOS_DIAGNOSTIC_INVALID_UTF8,
            "UTF-8 BOM",
            "UTF-8 BOM is not permitted");
        return RIBOS_PARSE_INVALID_UTF8;
    }

    while (offset < lexer->length) {
        if ((unsigned char)lexer->source[offset] == 0) {
            lexer->offset = offset;
            lexer->line = line;
            lexer->column = column;
            ribos_set_diagnostic(
                lexer,
                RIBOS_DIAGNOSTIC_INVALID_CHARACTER,
                "NUL",
                "NUL byte is not permitted");
            return RIBOS_PARSE_LEXICAL_ERROR;
        }
        if (!ribos_utf8_sequence_length(
                (const unsigned char *)&lexer->source[offset],
                lexer->length - offset,
                &sequence_length)) {
            lexer->offset = offset;
            lexer->line = line;
            lexer->column = column;
            ribos_set_diagnostic(
                lexer,
                RIBOS_DIAGNOSTIC_INVALID_UTF8,
                "invalid UTF-8",
                "source is not well-formed UTF-8");
            return RIBOS_PARSE_INVALID_UTF8;
        }
        if (lexer->source[offset] == '\n') {
            ++line;
            column = 1;
        } else {
            column += (uint32_t)sequence_length;
        }
        offset += sequence_length;
    }
    return RIBOS_PARSE_OK;
}

static RibosParseStatus
ribos_append_trivia(
    RibosLexer *lexer,
    RibosTriviaKind kind,
    size_t start,
    size_t length,
    uint32_t line,
    uint32_t column,
    uint32_t end_line,
    uint32_t end_column)
{
    RibosTrivia *replacement;
    size_t new_capacity;

    if (lexer->output.trivia_count == RIBOS_MAX_TRIVIA) {
        ribos_set_diagnostic(
            lexer,
            RIBOS_DIAGNOSTIC_RESOURCE_LIMIT,
            "",
            "trivia limit exceeded");
        return RIBOS_PARSE_LIMIT_EXCEEDED;
    }
    if (lexer->output.trivia_count == lexer->output.trivia_capacity) {
        new_capacity = lexer->output.trivia_capacity == 0 ?
            128 : lexer->output.trivia_capacity * 2;
        if (new_capacity > RIBOS_MAX_TRIVIA) {
            new_capacity = RIBOS_MAX_TRIVIA;
        }
        replacement = realloc(
            lexer->output.trivia,
            new_capacity * sizeof(*replacement));
        if (replacement == NULL) {
            return RIBOS_PARSE_NO_MEMORY;
        }
        lexer->output.trivia = replacement;
        lexer->output.trivia_capacity = new_capacity;
    }
    lexer->output.trivia[lexer->output.trivia_count++] = (RibosTrivia){
        .kind = kind,
        .start = lexer->source + start,
        .length = length,
        .span = {
            .start = {
                .byte_offset = start,
                .line = line,
                .column = column,
            },
            .end = {
                .byte_offset = start + length,
                .line = end_line,
                .column = end_column,
            },
        },
    };
    return RIBOS_PARSE_OK;
}

static RibosParseStatus
ribos_append_token(
    RibosLexer *lexer,
    int type,
    size_t start,
    size_t length,
    uint32_t line,
    uint32_t column)
{
    Token *replacement;
    size_t new_capacity;

    if (lexer->output.count == RIBOS_MAX_TOKENS) {
        ribos_set_diagnostic(
            lexer,
            RIBOS_DIAGNOSTIC_RESOURCE_LIMIT,
            "",
            "token limit exceeded");
        return RIBOS_PARSE_LIMIT_EXCEEDED;
    }
    if (lexer->output.count == lexer->output.capacity) {
        new_capacity = lexer->output.capacity == 0 ?
            128 : lexer->output.capacity * 2;
        if (new_capacity > RIBOS_MAX_TOKENS) {
            new_capacity = RIBOS_MAX_TOKENS;
        }
        replacement = realloc(
            lexer->output.tokens,
            new_capacity * sizeof(*replacement));
        if (replacement == NULL) {
            return RIBOS_PARSE_NO_MEMORY;
        }
        lexer->output.tokens = replacement;
        lexer->output.capacity = new_capacity;
    }

    lexer->output.tokens[lexer->output.count++] = (Token){
        .type = type,
        .start = lexer->source + start,
        .length = length,
        .byte_offset = start,
        .end_byte_offset = start + length,
        .line = line,
        .column = column,
        .end_line = line,
        .end_column = column + (uint32_t)length,
        .leading_trivia_index = lexer->output.next_token_trivia_index,
        .leading_trivia_count = lexer->output.trivia_count -
            lexer->output.next_token_trivia_index,
    };
    lexer->output.next_token_trivia_index = lexer->output.trivia_count;
    return RIBOS_PARSE_OK;
}

static int
ribos_token_text_is(const Token *token, const char *text)
{
    size_t length = strlen(text);

    return token->length == length &&
        memcmp(token->start, text, length) == 0;
}

static int
ribos_newline_is_suppressed(const RibosLexer *lexer)
{
    const Token *previous;

    if (lexer->paren_depth != 0 || lexer->bracket_depth != 0 ||
        lexer->output.count == 0) {
        return 1;
    }
    previous = &lexer->output.tokens[lexer->output.count - 1];
    switch (previous->type) {
    case EQUAL:
    case COMMA:
    case DOT:
    case RARROW:
    case FATARROW:
    case COLON:
    case PLUS:
    case MINUS:
    case STAR:
    case SLASH:
    case VBAR:
    case AMPER:
    case LESS:
    case GREATER:
    case EQEQUAL:
    case NOTEQUAL:
    case LESSEQUAL:
    case GREATEREQUAL:
    case PERCENT:
    case TILDE:
    case CIRCUMFLEX:
    case LEFTSHIFT:
    case RIGHTSHIFT:
        return 1;
    default:
        break;
    }
    return previous->type == NAME &&
        (ribos_token_text_is(previous, "and") ||
         ribos_token_text_is(previous, "or") ||
         ribos_token_text_is(previous, "not") ||
         ribos_token_text_is(previous, "in"));
}

static int
ribos_next_line_starts_logical_operator(const RibosLexer *lexer)
{
    size_t offset = lexer->offset;
    size_t word_start;
    size_t word_length;

    if (offset < lexer->length && lexer->source[offset] == '\r') {
        ++offset;
    }
    if (offset < lexer->length && lexer->source[offset] == '\n') {
        ++offset;
    }
    while (offset < lexer->length && lexer->source[offset] == ' ') {
        ++offset;
    }
    word_start = offset;
    while (offset < lexer->length &&
        ribos_is_identifier_continue(
            (unsigned char)lexer->source[offset])) {
        ++offset;
    }
    word_length = offset - word_start;
    return (word_length == 3 &&
            memcmp(lexer->source + word_start, "and", 3) == 0) ||
        (word_length == 2 &&
         memcmp(lexer->source + word_start, "or", 2) == 0);
}

static RibosParseStatus
ribos_emit_newline(RibosLexer *lexer)
{
    RibosParseStatus status = RIBOS_PARSE_OK;

    if (lexer->line_has_token &&
        !ribos_newline_is_suppressed(lexer) &&
        !ribos_next_line_starts_logical_operator(lexer)) {
        status = ribos_append_token(
            lexer,
            NEWLINE,
            lexer->offset,
            0,
            lexer->line,
            lexer->column);
    }
    lexer->line_has_token = 0;
    return status;
}

static int
ribos_digit_for_base(unsigned char byte, unsigned base)
{
    if (byte >= '0' && byte <= '9') {
        return (unsigned)(byte - '0') < base;
    }
    if (base == 16 &&
        ((byte >= 'a' && byte <= 'f') ||
         (byte >= 'A' && byte <= 'F'))) {
        return 1;
    }
    return 0;
}

static int
ribos_validate_digits(
    const char *digits,
    size_t length,
    unsigned base)
{
    size_t index;

    if (length == 0 || !ribos_digit_for_base((unsigned char)digits[0], base) ||
        !ribos_digit_for_base((unsigned char)digits[length - 1], base)) {
        return 0;
    }
    for (index = 0; index < length; ++index) {
        if (digits[index] == '_') {
            if (index == 0 || index + 1 == length ||
                !ribos_digit_for_base((unsigned char)digits[index - 1], base) ||
                !ribos_digit_for_base((unsigned char)digits[index + 1], base)) {
                return 0;
            }
        } else if (!ribos_digit_for_base((unsigned char)digits[index], base)) {
            return 0;
        }
    }
    return 1;
}

static RibosParseStatus
ribos_lex_number(RibosLexer *lexer)
{
    size_t start = lexer->offset;
    uint32_t line = lexer->line;
    uint32_t column = lexer->column;
    size_t digits_start;
    unsigned base = 10;
    size_t length;

    while (lexer->offset < lexer->length &&
        (ribos_is_identifier_continue(
             (unsigned char)lexer->source[lexer->offset]))) {
        ++lexer->offset;
        ++lexer->column;
    }
    length = lexer->offset - start;
    digits_start = start;
    if (length >= 2 && lexer->source[start] == '0' &&
        (lexer->source[start + 1] == 'x' ||
         lexer->source[start + 1] == 'X')) {
        base = 16;
        digits_start += 2;
    } else if (length >= 2 && lexer->source[start] == '0' &&
        (lexer->source[start + 1] == 'b' ||
         lexer->source[start + 1] == 'B')) {
        base = 2;
        digits_start += 2;
    } else if (length > 1 && lexer->source[start] == '0') {
        lexer->offset = start;
        lexer->line = line;
        lexer->column = column;
        ribos_set_diagnostic(
            lexer,
            RIBOS_DIAGNOSTIC_INVALID_NUMBER,
            "integer",
            "decimal integer cannot contain a leading zero");
        return RIBOS_PARSE_LEXICAL_ERROR;
    }
    if (!ribos_validate_digits(
            lexer->source + digits_start,
            lexer->offset - digits_start,
            base)) {
        lexer->offset = start;
        lexer->line = line;
        lexer->column = column;
        ribos_set_diagnostic(
            lexer,
            RIBOS_DIAGNOSTIC_INVALID_NUMBER,
            "integer",
            "invalid integer literal");
        return RIBOS_PARSE_LEXICAL_ERROR;
    }
    lexer->line_has_token = 1;
    return ribos_append_token(
        lexer,
        NUMBER,
        start,
        length,
        line,
        column);
}

static int
ribos_is_hex_digit(unsigned char byte)
{
    return ribos_digit_for_base(byte, 16);
}

static RibosParseStatus
ribos_lex_string(RibosLexer *lexer)
{
    size_t start = lexer->offset;
    uint32_t line = lexer->line;
    uint32_t column = lexer->column;
    unsigned char byte;

    ++lexer->offset;
    ++lexer->column;
    while (lexer->offset < lexer->length) {
        byte = (unsigned char)lexer->source[lexer->offset];
        if (byte == '"') {
            ++lexer->offset;
            ++lexer->column;
            lexer->line_has_token = 1;
            return ribos_append_token(
                lexer,
                STRING,
                start,
                lexer->offset - start,
                line,
                column);
        }
        if (byte == '\n' || byte == '\r') {
            break;
        }
        if (byte == '\\') {
            if (lexer->offset + 1 >= lexer->length) {
                break;
            }
            byte = (unsigned char)lexer->source[lexer->offset + 1];
            if (byte == 'x') {
                if (lexer->offset + 3 >= lexer->length ||
                    !ribos_is_hex_digit(
                        (unsigned char)lexer->source[lexer->offset + 2]) ||
                    !ribos_is_hex_digit(
                        (unsigned char)lexer->source[lexer->offset + 3])) {
                    ribos_set_diagnostic(
                        lexer,
                        RIBOS_DIAGNOSTIC_INVALID_STRING,
                        "\\x",
                        "hex escape requires exactly two hex digits");
                    return RIBOS_PARSE_LEXICAL_ERROR;
                }
                lexer->offset += 4;
                lexer->column += 4;
                continue;
            }
            if (byte != '\\' && byte != '"' && byte != 'n' &&
                byte != 'r' && byte != 't' && byte != '0') {
                ribos_set_diagnostic(
                    lexer,
                    RIBOS_DIAGNOSTIC_INVALID_STRING,
                    "escape",
                    "invalid string escape");
                return RIBOS_PARSE_LEXICAL_ERROR;
            }
            lexer->offset += 2;
            lexer->column += 2;
            continue;
        }
        ++lexer->offset;
        ++lexer->column;
    }
    ribos_set_diagnostic(
        lexer,
        RIBOS_DIAGNOSTIC_INVALID_STRING,
        "string",
        "unterminated string literal");
    return RIBOS_PARSE_LEXICAL_ERROR;
}

static RibosParseStatus
ribos_lex_identifier(RibosLexer *lexer)
{
    size_t start = lexer->offset;
    uint32_t line = lexer->line;
    uint32_t column = lexer->column;

    while (lexer->offset < lexer->length &&
        ribos_is_identifier_continue(
            (unsigned char)lexer->source[lexer->offset])) {
        ++lexer->offset;
        ++lexer->column;
    }
    if (lexer->source[start] == '_' && lexer->offset - start > 1 &&
        isdigit((unsigned char)lexer->source[start + 1])) {
        ribos_set_diagnostic(
            lexer,
            RIBOS_DIAGNOSTIC_INVALID_NUMBER,
            "integer",
            "numeric separator cannot precede a number");
        return RIBOS_PARSE_LEXICAL_ERROR;
    }
    lexer->line_has_token = 1;
    return ribos_append_token(
        lexer,
        NAME,
        start,
        lexer->offset - start,
        line,
        column);
}

static RibosParseStatus
ribos_lex_punctuator(RibosLexer *lexer)
{
    size_t index;
    const RibosPunctuator *punctuator;
    RibosParseStatus status;

    for (index = 0;
         index < sizeof(ribos_punctuators) / sizeof(ribos_punctuators[0]);
         ++index) {
        punctuator = &ribos_punctuators[index];
        if (punctuator->length <= lexer->length - lexer->offset &&
            memcmp(
                lexer->source + lexer->offset,
                punctuator->spelling,
                punctuator->length) == 0) {
            status = ribos_append_token(
                lexer,
                punctuator->type,
                lexer->offset,
                punctuator->length,
                lexer->line,
                lexer->column);
            if (status != RIBOS_PARSE_OK) {
                return status;
            }
            if (punctuator->type == LPAR) {
                ++lexer->paren_depth;
            } else if (punctuator->type == RPAR &&
                lexer->paren_depth != 0) {
                --lexer->paren_depth;
            } else if (punctuator->type == LSQB) {
                ++lexer->bracket_depth;
            } else if (punctuator->type == RSQB &&
                lexer->bracket_depth != 0) {
                --lexer->bracket_depth;
            }
            lexer->offset += punctuator->length;
            lexer->column += (uint32_t)punctuator->length;
            lexer->line_has_token = 1;
            return RIBOS_PARSE_OK;
        }
    }
    return RIBOS_PARSE_SYNTAX_ERROR;
}

RibosParseStatus
ribos_lex_source(
    const char *source,
    size_t source_length,
    Token **tokens,
    size_t *token_count,
    RibosTrivia **trivia,
    size_t *trivia_count,
    RibosDiagnostic *diagnostic)
{
    RibosLexer lexer = {
        .source = source,
        .length = source_length,
        .line = 1,
        .column = 1,
        .diagnostic = diagnostic,
    };
    RibosParseStatus status;
    unsigned char byte;

    if (source_length > RIBOS_MAX_SOURCE_BYTES) {
        ribos_set_diagnostic(
            &lexer,
            RIBOS_DIAGNOSTIC_RESOURCE_LIMIT,
            "",
            "source byte limit exceeded");
        return RIBOS_PARSE_LIMIT_EXCEEDED;
    }
    status = ribos_validate_utf8(&lexer);
    lexer.offset = 0;
    lexer.line = 1;
    lexer.column = 1;
    if (status != RIBOS_PARSE_OK) {
        return status;
    }

    while (lexer.offset < lexer.length) {
        byte = (unsigned char)lexer.source[lexer.offset];
        if (byte == ' ') {
            size_t start = lexer.offset;
            uint32_t column = lexer.column;

            while (lexer.offset < lexer.length &&
                lexer.source[lexer.offset] == ' ') {
                ++lexer.offset;
                ++lexer.column;
            }
            status = ribos_append_trivia(
                &lexer,
                RIBOS_TRIVIA_SPACE,
                start,
                lexer.offset - start,
                lexer.line,
                column,
                lexer.line,
                lexer.column);
            if (status != RIBOS_PARSE_OK) {
                goto fail;
            }
            continue;
        }
        if (byte == '\t') {
            ribos_set_diagnostic(
                &lexer,
                RIBOS_DIAGNOSTIC_INVALID_CHARACTER,
                "TAB",
                "TAB is not permitted outside a string");
            status = RIBOS_PARSE_LEXICAL_ERROR;
            goto fail;
        }
        if (byte == '\r') {
            size_t start = lexer.offset;
            uint32_t line = lexer.line;
            uint32_t column = lexer.column;

            if (lexer.offset + 1 >= lexer.length ||
                lexer.source[lexer.offset + 1] != '\n') {
                ribos_set_diagnostic(
                    &lexer,
                    RIBOS_DIAGNOSTIC_INVALID_CHARACTER,
                    "CR",
                    "bare CR is not permitted");
                status = RIBOS_PARSE_LEXICAL_ERROR;
                goto fail;
            }
            status = ribos_emit_newline(&lexer);
            if (status != RIBOS_PARSE_OK) {
                goto fail;
            }
            lexer.offset += 2;
            ++lexer.line;
            lexer.column = 1;
            status = ribos_append_trivia(
                &lexer,
                RIBOS_TRIVIA_NEWLINE,
                start,
                2,
                line,
                column,
                lexer.line,
                lexer.column);
            if (status != RIBOS_PARSE_OK) {
                goto fail;
            }
            continue;
        }
        if (byte == '\n') {
            size_t start = lexer.offset;
            uint32_t line = lexer.line;
            uint32_t column = lexer.column;

            status = ribos_emit_newline(&lexer);
            if (status != RIBOS_PARSE_OK) {
                goto fail;
            }
            ++lexer.offset;
            ++lexer.line;
            lexer.column = 1;
            status = ribos_append_trivia(
                &lexer,
                RIBOS_TRIVIA_NEWLINE,
                start,
                1,
                line,
                column,
                lexer.line,
                lexer.column);
            if (status != RIBOS_PARSE_OK) {
                goto fail;
            }
            continue;
        }
        if (byte == '#') {
            size_t start = lexer.offset;
            uint32_t column = lexer.column;

            while (lexer.offset < lexer.length &&
                lexer.source[lexer.offset] != '\n' &&
                lexer.source[lexer.offset] != '\r') {
                ++lexer.offset;
                ++lexer.column;
            }
            status = ribos_append_trivia(
                &lexer,
                RIBOS_TRIVIA_COMMENT,
                start,
                lexer.offset - start,
                lexer.line,
                column,
                lexer.line,
                lexer.column);
            if (status != RIBOS_PARSE_OK) {
                goto fail;
            }
            continue;
        }
        if (byte == '"') {
            status = ribos_lex_string(&lexer);
        } else if (isdigit(byte)) {
            status = ribos_lex_number(&lexer);
        } else if (ribos_is_ascii_letter(byte) || byte == '_') {
            status = ribos_lex_identifier(&lexer);
        } else {
            status = ribos_lex_punctuator(&lexer);
            if (status == RIBOS_PARSE_SYNTAX_ERROR) {
                char token[2] = {(char)byte, '\0'};
                ribos_set_diagnostic(
                    &lexer,
                    RIBOS_DIAGNOSTIC_INVALID_CHARACTER,
                    token,
                    "character is not part of Ribos syntax");
                status = RIBOS_PARSE_LEXICAL_ERROR;
            }
        }
        if (status != RIBOS_PARSE_OK) {
            goto fail;
        }
    }

    status = ribos_append_token(
        &lexer,
        ENDMARKER,
        lexer.offset,
        0,
        lexer.line,
        lexer.column);
    if (status != RIBOS_PARSE_OK) {
        goto fail;
    }
    *tokens = lexer.output.tokens;
    *token_count = lexer.output.count;
    *trivia = lexer.output.trivia;
    *trivia_count = lexer.output.trivia_count;
    return RIBOS_PARSE_OK;

fail:
    free(lexer.output.tokens);
    free(lexer.output.trivia);
    return status;
}

void
ribos_free_token_stream(Token *tokens, RibosTrivia *trivia)
{
    free(tokens);
    free(trivia);
}

const char *
ribos_token_name(int token_type)
{
    static const char *const names[] = {
        "ENDMARKER", "NAME", "NUMBER", "STRING", "NEWLINE",
        "(", ")", "[", "]", ":", ",", "+", "-", "*", "/",
        "|", "&", "<", ">", "=", ".", "%", "{", "}", "==",
        "!=", "<=", ">=", "~", "^", "<<", ">>", "@", "->",
        "=>", "?",
    };

    if (token_type < 0 ||
        (size_t)token_type >= sizeof(names) / sizeof(names[0])) {
        return "keyword";
    }
    return names[token_type];
}

int
ribos_token_is_reserved(const Token *token)
{
    static const char *const reserved[] = {
        "as", "async", "await", "break", "catch", "class", "continue",
        "defer", "except", "finally", "from", "import", "lambda", "raise",
        "throw", "trait", "try", "while", "with", "yield",
    };
    size_t index;

    if (token == NULL || token->type != NAME) {
        return 0;
    }
    for (index = 0; index < sizeof(reserved) / sizeof(reserved[0]); ++index) {
        if (ribos_token_text_is(token, reserved[index])) {
            return 1;
        }
    }
    return 0;
}
