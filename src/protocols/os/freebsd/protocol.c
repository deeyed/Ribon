#include <Ribon/boot/plan.h>
#include <Ribon/plugin/descriptor.h>
#include <Ribon/protocols/os/freebsd/boot.h>

/** @brief FreeBSD protocol stable ID를 allocation 없이 비교한다. */
static int freebsd_streq(const char *lhs, const char *rhs) {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }
    while (*lhs != '\0' && *rhs != '\0' && *lhs == *rhs) {
        ++lhs;
        ++rhs;
    }
    return *lhs == *rhs;
}

/** @brief Manifest가 FreeBSD protocol ABI v1을 선택했는지 검사한다. */
static int freebsd_match(const struct RibonManifestView *manifest) {
    return manifest != 0 &&
           freebsd_streq(manifest->protocol_id, "freebsd") &&
           manifest->protocol_abi_min <= 1u &&
           manifest->protocol_abi_max >= 1u ?
        RIBON_PROTOCOL_STATUS_OK :
        RIBON_PROTOCOL_STATUS_BAD_MANIFEST;
}

/** @brief FreeBSD loader EFI image component tuple을 검사한다. */
static int freebsd_validate_components(const struct RibonManifestView *manifest) {
    return freebsd_match(manifest) == RIBON_PROTOCOL_STATUS_OK &&
           manifest->components != 0 &&
           manifest->component_count == 1u &&
           manifest->components[0].role == RIBON_COMPONENT_ROLE_KERNEL &&
           manifest->components[0].name != 0 &&
           manifest->components[0].size != 0u ?
        RIBON_PROTOCOL_STATUS_OK :
        RIBON_PROTOCOL_STATUS_BAD_COMPONENTS;
}

/** @brief FreeBSD UEFI chainload가 요구하는 PE/COFF format을 반환한다. */
static uint64_t freebsd_select_image_formats(void) {
    return RIBON_IMAGE_FORMAT_MASK(RIBON_EXECUTABLE_FORMAT_PE_COFF);
}

/** @brief 검증된 PE/COFF를 firmware-managed terminal requirement로 봉인한다. */
static int freebsd_prepare_terminal(
    const struct RibonArchDescriptor *arch,
    const struct RibonBootPlan *plan,
    const struct RibonBootEnvironment *environment,
    const struct RibonHandoffArtifact *handoff,
    struct RibonTerminalRequest *out) {
    if (arch == 0 || plan == 0 || environment == 0 || out == 0 || handoff != 0 ||
        environment->kind != RIBON_ENVIRONMENT_UEFI ||
        plan->kernel_image.format != RIBON_EXECUTABLE_FORMAT_PE_COFF ||
        (plan->kernel_image.execution_support &
         RIBON_IMAGE_EXECUTION_FIRMWARE_MANAGED) == 0u ||
        plan->kernel_image.machine != arch->pe_coff_machine ||
        plan->kernel_direct_load_plan != 0 ||
        (environment->command_line.length != 0u &&
         environment->command_line.text == 0)) {
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
            .load_options_kind = environment->command_line.length == 0u ?
                RIBON_TERMINAL_LOAD_OPTIONS_NONE :
                RIBON_TERMINAL_LOAD_OPTIONS_UTF8_COMMAND_LINE,
            .load_options_size = environment->command_line.length,
            .load_options = environment->command_line.length == 0u ?
                0 : environment->command_line.text,
            .watchdog_timeout_ms = 30000u,
        },
    };
    return RIBON_PROTOCOL_STATUS_OK;
}

/** @brief 미지원 FreeBSD runtime confirmation을 명시적으로 거부한다. */
static int freebsd_validate_boot_health(
    const struct RibonBootHealthPayload *payload) {
    (void)payload;
    return RIBON_PROTOCOL_STATUS_UNSUPPORTED;
}

static const struct RibonBootProtocolOps freebsd_ops = {
    .size = sizeof(freebsd_ops),
    .abi_version = RIBON_BOOT_PROTOCOL_OPS_ABI_VERSION,
    .match = freebsd_match,
    .validate_components = freebsd_validate_components,
    .select_image_formats = freebsd_select_image_formats,
    .prepare_handoff = 0,
    .prepare_terminal = freebsd_prepare_terminal,
    .validate_boot_health = freebsd_validate_boot_health,
};

static const struct RibonBootProtocol freebsd_protocol = {
    .size = sizeof(freebsd_protocol),
    .abi_version = 1u,
    .id = "freebsd",
    .kernel_path = "freebsd/loader.efi",
    .terminal_execution = RIBON_TERMINAL_EXECUTION_FIRMWARE_MANAGED_IMAGE,
    .expectations = 0u,
    .supported_modes = RIBON_MODE_MASK(RIBON_MODE_NORMAL),
    .handoff_format = 0,
    .handoff_major = 0u,
    .ops = &freebsd_ops,
};

const struct RibonPluginDescriptor ribon_freebsd_protocol_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_freebsd_protocol_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_BOOT_PROTOCOL,
    .phase = RIBON_PLUGIN_PHASE_BOOT,
    .id = "protocol.freebsd",
    .provides =
        RIBON_CAP_BOOT_PROTOCOL |
        RIBON_CAP_BOOT_CONFIRMATION,
    .requires = RIBON_CAP_IMAGE_PE_COFF | RIBON_CAP_TERMINAL_IMAGE_LAUNCH,
    .architecture_mask = RIBON_ARCH_MASK_X86_64 | RIBON_ARCH_MASK_AARCH64,
    .environment_mask = RIBON_ENV_MASK_UEFI,
    .mode_mask = RIBON_MODE_MASK(RIBON_MODE_NORMAL),
    .arena_budget = 4096u,
    .input_budget = 64ull * 1024ull * 1024ull,
    .output_budget = 4096u,
    .deadline_ms = 30000u,
    .operations = &freebsd_protocol,
    .operations_size = sizeof(freebsd_protocol),
    .operations_abi = 1u,
    .validate_operations = ribon_protocol_plugin_operations_are_valid,
};
