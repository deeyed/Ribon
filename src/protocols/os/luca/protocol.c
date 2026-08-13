#include <Ribon/boot/plan.h>
#include <Ribon/plugin/descriptor.h>
#include <Ribon/protocols/os/luca/rlh1.h>

/** @brief 두 stable ID가 같은 byte sequence인지 검사한다. */
static int luca_streq(const char *lhs, const char *rhs) {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }
    while (*lhs != '\0' && *rhs != '\0') {
        if (*lhs != *rhs) {
            return 0;
        }
        ++lhs;
        ++rhs;
    }
    return *lhs == *rhs;
}

/** @brief Manifest가 LUCA Boot Protocol ABI v1을 허용하는지 검사한다. */
static int luca_match(const struct RibonManifestView *manifest) {
    if (manifest == 0 || !luca_streq(manifest->protocol_id, "luca")) {
        return RIBON_PROTOCOL_STATUS_BAD_MANIFEST;
    }
    if (manifest->protocol_abi_min == 0u ||
        manifest->protocol_abi_min > 1u ||
        manifest->protocol_abi_max < 1u ||
        manifest->protocol_abi_max < manifest->protocol_abi_min) {
        return RIBON_PROTOCOL_STATUS_BAD_MANIFEST;
    }
    return RIBON_PROTOCOL_STATUS_OK;
}

/** @brief LUCA manifest가 정확히 한 kernel과 허용 component만 가지는지 검사한다. */
static int luca_validate_components(const struct RibonManifestView *manifest) {
    uint32_t kernel_count = 0u;

    if (luca_match(manifest) != RIBON_PROTOCOL_STATUS_OK ||
        manifest->components == 0 ||
        manifest->component_count == 0u ||
        manifest->component_count > 32u) {
        return RIBON_PROTOCOL_STATUS_BAD_COMPONENTS;
    }
    for (uint32_t index = 0; index < manifest->component_count; ++index) {
        const struct RibonComponentDescriptor *component =
            &manifest->components[index];
        if (component->name == 0 ||
            component->size == 0u ||
            component->flags != RIBON_COMPONENT_FLAGS_NONE) {
            return RIBON_PROTOCOL_STATUS_BAD_COMPONENTS;
        }
        switch (component->role) {
        case RIBON_COMPONENT_ROLE_KERNEL:
            ++kernel_count;
            break;
        case RIBON_COMPONENT_ROLE_BOOT_MODULE:
        case RIBON_COMPONENT_ROLE_DEVICE_TREE:
            break;
        default:
            return RIBON_PROTOCOL_STATUS_BAD_COMPONENTS;
        }
    }
    return kernel_count == 1u ?
        RIBON_PROTOCOL_STATUS_OK :
        RIBON_PROTOCOL_STATUS_BAD_COMPONENTS;
}

/** @brief LUCA가 허용하는 OS image format을 반환한다. */
static uint64_t luca_select_image_formats(void) {
    return RIBON_IMAGE_FORMAT_MASK(RIBON_EXECUTABLE_FORMAT_ELF64);
}

/** @brief RLH1 artifact를 LUCA entry register invocation으로 봉인한다. */
static int luca_prepare_terminal(
    const struct RibonArchDescriptor *arch,
    const struct RibonBootPlan *plan,
    const struct RibonBootEnvironment *environment,
    const struct RibonHandoffArtifact *handoff,
    struct RibonTerminalRequest *out) {
    struct RibonEntryInvocation *entry;
    enum RibonRegisterAbi register_abi;
    enum RibonEntryTranslationRequirement translation =
        RIBON_ENTRY_TRANSLATION_PRESERVE_REACHABLE;
    (void)environment;
    if (arch == 0 || plan == 0 || handoff == 0 || handoff->data == 0 ||
        handoff->size == 0u || out == 0 ||
        plan->kernel_runtime_entry_address == 0u) {
        return RIBON_PROTOCOL_STATUS_BAD_ARGUMENT;
    }
    *out = (struct RibonTerminalRequest){
        .size = sizeof(*out),
        .abi_version = RIBON_TERMINAL_REQUEST_ABI_VERSION,
        .kind = RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY,
    };
    entry = &out->direct_entry;
    switch (arch->id) {
    case RIBON_ARCHITECTURE_X86_64:
        register_abi = RIBON_REGISTER_ABI_X86_64_RDI_RSI_RDX_RCX;
        break;
    case RIBON_ARCHITECTURE_AARCH64:
        register_abi = RIBON_REGISTER_ABI_AARCH64_X0_X1_X2_X3;
        break;
    case RIBON_ARCHITECTURE_RISCV64:
        register_abi = RIBON_REGISTER_ABI_RISCV64_A0_A1_A2_A3;
        translation = RIBON_ENTRY_TRANSLATION_DISABLED;
        break;
    default:
        *out = (struct RibonTerminalRequest){0};
        return RIBON_PROTOCOL_STATUS_BAD_ENTRY_CONTRACT;
    }
    *entry = (struct RibonEntryInvocation){
        .size = sizeof(*entry),
        .abi_version = RIBON_ENTRY_INVOCATION_ABI_VERSION,
        .entry_address = plan->kernel_runtime_entry_address,
        .register_abi = register_abi,
        .argument_count = 2u,
        .arguments = {
            (uint64_t)(uintptr_t)handoff->data,
            RIBON_LUCA_ENTRY_FLAG_RLH1,
        },
        .interrupts = RIBON_ENTRY_INTERRUPTS_MASKED,
        .privilege = RIBON_ENTRY_PRIVILEGE_CURRENT_SUPERVISOR,
        .translation = translation,
    };
    return RIBON_PROTOCOL_STATUS_OK;
}

/** @brief LUCA producer 계약이 열리기 전에는 health payload를 거부한다. */
static int luca_validate_boot_health(
    const struct RibonBootHealthPayload *payload) {
    (void)payload;
    return RIBON_PROTOCOL_STATUS_UNSUPPORTED;
}

static const struct RibonBootProtocolOps luca_ops = {
    .size = sizeof(luca_ops),
    .abi_version = RIBON_BOOT_PROTOCOL_OPS_ABI_VERSION,
    .match = luca_match,
    .validate_components = luca_validate_components,
    .select_image_formats = luca_select_image_formats,
    .prepare_handoff = ribon_luca_build_rlh1,
    .prepare_terminal = luca_prepare_terminal,
    .validate_boot_health = luca_validate_boot_health,
};

static const struct RibonBootProtocol luca_protocol = {
    .size = sizeof(luca_protocol),
    .abi_version = 1u,
    .id = "luca",
    .kernel_path = "kernel/kernel.elf",
    .terminal_execution = RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY,
    .expectations =
        RIBON_PROTOCOL_EXPECT_MEMORY_MAP |
        RIBON_PROTOCOL_EXPECT_KERNEL_IMAGE_LAYOUT |
        RIBON_PROTOCOL_ALLOW_DEVICE_TREE |
        RIBON_PROTOCOL_ALLOW_ACPI |
        RIBON_PROTOCOL_ALLOW_BOOT_MODULES,
    .supported_modes =
        RIBON_MODE_MASK(RIBON_MODE_NORMAL) |
        RIBON_MODE_MASK(RIBON_MODE_RECOVERY) |
        RIBON_MODE_MASK(RIBON_MODE_DIAGNOSTIC),
    .handoff_format = "rlh1",
    .handoff_major = RIBON_LUCA_RLH1_VERSION_MAJOR,
    .ops = &luca_ops,
};

/** @brief LUCA Boot Protocol plugin descriptor다. */
const struct RibonPluginDescriptor ribon_luca_protocol_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_luca_protocol_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_BOOT_PROTOCOL,
    .phase = RIBON_PLUGIN_PHASE_BOOT,
    .id = "protocol.luca",
    .provides =
        RIBON_CAP_BOOT_PROTOCOL |
        RIBON_CAP_HANDOFF |
        RIBON_CAP_ENTRY_CONTRACT |
        RIBON_CAP_BOOT_CONFIRMATION,
    .requires = RIBON_CAP_IMAGE_ELF64,
    .architecture_mask = RIBON_ARCH_MASK_ALL,
    .environment_mask = RIBON_ENV_MASK_ALL,
    .mode_mask =
        RIBON_MODE_MASK(RIBON_MODE_NORMAL) |
        RIBON_MODE_MASK(RIBON_MODE_RECOVERY) |
        RIBON_MODE_MASK(RIBON_MODE_DIAGNOSTIC),
    .arena_budget = 64ull * 1024ull,
    .input_budget = 64ull * 1024ull * 1024ull,
    .output_budget = RIBON_LUCA_RLH1_MAX_TOTAL_SIZE,
    .deadline_ms = 30000u,
    .operations = &luca_protocol,
    .operations_size = sizeof(luca_protocol),
    .operations_abi = 1u,
    .validate_operations = ribon_protocol_plugin_operations_are_valid,
};
