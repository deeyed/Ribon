#include <Ribon/boot/plan.h>
#include <Ribon/plugin/descriptor.h>
#include <Ribon/protocols/os/luca/rlh1.h>

#include <stdio.h>

static int check_entry_contract(
    const struct RibonBootProtocol *protocol,
    enum RibonArchitectureId architecture,
    enum RibonRegisterAbi expected_abi,
    enum RibonEntryTranslationRequirement expected_translation) {
    const unsigned char handoff_bytes[4] = {'R', 'L', 'H', '1'};
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
        .format = "rlh1",
        .version_major = 1u,
    };
    struct RibonTerminalRequest terminal = {0};

    if (protocol->ops->prepare_terminal(
            &arch,
            &plan,
            &environment,
            &handoff,
            &terminal) != RIBON_PROTOCOL_STATUS_OK ||
        terminal.kind != RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY ||
        terminal.direct_entry.entry_address != plan.kernel_runtime_entry_address ||
        terminal.direct_entry.register_abi != expected_abi ||
        terminal.direct_entry.argument_count != 2u ||
        terminal.direct_entry.arguments[0] != (uint64_t)(uintptr_t)handoff.data ||
        terminal.direct_entry.arguments[1] != RIBON_LUCA_ENTRY_FLAG_RLH1 ||
        terminal.direct_entry.interrupts != RIBON_ENTRY_INTERRUPTS_MASKED ||
        terminal.direct_entry.privilege != RIBON_ENTRY_PRIVILEGE_CURRENT_SUPERVISOR ||
        terminal.direct_entry.translation != expected_translation) {
        return 0;
    }
    return 1;
}

int main(void) {
    const struct RibonBootProtocol *protocol =
        (const struct RibonBootProtocol *)
            ribon_luca_protocol_plugin_descriptor.operations;

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
        fputs("luca_entry_contract_tests: entry contract mismatch\n", stderr);
        return 1;
    }

    puts("RIBON-LUCA-ENTRY-CONTRACT-OK");
    return 0;
}
