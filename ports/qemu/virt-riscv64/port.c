#include <Ribon/port/port.h>

#define RIBON_RISCV64_UART_BASE 0x10000000ull
#define RIBON_RISCV64_UART_THR 0u
#define RIBON_RISCV64_UART_LSR 5u
#define RIBON_RISCV64_UART_THR_EMPTY 0x20u
#define RIBON_RISCV64_POLL_LIMIT 1000000u

static volatile uint8_t *port_uart;

static int port_diagnostic_initialize(void *context) {
    if (context != &port_uart) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    port_uart = (volatile uint8_t *)(uintptr_t)RIBON_RISCV64_UART_BASE;
    return RIBON_SERVICE_STATUS_OK;
}

static int port_diagnostic_write(
    void *context,
    const void *data,
    uint64_t byte_count) {
    const uint8_t *bytes = data;
    if (context != &port_uart || port_uart == 0 ||
        (byte_count != 0u && bytes == 0)) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    for (uint64_t index = 0u; index < byte_count; ++index) {
        uint32_t poll = 0u;
        while ((port_uart[RIBON_RISCV64_UART_LSR] &
                RIBON_RISCV64_UART_THR_EMPTY) == 0u &&
               poll < RIBON_RISCV64_POLL_LIMIT) {
            ++poll;
        }
        if (poll == RIBON_RISCV64_POLL_LIMIT) {
            return RIBON_SERVICE_STATUS_TIMEOUT;
        }
        port_uart[RIBON_RISCV64_UART_THR] = bytes[index];
    }
    return RIBON_SERVICE_STATUS_OK;
}

static const struct RibonDiagnosticSinkServiceOperations diagnostic_operations = {
    .size = sizeof(diagnostic_operations),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .context = &port_uart,
    .initialize = port_diagnostic_initialize,
    .write = port_diagnostic_write,
};

static const struct RibonMachineDescriptionServiceOperations
    machine_description_operations = {
        .size = sizeof(machine_description_operations),
        .abi_version = RIBON_SERVICE_ABI_VERSION,
        .machine_id = "qemu-virt-riscv64",
        .native_input_capacity = 2ull * 1024ull * 1024ull,
    };

static const struct RibonPayloadPlacementServiceOperations placement_operations = {
    .size = sizeof(placement_operations),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .physical_base = 0x80400000ull,
    .physical_size = 32ull * 1024ull * 1024ull,
};

#define RIBON_RISCV64_SERVICE_FIELDS(kind_value, id_value, cap_value, operations_value, validator_value) \
    .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC, \
    .size = sizeof(struct RibonServiceDescriptor), \
    .abi_version = RIBON_SERVICE_ABI_VERSION, \
    .kind = (kind_value), \
    .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY, \
    .lifetime = RIBON_SERVICE_LIFETIME_BOOT, \
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION, \
    .id = (id_value), \
    .provides = (cap_value), \
    .architecture_mask = RIBON_ARCH_MASK_RISCV64, \
    .environment_mask = RIBON_ENV_MASK_RAW_FDT, \
    .mode_mask = RIBON_MODE_MASK_ALL, \
    .arena_budget = 1u, \
    .input_budget = 4096u, \
    .output_budget = 4096u, \
    .deadline_ms = 30000u, \
    .operations = &(operations_value), \
    .operations_size = sizeof(operations_value), \
    .operations_abi = RIBON_SERVICE_ABI_VERSION, \
    .validate_operations = (validator_value)

const struct RibonServiceDescriptor
    ribon_port_diagnostic_sink_service_descriptor = {
        RIBON_RISCV64_SERVICE_FIELDS(
            RIBON_SERVICE_KIND_DIAGNOSTIC_SINK,
            "service.port.diagnostic-sink",
            RIBON_CAP_DIAGNOSTIC_SINK,
            diagnostic_operations,
            ribon_diagnostic_sink_service_operations_are_valid),
    };

const struct RibonServiceDescriptor
    ribon_port_machine_description_service_descriptor = {
        RIBON_RISCV64_SERVICE_FIELDS(
            RIBON_SERVICE_KIND_MACHINE_DESCRIPTION,
            "service.port.machine-description",
            RIBON_CAP_MACHINE_DESCRIPTION,
            machine_description_operations,
            ribon_machine_description_service_operations_are_valid),
    };

const struct RibonServiceDescriptor
    ribon_port_payload_placement_service_descriptor = {
        RIBON_RISCV64_SERVICE_FIELDS(
            RIBON_SERVICE_KIND_PAYLOAD_PLACEMENT,
            "service.port.payload-placement",
            RIBON_CAP_PAYLOAD_PLACEMENT,
            placement_operations,
            ribon_payload_placement_service_operations_are_valid),
    };

static const struct RibonPortDescriptor selected_port = {
    .size = sizeof(selected_port),
    .abi_version = RIBON_PORT_ABI_VERSION,
    .id = "qemu-virt-riscv64",
    .architecture = RIBON_ARCHITECTURE_RISCV64,
    .environment = RIBON_ENVIRONMENT_RAW_FDT,
    .timer_frequency_hz = 10000000u,
    .diagnostic_sink = &ribon_port_diagnostic_sink_service_descriptor,
    .machine_description = &ribon_port_machine_description_service_descriptor,
    .payload_placement = &ribon_port_payload_placement_service_descriptor,
};

const struct RibonPortDescriptor *ribon_port_selected(void) {
    return &selected_port;
}

#undef RIBON_RISCV64_SERVICE_FIELDS
