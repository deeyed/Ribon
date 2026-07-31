#include <Ribon/protocol/protocol.h>
#include <Ribon/protocols/os/freebsd/boot.h>
#include <Ribon/protocols/os/linux/boot.h>
#include <Ribon/protocols/os/zircon/zbi.h>

#include <stdio.h>

struct TestZbiHeader {
    uint32_t type;
    uint32_t length;
    uint32_t extra;
    uint32_t flags;
    uint32_t reserved0;
    uint32_t reserved1;
    uint32_t magic;
    uint32_t crc32;
};

static int descriptor_accepts_kernel(
    const struct RibonPluginDescriptor *descriptor,
    const char *protocol_id) {
    const struct RibonBootProtocol *protocol = descriptor->operations;
    const struct RibonComponentDescriptor component = {
        .role = RIBON_COMPONENT_ROLE_KERNEL,
        .name = "kernel",
        .size = 4096u,
    };
    const struct RibonManifestView manifest = {
        .protocol_id = protocol_id,
        .protocol_abi_min = 1u,
        .protocol_abi_max = 1u,
        .components = &component,
        .component_count = 1u,
    };
    return ribon_protocol_plugin_operations_are_valid(descriptor) &&
           protocol->ops->match(&manifest) == RIBON_PROTOCOL_STATUS_OK;
}

int main(void) {
    const struct TestZbiHeader zbi = {
        .type = 0x544f4f42u,
        .length = 0u,
        .magic = 0xb5781729u,
    };
    const struct RibonBootProtocol *freebsd =
        ribon_freebsd_protocol_plugin_descriptor.operations;
    const struct RibonBootProtocol *linux =
        ribon_linux_protocol_plugin_descriptor.operations;
    const struct RibonComponentDescriptor linux_components[] = {
        {
            .role = RIBON_COMPONENT_ROLE_KERNEL,
            .name = "kernel",
            .size = 4096u,
        },
        {
            .role = RIBON_COMPONENT_ROLE_BOOT_MODULE,
            .name = "initrd",
            .size = 4096u,
        },
    };
    const struct RibonManifestView linux_module_manifest = {
        .protocol_id = "linux",
        .protocol_abi_min = 1u,
        .protocol_abi_max = 1u,
        .components = linux_components,
        .component_count = 2u,
    };
    struct RibonHandoffArtifact handoff = {0};
    unsigned char buffer[64];

    if (!descriptor_accepts_kernel(
            &ribon_linux_protocol_plugin_descriptor,
            "linux") ||
        !descriptor_accepts_kernel(
            &ribon_freebsd_protocol_plugin_descriptor,
            "freebsd") ||
        !ribon_protocol_plugin_operations_are_valid(
            &ribon_zircon_protocol_plugin_descriptor) ||
        !ribon_zircon_zbi_is_valid(&zbi, sizeof(zbi)) ||
        (linux->expectations & RIBON_PROTOCOL_ALLOW_BOOT_MODULES) != 0u ||
        linux->ops->validate_components(&linux_module_manifest) !=
            RIBON_PROTOCOL_STATUS_BAD_COMPONENTS ||
        freebsd->ops->prepare_handoff(
            0, 0, 0, buffer, sizeof(buffer), &handoff) !=
            RIBON_PROTOCOL_HANDOFF_STATUS_UNSUPPORTED) {
        fputs("os_package_tests: OS package contract rejected\n", stderr);
        return 1;
    }
    puts("RIBON-SR01-OS-PACKAGES-OK");
    return 0;
}
