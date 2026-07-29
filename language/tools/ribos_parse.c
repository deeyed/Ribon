#include "ribos/parser.h"

#include <stdio.h>
#include <stdlib.h>

static char *
ribos_read_file(const char *path, size_t *source_length)
{
    FILE *input;
    long measured_length;
    char *source;
    size_t bytes_read;

    input = fopen(path, "rb");
    if (input == NULL) {
        return NULL;
    }
    if (fseek(input, 0, SEEK_END) != 0) {
        (void)fclose(input);
        return NULL;
    }
    measured_length = ftell(input);
    if (measured_length < 0 || fseek(input, 0, SEEK_SET) != 0) {
        (void)fclose(input);
        return NULL;
    }
    source = malloc((size_t)measured_length + 1);
    if (source == NULL) {
        (void)fclose(input);
        return NULL;
    }
    bytes_read = fread(source, 1, (size_t)measured_length, input);
    if (bytes_read != (size_t)measured_length || fclose(input) != 0) {
        free(source);
        return NULL;
    }
    source[bytes_read] = '\0';
    *source_length = bytes_read;
    return source;
}

int
main(int argc, char **argv)
{
    RibosParseSummary summary;
    RibosDiagnostic diagnostic;
    RibosParseStatus status;
    char *source;
    size_t source_length;

    if (argc != 2) {
        (void)fprintf(stderr, "usage: %s SOURCE.ribos\n", argv[0]);
        return 64;
    }
    source = ribos_read_file(argv[1], &source_length);
    if (source == NULL) {
        (void)fprintf(
            stderr,
            "RIBOS-PARSER-PILOT-FAIL status=io-error file=%s\n",
            argv[1]);
        return 66;
    }

    status = ribos_parse_source(
        source,
        source_length,
        &summary,
        &diagnostic);
    free(source);
    if (status != RIBOS_PARSE_OK) {
        (void)fprintf(
            stderr,
            "RIBOS-PARSER-PILOT-FAIL status=%s kind=%s "
            "line=%u column=%u offset=%zu token=%s message=%s file=%s\n",
            ribos_parse_status_name(status),
            ribos_diagnostic_kind_name(diagnostic.kind),
            diagnostic.location.line,
            diagnostic.location.column,
            diagnostic.location.byte_offset,
            diagnostic.token,
            diagnostic.message,
            argv[1]);
        return 2;
    }

    (void)printf(
        "RIBOS-PARSER-PILOT-OK file=%s bytes=%zu tokens=%zu "
        "declarations=%zu depth=%u\n",
        argv[1],
        summary.source_bytes,
        summary.token_count,
        summary.declaration_count,
        summary.max_parser_depth);
    return 0;
}
