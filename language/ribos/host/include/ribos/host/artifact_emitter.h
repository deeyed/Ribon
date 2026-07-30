#ifndef RIBOS_HOST_ARTIFACT_EMITTER_H
#define RIBOS_HOST_ARTIFACT_EMITTER_H

#include "ribos/artifact/format.h"
#include "ribos/ir/ir.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file artifact_emitter.h
 * @brief Validated Policy IR에서 canonical Ribos artifact를 생성하는 host API.
 */

/** Caller가 제공하는 optional signature envelope bytes다. */
typedef struct RibosArtifactSignature {
    RibosArtifactSignatureAlgorithm algorithm;
    const uint8_t *key_id;
    size_t key_id_length;
    const uint8_t *signature;
    size_t signature_length;
} RibosArtifactSignature;

/** Deterministic artifact emission 선택이다. */
typedef struct RibosArtifactEmitOptions {
    uint8_t include_source_map;
    RibosArtifactSignature signature;
} RibosArtifactEmitOptions;

/**
 * Policy IR을 resource-close한 뒤 canonical little-endian artifact를 기록한다.
 *
 * output이 NULL이면 required_size만 계산한다. Signature verification은 수행하지
 * 않으며 supplied signature가 frozen envelope shape와 일치하는지만 검사한다.
 */
RibosArtifactStatus ribos_artifact_emit_v1(
    const RibosIrModule *module,
    const RibosArtifactEmitOptions *options,
    uint8_t *output,
    size_t output_capacity,
    size_t *required_size);

#ifdef __cplusplus
}
#endif

#endif
