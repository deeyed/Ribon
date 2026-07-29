#include <Ribon/boot/plan.h>
#include <Ribon/plugin/descriptor.h>
#include <Ribon/protocols/os/parus/rph1.h>

#include <stdio.h>

static int check_entry_contract(
    const struct RibonBootProtocol *protocol,
    enum RibonArchitectureId architecture,
    enum RibonRegisterAbi expected_abi,
    enum RibonEntryTranslationRequirement expected_translation) {
    const unsigned char handoff_bytes[4] = {'R', 'P', 'H', '1'};
    const struct RibonArchDescriptor arch = {
        .size = sizeof(arch),
        .abi_version = RIBON_ARCH_OPS_ABI_VERSION,
        .id = architecture,
    };
    const struct RibonBootPlan plan = {
        .kernel_runtime_entry_address = 0x80400000u,
    };
    const struct RibonBootEnvironment environment = {0};
    const struct RibonHandoffArtifact handoff = {
        .data = handoff_bytes,
        .size = sizeof(handoff_bytes),
        .format = "rph1",
        .version_major = 1u,
    };
    struct RibonEntryInvocation invocation = {0};

    if (protocol->ops->prepare_entry_invocation(
            &arch,
            &plan,
            &environment,
            &handoff,
            &invocation) != RIBON_PROTOCOL_STATUS_OK ||
        invocation.entry_address != plan.kernel_runtime_entry_address ||
        invocation.register_abi != expected_abi ||
        invocation.argument_count != 2u ||
        invocation.arguments[0] != (uint64_t)(uintptr_t)handoff.data ||
        invocation.arguments[1] != RIBON_PARUS_ENTRY_FLAG_RPH1 ||
        invocation.interrupts != RIBON_ENTRY_INTERRUPTS_MASKED ||
        invocation.privilege != RIBON_ENTRY_PRIVILEGE_CURRENT_SUPERVISOR ||
        invocation.translation != expected_translation) {
        return 0;
    }
    return 1;
}

int main(void) {
    const struct RibonBootProtocol *protocol =
        (const struct RibonBootProtocol *)
            ribon_parus_protocol_plugin_descriptor.operations;

    if (protocol == 0 ||
        !check_entry_contract(
            protocol,
            RIBON_ARCHITECTURE_X86_64,
            RIBON_REGISTER_ABI_X86_64_RDI_RSI_RDX_RCX,
            RIBON_ENTRY_TRANSLATION_PRESERVE_REACHABLE) ||
        !check_entry_contract(
            protocol,
            RIBON_ARCHITECTURE_AARCH64,
            RIBON_REGISTER_ABI_AARCH64_X0_X1_X2_X3,
            RIBON_ENTRY_TRANSLATION_PRESERVE_REACHABLE) ||
        !check_entry_contract(
            protocol,
            RIBON_ARCHITECTURE_RISCV64,
            RIBON_REGISTER_ABI_RISCV64_A0_A1_A2_A3,
            RIBON_ENTRY_TRANSLATION_DISABLED)) {
        fputs("parus_entry_contract_tests: entry contract mismatch\n", stderr);
        return 1;
    }

    puts("RIBON-PARUS-ENTRY-CONTRACT-OK");
    return 0;
}
