#include <Ribon/arch/x86_64/io.h>
#include <Ribon/port/port.h>

#define RIBON_PC_COM1_BASE 0x3f8u
#define RIBON_PC_COM1_POLL_LIMIT 1000000u

static int diagnostic_ready;

static int pc_diagnostic_initialize(void *context) {
    if (context != &diagnostic_ready) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    ribon_x86_64_out8(RIBON_PC_COM1_BASE + 1u, 0x00u);
    ribon_x86_64_out8(RIBON_PC_COM1_BASE + 3u, 0x80u);
    ribon_x86_64_out8(RIBON_PC_COM1_BASE + 0u, 0x01u);
    ribon_x86_64_out8(RIBON_PC_COM1_BASE + 1u, 0x00u);
    ribon_x86_64_out8(RIBON_PC_COM1_BASE + 3u, 0x03u);
    ribon_x86_64_out8(RIBON_PC_COM1_BASE + 2u, 0xc7u);
    ribon_x86_64_out8(RIBON_PC_COM1_BASE + 4u, 0x0bu);
    diagnostic_ready = 1;
    return RIBON_SERVICE_STATUS_OK;
}

static int pc_diagnostic_write(
    void *context,
    const void *data,
    uint64_t byte_count) {
    const uint8_t *bytes = data;
    if (context != &diagnostic_ready || !diagnostic_ready ||
        (byte_count != 0u && bytes == 0)) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    for (uint64_t index = 0u; index < byte_count; ++index) {
        uint32_t poll;
        for (poll = 0u; poll < RIBON_PC_COM1_POLL_LIMIT; ++poll) {
            if ((ribon_x86_64_in8(RIBON_PC_COM1_BASE + 5u) & 0x20u) != 0u) {
                ribon_x86_64_out8(RIBON_PC_COM1_BASE, bytes[index]);
                break;
            }
        }
        if (poll == RIBON_PC_COM1_POLL_LIMIT) {
            return RIBON_SERVICE_STATUS_TIMEOUT;
        }
    }
    return RIBON_SERVICE_STATUS_OK;
}

static const struct RibonDiagnosticSinkServiceOperations diagnostic_operations = {
    .size = sizeof(diagnostic_operations),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .context = &diagnostic_ready,
    .initialize = pc_diagnostic_initialize,
    .write = pc_diagnostic_write,
};

const struct RibonServiceDescriptor
    ribon_port_diagnostic_sink_service_descriptor = {
        .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC,
        .size = sizeof(ribon_port_diagnostic_sink_service_descriptor),
        .abi_version = RIBON_SERVICE_ABI_VERSION,
        .kind = RIBON_SERVICE_KIND_DIAGNOSTIC_SINK,
        .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY,
        .lifetime = RIBON_SERVICE_LIFETIME_PERSISTENT,
        .phase = RIBON_PLUGIN_PHASE_EARLY,
        .id = "service.port.diagnostic-sink",
        .provides = RIBON_CAP_DIAGNOSTIC_SINK,
        .architecture_mask = RIBON_ARCH_MASK_X86_64,
        .environment_mask = RIBON_ENV_MASK_UEFI,
        .mode_mask = RIBON_MODE_MASK_ALL,
        .arena_budget = 1u,
        .input_budget = 4096u,
        .output_budget = 4096u,
        .deadline_ms = 30000u,
        .operations = &diagnostic_operations,
        .operations_size = sizeof(diagnostic_operations),
        .operations_abi = RIBON_SERVICE_ABI_VERSION,
        .validate_operations =
            ribon_diagnostic_sink_service_operations_are_valid,
    };

static const struct RibonPortDescriptor selected_port = {
    .size = sizeof(selected_port),
    .abi_version = RIBON_PORT_ABI_VERSION,
    .id = "qemu-pc-x86_64",
    .architecture = RIBON_ARCHITECTURE_X86_64,
    .environment = RIBON_ENVIRONMENT_UEFI,
    .timer_frequency_hz = 1000000000u,
    .diagnostic_sink = &ribon_port_diagnostic_sink_service_descriptor,
};

const struct RibonPortDescriptor *ribon_port_selected(void) {
    return &selected_port;
}
