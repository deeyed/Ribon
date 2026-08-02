#ifndef RIBON_PROTOCOL_CONFIRMATION_H
#define RIBON_PROTOCOL_CONFIRMATION_H

#include <stddef.h>
#include <stdint.h>

/** @brief Protocol callback이 해석하는 authenticated health payload view다. */
struct RibonBootHealthPayload {
    uint32_t size; /**< `sizeof(struct RibonBootHealthPayload)`이다. */
    uint32_t abi_version; /**< Generic envelope ABI version이다. */
    const uint8_t *bytes; /**< Protocol-owned immutable payload다. */
    size_t byte_size; /**< Payload의 exact bounded byte 수다. */
    uint8_t digest[32]; /**< Envelope가 결속한 SHA-256이다. */
};

#endif
