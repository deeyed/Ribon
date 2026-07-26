#include <Ribon/firmware/services.h>
#include <Ribon/plugin/descriptor.h>

#define RIBON_SERVICE_CAP_ALL ((1ull << 11) - 1ull)

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
    return RIBON_SERVICE_STATUS_UNSUPPORTED;
}

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
    return RIBON_SERVICE_STATUS_UNSUPPORTED;
}

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
    return RIBON_SERVICE_STATUS_UNSUPPORTED;
}

static int unsupported_storage_flush(
    void *context,
    uint32_t slot,
    uint64_t deadline_ticks) {
    (void)context;
    (void)slot;
    (void)deadline_ticks;
    return RIBON_SERVICE_STATUS_UNSUPPORTED;
}

static int unsupported_timer_now(void *context, uint64_t *ticks_out) {
    (void)context;
    if (ticks_out != 0) {
        *ticks_out = 0u;
    }
    return RIBON_SERVICE_STATUS_UNSUPPORTED;
}

static int unsupported_watchdog_arm(void *context, uint64_t deadline_ticks) {
    (void)context;
    (void)deadline_ticks;
    return RIBON_SERVICE_STATUS_UNSUPPORTED;
}

static int unsupported_reset(void *context, uint32_t reason) {
    (void)context;
    (void)reason;
    return RIBON_SERVICE_STATUS_UNSUPPORTED;
}

static int unsupported_metadata_read(
    void *context,
    uint64_t offset,
    void *buffer,
    uint64_t size) {
    (void)context;
    (void)offset;
    (void)buffer;
    (void)size;
    return RIBON_SERVICE_STATUS_UNSUPPORTED;
}

static int unsupported_metadata_write(
    void *context,
    uint64_t offset,
    const void *buffer,
    uint64_t size) {
    (void)context;
    (void)offset;
    (void)buffer;
    (void)size;
    return RIBON_SERVICE_STATUS_UNSUPPORTED;
}

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
    return RIBON_SERVICE_STATUS_UNSUPPORTED;
}

static int unsupported_random_fill(void *context, void *buffer, uint64_t size) {
    (void)context;
    (void)buffer;
    (void)size;
    return RIBON_SERVICE_STATUS_UNSUPPORTED;
}

static int unsupported_diagnostic_write(
    void *context,
    const void *buffer,
    uint64_t size) {
    (void)context;
    (void)buffer;
    (void)size;
    return RIBON_SERVICE_STATUS_UNSUPPORTED;
}

/** @brief 모든 operation을 explicit unsupported callback으로 초기화한다. */
void ribon_service_table_init_unsupported(
    struct RibonServiceTable *services,
    void *context) {
    if (services == 0) {
        return;
    }
    *services = (struct RibonServiceTable){
        .size = sizeof(*services),
        .abi_version = RIBON_SERVICE_TABLE_ABI_VERSION,
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

/** @brief Service capability와 callback 존재 여부를 정확히 대조한다. */
int ribon_service_table_is_valid(const struct RibonServiceTable *services) {
    if (services == 0 ||
        services->size != sizeof(*services) ||
        services->abi_version != RIBON_SERVICE_TABLE_ABI_VERSION ||
        (services->capabilities & ~RIBON_SERVICE_CAP_ALL) != 0u ||
        services->boot_source_read == 0 ||
        services->inactive_slot_write == 0 ||
        services->inactive_slot_erase == 0 ||
        services->storage_flush == 0 ||
        services->timer_now == 0 ||
        services->watchdog_arm == 0 ||
        services->reset == 0 ||
        services->metadata_read == 0 ||
        services->metadata_write == 0 ||
        services->network_fetch == 0 ||
        services->random_fill == 0 ||
        services->diagnostic_write == 0) {
        return 0;
    }
    if ((services->capabilities & RIBON_CAP_MONOTONIC_TIMER) != 0u &&
        services->timer_frequency_hz == 0u) {
        return 0;
    }
#define RIBON_CALLBACK_MATCHES(capability, member, unsupported) \
    ((((services->capabilities & (capability)) != 0u) != \
      (services->member != (unsupported))) ? 0 : 1)
    return
        RIBON_CALLBACK_MATCHES(
            RIBON_CAP_BOOT_SOURCE_READ,
            boot_source_read,
            unsupported_boot_source_read) &&
        RIBON_CALLBACK_MATCHES(
            RIBON_CAP_INACTIVE_SLOT_WRITE,
            inactive_slot_write,
            unsupported_slot_write) &&
        RIBON_CALLBACK_MATCHES(
            RIBON_CAP_INACTIVE_SLOT_ERASE,
            inactive_slot_erase,
            unsupported_slot_erase) &&
        RIBON_CALLBACK_MATCHES(
            RIBON_CAP_STORAGE_FLUSH,
            storage_flush,
            unsupported_storage_flush) &&
        RIBON_CALLBACK_MATCHES(
            RIBON_CAP_MONOTONIC_TIMER,
            timer_now,
            unsupported_timer_now) &&
        RIBON_CALLBACK_MATCHES(
            RIBON_CAP_WATCHDOG,
            watchdog_arm,
            unsupported_watchdog_arm) &&
        RIBON_CALLBACK_MATCHES(RIBON_CAP_RESET, reset, unsupported_reset) &&
        RIBON_CALLBACK_MATCHES(
            RIBON_CAP_PERSISTENT_METADATA,
            metadata_read,
            unsupported_metadata_read) &&
        RIBON_CALLBACK_MATCHES(
            RIBON_CAP_PERSISTENT_METADATA,
            metadata_write,
            unsupported_metadata_write) &&
        RIBON_CALLBACK_MATCHES(
            RIBON_CAP_NETWORK_TRANSPORT,
            network_fetch,
            unsupported_network_fetch) &&
        RIBON_CALLBACK_MATCHES(
            RIBON_CAP_RANDOM_NONCE,
            random_fill,
            unsupported_random_fill) &&
        RIBON_CALLBACK_MATCHES(
            RIBON_CAP_DIAGNOSTIC_SINK,
            diagnostic_write,
            unsupported_diagnostic_write);
#undef RIBON_CALLBACK_MATCHES
}

/** @brief 요청 service capability를 모두 제공하는지 검사한다. */
int ribon_service_table_supports(
    const struct RibonServiceTable *services,
    uint64_t capabilities) {
    return ribon_service_table_is_valid(services) &&
           (capabilities & ~RIBON_SERVICE_CAP_ALL) == 0u &&
           (services->capabilities & capabilities) == capabilities;
}

/** @brief Environment plugin descriptor와 service table을 함께 검사한다. */
int ribon_environment_plugin_operations_are_valid(
    const struct RibonPluginDescriptor *descriptor) {
    const struct RibonServiceTable *services;
    if (descriptor == 0 ||
        descriptor->kind != RIBON_PLUGIN_KIND_ENVIRONMENT ||
        descriptor->operations_size != sizeof(struct RibonServiceTable) ||
        descriptor->operations_abi != RIBON_SERVICE_TABLE_ABI_VERSION ||
        (descriptor->provides & ~RIBON_SERVICE_CAP_ALL) != 0u) {
        return 0;
    }
    services = (const struct RibonServiceTable *)descriptor->operations;
    return ribon_service_table_is_valid(services) &&
           services->capabilities == descriptor->provides;
}
