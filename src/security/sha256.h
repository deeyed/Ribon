#ifndef RIBON_SECURITY_PRIVATE_SHA256_H
#define RIBON_SECURITY_PRIVATE_SHA256_H

#include <stddef.h>
#include <stdint.h>

/** @brief Security implementation 내부 immutable byte view의 SHA-256을 계산한다. */
void ribon_security_sha256(
    const uint8_t *bytes,
    size_t size,
    uint8_t digest[32]);

#endif
