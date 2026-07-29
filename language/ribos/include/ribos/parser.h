#ifndef RIBOS_PARSER_H
#define RIBOS_PARSER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file parser.h
 * @brief Host-side syntax parser interface for Ribos source.
 *
 * This interface validates lexical structure and the accepted Ribos grammar.
 * Static semantics are exposed separately through <ribos/compiler.h>.
 */

/** Stable result category returned by the parser pilot. */
typedef enum RibosParseStatus {
    RIBOS_PARSE_OK = 0,
    RIBOS_PARSE_INVALID_ARGUMENT,
    RIBOS_PARSE_INVALID_UTF8,
    RIBOS_PARSE_LEXICAL_ERROR,
    RIBOS_PARSE_SYNTAX_ERROR,
    RIBOS_PARSE_NO_MEMORY,
    RIBOS_PARSE_LIMIT_EXCEEDED
} RibosParseStatus;

/** Stable primary diagnostic category. */
typedef enum RibosDiagnosticKind {
    RIBOS_DIAGNOSTIC_NONE = 0,
    RIBOS_DIAGNOSTIC_INVALID_UTF8,
    RIBOS_DIAGNOSTIC_INVALID_CHARACTER,
    RIBOS_DIAGNOSTIC_INVALID_NUMBER,
    RIBOS_DIAGNOSTIC_INVALID_STRING,
    RIBOS_DIAGNOSTIC_RESERVED_FEATURE,
    RIBOS_DIAGNOSTIC_SYNTAX,
    RIBOS_DIAGNOSTIC_RESOURCE_LIMIT
} RibosDiagnosticKind;

/** Byte-oriented source location with one-based line and column values. */
typedef struct RibosSourceLocation {
    size_t byte_offset;
    uint32_t line;
    uint32_t column;
} RibosSourceLocation;

/** Half-open source byte range with stable start and end locations. */
typedef struct RibosSourceSpan {
    RibosSourceLocation start;
    RibosSourceLocation end;
} RibosSourceSpan;

/** Bounded diagnostic emitted by lexical or syntax validation. */
typedef struct RibosDiagnostic {
    RibosDiagnosticKind kind;
    RibosSourceLocation location;
    char token[48];
    char message[160];
} RibosDiagnostic;

/** Successful parser-pilot measurements. */
typedef struct RibosParseSummary {
    size_t source_bytes;
    size_t token_count;
    size_t declaration_count;
    uint32_t max_parser_depth;
} RibosParseSummary;

/**
 * Parse one immutable Ribos source span.
 *
 * @param source UTF-8 source bytes. The span need not be NUL terminated.
 * @param source_length Number of source bytes.
 * @param summary Receives syntax-only measurements on success.
 * @param diagnostic Receives a bounded primary diagnostic on failure.
 * @return A stable parser status.
 */
RibosParseStatus ribos_parse_source(
    const char *source,
    size_t source_length,
    RibosParseSummary *summary,
    RibosDiagnostic *diagnostic);

/** Return a stable ASCII spelling for one parser status. */
const char *ribos_parse_status_name(RibosParseStatus status);

/** Return a stable ASCII spelling for one diagnostic category. */
const char *ribos_diagnostic_kind_name(RibosDiagnosticKind kind);

#ifdef __cplusplus
}
#endif

#endif
