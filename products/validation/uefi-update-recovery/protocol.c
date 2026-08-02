#include <Ribon/plugin/descriptor.h>
#include <Ribon/protocol/protocol.h>

static int validation_match(const struct RibonManifestView *manifest)
{
    (void)manifest;
    return RIBON_PROTOCOL_STATUS_UNSUPPORTED;
}

static int validation_components(const struct RibonManifestView *manifest)
{
    (void)manifest;
    return RIBON_PROTOCOL_STATUS_UNSUPPORTED;
}

static uint64_t validation_formats(void)
{
    return RIBON_IMAGE_FORMAT_MASK(RIBON_EXECUTABLE_FORMAT_ELF64);
}

static int validation_handoff(
    const struct RibonBootPlan *plan,
    const struct RibonBootEnvironment *environment,
    const struct RibonMutableMemoryMap *memory_map,
    void *buffer,
    uint64_t capacity,
    struct RibonHandoffArtifact *out)
{
    (void)plan; (void)environment; (void)memory_map;
    (void)buffer; (void)capacity; (void)out;
    return RIBON_PROTOCOL_HANDOFF_STATUS_UNSUPPORTED;
}

static int validation_entry(
    const struct RibonArchDescriptor *arch,
    const struct RibonBootPlan *plan,
    const struct RibonBootEnvironment *environment,
    const struct RibonHandoffArtifact *handoff,
    struct RibonTerminalRequest *out)
{
    (void)arch; (void)plan; (void)environment; (void)handoff; (void)out;
    return RIBON_PROTOCOL_STATUS_UNSUPPORTED;
}

static int validation_boot_health(
    const struct RibonBootHealthPayload *payload)
{
    static const uint8_t healthy[8] = {'R','E','F','H',1u,1u,0u,0u};
    uint8_t difference = 0u;
    uint32_t index;
    if (payload == NULL || payload->size != sizeof(*payload) ||
        payload->abi_version != 1u || payload->bytes == NULL ||
        payload->byte_size != sizeof(healthy)) {
        return RIBON_PROTOCOL_STATUS_BAD_CONFIRMATION;
    }
    for (index = 0u; index < sizeof(healthy); ++index) {
        difference |= payload->bytes[index] ^ healthy[index];
    }
    return difference == 0u ? RIBON_PROTOCOL_STATUS_OK :
        RIBON_PROTOCOL_STATUS_BAD_CONFIRMATION;
}

static const struct RibonBootProtocolOps validation_operations = {
    .size = sizeof(validation_operations),
    .abi_version = RIBON_BOOT_PROTOCOL_OPS_ABI_VERSION,
    .match = validation_match,
    .validate_components = validation_components,
    .select_image_formats = validation_formats,
    .prepare_handoff = validation_handoff,
    .prepare_terminal = validation_entry,
    .validate_boot_health = validation_boot_health,
};

static const struct RibonBootProtocol validation_protocol = {
    .size = sizeof(validation_protocol),
    .abi_version = 1u,
    .id = "validation-update",
    .kernel_path = "/RIBON/UNUSED.ELF",
    .terminal_execution = RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY,
    .supported_modes = RIBON_MODE_MASK(RIBON_MODE_RECOVERY),
    .handoff_format = "none-validation-only",
    .handoff_major = 1u,
    .ops = &validation_operations,
};

const struct RibonPluginDescriptor
ribon_uefi_update_validation_protocol_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_uefi_update_validation_protocol_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_BOOT_PROTOCOL,
    .phase = RIBON_PLUGIN_PHASE_BOOT,
    .id = "protocol.validation-update",
    .provides = RIBON_CAP_BOOT_PROTOCOL | RIBON_CAP_HANDOFF |
        RIBON_CAP_ENTRY_CONTRACT | RIBON_CAP_BOOT_CONFIRMATION,
    .requires = RIBON_CAP_IMAGE_ELF64,
    .architecture_mask = RIBON_ARCH_MASK_ALL,
    .environment_mask = RIBON_ENV_MASK_ALL,
    .mode_mask = RIBON_MODE_MASK(RIBON_MODE_RECOVERY),
    .arena_budget = 1u,
    .input_budget = 4096u,
    .output_budget = 4096u,
    .deadline_ms = 30000u,
    .operations = &validation_protocol,
    .operations_size = sizeof(validation_protocol),
    .operations_abi = 1u,
    .validate_operations = ribon_protocol_plugin_operations_are_valid,
};
