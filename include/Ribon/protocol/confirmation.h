#ifndef RIBON_PROTOCOL_CONFIRMATION_H
#define RIBON_PROTOCOL_CONFIRMATION_H

#include <stdint.h>

/** @brief Boot confirmation nonce의 고정 byte 수다. */
#define RIBON_BOOT_CONFIRMATION_NONCE_SIZE 32u

/** @brief OS가 기록할 수 있는 confirmation 결과다. */
enum RibonBootConfirmationResult {
    RIBON_BOOT_CONFIRMATION_HEALTHY = 1,
};

/** @brief Parser가 검증한 OS-specific confirmation view다. */
struct RibonBootConfirmation {
    const char *protocol_id; /**< Confirmation을 해석할 protocol ID다. */
    uint64_t generation; /**< 확인 대상 slot generation이다. */
    uint8_t nonce[RIBON_BOOT_CONFIRMATION_NONCE_SIZE]; /**< Attempt nonce다. */
    uint32_t nonce_size; /**< 유효 nonce byte 수다. */
    enum RibonBootConfirmationResult result; /**< OS health 결과다. */
};

/** @brief 선택한 attempt와 confirmation을 묶는 기대값이다. */
struct RibonBootConfirmationExpectation {
    uint64_t generation; /**< 선택한 attempt generation이다. */
    uint8_t nonce[RIBON_BOOT_CONFIRMATION_NONCE_SIZE]; /**< 선택 시 생성한 nonce다. */
    uint32_t nonce_size; /**< 유효 nonce byte 수다. */
};

#endif
