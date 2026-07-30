#ifndef RIBOS_VM_PREPARED_H
#define RIBOS_VM_PREPARED_H

#include <stddef.h>
#include <stdint.h>

#include "ribos/artifact/format.h"
#include "ribos/schema/schema.h"
#include "ribos/vm/runtime.h"
#include "ribos/vm/verifier.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file prepared.h
 * @brief Product authorization과 verifier 결과를 봉인하는 VM 준비 계약.
 *
 * 이 API는 caller-owned workspace만 사용한다. Production execute API는 raw
 * artifact byte가 아니라 이 header의 opaque `RibosPreparedProgram`만 받는다.
 */

#define RIBOS_VM_AUTHORIZATION_V1_MAJOR 1u
#define RIBOS_VM_AUTHORIZATION_V1_MINOR 0u
#define RIBOS_VM_PREPARED_PROGRAM_V1_MAJOR 1u
#define RIBOS_VM_PREPARED_PROGRAM_V1_MINOR 0u

typedef struct RibosAuthorizedArtifact RibosAuthorizedArtifact;

/** Product authority가 내릴 수 있는 유일한 affirmative decision이다. */
typedef enum RibosArtifactAuthorizationDecision {
    RIBOS_ARTIFACT_AUTHORIZATION_GRANTED = 1
} RibosArtifactAuthorizationDecision;

/**
 * Generic VM이 product authorization callback에 제공하는 immutable request다.
 *
 * Signature 검증, key selection과 rollback 판단은 callback의 product authority가
 * 소유한다. Pointer는 callback 동안만 유효하다.
 */
typedef struct RibosArtifactAuthorizationRequest {
    uint32_t size;
    uint16_t authorization_major;
    uint16_t authorization_minor;
    uint32_t flags;
    uint32_t envelope_flags;
    uint32_t signature_algorithm;
    uint32_t reserved0;
    const uint8_t *artifact;
    uint64_t artifact_size;
    const uint8_t *key_id;
    uint64_t key_id_size;
    const uint8_t *signature;
    uint64_t signature_size;
    uint8_t artifact_hash[RIBOS_VM_DIGEST_BYTES];
    uint8_t schema_digest[RIBOS_VM_DIGEST_BYTES];
    uint64_t reserved[4];
} RibosArtifactAuthorizationRequest;

/**
 * Product authority가 승인 시 채우는 fixed-width receipt다.
 *
 * `helper_execution_digest`는 이 product decision이 허가한 target helper table을
 * 결박한다. Signed artifact이면 key identity digest도 nonzero여야 한다.
 */
typedef struct RibosArtifactAuthorizationReceipt {
    uint32_t size;
    uint16_t authorization_major;
    uint16_t authorization_minor;
    uint32_t flags;
    uint32_t decision;
    uint64_t authority_generation;
    uint64_t manifest_sequence;
    uint64_t rollback_floor;
    uint8_t artifact_hash[RIBOS_VM_DIGEST_BYTES];
    uint8_t schema_digest[RIBOS_VM_DIGEST_BYTES];
    uint8_t helper_execution_digest[RIBOS_VM_DIGEST_BYTES];
    uint8_t key_identity_digest[RIBOS_VM_DIGEST_BYTES];
    uint8_t policy_identity_digest[RIBOS_VM_DIGEST_BYTES];
    uint64_t reserved[4];
} RibosArtifactAuthorizationReceipt;

/**
 * Product-owned signature/key/rollback authority callback이다.
 *
 * 반환값은 `RibosVmStatus` registry를 사용한다. 승인하지 않으면
 * `RIBOS_VM_STATUS_NOT_AUTHORIZED`, 승인하면 `OK`와 완전한 receipt를 반환한다.
 */
typedef uint32_t (*RibosArtifactAuthorizeFn)(
    void *authority_context,
    const RibosArtifactAuthorizationRequest *request,
    RibosArtifactAuthorizationReceipt *receipt);

/** Product authority callback과 process-local context binding이다. */
typedef struct RibosArtifactAuthorizer {
    uint32_t size;
    uint16_t authorization_major;
    uint16_t authorization_minor;
    uint32_t flags;
    uint32_t reserved0;
    void *authority_context;
    RibosArtifactAuthorizeFn authorize;
    uint64_t reserved[4];
} RibosArtifactAuthorizer;

/** Opaque authorized artifact workspace의 최소 alignment를 반환한다. */
size_t ribos_authorized_artifact_workspace_alignment_v1(void);

/** Artifact copy와 authorization state에 필요한 caller-owned byte 수를 계산한다. */
RibosVmStatus ribos_authorized_artifact_workspace_size_v1(
    size_t artifact_size,
    size_t *required_size);

/**
 * Structural open/hash 뒤 product callback을 실행하고 artifact copy를 봉인한다.
 *
 * Raw input은 반환 뒤 보존하지 않는다. Callback이 성공해도 receipt와 copied byte가
 * 다시 일치하지 않으면 authorized state를 만들지 않는다.
 */
RibosVmStatus ribos_authorize_artifact_v1(
    const uint8_t *artifact,
    size_t artifact_size,
    const RibosArtifactAuthorizer *authorizer,
    void *workspace,
    size_t workspace_size,
    const RibosAuthorizedArtifact **authorized_artifact);

/** Opaque authorized object와 copied artifact hash를 다시 검증한다. */
RibosVmStatus ribos_authorized_artifact_validate_v1(
    const RibosAuthorizedArtifact *authorized_artifact);

/** Canonical helper execution contract identity를 allocation 없이 계산한다. */
RibosVmStatus ribos_vm_helper_contract_compute_identity_v1(
    const RibosVmHelperContract *contract,
    uint8_t digest[RIBOS_VM_DIGEST_BYTES]);

/** Opaque PreparedProgram workspace의 최소 alignment를 반환한다. */
size_t ribos_prepared_program_workspace_alignment_v1(void);

/**
 * Immutable artifact/helper copy와 verifier scratch에 필요한 byte 수를 계산한다.
 *
 * Query는 authorization이나 prepared state를 새로 만들지 않는다.
 */
RibosVmStatus ribos_prepared_program_workspace_size_v1(
    const RibosAuthorizedArtifact *authorized_artifact,
    const RibosVmHelperContract *helper_contract,
    size_t *required_size);

/**
 * Authorized artifact를 Stage-1, Stage-2로 검증하고 execution binding을 봉인한다.
 *
 * 선택 schema는 canonical digest 계산과 verifier 동안만 borrow한다. Artifact와 helper
 * binding은 caller-owned workspace로 복사한다. 실패 시 output은 NULL이며 report는 첫
 * verifier failure를 보존한다.
 */
RibosVmStatus ribos_prepare_program_v1(
    const RibosAuthorizedArtifact *authorized_artifact,
    const RibosProductSchema *schema,
    const RibosVmHelperContract *helper_contract,
    const RibosVmLimits *effective_limits,
    void *workspace,
    size_t workspace_size,
    RibosVerifierReport *report,
    const RibosPreparedProgram **prepared_program);

/** PreparedProgram의 artifact/helper/binding seal을 다시 검증한다. */
RibosVmStatus ribos_prepared_program_validate_v1(
    const RibosPreparedProgram *prepared_program);

/** PreparedProgram에 봉인된 independent verifier report를 borrow한다. */
const RibosVerifierReport *ribos_prepared_program_report_v1(
    const RibosPreparedProgram *prepared_program);

/** PreparedProgram에 봉인된 effective limits를 borrow한다. */
const RibosVmLimits *ribos_prepared_program_limits_v1(
    const RibosPreparedProgram *prepared_program);

/** PreparedProgram의 canonical binding identity 32 byte를 borrow한다. */
const uint8_t *ribos_prepared_program_binding_digest_v1(
    const RibosPreparedProgram *prepared_program);

/** PreparedProgram이 소유하는 immutable artifact view를 borrow한다. */
const RibosArtifactView *ribos_prepared_program_artifact_view_v1(
    const RibosPreparedProgram *prepared_program);

/**
 * Verified artifact type의 schema ownership과 named type class를 반환한다.
 *
 * Non-named type의 `schema_type_class`는 `RIBOS_VM_INVALID_ID`다. Aggregate
 * ownership은 member/element/payload ownership의 최댓값으로 이미 닫혀 있다.
 */
RibosVmStatus ribos_prepared_program_type_semantics_v1(
    const RibosPreparedProgram *prepared_program,
    uint32_t type_id,
    uint32_t *ownership,
    uint32_t *schema_type_class);

/** PreparedProgram이 복사해 소유하는 helper contract를 borrow한다. */
const RibosVmHelperContract *ribos_prepared_program_helper_contract_v1(
    const RibosPreparedProgram *prepared_program);

#ifdef __cplusplus
}
#endif

#endif
