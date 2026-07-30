#include "parser_internal.h"

#include "ribos/host/allocator.h"
#include "ribos/host/artifact_emitter.h"
#include "ribos/host/writer.h"
#include "ribos/ir/analysis.h"

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

static int
ribos_write_file(
    const char *path,
    const uint8_t *bytes,
    size_t byte_count)
{
    FILE *output = fopen(path, "wb");
    size_t written;
    int close_status;

    if (output == NULL) {
        return 0;
    }
    written = fwrite(bytes, 1, byte_count, output);
    close_status = fclose(output);
    return written == byte_count && close_status == 0;
}

static void
ribos_print_digest(
    const uint8_t digest[RIBOS_SCHEMA_DIGEST_BYTES])
{
    size_t index;

    for (index = 0; index < RIBOS_SCHEMA_DIGEST_BYTES; ++index) {
        (void)printf("%02x", digest[index]);
    }
}

int
main(int argc, char **argv)
{
    RibosParseSummary summary;
    RibosDiagnostic diagnostic;
    RibosParseStatus status;
    RibosCompileSummary compile_summary;
    RibosCompileDiagnostic compile_diagnostic;
    RibosCompileStatus compile_status;
    char *source;
    size_t source_length;
    const char *path;
    unsigned dump_flags = 0;
    int compile_mode = 0;
    int ir_mode = 0;
    int resource_mode = 0;
    int artifact_mode = 0;
    const char *artifact_path = NULL;
    const RibosAllocator *allocator = ribos_host_allocator();
    RibosWriter stdout_writer = ribos_host_file_writer(stdout);

    if (argc == 2) {
        path = argv[1];
    } else if (argc == 3 &&
        (strcmp(argv[1], "--check") == 0 ||
         strcmp(argv[1], "--dump-tokens") == 0 ||
         strcmp(argv[1], "--dump-ast") == 0 ||
         strcmp(argv[1], "--dump-semantics") == 0 ||
         strcmp(argv[1], "--dump-ir") == 0 ||
         strcmp(argv[1], "--dump-resources") == 0)) {
        compile_mode = 1;
        path = argv[2];
        if (strcmp(argv[1], "--dump-tokens") == 0) {
            dump_flags = RIBOS_DUMP_TOKENS;
        } else if (strcmp(argv[1], "--dump-ast") == 0) {
            dump_flags = RIBOS_DUMP_AST;
        } else if (strcmp(argv[1], "--dump-semantics") == 0) {
            dump_flags = RIBOS_DUMP_TOKENS |
                RIBOS_DUMP_AST |
                RIBOS_DUMP_SEMANTICS;
        } else if (strcmp(argv[1], "--dump-ir") == 0) {
            ir_mode = 1;
        } else if (strcmp(argv[1], "--dump-resources") == 0) {
            ir_mode = 1;
            resource_mode = 1;
        }
    } else if (argc == 4 &&
        strcmp(argv[1], "--emit-artifact") == 0) {
        compile_mode = 1;
        ir_mode = 1;
        artifact_mode = 1;
        artifact_path = argv[2];
        path = argv[3];
    } else {
        (void)fprintf(
            stderr,
            "usage: %s [--check|--dump-tokens|--dump-ast|"
            "--dump-semantics|--dump-ir|--dump-resources] SOURCE.rbs\n"
            "       %s --emit-artifact OUTPUT.rba SOURCE.rbs\n",
            argv[0],
            argv[0]);
        return 64;
    }
    source = ribos_read_file(path, &source_length);
    if (source == NULL) {
        (void)fprintf(
            stderr,
            "RIBOS-PARSER-PILOT-FAIL status=io-error file=%s\n",
            path);
        return 66;
    }

    if (compile_mode) {
        RibosIrModule *ir_module =
            ir_mode ? ribos_ir_module_create(allocator) : NULL;

        if (ir_mode && ir_module == NULL) {
            free(source);
            (void)fprintf(
                stderr,
                "RIBOS-COMPILER-FAIL status=no-memory "
                "code=E_RESOURCE_LIMIT file=%s\n",
                path);
            return 70;
        }
        if (ir_mode) {
            compile_status = ribos_compile_source_to_ir(
                allocator,
                source,
                source_length,
                ribos_schema_reference_v1(),
                ir_module,
                &compile_summary,
                &compile_diagnostic);
        } else {
            compile_status = ribos_compile_source_with_dump(
                allocator,
                source,
                source_length,
                ribos_schema_reference_v1(),
                NULL,
                &compile_summary,
                &compile_diagnostic,
                &stdout_writer,
                dump_flags);
        }
        free(source);
        if (compile_status != RIBOS_COMPILE_OK) {
            ribos_ir_module_destroy(ir_module);
            if (compile_status == RIBOS_COMPILE_PARSE_ERROR) {
                (void)fprintf(
                    stderr,
                    "RIBOS-COMPILER-FAIL status=%s code=E_PARSE_%s "
                    "line=%u column=%u offset=%zu token=%s message=%s "
                    "file=%s\n",
                    ribos_compile_status_name(compile_status),
                    ribos_diagnostic_kind_name(
                        compile_diagnostic.parse.kind),
                    compile_diagnostic.parse.location.line,
                    compile_diagnostic.parse.location.column,
                    compile_diagnostic.parse.location.byte_offset,
                    compile_diagnostic.parse.token,
                    compile_diagnostic.parse.message,
                    path);
            } else {
                (void)fprintf(
                    stderr,
                    "RIBOS-COMPILER-FAIL status=%s code=%s "
                    "line=%u column=%u start=%zu end=%zu symbol=%s "
                    "expected=%s actual=%s message=%s file=%s\n",
                    ribos_compile_status_name(compile_status),
                    ribos_compile_diagnostic_code_name(
                        compile_diagnostic.code),
                    compile_diagnostic.span.start.line,
                    compile_diagnostic.span.start.column,
                    compile_diagnostic.span.start.byte_offset,
                    compile_diagnostic.span.end.byte_offset,
                    compile_diagnostic.symbol,
                    compile_diagnostic.expected,
                    compile_diagnostic.actual,
                    compile_diagnostic.message,
                    path);
            }
            return 2;
        }
        if (ir_mode && !resource_mode && !artifact_mode &&
            (ribos_ir_validate_v1(ir_module) != RIBOS_IR_OK ||
             ribos_ir_dump_v1(
                 ir_module,
                 &stdout_writer) != RIBOS_IR_OK)) {
            ribos_ir_module_destroy(ir_module);
            (void)fprintf(
                stderr,
                "RIBOS-COMPILER-FAIL status=ir-error "
                "code=E_IR_LOWERING file=%s\n",
                path);
            return 2;
        }
        if (resource_mode) {
            RibosIrResourceClosure *resources =
                ribos_ir_resource_closure_create(allocator);
            RibosIrStatus resource_status =
                resources == NULL ?
                    RIBOS_IR_RESOURCE_EXCEEDED :
                    ribos_ir_analyze_resources_v1(
                        ir_module,
                        resources);

            if (resource_status == RIBOS_IR_OK) {
                resource_status = ribos_ir_dump_resources_v1(
                    resources,
                    &stdout_writer);
            }
            ribos_ir_resource_closure_destroy(resources);
            if (resource_status != RIBOS_IR_OK) {
                ribos_ir_module_destroy(ir_module);
                (void)fprintf(
                    stderr,
                    "RIBOS-COMPILER-FAIL status=ir-error "
                    "code=E_IR_LOWERING file=%s\n",
                    path);
                return 2;
            }
        }
        if (artifact_mode) {
            RibosArtifactEmitOptions artifact_options = {
                .include_source_map = 1,
            };
            RibosArtifactView artifact_view;
            RibosArtifactStatus artifact_status;
            uint8_t *artifact = NULL;
            size_t artifact_size = 0;
            size_t encoded_size = 0;

            artifact_status = ribos_artifact_emit_v1(
                ir_module,
                &artifact_options,
                NULL,
                0,
                &artifact_size);
            if (artifact_status == RIBOS_ARTIFACT_OK) {
                artifact = malloc(artifact_size);
                if (artifact == NULL) {
                    artifact_status =
                        RIBOS_ARTIFACT_CAPACITY_EXCEEDED;
                }
            }
            if (artifact_status == RIBOS_ARTIFACT_OK) {
                artifact_status = ribos_artifact_emit_v1(
                    ir_module,
                    &artifact_options,
                    artifact,
                    artifact_size,
                    &encoded_size);
            }
            if (artifact_status == RIBOS_ARTIFACT_OK &&
                encoded_size != artifact_size) {
                artifact_status = RIBOS_ARTIFACT_INVALID_FORMAT;
            }
            if (artifact_status == RIBOS_ARTIFACT_OK &&
                encoded_size == artifact_size) {
                artifact_status = ribos_artifact_open_v1(
                    artifact,
                    artifact_size,
                    &artifact_view);
            }
            if (artifact_status == RIBOS_ARTIFACT_OK &&
                !ribos_write_file(
                    artifact_path,
                    artifact,
                    artifact_size)) {
                artifact_status = RIBOS_ARTIFACT_INVALID_ARGUMENT;
            }
            if (artifact_status != RIBOS_ARTIFACT_OK) {
                free(artifact);
                ribos_ir_module_destroy(ir_module);
                (void)fprintf(
                    stderr,
                    "RIBOS-ARTIFACT-EMIT-FAIL status=%u "
                    "source=%s artifact=%s\n",
                    (unsigned)artifact_status,
                    path,
                    artifact_path);
                return 2;
            }
            (void)printf(
                "RIBOS-ARTIFACT-EMIT-OK source=%s artifact=%s "
                "bytes=%zu vm=%u.%u isa=%u.%u sections=%zu sha256=",
                path,
                artifact_path,
                artifact_size,
                artifact_view.vm_abi_major,
                artifact_view.vm_abi_minor,
                artifact_view.isa_major,
                artifact_view.isa_minor,
                artifact_view.section_count);
            ribos_print_digest(artifact_view.artifact_hash);
            (void)printf("\n");
            free(artifact);
        }
        ribos_ir_module_destroy(ir_module);
        (void)printf(
            "RIBOS-COMPILER-OK file=%s bytes=%zu tokens=%zu ast=%zu "
            "reductions=%zu arena=%zu transient-peak=%zu types=%zu "
            "functions=%zu helper-sites=%zu helper-upper=%llu "
            "instruction-upper=%llu instruction-budget=%llu "
            "helper-budget=%llu frame-bytes=%u aggregate-bytes=%u "
            "stack-bytes=%llu reachable-blocks=%u loops=%u "
            "declared=0x%08x required=0x%08x scope-depth=%u "
            "call-depth=%u\n",
            path,
            compile_summary.syntax.source_bytes,
            compile_summary.syntax.token_count,
            compile_summary.ast_node_count,
            compile_summary.ast_reduction_count,
            compile_summary.parser_arena_bytes,
            compile_summary.peak_transient_bytes,
            compile_summary.type_count,
            compile_summary.function_count,
            compile_summary.helper_call_site_count,
            (unsigned long long)
                compile_summary.helper_call_upper_bound,
            (unsigned long long)
                compile_summary.instruction_upper_bound,
            (unsigned long long)
                compile_summary.declared_instruction_budget,
            (unsigned long long)
                compile_summary.declared_helper_budget,
            compile_summary.frame_byte_upper_bound,
            compile_summary.aggregate_storage_upper_bound,
            (unsigned long long)
                compile_summary.maximum_stack_bytes,
            compile_summary.reachable_block_count,
            compile_summary.bounded_loop_count,
            compile_summary.declared_capabilities,
            compile_summary.required_capabilities,
            compile_summary.max_scope_depth,
            compile_summary.max_call_depth);
        return 0;
    }

    status = ribos_parse_source(
        allocator,
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
            path);
        return 2;
    }

    (void)printf(
        "RIBOS-PARSER-PILOT-OK file=%s bytes=%zu tokens=%zu "
        "declarations=%zu depth=%u\n",
        path,
        summary.source_bytes,
        summary.token_count,
        summary.declaration_count,
        summary.max_parser_depth);
    return 0;
}
