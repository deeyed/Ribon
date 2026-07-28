#ifndef RIBON_PORT_ID
#error "RIBON_PORT_ID must be defined"
#endif
#ifndef RIBON_PORT_ARCHITECTURE
#error "RIBON_PORT_ARCHITECTURE must be defined"
#endif
#ifndef RIBON_PORT_ARCH_MASK
#error "RIBON_PORT_ARCH_MASK must be defined"
#endif
#ifndef RIBON_PORT_UART_BASE
#error "RIBON_PORT_UART_BASE must be defined"
#endif
#ifndef RIBON_PORT_UART_POLL_LIMIT
#error "RIBON_PORT_UART_POLL_LIMIT must be defined"
#endif
#ifndef RIBON_PORT_TIMER_FREQUENCY
#error "RIBON_PORT_TIMER_FREQUENCY must be defined"
#endif
#ifndef RIBON_PORT_NATIVE_INPUT_CAPACITY
#error "RIBON_PORT_NATIVE_INPUT_CAPACITY must be defined"
#endif
#ifndef RIBON_PORT_PAYLOAD_BASE
#error "RIBON_PORT_PAYLOAD_BASE must be defined"
#endif
#ifndef RIBON_PORT_PAYLOAD_SIZE
#error "RIBON_PORT_PAYLOAD_SIZE must be defined"
#endif

#include <Ribon/port/port.h>

#include "../../src/common/drivers/serial/pl011.h"

static struct RibonPl011 port_diagnostic_uart;

static int port_diagnostic_initialize(void *context) {
    if (context != &port_diagnostic_uart) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    return ribon_pl011_initialize(
               &port_diagnostic_uart,
               RIBON_PORT_UART_BASE,
               RIBON_PORT_UART_POLL_LIMIT) == 0 ?
        RIBON_SERVICE_STATUS_OK :
        RIBON_SERVICE_STATUS_IO;
}

static int port_diagnostic_write(
    void *context,
    const void *data,
    uint64_t byte_count) {
    const uint8_t *bytes = data;
    if (context != &port_diagnostic_uart ||
        (byte_count != 0u && bytes == 0)) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    for (uint64_t index = 0u; index < byte_count; ++index) {
        if (ribon_pl011_write_byte(&port_diagnostic_uart, bytes[index]) != 0) {
            return RIBON_SERVICE_STATUS_IO;
        }
    }
    return RIBON_SERVICE_STATUS_OK;
}

static const struct RibonDiagnosticSinkServiceOperations
    port_diagnostic_operations = {
        .size = sizeof(port_diagnostic_operations),
        .abi_version = RIBON_SERVICE_ABI_VERSION,
        .context = &port_diagnostic_uart,
        .initialize = port_diagnostic_initialize,
        .write = port_diagnostic_write,
    };

static const struct RibonMachineDescriptionServiceOperations
    port_machine_description_operations = {
        .size = sizeof(port_machine_description_operations),
        .abi_version = RIBON_SERVICE_ABI_VERSION,
        .machine_id = RIBON_PORT_ID,
        .native_input_capacity = RIBON_PORT_NATIVE_INPUT_CAPACITY,
    };

static const struct RibonPayloadPlacementServiceOperations
    port_payload_placement_operations = {
        .size = sizeof(port_payload_placement_operations),
        .abi_version = RIBON_SERVICE_ABI_VERSION,
        .physical_base = RIBON_PORT_PAYLOAD_BASE,
        .physical_size = RIBON_PORT_PAYLOAD_SIZE,
    };

#define RIBON_PORT_SERVICE_FIELDS(kind_value, id_value, cap_value, operations_value, validator_value) \
    .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC, \
    .size = sizeof(struct RibonServiceDescriptor), \
    .abi_version = RIBON_SERVICE_ABI_VERSION, \
    .kind = (kind_value), \
    .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY, \
    .lifetime = RIBON_SERVICE_LIFETIME_BOOT, \
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION, \
    .id = (id_value), \
    .provides = (cap_value), \
    .architecture_mask = RIBON_PORT_ARCH_MASK, \
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
        RIBON_PORT_SERVICE_FIELDS(
            RIBON_SERVICE_KIND_DIAGNOSTIC_SINK,
            "service.port.diagnostic-sink",
            RIBON_CAP_DIAGNOSTIC_SINK,
            port_diagnostic_operations,
            ribon_diagnostic_sink_service_operations_are_valid),
    };

const struct RibonServiceDescriptor
    ribon_port_machine_description_service_descriptor = {
        RIBON_PORT_SERVICE_FIELDS(
            RIBON_SERVICE_KIND_MACHINE_DESCRIPTION,
            "service.port.machine-description",
            RIBON_CAP_MACHINE_DESCRIPTION,
            port_machine_description_operations,
            ribon_machine_description_service_operations_are_valid),
    };

const struct RibonServiceDescriptor
    ribon_port_payload_placement_service_descriptor = {
        RIBON_PORT_SERVICE_FIELDS(
            RIBON_SERVICE_KIND_PAYLOAD_PLACEMENT,
            "service.port.payload-placement",
            RIBON_CAP_PAYLOAD_PLACEMENT,
            port_payload_placement_operations,
            ribon_payload_placement_service_operations_are_valid),
    };

static const struct RibonPortDescriptor selected_port = {
    .size = sizeof(selected_port),
    .abi_version = RIBON_PORT_ABI_VERSION,
    .id = RIBON_PORT_ID,
    .architecture = RIBON_PORT_ARCHITECTURE,
    .environment = RIBON_ENVIRONMENT_RAW_FDT,
    .timer_frequency_hz = RIBON_PORT_TIMER_FREQUENCY,
    .diagnostic_sink = &ribon_port_diagnostic_sink_service_descriptor,
    .machine_description = &ribon_port_machine_description_service_descriptor,
    .payload_placement = &ribon_port_payload_placement_service_descriptor,
};

const struct RibonPortDescriptor *ribon_port_selected(void) {
    return &selected_port;
}

#undef RIBON_PORT_SERVICE_FIELDS
