#ifndef RIBON_COMMON_DRIVERS_SERIAL_PL011_H
#define RIBON_COMMON_DRIVERS_SERIAL_PL011_H

#include <stdint.h>

/** @brief Caller-selected PL011 MMIO resource와 bounded polling 상한이다. */
struct RibonPl011 {
    volatile uint32_t *registers; /**< Validated MMIO base의 borrowed mapping이다. */
    uint32_t poll_limit; /**< 한 byte마다 허용할 flag read 횟수다. */
};

/** @brief PL011 flag register의 TX FIFO full bit다. */
#define RIBON_PL011_FLAG_TX_FULL (1u << 5)

/** @brief Target-owned MMIO resource로 allocation-free writer를 초기화한다. */
int ribon_pl011_initialize(
    struct RibonPl011 *out,
    uint64_t physical_base,
    uint32_t poll_limit);

/** @brief Polling 상한 안에 한 byte를 전송한다. */
int ribon_pl011_write_byte(struct RibonPl011 *uart, uint8_t value);

/** @brief NUL-terminated diagnostic marker를 polling 방식으로 전송한다. */
int ribon_pl011_write(struct RibonPl011 *uart, const char *text);

#endif
