#ifndef RIBON_SECURITY_ED25519_H
#define RIBON_SECURITY_ED25519_H

#include <Ribon/security/signature.h>

/** @brief Monocypher 4.0.3 기반 strict Ed25519 production provider다. */
extern const struct RibonSignatureProvider
    ribon_ed25519_signature_provider_descriptor;

#endif
