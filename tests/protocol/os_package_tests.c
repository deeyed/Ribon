#include <Ribon/protocol/protocol.h>
#include <Ribon/boot/plan.h>
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
            .name = "initramfs",
            .size = 4096u,
        },
        {
            .role = RIBON_COMPONENT_ROLE_DEVICE_TREE,
            .name = "platform.dtb",
            .size = 4096u,
        },
    };
    const struct RibonManifestView linux_module_manifest = {
        .protocol_id = "linux",
        .protocol_abi_min = 1u,
        .protocol_abi_max = 1u,
        .components = linux_components,
        .component_count = 3u,
    };
    const struct RibonArchDescriptor x86_64 = {
        .id = RIBON_ARCHITECTURE_X86_64,
        .pe_coff_machine = 0x8664u,
    };
    const struct RibonBootEnvironment uefi = {
        .kind = RIBON_ENVIRONMENT_UEFI,
    };
    const struct RibonBootPlan managed_plan = {
        .kernel_image = {
            .size = sizeof(struct RibonValidatedImage),
            .abi_version = RIBON_VALIDATED_IMAGE_ABI_VERSION,
            .format = RIBON_EXECUTABLE_FORMAT_PE_COFF,
            .machine = 0x8664u,
            .execution_support = RIBON_IMAGE_EXECUTION_FIRMWARE_MANAGED,
            .image_size = 4096u,
        },
    };
    struct RibonTerminalRequest terminal = {0};
    struct RibonTerminalRequest invalid_terminal;
    struct RibonBootPlan invalid_managed_plan;

    if (!descriptor_accepts_kernel(
            &ribon_linux_protocol_plugin_descriptor,
            "linux") ||
        !descriptor_accepts_kernel(
            &ribon_freebsd_protocol_plugin_descriptor,
            "freebsd") ||
        !ribon_protocol_plugin_operations_are_valid(
            &ribon_zircon_protocol_plugin_descriptor) ||
        !ribon_zircon_zbi_is_valid(&zbi, sizeof(zbi)) ||
        (linux->expectations & RIBON_PROTOCOL_ALLOW_BOOT_MODULES) == 0u ||
        linux->ops->validate_components(&linux_module_manifest) !=
            RIBON_PROTOCOL_STATUS_OK ||
        freebsd->terminal_execution !=
            RIBON_TERMINAL_EXECUTION_FIRMWARE_MANAGED_IMAGE ||
        freebsd->ops->prepare_handoff != 0 ||
        freebsd->ops->prepare_terminal(
            &x86_64, &managed_plan, &uefi, 0, &terminal) !=
            RIBON_PROTOCOL_STATUS_OK ||
        !ribon_terminal_request_is_valid(&terminal)) {
        fputs("os_package_tests: OS package contract rejected\n", stderr);
        return 1;
    }
    invalid_terminal = terminal;
    invalid_terminal.kind = (enum RibonTerminalExecutionKind)99;
    if (ribon_terminal_request_is_valid(&invalid_terminal)) {
        fputs("os_package_tests: unknown terminal kind accepted\n", stderr);
        return 1;
    }
    invalid_terminal = terminal;
    invalid_terminal.direct_entry.size = sizeof(invalid_terminal.direct_entry);
    invalid_terminal.direct_entry.abi_version = RIBON_ENTRY_INVOCATION_ABI_VERSION;
    invalid_terminal.direct_entry.entry_address = 1u;
    if (ribon_terminal_request_is_valid(&invalid_terminal)) {
        fputs("os_package_tests: managed terminal carried direct entry\n", stderr);
        return 1;
    }
    invalid_terminal = (struct RibonTerminalRequest){
        .size = sizeof(invalid_terminal),
        .abi_version = RIBON_TERMINAL_REQUEST_ABI_VERSION,
        .kind = RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY,
    };
    if (ribon_terminal_request_is_valid(&invalid_terminal)) {
        fputs("os_package_tests: direct terminal omitted entry\n", stderr);
        return 1;
    }
    invalid_managed_plan = managed_plan;
    invalid_managed_plan.kernel_image.machine = 0xaa64u;
    if (freebsd->ops->prepare_terminal(
            &x86_64, &invalid_managed_plan, &uefi, 0, &terminal) !=
        RIBON_PROTOCOL_STATUS_BAD_ENTRY_CONTRACT) {
        fputs("os_package_tests: managed machine mismatch accepted\n", stderr);
        return 1;
    }
    invalid_managed_plan = managed_plan;
    invalid_managed_plan.kernel_image.format = RIBON_EXECUTABLE_FORMAT_ELF64;
    if (freebsd->ops->prepare_terminal(
            &x86_64, &invalid_managed_plan, &uefi, 0, &terminal) !=
        RIBON_PROTOCOL_STATUS_BAD_ENTRY_CONTRACT) {
        fputs("os_package_tests: managed format mismatch accepted\n", stderr);
        return 1;
    }
    puts("RIBON-SR01-OS-PACKAGES-OK");
    return 0;
}
