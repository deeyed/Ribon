#include <Ribon/arch/ops.h>
#include <Ribon/boot/image.h>

#include <stdio.h>
#include <string.h>

static int expected_arch(
    const char *name,
    uint16_t *machine_out,
    uint64_t *capabilities_out,
    enum RibonRegisterAbi *register_abi_out,
    enum RibonEntryTranslationRequirement *translation_out) {
    *capabilities_out =
        RIBON_ARCH_CAP_VALIDATE_DIRECT_LOAD |
        RIBON_ARCH_CAP_CACHE_SYNC |
        RIBON_ARCH_CAP_HALT |
        RIBON_ARCH_CAP_MONOTONIC_COUNTER;
    if (strcmp(name, "x86_64") == 0) {
        *machine_out = 62u;
        *register_abi_out = RIBON_REGISTER_ABI_X86_64_RDI_RSI_RDX_RCX;
        *translation_out = RIBON_ENTRY_TRANSLATION_PRESERVE_REACHABLE;
        *capabilities_out |=
            RIBON_ARCH_CAP_DIRECT_HIGH_ENTRY |
            RIBON_ARCH_CAP_ENTRY_BRIDGE;
        return 1;
    }
    if (strcmp(name, "aarch64") == 0) {
        *machine_out = 183u;
        *register_abi_out = RIBON_REGISTER_ABI_AARCH64_X0_X1_X2_X3;
        *translation_out = RIBON_ENTRY_TRANSLATION_PRESERVE_REACHABLE;
        *capabilities_out |=
            RIBON_ARCH_CAP_DIRECT_HIGH_ENTRY |
            RIBON_ARCH_CAP_ENTRY_BRIDGE |
            RIBON_ARCH_CAP_POST_EXIT_IDENTITY;
        return 1;
    }
    if (strcmp(name, "riscv64") == 0) {
        *machine_out = 243u;
        *register_abi_out = RIBON_REGISTER_ABI_RISCV64_A0_A1_A2_A3;
        *translation_out = RIBON_ENTRY_TRANSLATION_DISABLED;
        *capabilities_out |= RIBON_ARCH_CAP_ENTRY_BRIDGE;
        return 1;
    }
    return 0;
}

int main(void) {
    const struct RibonArchOps *ops = ribon_arch_selected_ops();
    uint16_t expected_machine = 0u;
    uint64_t expected_capabilities = 0u;
    enum RibonRegisterAbi register_abi =
        RIBON_REGISTER_ABI_X86_64_RDI_RSI_RDX_RCX;
    enum RibonEntryTranslationRequirement translation =
        RIBON_ENTRY_TRANSLATION_PRESERVE_REACHABLE;
    struct RibonLoadSegment segment = {
        .file_size = 0x1000u,
        .memory_size = 0x1000u,
        .virtual_address = 0x200000u,
        .flags = RIBON_LOAD_SEGMENT_EXECUTE,
    };
    struct RibonDirectLoadPlan payload = {
        .size = sizeof(payload),
        .abi_version = RIBON_DIRECT_LOAD_PLAN_ABI_VERSION,
        .segment_count = 1u,
        .entry_point = 0x200078u,
        .segments = &segment,
        .segment_capacity = 1u,
    };
    struct RibonValidatedImage validated = {
        .size = sizeof(validated),
        .abi_version = RIBON_VALIDATED_IMAGE_ABI_VERSION,
        .format = RIBON_EXECUTABLE_FORMAT_ELF64,
        .execution_support = RIBON_IMAGE_EXECUTION_DIRECT_ENTRY,
        .image_size = 4096u,
    };
    struct RibonEntryInvocation invocation = {
        .size = sizeof(invocation),
        .abi_version = RIBON_ENTRY_INVOCATION_ABI_VERSION,
        .entry_address = 0x200078u,
        .argument_count = 2u,
        .arguments = {0x1000u, 1u},
        .interrupts = RIBON_ENTRY_INTERRUPTS_MASKED,
        .privilege = RIBON_ENTRY_PRIVILEGE_CURRENT_SUPERVISOR,
    };
    struct RibonPreparedEntry prepared;
    static _Alignas(4096) uint64_t post_exit_identity_tables[1024];
    uint64_t post_exit_identity_root = 0u;

    if (ops == 0 ||
        !ribon_arch_ops_are_valid(ops) ||
        ops->descriptor != ribon_arch_selected() ||
        !expected_arch(
            ops->descriptor->canonical_name,
            &expected_machine,
            &expected_capabilities,
            &register_abi,
            &translation) ||
        ops->capabilities != expected_capabilities) {
        fputs("arch_ops_tests: operation table mismatch\n", stderr);
        return 1;
    }
    validated.machine = expected_machine;
    if (ops->validate_direct_load(ops->descriptor, &validated, &payload) !=
        RIBON_ARCH_OPERATION_OK) {
        fputs("arch_ops_tests: valid payload rejected\n", stderr);
        return 1;
    }
    if (strcmp(ops->descriptor->canonical_name, "aarch64") == 0) {
        if (ops->prepare_post_exit_identity == 0 ||
            ops->activate_post_exit_identity == 0 ||
            ops->enter_post_exit_stack == 0 ||
            ops->prepare_post_exit_identity(
                post_exit_identity_tables,
                sizeof(post_exit_identity_tables),
                UINT64_C(0x40000000),
                &post_exit_identity_root) != RIBON_ARCH_OPERATION_OK ||
            post_exit_identity_root !=
                (uint64_t)(uintptr_t)post_exit_identity_tables ||
            (post_exit_identity_tables[0] & 3u) != 3u ||
            (post_exit_identity_tables[512] & 1u) == 0u ||
            ops->prepare_post_exit_identity(
                post_exit_identity_tables,
                sizeof(post_exit_identity_tables) - 1u,
                UINT64_C(0x40000000),
                &post_exit_identity_root) != RIBON_ARCH_OPERATION_BAD_ARGUMENT ||
            ops->enter_post_exit_stack(0u, 0, 0) !=
                RIBON_ARCH_OPERATION_BAD_ARGUMENT) {
            fputs("arch_ops_tests: post-exit identity table contract failed\n", stderr);
            return 1;
        }
    } else if (ops->prepare_post_exit_identity != 0 ||
               ops->activate_post_exit_identity != 0 ||
               ops->enter_post_exit_stack != 0) {
        fputs("arch_ops_tests: unsupported post-exit identity callbacks published\n", stderr);
        return 1;
    }
    ++validated.machine;
    if (ops->validate_direct_load(ops->descriptor, &validated, &payload) !=
        RIBON_ARCH_OPERATION_INVALID_PAYLOAD) {
        fputs("arch_ops_tests: wrong machine accepted\n", stderr);
        return 1;
    }
    invocation.register_abi = register_abi;
    invocation.translation = translation;
    if (ops->prepare_entry(ops->descriptor, &invocation, &prepared) !=
            RIBON_ARCH_OPERATION_OK ||
        prepared.invocation.translation != translation ||
        prepared.translation_root != 0u) {
        fputs("arch_ops_tests: valid entry contract rejected\n", stderr);
        return 1;
    }
    invocation.translation =
        strcmp(ops->descriptor->canonical_name, "aarch64") == 0 ?
            (enum RibonEntryTranslationRequirement)3u :
            translation == RIBON_ENTRY_TRANSLATION_DISABLED ?
                RIBON_ENTRY_TRANSLATION_DIRECT_HIGH_BRIDGE :
                RIBON_ENTRY_TRANSLATION_DISABLED;
    if (ops->prepare_entry(ops->descriptor, &invocation, &prepared) !=
            RIBON_ARCH_OPERATION_BAD_ARGUMENT ||
        prepared.invocation.size != 0u) {
        fputs("arch_ops_tests: wrong translation contract accepted\n", stderr);
        return 1;
    }
    invocation.translation = translation;
    if (strcmp(ops->descriptor->canonical_name, "aarch64") == 0) {
        invocation.translation = RIBON_ENTRY_TRANSLATION_DISABLED;
        invocation.privilege = RIBON_ENTRY_PRIVILEGE_AARCH64_EL1;
        if (ops->prepare_entry(ops->descriptor, &invocation, &prepared) !=
                RIBON_ARCH_OPERATION_OK ||
            prepared.invocation.translation != RIBON_ENTRY_TRANSLATION_DISABLED ||
            prepared.invocation.privilege != RIBON_ENTRY_PRIVILEGE_AARCH64_EL1) {
            fputs("arch_ops_tests: AArch64 disabled EL1 contract rejected\n", stderr);
            return 1;
        }
        invocation.privilege = RIBON_ENTRY_PRIVILEGE_CURRENT_SUPERVISOR;
        if (ops->prepare_entry(ops->descriptor, &invocation, &prepared) !=
                RIBON_ARCH_OPERATION_BAD_ARGUMENT ||
            prepared.invocation.size != 0u) {
            fputs("arch_ops_tests: mismatched AArch64 entry state accepted\n", stderr);
            return 1;
        }
        invocation.translation = translation;
    }
    invocation.register_abi =
        (enum RibonRegisterAbi)(((uint32_t)register_abi + 1u) % 3u);
    if (ops->prepare_entry(ops->descriptor, &invocation, &prepared) !=
            RIBON_ARCH_OPERATION_BAD_ARGUMENT ||
        prepared.invocation.size != 0u) {
        fputs("arch_ops_tests: wrong register ABI accepted\n", stderr);
        return 1;
    }
    invocation.register_abi = register_abi;
    invocation.privilege = (enum RibonEntryPrivilegeRequirement)2u;
    if (ops->prepare_entry(ops->descriptor, &invocation, &prepared) !=
            RIBON_ARCH_OPERATION_BAD_ARGUMENT ||
        prepared.invocation.size != 0u) {
        fputs("arch_ops_tests: wrong privilege contract accepted\n", stderr);
        return 1;
    }
    printf("RIBON-ARCH-OPS-OK %s\n", ops->descriptor->canonical_name);
    return 0;
}
