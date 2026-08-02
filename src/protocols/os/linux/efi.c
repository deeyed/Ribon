#include <Ribon/boot/plan.h>
#include <Ribon/plugin/descriptor.h>
#include <Ribon/protocols/os/linux/boot.h>

/** @brief 두 stable protocol ID가 같은지 검사한다. */
static int linux_efi_streq(const char *lhs, const char *rhs) {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }
    while (*lhs != '\0' && *rhs != '\0' && *lhs == *rhs) {
        ++lhs;
        ++rhs;
    }
    return *lhs == *rhs;
}

/** @brief Manifest가 Linux EFI protocol ABI를 선택하는지 검사한다. */
static int linux_efi_match(const struct RibonManifestView *manifest) {
    return manifest != 0 && linux_efi_streq(manifest->protocol_id, "linux-efi") &&
           manifest->protocol_abi_min <= 1u && manifest->protocol_abi_max >= 1u ?
        RIBON_PROTOCOL_STATUS_OK : RIBON_PROTOCOL_STATUS_BAD_MANIFEST;
}

/** @brief Managed EFI boot에 정확히 하나의 non-empty kernel을 요구한다. */
static int linux_efi_validate_components(const struct RibonManifestView *manifest) {
    return linux_efi_match(manifest) == RIBON_PROTOCOL_STATUS_OK &&
           manifest->components != 0 && manifest->component_count == 1u &&
           manifest->components[0].role == RIBON_COMPONENT_ROLE_KERNEL &&
           manifest->components[0].name != 0 && manifest->components[0].size != 0u ?
        RIBON_PROTOCOL_STATUS_OK : RIBON_PROTOCOL_STATUS_BAD_COMPONENTS;
}

/** @brief Linux EFI stub에 허용되는 executable format mask를 반환한다. */
static uint64_t linux_efi_select_image_formats(void) {
    return RIBON_IMAGE_FORMAT_MASK(RIBON_EXECUTABLE_FORMAT_PE_COFF);
}

/** @brief Linux EFI stub의 command line과 watchdog을 managed request로 봉인한다. */
static int linux_efi_prepare_terminal(
    const struct RibonArchDescriptor *arch,
    const struct RibonBootPlan *plan,
    const struct RibonBootEnvironment *environment,
    const struct RibonHandoffArtifact *handoff,
    struct RibonTerminalRequest *out) {
    if (arch == 0 || plan == 0 || environment == 0 || out == 0 || handoff != 0 ||
        arch->id != RIBON_ARCHITECTURE_X86_64 ||
        environment->kind != RIBON_ENVIRONMENT_UEFI ||
        plan->kernel_image.format != RIBON_EXECUTABLE_FORMAT_PE_COFF ||
        (plan->kernel_image.execution_support &
         RIBON_IMAGE_EXECUTION_FIRMWARE_MANAGED) == 0u ||
        plan->kernel_image.machine != arch->pe_coff_machine ||
        plan->kernel_direct_load_plan != 0 ||
        environment->command_line.text == 0 ||
        environment->command_line.length == 0u) {
        if (out != 0) {
            *out = (struct RibonTerminalRequest){0};
        }
        return RIBON_PROTOCOL_STATUS_BAD_ENTRY_CONTRACT;
    }
    *out = (struct RibonTerminalRequest){
        .size = sizeof(*out),
        .abi_version = RIBON_TERMINAL_REQUEST_ABI_VERSION,
        .kind = RIBON_TERMINAL_EXECUTION_FIRMWARE_MANAGED_IMAGE,
        .managed_image = {
            .load_options_kind = RIBON_TERMINAL_LOAD_OPTIONS_UTF8_COMMAND_LINE,
            .load_options_size = environment->command_line.length,
            .load_options = environment->command_line.text,
            .watchdog_timeout_ms = 30000u,
        },
    };
    return RIBON_PROTOCOL_STATUS_OK;
}

/** @brief Linux health payload codec이 없으므로 항상 fail-closed한다. */
static int linux_efi_validate_boot_health(const struct RibonBootHealthPayload *payload) {
    (void)payload;
    return RIBON_PROTOCOL_STATUS_UNSUPPORTED;
}

static const struct RibonBootProtocolOps linux_efi_ops = {
    .size = sizeof(linux_efi_ops),
    .abi_version = RIBON_BOOT_PROTOCOL_OPS_ABI_VERSION,
    .match = linux_efi_match,
    .validate_components = linux_efi_validate_components,
    .select_image_formats = linux_efi_select_image_formats,
    .prepare_handoff = 0,
    .prepare_terminal = linux_efi_prepare_terminal,
    .validate_boot_health = linux_efi_validate_boot_health,
};

static const struct RibonBootProtocol linux_efi_protocol = {
    .size = sizeof(linux_efi_protocol),
    .abi_version = 1u,
    .id = "linux-efi",
    .kernel_path = "linux/bzImage.efi",
    .terminal_execution = RIBON_TERMINAL_EXECUTION_FIRMWARE_MANAGED_IMAGE,
    .expectations = 0u,
    .supported_modes = RIBON_MODE_MASK(RIBON_MODE_NORMAL),
    .handoff_format = 0,
    .handoff_major = 0u,
    .ops = &linux_efi_ops,
};

const struct RibonPluginDescriptor ribon_linux_efi_protocol_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_linux_efi_protocol_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_BOOT_PROTOCOL,
    .phase = RIBON_PLUGIN_PHASE_BOOT,
    .id = "protocol.linux-efi",
    .provides = RIBON_CAP_BOOT_PROTOCOL | RIBON_CAP_BOOT_CONFIRMATION,
    .requires = RIBON_CAP_IMAGE_PE_COFF | RIBON_CAP_TERMINAL_IMAGE_LAUNCH,
    .architecture_mask = RIBON_ARCH_MASK_X86_64,
    .environment_mask = RIBON_ENV_MASK_UEFI,
    .mode_mask = RIBON_MODE_MASK(RIBON_MODE_NORMAL),
    .arena_budget = 4096u,
    .input_budget = 64ull * 1024ull * 1024ull,
    .output_budget = 4096u,
    .deadline_ms = 30000u,
    .operations = &linux_efi_protocol,
    .operations_size = sizeof(linux_efi_protocol),
    .operations_abi = 1u,
    .validate_operations = ribon_protocol_plugin_operations_are_valid,
};
