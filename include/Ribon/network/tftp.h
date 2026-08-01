#ifndef RIBON_NETWORK_TFTP_H
#define RIBON_NETWORK_TFTP_H

#include <stddef.h>
#include <stdint.h>

/** @brief Minimal bounded TFTP packet guard ABI version이다. */
#define RIBON_TFTP_GUARD_ABI_VERSION 1u

/** @brief SNP/native provider가 공유하는 allocation-free transfer state다. */
struct RibonTftpGuard {
    uint32_t size; /**< `sizeof(struct RibonTftpGuard)`이다. */
    uint32_t abi_version; /**< `RIBON_TFTP_GUARD_ABI_VERSION`이다. */
    uint16_t block_size; /**< 협상된 DATA payload 상한이다. */
    uint16_t expected_block; /**< 다음 새 DATA block 번호다. */
    uint32_t packet_count; /**< 허용된 packet 수다. */
    uint64_t maximum_bytes; /**< 전체 payload 상한이다. */
    uint64_t received_bytes; /**< 중복을 제외한 누적 payload byte 수다. */
    uint8_t options_complete; /**< OACK 또는 첫 DATA 뒤 1이다. */
    uint8_t complete; /**< Short final DATA 뒤 1이다. */
    uint8_t reserved[6]; /**< v1에서는 0이다. */
};

/** @brief Minimal TFTP guard의 stable parser 결과다. */
enum RibonTftpGuardStatus {
    RIBON_TFTP_GUARD_STATUS_OK = 0,
    RIBON_TFTP_GUARD_STATUS_DUPLICATE = 1,
    RIBON_TFTP_GUARD_STATUS_COMPLETE = 2,
    RIBON_TFTP_GUARD_STATUS_INVALID_ARGUMENT = -1,
    RIBON_TFTP_GUARD_STATUS_MALFORMED = -2,
    RIBON_TFTP_GUARD_STATUS_OUT_OF_ORDER = -3,
    RIBON_TFTP_GUARD_STATUS_CAPACITY = -4,
    RIBON_TFTP_GUARD_STATUS_REMOTE_ERROR = -5,
    RIBON_TFTP_GUARD_STATUS_UNSUPPORTED_OPTION = -6,
};

/** @brief Guard를 one-transfer, 16-bit block-number TFTP state로 초기화한다. */
int ribon_tftp_guard_initialize(
    struct RibonTftpGuard *guard,
    uint16_t requested_block_size,
    uint64_t maximum_bytes);

/**
 * @brief 한 wire packet을 검증하고 새 DATA payload span을 빌려준다.
 *
 * Output span은 호출 중에만 입력 packet을 빌리며 duplicate/OACK에는 길이 0을 반환한다.
 */
int ribon_tftp_guard_accept(
    struct RibonTftpGuard *guard,
    const uint8_t *packet,
    size_t packet_size,
    const uint8_t **payload,
    size_t *payload_size);

#endif
