#include <Ribon/port/port.h>

/** @brief Port tuple와 capability-specific service descriptor를 검사한다. */
int ribon_port_descriptor_is_valid(const struct RibonPortDescriptor *port) {
    if (port == 0 ||
        port->size != sizeof(*port) ||
        port->abi_version != RIBON_PORT_ABI_VERSION ||
        port->id == 0 ||
        port->id[0] == '\0' ||
        port->architecture < RIBON_ARCHITECTURE_X86_64 ||
        port->architecture > RIBON_ARCHITECTURE_RISCV64 ||
        port->environment < RIBON_ENVIRONMENT_HOST ||
        port->environment > RIBON_ENVIRONMENT_SBI ||
        port->timer_frequency_hz == 0u ||
        (port->machine_description != 0 &&
         !ribon_machine_description_service_operations_are_valid(
             port->machine_description)) ||
        (port->payload_placement != 0 &&
         !ribon_payload_placement_service_operations_are_valid(
             port->payload_placement))) {
        return 0;
    }
    return port->diagnostic_sink == 0 ||
           ribon_diagnostic_sink_service_operations_are_valid(
               port->diagnostic_sink);
}
