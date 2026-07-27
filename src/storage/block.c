#include <Ribon/storage/block.h>

/** @brief Unsigned value가 power-of-two인지 검사한다. */
static int block_is_power_of_two(uint32_t value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}

/** @brief Read-only block provider ABI와 bounded geometry를 검사한다. */
int ribon_read_only_block_device_is_valid(const struct RibonReadOnlyBlockDevice *device) {
    return device != 0 && device->size == sizeof(*device) &&
           device->abi_version == RIBON_READ_ONLY_BLOCK_DEVICE_ABI_VERSION &&
           device->logical_block_bytes >= 512u &&
           device->logical_block_bytes <= 4096u &&
           block_is_power_of_two(device->logical_block_bytes) &&
           device->max_read_blocks != 0u && device->block_count != 0u &&
           device->context != 0 && device->read != 0;
}

/** @brief Block operation result를 stable diagnostic name으로 변환한다. */
const char *ribon_block_status_name(enum RibonBlockStatus status) {
    switch (status) {
    case RIBON_BLOCK_STATUS_OK:
        return "ok";
    case RIBON_BLOCK_STATUS_BAD_ARGUMENT:
        return "bad-argument";
    case RIBON_BLOCK_STATUS_OUT_OF_RANGE:
        return "out-of-range";
    case RIBON_BLOCK_STATUS_IO:
        return "io";
    case RIBON_BLOCK_STATUS_SHORT_READ:
        return "short-read";
    case RIBON_BLOCK_STATUS_UNSUPPORTED:
        return "unsupported";
    }
    return "unknown";
}
