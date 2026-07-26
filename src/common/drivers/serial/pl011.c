#include "pl011.h"

#define RIBON_PL011_DATA_INDEX (0x00u / sizeof(uint32_t))
#define RIBON_PL011_FLAG_INDEX (0x18u / sizeof(uint32_t))

/** @brief Target-owned MMIO resource로 allocation-free writer를 초기화한다. */
int ribon_pl011_initialize(
    struct RibonPl011 *out,
    uint64_t physical_base,
    uint32_t poll_limit) {
    if (out == 0 || physical_base == 0u ||
        (physical_base & (sizeof(uint32_t) - 1u)) != 0u ||
        poll_limit == 0u) {
        return -1;
    }
    *out = (struct RibonPl011){
        .registers = (volatile uint32_t *)(uintptr_t)physical_base,
        .poll_limit = poll_limit,
    };
    return 0;
}

/** @brief Polling 상한 안에 한 byte를 전송한다. */
int ribon_pl011_write_byte(struct RibonPl011 *uart, uint8_t value) {
    if (uart == 0 || uart->registers == 0 || uart->poll_limit == 0u) {
        return -1;
    }
    for (uint32_t poll = 0u; poll < uart->poll_limit; ++poll) {
        if ((uart->registers[RIBON_PL011_FLAG_INDEX] &
             RIBON_PL011_FLAG_TX_FULL) == 0u) {
            uart->registers[RIBON_PL011_DATA_INDEX] = value;
            return 0;
        }
    }
    return -2;
}

/** @brief NUL-terminated diagnostic marker를 polling 방식으로 전송한다. */
int ribon_pl011_write(struct RibonPl011 *uart, const char *text) {
    if (uart == 0 || text == 0) {
        return -1;
    }
    while (*text != '\0') {
        if (ribon_pl011_write_byte(uart, (uint8_t)*text) != 0) {
            return -2;
        }
        ++text;
    }
    return 0;
}
