#include <Ribon/network/tftp.h>

#include <string.h>

/** @brief Network-order 16-bit field를 host integer로 읽는다. */
static uint16_t load_be16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] << 8u) | bytes[1];
}

/** @brief ASCII option token을 대소문자 구분 없이 blksize와 비교한다. */
static int is_blksize(const uint8_t *bytes, size_t size)
{
    static const char name[] = "blksize";
    size_t index;
    if (size != sizeof(name) - 1u) {
        return 0;
    }
    for (index = 0u; index < size; ++index) {
        uint8_t value = bytes[index];
        if (value >= 'A' && value <= 'Z') {
            value = (uint8_t)(value + ('a' - 'A'));
        }
        if (value != (uint8_t)name[index]) {
            return 0;
        }
    }
    return 1;
}

/** @brief Decimal block-size option을 checked 16-bit 값으로 읽는다. */
static int parse_block_size(const uint8_t *bytes, size_t size, uint16_t *value)
{
    uint32_t result = 0u;
    size_t index;
    if (size == 0u || size > 5u || value == NULL) {
        return 0;
    }
    for (index = 0u; index < size; ++index) {
        if (bytes[index] < '0' || bytes[index] > '9' ||
            result > (UINT16_MAX - (uint32_t)(bytes[index] - '0')) / 10u) {
            return 0;
        }
        result = result * 10u + (uint32_t)(bytes[index] - '0');
    }
    if (result < 512u || result > 1468u) {
        return 0;
    }
    *value = (uint16_t)result;
    return 1;
}

/** @brief Guard를 one-transfer, 16-bit block-number TFTP state로 초기화한다. */
int ribon_tftp_guard_initialize(
    struct RibonTftpGuard *guard,
    uint16_t requested_block_size,
    uint64_t maximum_bytes)
{
    if (guard == NULL || requested_block_size < 512u ||
        requested_block_size > 1468u || maximum_bytes == 0u) {
        return RIBON_TFTP_GUARD_STATUS_INVALID_ARGUMENT;
    }
    memset(guard, 0, sizeof(*guard));
    guard->size = sizeof(*guard);
    guard->abi_version = RIBON_TFTP_GUARD_ABI_VERSION;
    guard->block_size = requested_block_size;
    guard->expected_block = 1u;
    guard->maximum_bytes = maximum_bytes;
    return RIBON_TFTP_GUARD_STATUS_OK;
}

/** @brief OACK의 exact name/value pair 하나를 검증한다. */
static int accept_oack(
    struct RibonTftpGuard *guard,
    const uint8_t *packet,
    size_t packet_size)
{
    size_t name_end = 2u;
    size_t value_end;
    uint16_t negotiated;
    if (guard->options_complete || guard->expected_block != 1u || packet_size < 6u) {
        return RIBON_TFTP_GUARD_STATUS_MALFORMED;
    }
    while (name_end < packet_size && packet[name_end] != 0u) {
        ++name_end;
    }
    if (name_end == 2u || name_end == packet_size ||
        !is_blksize(packet + 2u, name_end - 2u)) {
        return RIBON_TFTP_GUARD_STATUS_UNSUPPORTED_OPTION;
    }
    value_end = name_end + 1u;
    while (value_end < packet_size && packet[value_end] != 0u) {
        ++value_end;
    }
    if (value_end == name_end + 1u || value_end + 1u != packet_size ||
        !parse_block_size(packet + name_end + 1u,
            value_end - name_end - 1u, &negotiated) ||
        negotiated > guard->block_size) {
        return RIBON_TFTP_GUARD_STATUS_MALFORMED;
    }
    guard->block_size = negotiated;
    guard->options_complete = 1u;
    ++guard->packet_count;
    return RIBON_TFTP_GUARD_STATUS_OK;
}

/** @brief 한 wire packet을 검증하고 새 DATA payload span을 빌려준다. */
int ribon_tftp_guard_accept(
    struct RibonTftpGuard *guard,
    const uint8_t *packet,
    size_t packet_size,
    const uint8_t **payload,
    size_t *payload_size)
{
    uint16_t opcode;
    uint16_t block;
    size_t data_size;
    if (payload != NULL) {
        *payload = NULL;
    }
    if (payload_size != NULL) {
        *payload_size = 0u;
    }
    if (guard == NULL || packet == NULL || payload == NULL ||
        payload_size == NULL || guard->size != sizeof(*guard) ||
        guard->abi_version != RIBON_TFTP_GUARD_ABI_VERSION ||
        guard->complete || packet_size < 2u || guard->packet_count >= 65535u) {
        return RIBON_TFTP_GUARD_STATUS_INVALID_ARGUMENT;
    }
    opcode = load_be16(packet);
    if (opcode == 6u) {
        return accept_oack(guard, packet, packet_size);
    }
    if (opcode == 5u) {
        return packet_size >= 5u ? RIBON_TFTP_GUARD_STATUS_REMOTE_ERROR :
            RIBON_TFTP_GUARD_STATUS_MALFORMED;
    }
    if (opcode != 3u || packet_size < 4u) {
        return RIBON_TFTP_GUARD_STATUS_MALFORMED;
    }
    block = load_be16(packet + 2u);
    data_size = packet_size - 4u;
    if (data_size > guard->block_size) {
        return RIBON_TFTP_GUARD_STATUS_MALFORMED;
    }
    if (guard->expected_block > 1u &&
        block == (uint16_t)(guard->expected_block - 1u)) {
        return RIBON_TFTP_GUARD_STATUS_DUPLICATE;
    }
    if (block != guard->expected_block || guard->expected_block == UINT16_MAX) {
        return RIBON_TFTP_GUARD_STATUS_OUT_OF_ORDER;
    }
    if ((uint64_t)data_size > guard->maximum_bytes - guard->received_bytes) {
        return RIBON_TFTP_GUARD_STATUS_CAPACITY;
    }
    guard->options_complete = 1u;
    guard->received_bytes += data_size;
    ++guard->expected_block;
    ++guard->packet_count;
    *payload = packet + 4u;
    *payload_size = data_size;
    if (data_size < guard->block_size) {
        guard->complete = 1u;
        return RIBON_TFTP_GUARD_STATUS_COMPLETE;
    }
    return RIBON_TFTP_GUARD_STATUS_OK;
}
