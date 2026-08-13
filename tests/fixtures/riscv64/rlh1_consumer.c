#include <stdint.h>

#include <Ribon/protocols/os/luca/rlh1.h>

#define FIXTURE_UART_BASE 0x10000000ull
#define FIXTURE_UART_THR 0u
#define FIXTURE_UART_LSR 5u
#define FIXTURE_UART_THR_EMPTY 0x20u
#define FIXTURE_UART_POLL_LIMIT 1000000u
#define FIXTURE_SSTATUS_SIE (1ull << 1)
#define FIXTURE_RISCV64_PROVENANCE_ARCH 3u

static const char fixture_provenance[]
    __attribute__((used, section(".rodata.fixture"))) =
        "RIBON-RISCV64-RLH1-FIXTURE-V1";

/** @brief QEMU virt 16550 UART에 한 byte를 bounded polling으로 기록한다. */
static int fixture_putc(unsigned char value) {
    volatile unsigned char *uart =
        (volatile unsigned char *)(uintptr_t)FIXTURE_UART_BASE;
    for (uint32_t poll = 0u; poll < FIXTURE_UART_POLL_LIMIT; ++poll) {
        if ((uart[FIXTURE_UART_LSR] & FIXTURE_UART_THR_EMPTY) != 0u) {
            uart[FIXTURE_UART_THR] = value;
            return 1;
        }
    }
    return 0;
}

/** @brief NUL-terminated fixture marker를 allocation 없이 기록한다. */
static void fixture_write(const char *text) {
    for (uint32_t index = 0u; text[index] != '\0'; ++index) {
        if (!fixture_putc((unsigned char)text[index])) {
            break;
        }
    }
}

/** @brief Stable failure receipt를 기록하고 terminal wait로 전환한다. */
static _Noreturn void fixture_fail(const char *reason) {
    fixture_write("RIBON-RLH1-RISCV64-FIXTURE-FAIL:");
    fixture_write(reason);
    fixture_write("\r\n");
    for (;;) {
        __asm__ __volatile__("wfi");
    }
}

/** @brief Little-endian byte sequence에서 u16을 읽는다. */
static uint16_t fixture_read_u16(const unsigned char *bytes, uint64_t offset) {
    return (uint16_t)bytes[offset] |
           ((uint16_t)bytes[offset + 1u] << 8u);
}

/** @brief Little-endian byte sequence에서 u32를 읽는다. */
static uint32_t fixture_read_u32(const unsigned char *bytes, uint64_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

/** @brief Little-endian byte sequence에서 u64를 읽는다. */
static uint64_t fixture_read_u64(const unsigned char *bytes, uint64_t offset) {
    uint64_t value = 0u;
    for (uint32_t index = 0u; index < 8u; ++index) {
        value |= (uint64_t)bytes[offset + index] << (index * 8u);
    }
    return value;
}

/** @brief RLH1 domain separator를 Ribon producer와 독립적으로 검증한다. */
static int fixture_domain_is_valid(const unsigned char *bytes) {
    static const unsigned char expected[RIBON_LUCA_RLH1_DOMAIN_SIZE] = {
        'R', 'I', 'B', 'O', 'N', '_', 'L', 'U',
        'C', 'A', '_', 'R', 'L', 'H', '1', '\0',
    };
    for (uint32_t index = 0u; index < RIBON_LUCA_RLH1_DOMAIN_SIZE; ++index) {
        if (bytes[RIBON_LUCA_RLH1_HEADER_DOMAIN_OFFSET + index] != expected[index]) {
            return 0;
        }
    }
    return 1;
}

/** @brief RLH1 CRC field를 0으로 간주해 독립 CRC32C를 계산한다. */
static uint32_t fixture_crc32c(const unsigned char *bytes, uint32_t size) {
    uint32_t crc = UINT32_MAX;
    for (uint32_t index = 0u; index < size; ++index) {
        unsigned char value = bytes[index];
        if (index >= RIBON_LUCA_RLH1_HEADER_CRC32C_OFFSET &&
            index < RIBON_LUCA_RLH1_HEADER_CRC32C_OFFSET + 4u) {
            value = 0u;
        }
        crc ^= value;
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            crc = (crc >> 1u) ^
                  (0x82f63b78u & (uint32_t)-(int32_t)(crc & 1u));
        }
    }
    return ~crc;
}

/** @brief RLH1 header와 section table의 bounded shape를 검증한다. */
static void fixture_validate_header(
    const unsigned char *bytes,
    uint32_t *total_size_out,
    uint32_t *table_offset_out,
    uint16_t *section_count_out) {
    const uint32_t total_size =
        fixture_read_u32(bytes, RIBON_LUCA_RLH1_HEADER_TOTAL_SIZE_OFFSET);
    const uint32_t table_offset =
        fixture_read_u32(
            bytes,
            RIBON_LUCA_RLH1_HEADER_SECTION_TABLE_OFFSET);
    const uint16_t section_count =
        fixture_read_u16(
            bytes,
            RIBON_LUCA_RLH1_HEADER_SECTION_COUNT_OFFSET);
    const uint64_t table_end =
        (uint64_t)table_offset +
        ((uint64_t)section_count * RIBON_LUCA_RLH1_SECTION_ENTRY_SIZE);

    if (fixture_read_u32(bytes, RIBON_LUCA_RLH1_HEADER_MAGIC_OFFSET) !=
            RIBON_LUCA_RLH1_MAGIC ||
        fixture_read_u16(
            bytes,
            RIBON_LUCA_RLH1_HEADER_VERSION_MAJOR_OFFSET) !=
            RIBON_LUCA_RLH1_VERSION_MAJOR ||
        fixture_read_u16(
            bytes,
            RIBON_LUCA_RLH1_HEADER_VERSION_MINOR_OFFSET) >
            RIBON_LUCA_RLH1_VERSION_MINOR ||
        fixture_read_u16(bytes, RIBON_LUCA_RLH1_HEADER_SIZE_OFFSET) !=
            RIBON_LUCA_RLH1_HEADER_SIZE ||
        fixture_read_u16(
            bytes,
            RIBON_LUCA_RLH1_HEADER_SECTION_ENTRY_SIZE_OFFSET) !=
            RIBON_LUCA_RLH1_SECTION_ENTRY_SIZE ||
        section_count == 0u ||
        section_count > RIBON_LUCA_RLH1_MAX_SECTIONS ||
        total_size < RIBON_LUCA_RLH1_HEADER_SIZE ||
        total_size > RIBON_LUCA_RLH1_MAX_TOTAL_SIZE ||
        table_offset < RIBON_LUCA_RLH1_HEADER_SIZE ||
        (table_offset % RIBON_LUCA_RLH1_PAYLOAD_ALIGNMENT) != 0u ||
        table_end > total_size ||
        fixture_read_u16(
            bytes,
            RIBON_LUCA_RLH1_HEADER_RESERVED0_OFFSET) != 0u ||
        fixture_read_u32(
            bytes,
            RIBON_LUCA_RLH1_HEADER_RESERVED1_OFFSET) != 0u ||
        !fixture_domain_is_valid(bytes) ||
        fixture_read_u64(
            bytes,
            RIBON_LUCA_RLH1_HEADER_RESERVED2_OFFSET) != 0u) {
        fixture_fail("header");
    }
    if (fixture_read_u32(bytes, RIBON_LUCA_RLH1_HEADER_CRC32C_OFFSET) !=
        fixture_crc32c(bytes, total_size)) {
        fixture_fail("crc32c");
    }
    *total_size_out = total_size;
    *table_offset_out = table_offset;
    *section_count_out = section_count;
}

/** @brief RISC-V provenance와 singleton BOOT_CPU section을 독립 검증한다. */
static uint64_t fixture_validate_sections(
    const unsigned char *bytes,
    uint32_t total_size,
    uint32_t table_offset,
    uint16_t section_count) {
    uint32_t boot_cpu_count = 0u;
    uint32_t provenance_count = 0u;
    uint64_t boot_cpu_id = UINT64_MAX;

    for (uint16_t index = 0u; index < section_count; ++index) {
        const unsigned char *section =
            bytes + table_offset +
            ((uint64_t)index * RIBON_LUCA_RLH1_SECTION_ENTRY_SIZE);
        const uint32_t type =
            fixture_read_u32(section, RIBON_LUCA_RLH1_SECTION_TYPE_OFFSET);
        const uint32_t flags =
            fixture_read_u32(section, RIBON_LUCA_RLH1_SECTION_FLAGS_OFFSET);
        const uint64_t offset =
            fixture_read_u64(
                section,
                RIBON_LUCA_RLH1_SECTION_PAYLOAD_OFFSET);
        const uint64_t length =
            fixture_read_u64(
                section,
                RIBON_LUCA_RLH1_SECTION_LENGTH_OFFSET);
        const uint32_t alignment =
            fixture_read_u32(
                section,
                RIBON_LUCA_RLH1_SECTION_ALIGNMENT_OFFSET);
        const unsigned char *payload;

        if (offset > total_size ||
            length > (uint64_t)total_size - offset ||
            alignment != RIBON_LUCA_RLH1_PAYLOAD_ALIGNMENT ||
            (offset % alignment) != 0u ||
            fixture_read_u32(
                section,
                RIBON_LUCA_RLH1_SECTION_RESERVED_OFFSET) != 0u) {
            fixture_fail("section-bounds");
        }
        payload = bytes + offset;
        if (type == RIBON_LUCA_RLH1_SECTION_PROVENANCE) {
            ++provenance_count;
            if (flags != RIBON_LUCA_RLH1_SECTION_REQUIRED_TO_UNDERSTAND ||
                length != RIBON_LUCA_RLH1_PROVENANCE_SIZE ||
                fixture_read_u32(payload, 4u) !=
                    FIXTURE_RISCV64_PROVENANCE_ARCH ||
                fixture_read_u32(payload, 20u) != 0u ||
                fixture_read_u64(payload, 24u) != 0u) {
                fixture_fail("provenance");
            }
        } else if (type == RIBON_LUCA_RLH1_SECTION_BOOT_CPU) {
            ++boot_cpu_count;
            if (flags != RIBON_LUCA_RLH1_SECTION_REQUIRED_TO_UNDERSTAND ||
                length != RIBON_LUCA_RLH1_BOOT_CPU_SIZE ||
                fixture_read_u32(
                    payload,
                    RIBON_LUCA_RLH1_BOOT_CPU_NAMESPACE_OFFSET) !=
                    RIBON_LUCA_RLH1_BOOT_CPU_NAMESPACE_RISCV_HART_ID ||
                fixture_read_u32(
                    payload,
                    RIBON_LUCA_RLH1_BOOT_CPU_FLAGS_OFFSET) !=
                    RIBON_LUCA_RLH1_BOOT_CPU_FLAG_BOOTSTRAP ||
                fixture_read_u64(
                    payload,
                    RIBON_LUCA_RLH1_BOOT_CPU_RESERVED0_OFFSET) != 0u ||
                fixture_read_u64(
                    payload,
                    RIBON_LUCA_RLH1_BOOT_CPU_RESERVED1_OFFSET) != 0u) {
                fixture_fail("boot-cpu-shape");
            }
            boot_cpu_id =
                fixture_read_u64(
                    payload,
                    RIBON_LUCA_RLH1_BOOT_CPU_ID_OFFSET);
        }
    }
    if (provenance_count != 1u) {
        fixture_fail("provenance-count");
    }
    if (boot_cpu_count != 1u) {
        fixture_fail("boot-cpu-count");
    }
    return boot_cpu_id;
}

/**
 * @brief Ribon의 RISC-V LUCA entry ABI와 RLH1 artifact를 QEMU에서 소비한다.
 *
 * 이 fixture는 실제 LUCA kernel이 아니며 Ribon 소유 contract acceptance만 제공한다.
 */
_Noreturn void ribon_rlh1_fixture_main(
    uint64_t rlh1_address,
    uint64_t entry_flags) {
    const unsigned char *bytes =
        (const unsigned char *)(uintptr_t)rlh1_address;
    uint64_t satp;
    uint64_t sstatus;
    uint32_t total_size;
    uint32_t table_offset;
    uint16_t section_count;
    uint64_t boot_cpu_id;

    (void)fixture_provenance;
    fixture_write("RIBON-RLH1-RISCV64-FIXTURE-ENTRY\r\n");
    if (rlh1_address == 0u ||
        (rlh1_address % RIBON_LUCA_RLH1_PAYLOAD_ALIGNMENT) != 0u ||
        entry_flags != RIBON_LUCA_ENTRY_FLAG_RLH1) {
        fixture_fail("entry-abi");
    }
    __asm__ __volatile__("csrr %0, satp" : "=r"(satp));
    __asm__ __volatile__("csrr %0, sstatus" : "=r"(sstatus));
    if (satp != 0u || (sstatus & FIXTURE_SSTATUS_SIE) != 0u) {
        fixture_fail("entry-state");
    }
    fixture_write("RIBON-RLH1-RISCV64-FIXTURE-MMU-OFF\r\n");

    fixture_validate_header(
        bytes,
        &total_size,
        &table_offset,
        &section_count);
    fixture_write("RIBON-RLH1-RISCV64-FIXTURE-RLH1-OK\r\n");
    boot_cpu_id = fixture_validate_sections(
        bytes,
        total_size,
        table_offset,
        section_count);
    if (boot_cpu_id != 0u) {
        fixture_fail("boot-hart-id");
    }
    fixture_write("RIBON-RLH1-RISCV64-FIXTURE-BOOT-CPU-OK\r\n");
    fixture_write("RIBON-RLH1-RISCV64-FIXTURE-OK\r\n");
    for (;;) {
        __asm__ __volatile__("wfi");
    }
}
