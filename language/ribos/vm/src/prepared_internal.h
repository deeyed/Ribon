#ifndef RIBOS_VM_PREPARED_INTERNAL_H
#define RIBOS_VM_PREPARED_INTERNAL_H

#include "ribos/vm/prepared.h"

#define RIBOS_AUTHORIZED_ARTIFACT_MAGIC UINT64_C(0x5242414155544831)
#define RIBOS_PREPARED_PROGRAM_MAGIC UINT64_C(0x5242505245505631)

struct RibosAuthorizedArtifact {
    uint64_t magic;
    uint16_t major;
    uint16_t minor;
    uint32_t state;
    const uint8_t *artifact;
    size_t artifact_size;
    RibosArtifactView view;
    RibosArtifactAuthorizationReceipt receipt;
    uint8_t artifact_bytes_digest[RIBOS_VM_DIGEST_BYTES];
    uint8_t receipt_digest[RIBOS_VM_DIGEST_BYTES];
};

struct RibosPreparedProgram {
    uint64_t magic;
    uint16_t major;
    uint16_t minor;
    uint32_t state;
    const uint8_t *artifact;
    size_t artifact_size;
    RibosArtifactView view;
    uint8_t artifact_bytes_digest[RIBOS_VM_DIGEST_BYTES];
    RibosArtifactAuthorizationReceipt authorization;
    uint8_t authorization_digest[RIBOS_VM_DIGEST_BYTES];
    uint8_t schema_digest[RIBOS_VM_DIGEST_BYTES];
    uint8_t helper_execution_digest[RIBOS_VM_DIGEST_BYTES];
    uint8_t binding_digest[RIBOS_VM_DIGEST_BYTES];
    RibosVerifierReport report;
    RibosVmLimits limits;
    RibosVmHelperContract helper_contract;
};

#endif
