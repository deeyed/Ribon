#include <Ribon/port/port.h>

#include <stdio.h>

extern const struct RibonServiceDescriptor
    ribon_port_diagnostic_sink_service_descriptor;
extern const struct RibonServiceDescriptor
    ribon_port_machine_description_service_descriptor;
extern const struct RibonServiceDescriptor
    ribon_port_payload_placement_service_descriptor;

int main(void) {
    const struct RibonPortDescriptor *port = ribon_port_selected();
    if (!ribon_port_descriptor_is_valid(port) ||
        port->diagnostic_sink !=
            &ribon_port_diagnostic_sink_service_descriptor ||
        port->machine_description !=
            &ribon_port_machine_description_service_descriptor ||
        port->payload_placement !=
            &ribon_port_payload_placement_service_descriptor ||
        port->diagnostic_sink->kind != RIBON_SERVICE_KIND_DIAGNOSTIC_SINK ||
        port->machine_description->kind !=
            RIBON_SERVICE_KIND_MACHINE_DESCRIPTION ||
        port->payload_placement->kind !=
            RIBON_SERVICE_KIND_PAYLOAD_PLACEMENT) {
        fputs("port_service_tests: split port authorities rejected\n", stderr);
        return 1;
    }
    puts("RIBON-SR01-PORT-SERVICES-OK");
    return 0;
}
