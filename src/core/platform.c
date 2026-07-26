#include <Ribon/platform.h>

/** @brief 지원하지 않는 boot source read를 명시적으로 거절한다. */
static int unsupported_boot_source_read(
    void *context,
    const struct RibonBootSource *source,
    uint64_t offset,
    void *buffer,
    uint64_t size,
    uint64_t deadline_ticks) {
    (void)context;
    (void)source;
    (void)offset;
    (void)buffer;
    (void)size;
    (void)deadline_ticks;
    return RIBON_PLATFORM_STATUS_UNSUPPORTED;
}

/** @brief 지원하지 않는 inactive slot write를 명시적으로 거절한다. */
static int unsupported_slot_write(
    void *context,
    uint32_t slot,
    uint64_t offset,
    const void *buffer,
    uint64_t size,
    uint64_t deadline_ticks) {
    (void)context;
    (void)slot;
    (void)offset;
    (void)buffer;
    (void)size;
    (void)deadline_ticks;
    return RIBON_PLATFORM_STATUS_UNSUPPORTED;
}

/** @brief 지원하지 않는 inactive slot erase를 명시적으로 거절한다. */
static int unsupported_slot_erase(
    void *context,
    uint32_t slot,
    uint64_t offset,
    uint64_t size,
    uint64_t deadline_ticks) {
    (void)context;
    (void)slot;
    (void)offset;
    (void)size;
    (void)deadline_ticks;
    return RIBON_PLATFORM_STATUS_UNSUPPORTED;
}

/** @brief 지원하지 않는 durable flush를 명시적으로 거절한다. */
static int unsupported_storage_flush(
    void *context,
    uint32_t slot,
    uint64_t deadline_ticks) {
    (void)context;
    (void)slot;
    (void)deadline_ticks;
    return RIBON_PLATFORM_STATUS_UNSUPPORTED;
}

/** @brief 지원하지 않는 monotonic timer read를 명시적으로 거절한다. */
static int unsupported_timer_now(void *context, uint64_t *ticks_out) {
    (void)context;
    if (ticks_out != 0) {
        *ticks_out = 0u;
    }
    return RIBON_PLATFORM_STATUS_UNSUPPORTED;
}

/** @brief 지원하지 않는 watchdog arm을 명시적으로 거절한다. */
static int unsupported_watchdog_arm(void *context, uint64_t deadline_ticks) {
    (void)context;
    (void)deadline_ticks;
    return RIBON_PLATFORM_STATUS_UNSUPPORTED;
}

/** @brief 지원하지 않는 reset을 명시적으로 거절한다. */
static int unsupported_reset(void *context, uint32_t reason) {
    (void)context;
    (void)reason;
    return RIBON_PLATFORM_STATUS_UNSUPPORTED;
}

/** @brief 지원하지 않는 metadata read를 명시적으로 거절한다. */
static int unsupported_metadata_read(
    void *context,
    uint64_t offset,
    void *buffer,
    uint64_t size) {
    (void)context;
    (void)offset;
    (void)buffer;
    (void)size;
    return RIBON_PLATFORM_STATUS_UNSUPPORTED;
}

/** @brief 지원하지 않는 metadata write를 명시적으로 거절한다. */
static int unsupported_metadata_write(
    void *context,
    uint64_t offset,
    const void *buffer,
    uint64_t size) {
    (void)context;
    (void)offset;
    (void)buffer;
    (void)size;
    return RIBON_PLATFORM_STATUS_UNSUPPORTED;
}

/** @brief 지원하지 않는 network fetch를 명시적으로 거절한다. */
static int unsupported_network_fetch(
    void *context,
    const struct RibonBootSource *source,
    uint64_t offset,
    void *buffer,
    uint64_t capacity,
    uint64_t *received_out,
    uint64_t deadline_ticks) {
    (void)context;
    (void)source;
    (void)offset;
    (void)buffer;
    (void)capacity;
    (void)deadline_ticks;
    if (received_out != 0) {
        *received_out = 0u;
    }
    return RIBON_PLATFORM_STATUS_UNSUPPORTED;
}

/** @brief 지원하지 않는 random fill을 명시적으로 거절한다. */
static int unsupported_random_fill(void *context, void *buffer, uint64_t size) {
    (void)context;
    (void)buffer;
    (void)size;
    return RIBON_PLATFORM_STATUS_UNSUPPORTED;
}

/** @brief 지원하지 않는 diagnostic write를 명시적으로 거절한다. */
static int unsupported_diagnostic_write(
    void *context,
    const void *buffer,
    uint64_t size) {
    (void)context;
    (void)buffer;
    (void)size;
    return RIBON_PLATFORM_STATUS_UNSUPPORTED;
}

/** @brief 모든 capability가 명시적으로 unsupported인 operation table을 만든다. */
void ribon_platform_ops_init_unsupported(
    struct RibonPlatformOps *ops,
    enum RibonFirmwareKind firmware,
    const char *platform_name,
    void *context) {
    if (ops == 0) {
        return;
    }
    *ops = (struct RibonPlatformOps){
        .abi_version = RIBON_PLATFORM_OPS_ABI_VERSION,
        .facts = {
            .firmware = firmware,
            .platform_name = platform_name,
            .timer_frequency_hz = 0u,
            .reset_reason = 0u,
            .capabilities = 0u,
        },
        .capabilities = {
            .supported = 0u,
            .unsupported = RIBON_PLATFORM_CAP_ALL,
        },
        .context = context,
        .boot_source_read = unsupported_boot_source_read,
        .inactive_slot_write = unsupported_slot_write,
        .inactive_slot_erase = unsupported_slot_erase,
        .storage_flush = unsupported_storage_flush,
        .timer_now = unsupported_timer_now,
        .watchdog_arm = unsupported_watchdog_arm,
        .reset = unsupported_reset,
        .metadata_read = unsupported_metadata_read,
        .metadata_write = unsupported_metadata_write,
        .network_fetch = unsupported_network_fetch,
        .random_fill = unsupported_random_fill,
        .diagnostic_write = unsupported_diagnostic_write,
    };
}

/** @brief Platform operation table의 ABI, bit 분류, callback 완전성을 검사한다. */
int ribon_platform_ops_are_valid(const struct RibonPlatformOps *ops) {
    uint64_t classified;

    if (ops == 0 ||
        ops->abi_version != RIBON_PLATFORM_OPS_ABI_VERSION ||
        ops->facts.firmware < RIBON_FIRMWARE_HOST ||
        ops->facts.firmware > RIBON_FIRMWARE_RASPBERRY_PI ||
        ops->facts.platform_name == 0) {
        return 0;
    }
    if ((ops->capabilities.supported & ops->capabilities.unsupported) != 0u) {
        return 0;
    }
    classified = ops->capabilities.supported | ops->capabilities.unsupported;
    if (classified != RIBON_PLATFORM_CAP_ALL ||
        ops->facts.capabilities != ops->capabilities.supported) {
        return 0;
    }
    if ((ops->capabilities.supported & RIBON_PLATFORM_CAP_MONOTONIC_TIMER) != 0u &&
        ops->facts.timer_frequency_hz == 0u) {
        return 0;
    }
    if (((ops->capabilities.supported & RIBON_PLATFORM_CAP_BOOT_SOURCE_READ) != 0u) ==
        (ops->boot_source_read == unsupported_boot_source_read) ||
        ((ops->capabilities.supported & RIBON_PLATFORM_CAP_INACTIVE_SLOT_WRITE) != 0u) ==
        (ops->inactive_slot_write == unsupported_slot_write) ||
        ((ops->capabilities.supported & RIBON_PLATFORM_CAP_INACTIVE_SLOT_ERASE) != 0u) ==
        (ops->inactive_slot_erase == unsupported_slot_erase) ||
        ((ops->capabilities.supported & RIBON_PLATFORM_CAP_STORAGE_FLUSH) != 0u) ==
        (ops->storage_flush == unsupported_storage_flush) ||
        ((ops->capabilities.supported & RIBON_PLATFORM_CAP_MONOTONIC_TIMER) != 0u) ==
        (ops->timer_now == unsupported_timer_now) ||
        ((ops->capabilities.supported & RIBON_PLATFORM_CAP_WATCHDOG) != 0u) ==
        (ops->watchdog_arm == unsupported_watchdog_arm) ||
        ((ops->capabilities.supported & RIBON_PLATFORM_CAP_RESET) != 0u) ==
        (ops->reset == unsupported_reset) ||
        ((ops->capabilities.supported & RIBON_PLATFORM_CAP_PERSISTENT_METADATA) != 0u) ==
        (ops->metadata_read == unsupported_metadata_read) ||
        ((ops->capabilities.supported & RIBON_PLATFORM_CAP_PERSISTENT_METADATA) != 0u) ==
        (ops->metadata_write == unsupported_metadata_write) ||
        ((ops->capabilities.supported & RIBON_PLATFORM_CAP_NETWORK_TRANSPORT) != 0u) ==
        (ops->network_fetch == unsupported_network_fetch) ||
        ((ops->capabilities.supported & RIBON_PLATFORM_CAP_RANDOM_NONCE) != 0u) ==
        (ops->random_fill == unsupported_random_fill) ||
        ((ops->capabilities.supported & RIBON_PLATFORM_CAP_DIAGNOSTIC_SINK) != 0u) ==
        (ops->diagnostic_write == unsupported_diagnostic_write)) {
        return 0;
    }
    return ops->boot_source_read != 0 &&
           ops->inactive_slot_write != 0 &&
           ops->inactive_slot_erase != 0 &&
           ops->storage_flush != 0 &&
           ops->timer_now != 0 &&
           ops->watchdog_arm != 0 &&
           ops->reset != 0 &&
           ops->metadata_read != 0 &&
           ops->metadata_write != 0 &&
           ops->network_fetch != 0 &&
           ops->random_fill != 0 &&
           ops->diagnostic_write != 0;
}

/** @brief 요청한 capability를 모두 지원하는지 검사한다. */
int ribon_platform_ops_supports(
    const struct RibonPlatformOps *ops,
    uint64_t capabilities) {
    if (!ribon_platform_ops_are_valid(ops) ||
        (capabilities & ~RIBON_PLATFORM_CAP_ALL) != 0u) {
        return 0;
    }
    return (ops->capabilities.supported & capabilities) == capabilities;
}
