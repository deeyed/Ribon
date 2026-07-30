#ifndef RIBOS_VM_VERIFIER_H
#define RIBOS_VM_VERIFIER_H

#include <stddef.h>
#include <stdint.h>

#include "ribos/artifact/format.h"
#include "ribos/schema/schema.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file verifier.h
 * @brief Compiler를 신뢰하지 않는 Ribos bytecode verifier 계약.
 */

#define RIBOS_VERIFIER_V1_MAJOR 1u
#define RIBOS_VERIFIER_V1_MINOR 0u
#define RIBOS_VERIFIER_MAX_VALUE_BYTES (1024u * 1024u)
#define RIBOS_VERIFIER_MAX_FRAME_BYTES (16u * 1024u * 1024u)
#define RIBOS_VERIFIER_MAX_STACK_BYTES (64u * 1024u * 1024u)

/**
 * Two-stage verification의 stable 결과다.
 *
 * 이 상태는 실행 certificate가 아니다. Signature, rollback policy와 후속
 * runtime-counter gate가 별도로 성공해야 dispatch할 수 있다.
 */
typedef enum RibosVerifierStatus {
    RIBOS_VERIFIER_OK = 0,
    RIBOS_VERIFIER_INVALID_ARGUMENT,
    RIBOS_VERIFIER_WORKSPACE_TOO_SMALL,
    RIBOS_VERIFIER_STRUCTURAL_ERROR,
    RIBOS_VERIFIER_SCHEMA_MISMATCH,
    RIBOS_VERIFIER_INVALID_TYPE,
    RIBOS_VERIFIER_INVALID_CONSTANT,
    RIBOS_VERIFIER_INVALID_FUNCTION,
    RIBOS_VERIFIER_INVALID_BLOCK,
    RIBOS_VERIFIER_INVALID_SLOT,
    RIBOS_VERIFIER_INVALID_INSTRUCTION,
    RIBOS_VERIFIER_INVALID_TARGET,
    RIBOS_VERIFIER_UNINITIALIZED_SLOT,
    RIBOS_VERIFIER_TYPE_MISMATCH,
    RIBOS_VERIFIER_FRAME_MISMATCH,
    RIBOS_VERIFIER_RECURSIVE_CALL,
    RIBOS_VERIFIER_RESOURCE_MISMATCH,
    RIBOS_VERIFIER_CAPABILITY_MISMATCH,
    RIBOS_VERIFIER_OPAQUE_FORGERY,
    RIBOS_VERIFIER_TYPESTATE_VIOLATION,
    RIBOS_VERIFIER_OWNERSHIP_VIOLATION,
    RIBOS_VERIFIER_BUDGET_EXCEEDED,
    RIBOS_VERIFIER_TERMINAL_ACTION_VIOLATION,
    RIBOS_VERIFIER_FAIL_CLOSED_VIOLATION
} RibosVerifierStatus;

/** Failure 위치를 식별하는 artifact table 분류다. */
typedef enum RibosVerifierSubject {
    RIBOS_VERIFIER_SUBJECT_ARTIFACT = 0,
    RIBOS_VERIFIER_SUBJECT_SCHEMA,
    RIBOS_VERIFIER_SUBJECT_TYPE,
    RIBOS_VERIFIER_SUBJECT_SHAPE,
    RIBOS_VERIFIER_SUBJECT_CONSTANT,
    RIBOS_VERIFIER_SUBJECT_FUNCTION,
    RIBOS_VERIFIER_SUBJECT_BLOCK,
    RIBOS_VERIFIER_SUBJECT_SLOT,
    RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
    RIBOS_VERIFIER_SUBJECT_OPERAND
} RibosVerifierSubject;

/**
 * Two-stage verifier의 deterministic report다.
 *
 * 실패하면 subject와 subject_id가 첫 fail-closed 지점을 가리킨다. 성공하면
 * recomputed 필드는 bytecode에서 독립 재도출한 entry function 결과다.
 */
typedef struct RibosVerifierReport {
    uint16_t verifier_major;
    uint16_t verifier_minor;
    RibosVerifierStatus status;
    RibosVerifierSubject subject;
    uint32_t subject_id;
    uint32_t detail;
    uint32_t verified_type_count;
    uint32_t verified_function_count;
    uint32_t verified_block_count;
    uint32_t verified_instruction_count;
    uint32_t recomputed_frame_bytes;
    uint32_t recomputed_call_depth;
    uint64_t recomputed_stack_bytes;
    uint32_t recomputed_reachable_capabilities;
    uint64_t recomputed_instruction_upper_bound;
    uint64_t recomputed_helper_upper_bound;
} RibosVerifierReport;

/**
 * Artifact별 caller-owned verifier scratch의 최소 byte 수를 계산한다.
 *
 * 함수는 artifact의 structural reader만 실행하고 allocation하지 않는다. 반환한
 * 크기는 같은 artifact의 Stage-1 또는 Stage-2 verifier 호출에 유효하다.
 */
RibosVerifierStatus ribos_verifier_workspace_size_v1(
    const uint8_t *artifact,
    size_t artifact_size,
    size_t *required_size,
    RibosVerifierReport *report);

/**
 * Compiler/Policy IR 없이 artifact와 selected product schema를 독립 검증한다.
 *
 * workspace는 호출자가 소유하며 함수는 heap을 사용하지 않는다. 성공은 Stage-1
 * structural/type/CFG/frame verification만 의미하고 artifact 실행 권한을 만들지
 * 않는다.
 */
RibosVerifierStatus ribos_verify_artifact_stage1_v1(
    const uint8_t *artifact,
    size_t artifact_size,
    const RibosProductSchema *schema,
    void *workspace,
    size_t workspace_size,
    RibosVerifierReport *report);

/**
 * Stage-1 이후 capability, ownership, exact resource와 terminal policy를 닫는다.
 *
 * Helper ABI와 product schema가 ownership/typestate authority다. 함수는 compiler가
 * 기록한 capability와 bound를 승인하지 않고 reachable bytecode에서 다시 계산한다.
 * Policy fault는 catch할 수 없고 TRAP 또는 error Result로 fail closed해야 한다.
 */
RibosVerifierStatus ribos_verify_artifact_stage2_v1(
    const uint8_t *artifact,
    size_t artifact_size,
    const RibosProductSchema *schema,
    void *workspace,
    size_t workspace_size,
    RibosVerifierReport *report);

/** Stable verifier status의 ASCII spelling을 반환한다. */
const char *ribos_verifier_status_name(RibosVerifierStatus status);

#ifdef __cplusplus
}
#endif

#endif
