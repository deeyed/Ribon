#ifndef RIBOS_COMPILER_H
#define RIBOS_COMPILER_H

#include <stddef.h>
#include <stdint.h>

#include "ribos/frontend/parser.h"
#include "ribos/ir/ir.h"
#include "ribos/schema/schema.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file compiler.h
 * @brief Ribos의 VM 독립 정적 의미 분석 인터페이스.
 *
 * 이 인터페이스는 source를 bounded typed AST로 낮추고 이름, 타입, mutation,
 * capability, helper-call bound를 검사한다. 선택적으로 typed AST를 Policy IR
 * v1으로 낮추지만 bytecode와 VM 실행은 이 인터페이스의 권한이 아니다.
 */

/** 정적 의미 분석의 안정된 종료 상태다. */
typedef enum RibosCompileStatus {
    RIBOS_COMPILE_OK = 0,
    RIBOS_COMPILE_INVALID_ARGUMENT,
    RIBOS_COMPILE_PARSE_ERROR,
    RIBOS_COMPILE_NAME_ERROR,
    RIBOS_COMPILE_TYPE_ERROR,
    RIBOS_COMPILE_CAPABILITY_ERROR,
    RIBOS_COMPILE_BOUND_ERROR,
    RIBOS_COMPILE_SCHEMA_ERROR,
    RIBOS_COMPILE_IR_ERROR,
    RIBOS_COMPILE_NO_MEMORY,
    RIBOS_COMPILE_INTERNAL_ERROR
} RibosCompileStatus;

/** Diagnostic 소비자가 분기할 수 있는 안정된 오류 코드다. */
typedef enum RibosCompileDiagnosticCode {
    RIBOS_E_NONE = 0,
    RIBOS_E_DUPLICATE_DECLARATION,
    RIBOS_E_DUPLICATE_BINDING,
    RIBOS_E_UNKNOWN_NAME,
    RIBOS_E_UNKNOWN_TYPE,
    RIBOS_E_UNKNOWN_MEMBER,
    RIBOS_E_INVALID_DECORATOR,
    RIBOS_E_TYPE_MISMATCH,
    RIBOS_E_ARGUMENT_COUNT_MISMATCH,
    RIBOS_E_ARGUMENT_TYPE_MISMATCH,
    RIBOS_E_CONDITION_NOT_BOOL,
    RIBOS_E_RETURN_TYPE_MISMATCH,
    RIBOS_E_MISSING_RETURN,
    RIBOS_E_NON_EXHAUSTIVE_MATCH,
    RIBOS_E_MUTATE_IMMUTABLE_BINDING,
    RIBOS_E_INVALID_ASSIGNMENT_TARGET,
    RIBOS_E_CANNOT_INFER_EMPTY_COLLECTION,
    RIBOS_E_COLLECTION_ELEMENT_TYPE_MISMATCH,
    RIBOS_E_COLLECTION_BOUND_EXCEEDED,
    RIBOS_E_UNBOUNDED_ITERATION,
    RIBOS_E_RESULT_MUST_BE_USED,
    RIBOS_E_CAPABILITY_NOT_DECLARED,
    RIBOS_E_PURE_FUNCTION_HAS_EFFECT,
    RIBOS_E_HELPER_BUDGET_EXCEEDED,
    RIBOS_E_RECURSIVE_CALL_GRAPH,
    RIBOS_E_SCHEMA_INVALID,
    RIBOS_E_IR_LOWERING,
    RIBOS_E_RESOURCE_LIMIT,
    RIBOS_E_INTERNAL
} RibosCompileDiagnosticCode;

/** Source-level 정적 검사가 성공했을 때 봉인되는 측정값이다. */
typedef struct RibosCompileSummary {
    RibosParseSummary syntax;
    size_t ast_node_count;
    size_t ast_reduction_count;
    size_t parser_arena_bytes;
    size_t peak_transient_bytes;
    size_t type_count;
    size_t function_count;
    size_t helper_call_site_count;
    uint64_t helper_call_upper_bound;
    uint64_t declared_instruction_budget;
    uint64_t declared_helper_budget;
    uint32_t declared_capabilities;
    uint32_t required_capabilities;
    uint32_t max_scope_depth;
    uint32_t max_call_depth;
    uint8_t schema_digest[RIBOS_SCHEMA_DIGEST_BYTES];
} RibosCompileSummary;

/** 하나의 primary static-semantic diagnostic이다. */
typedef struct RibosCompileDiagnostic {
    RibosCompileDiagnosticCode code;
    RibosSourceSpan span;
    char symbol[64];
    char expected[96];
    char actual[96];
    char message[192];
    RibosDiagnostic parse;
} RibosCompileDiagnostic;

/**
 * Source 하나를 parse하고 VM 독립 정적 의미를 검사한다.
 *
 * @param source UTF-8 source byte span.
 * @param source_length source byte 수.
 * @param summary 성공 시 bounded compile 측정값을 받는다.
 * @param diagnostic 실패 시 첫 primary diagnostic을 받는다.
 * @return 정적 의미 분석 종료 상태.
 */
RibosCompileStatus ribos_compile_source(
    const char *source,
    size_t source_length,
    RibosCompileSummary *summary,
    RibosCompileDiagnostic *diagnostic);

/**
 * 선택된 product schema로 source의 정적 의미를 검사한다.
 *
 * @param schema compiler와 verifier가 공유하는 immutable product schema.
 */
RibosCompileStatus ribos_compile_source_with_schema(
    const char *source,
    size_t source_length,
    const RibosProductSchema *schema,
    RibosCompileSummary *summary,
    RibosCompileDiagnostic *diagnostic);

/**
 * 정적 검사가 성공한 source를 explicit-CFG Policy IR v1으로 낮춘다.
 *
 * Caller가 생성한 module은 성공과 실패 모두 caller가 destroy한다. 실패 시 module은
 * 빈 상태로 reset되어 부분 IR을 권위 있는 결과로 사용할 수 없다.
 */
RibosCompileStatus ribos_compile_source_to_ir(
    const char *source,
    size_t source_length,
    const RibosProductSchema *schema,
    RibosIrModule *module,
    RibosCompileSummary *summary,
    RibosCompileDiagnostic *diagnostic);

/** Compile status의 안정된 ASCII spelling을 반환한다. */
const char *ribos_compile_status_name(RibosCompileStatus status);

/** Diagnostic code의 안정된 ASCII spelling을 반환한다. */
const char *ribos_compile_diagnostic_code_name(
    RibosCompileDiagnosticCode code);

#ifdef __cplusplus
}
#endif

#endif
