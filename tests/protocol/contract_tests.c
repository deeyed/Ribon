#include <Ribon/arch/entry.h>
#include <Ribon/arch/ops.h>
#include <Ribon/plugin/descriptor.h>
#include <Ribon/protocol/protocol.h>

#include <stdio.h>

int main(void) {
    const struct RibonBootProtocol *protocol =
        (const struct RibonBootProtocol *)
            ribon_synthetic_protocol_plugin_descriptor.operations;
    struct RibonComponentDescriptor components[] = {
        {
            .role = RIBON_COMPONENT_ROLE_KERNEL,
            .name = "kernel",
            .size = 4096u,
        },
    };
    struct RibonManifestView manifest = {
        .protocol_id = "synthetic",
        .protocol_abi_min = 1u,
        .protocol_abi_max = 1u,
        .components = components,
        .component_count = 1u,
    };
    struct RibonEntryContract entry;
    struct RibonBootProtocol invalid;
    struct RibonBootProtocolOps invalid_ops;
    struct RibonPluginDescriptor invalid_plugin;

    if (!ribon_boot_protocol_is_valid(protocol) ||
        protocol->ops->match(&manifest) != RIBON_PROTOCOL_STATUS_OK ||
        protocol->ops->validate_components(&manifest) !=
            RIBON_PROTOCOL_STATUS_OK ||
        protocol->ops->select_entry_contract(
            ribon_arch_selected(),
            &entry) != RIBON_PROTOCOL_STATUS_OK ||
        entry.required_entry_flags != RIBON_KERNEL_ENTRY_FLAG_RPH1) {
        fputs("contract_tests: valid synthetic protocol failed\n", stderr);
        return 1;
    }

    invalid = *protocol;
    invalid_ops = *protocol->ops;
    invalid_ops.prepare_handoff = 0;
    invalid.ops = &invalid_ops;
    if (ribon_boot_protocol_is_valid(&invalid)) {
        fputs("contract_tests: missing handoff callback accepted\n", stderr);
        return 1;
    }

    invalid_plugin = ribon_synthetic_protocol_plugin_descriptor;
    invalid_plugin.provides &= ~RIBON_CAP_HANDOFF;
    if (ribon_plugin_descriptor_is_valid(&invalid_plugin)) {
        fputs("contract_tests: capability/callback mismatch accepted\n", stderr);
        return 1;
    }

    puts("RIBON-R3-PROTOCOL-CONTRACT-OK");
    return 0;
}
