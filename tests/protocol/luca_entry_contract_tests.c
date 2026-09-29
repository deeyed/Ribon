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
    const uint64_t expected_flags = RIBON_LUCA_ENTRY_FLAG_RLH1 |
        (architecture == RIBON_ARCHITECTURE_AARCH64 ?
            RIBON_LUCA_ENTRY_FLAG_EL1_NORMALIZED : 0u);
    const enum RibonEntryPrivilegeRequirement expected_privilege =
        architecture == RIBON_ARCHITECTURE_AARCH64 ?
            RIBON_ENTRY_PRIVILEGE_AARCH64_EL1 :
            RIBON_ENTRY_PRIVILEGE_CURRENT_SUPERVISOR;

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
        terminal.direct_entry.arguments[1] != expected_flags ||
        terminal.direct_entry.interrupts != RIBON_ENTRY_INTERRUPTS_MASKED ||
        terminal.direct_entry.privilege != expected_privilege ||
        terminal.direct_entry.translation != expected_translation) {
        return 0;
    }
    return 1;
}

/** @brief x86_64 direct-high plan이 high entry와 4-register tuple을 만드는지 검사한다. */
static int check_x86_64_direct_high_contract(
    const struct RibonBootProtocol *protocol) {
    const unsigned char handoff_bytes[4] = {'R', 'L', 'H', '1'};
    const struct RibonArchDescriptor arch = {
        .size = sizeof(arch),
        .abi_version = RIBON_ARCH_OPS_ABI_VERSION,
        .id = RIBON_ARCHITECTURE_X86_64,
    };
    const struct RibonBootPlan plan = {
        .kernel_runtime_entry_address = UINT64_C(0x400000),
        .kernel_high_entry_virtual_address = UINT64_C(0xffffffff80200000),
        .kernel_transition_root_physical = UINT64_C(0x300000),
        .kernel_transition_bytes = UINT64_C(0xa000),
    };
    const struct RibonBootEnvironment environment = {0};
    const struct RibonHandoffArtifact handoff = {
        .data = handoff_bytes,
        .size = sizeof(handoff_bytes),
        .format = "rlh1",
        .version_major = 1u,
    };
    struct RibonTerminalRequest terminal = {0};
    const uint64_t expected_flags =
        RIBON_LUCA_ENTRY_FLAG_RLH1 |
        RIBON_LUCA_ENTRY_FLAG_ENTERED_HIGH |
        RIBON_LUCA_ENTRY_FLAG_DIRECT_HIGH;
    return protocol->ops->prepare_terminal(
               &arch, &plan, &environment, &handoff, &terminal) ==
               RIBON_PROTOCOL_STATUS_OK &&
           terminal.kind == RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY &&
           terminal.direct_entry.entry_address ==
               plan.kernel_high_entry_virtual_address &&
           terminal.direct_entry.argument_count == 4u &&
           terminal.direct_entry.arguments[0] ==
               (uint64_t)(uintptr_t)handoff.data &&
           terminal.direct_entry.arguments[1] == expected_flags &&
           terminal.direct_entry.arguments[2] ==
               plan.kernel_transition_root_physical &&
           terminal.direct_entry.arguments[3] ==
               plan.kernel_transition_bytes &&
           terminal.direct_entry.translation ==
               RIBON_ENTRY_TRANSLATION_DIRECT_HIGH_BRIDGE;
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
            RIBON_ENTRY_TRANSLATION_DISABLED) ||
        !check_x86_64_direct_high_contract(protocol)) {
        fputs("luca_entry_contract_tests: entry contract mismatch\n", stderr);
        return 1;
    }

    puts("RIBON-LUCA-ENTRY-CONTRACT-OK");
    return 0;
}
