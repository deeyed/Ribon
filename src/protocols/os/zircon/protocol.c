#include <Ribon/boot/plan.h>
#include <Ribon/plugin/descriptor.h>
#include <Ribon/protocols/os/zircon/zbi.h>

#define RIBON_ZBI_ITEM_MAGIC 0xb5781729u
#define RIBON_ZBI_TYPE_CONTAINER 0x544f4f42u

struct RibonZbiHeader {
    uint32_t type;
    uint32_t length;
    uint32_t extra;
    uint32_t flags;
    uint32_t reserved0;
    uint32_t reserved1;
    uint32_t magic;
    uint32_t crc32;
};

/** @brief Caller-owned bytes가 bounded ZBI container header인지 검사한다. */
int ribon_zircon_zbi_is_valid(const void *data, uint64_t size) {
    const struct RibonZbiHeader *header = data;
    return data != 0 &&
           size >= sizeof(*header) &&
           header->type == RIBON_ZBI_TYPE_CONTAINER &&
           header->magic == RIBON_ZBI_ITEM_MAGIC &&
           header->length <= size - sizeof(*header);
}

/** @brief Zircon protocol stable ID를 allocation 없이 비교한다. */
static int zircon_streq(const char *lhs, const char *rhs) {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }
    while (*lhs != '\0' && *rhs != '\0' && *lhs == *rhs) {
        ++lhs;
        ++rhs;
    }
    return *lhs == *rhs;
}

/** @brief Manifest가 Zircon protocol ABI v1을 선택했는지 검사한다. */
static int zircon_match(const struct RibonManifestView *manifest) {
    return manifest != 0 &&
           zircon_streq(manifest->protocol_id, "zircon") &&
           manifest->protocol_abi_min <= 1u &&
           manifest->protocol_abi_max >= 1u ?
        RIBON_PROTOCOL_STATUS_OK :
        RIBON_PROTOCOL_STATUS_BAD_MANIFEST;
}

/** @brief Zircon kernel과 ZBI component tuple을 bounded하게 검사한다. */
static int zircon_validate_components(const struct RibonManifestView *manifest) {
    uint32_t kernels = 0u;
    uint32_t zbis = 0u;
    if (zircon_match(manifest) != RIBON_PROTOCOL_STATUS_OK ||
        manifest->components == 0 ||
        manifest->component_count == 0u ||
        manifest->component_count > 2u) {
        return RIBON_PROTOCOL_STATUS_BAD_COMPONENTS;
    }
    for (uint32_t index = 0u; index < manifest->component_count; ++index) {
        const struct RibonComponentDescriptor *component =
            &manifest->components[index];
        if (component->name == 0 || component->size == 0u ||
            component->flags != RIBON_COMPONENT_FLAGS_NONE) {
            return RIBON_PROTOCOL_STATUS_BAD_COMPONENTS;
        }
        kernels += component->role == RIBON_COMPONENT_ROLE_KERNEL ? 1u : 0u;
        zbis += component->role == RIBON_COMPONENT_ROLE_BOOT_MODULE ? 1u : 0u;
    }
    return kernels == 1u && zbis == 1u ?
        RIBON_PROTOCOL_STATUS_OK :
        RIBON_PROTOCOL_STATUS_BAD_COMPONENTS;
}

/** @brief Experimental Zircon wrapper가 허용하는 ELF64 format을 반환한다. */
static uint64_t zircon_select_image_formats(void) {
    return RIBON_IMAGE_FORMAT_MASK(RIBON_EXECUTABLE_FORMAT_ELF64);
}

/** @brief 검증된 ZBI module을 caller-owned handoff storage로 복사한다. */
static int zircon_prepare_handoff(
    const struct RibonBootPlan *plan,
    const struct RibonBootEnvironment *environment,
    const struct RibonMutableMemoryMap *normalized_memory_map,
    void *buffer,
    uint64_t capacity,
    struct RibonHandoffArtifact *out) {
    const struct RibonBootModule *module;
    const uint8_t *source;
    uint8_t *destination = buffer;
    (void)plan;
    (void)normalized_memory_map;
    if (environment == 0 || buffer == 0 || out == 0 ||
        (environment->flags & RIBON_BOOT_ENV_HAS_BOOT_MODULES) == 0u ||
        environment->boot_modules.module_count != 1u) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN;
    }
    module = &environment->boot_modules.modules[0];
    source = (const uint8_t *)(uintptr_t)module->physical_address;
    if (module->physical_address == 0u || module->size > capacity ||
        !ribon_zircon_zbi_is_valid(source, module->size)) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN;
    }
    for (uint64_t index = 0u; index < module->size; ++index) {
        destination[index] = source[index];
    }
    *out = (struct RibonHandoffArtifact){
        .data = buffer,
        .size = module->size,
        .format = "zbi",
        .version_major = 1u,
        .section_count = 1u,
    };
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}

/** @brief AArch64 Zircon ZBI entry invocation을 봉인한다. */
static int zircon_prepare_entry_invocation(
    const struct RibonArchDescriptor *arch,
    const struct RibonBootPlan *plan,
    const struct RibonBootEnvironment *environment,
    const struct RibonHandoffArtifact *handoff,
    struct RibonEntryInvocation *out) {
    (void)environment;
    if (arch == 0 || plan == 0 || handoff == 0 || out == 0 ||
        arch->id != RIBON_ARCHITECTURE_AARCH64 ||
        !ribon_zircon_zbi_is_valid(handoff->data, handoff->size)) {
        return RIBON_PROTOCOL_STATUS_UNSUPPORTED;
    }
    *out = (struct RibonEntryInvocation){
        .size = sizeof(*out),
        .abi_version = RIBON_ENTRY_INVOCATION_ABI_VERSION,
        .entry_address = plan->kernel_runtime_entry_address,
        .register_abi = RIBON_REGISTER_ABI_AARCH64_X0_X1_X2_X3,
        .argument_count = 1u,
        .arguments = {(uint64_t)(uintptr_t)handoff->data},
        .interrupts = RIBON_ENTRY_INTERRUPTS_MASKED,
        .privilege = RIBON_ENTRY_PRIVILEGE_CURRENT_SUPERVISOR,
        .translation = RIBON_ENTRY_TRANSLATION_PRESERVE_REACHABLE,
    };
    return RIBON_PROTOCOL_STATUS_OK;
}

/** @brief Zircon boot confirmation의 generation과 result를 검증한다. */
static int zircon_validate_confirmation(
    const struct RibonBootConfirmation *confirmation,
    const struct RibonBootConfirmationExpectation *expected) {
    return confirmation != 0 && expected != 0 &&
           zircon_streq(confirmation->protocol_id, "zircon") &&
           confirmation->result == RIBON_BOOT_CONFIRMATION_HEALTHY &&
           confirmation->generation == expected->generation ?
        RIBON_PROTOCOL_STATUS_OK :
        RIBON_PROTOCOL_STATUS_BAD_CONFIRMATION;
}

static const struct RibonBootProtocolOps zircon_ops = {
    .size = sizeof(zircon_ops),
    .abi_version = RIBON_BOOT_PROTOCOL_OPS_ABI_VERSION,
    .match = zircon_match,
    .validate_components = zircon_validate_components,
    .select_image_formats = zircon_select_image_formats,
    .prepare_handoff = zircon_prepare_handoff,
    .prepare_entry_invocation = zircon_prepare_entry_invocation,
    .validate_confirmation = zircon_validate_confirmation,
};

static const struct RibonBootProtocol zircon_protocol = {
    .size = sizeof(zircon_protocol),
    .abi_version = 1u,
    .id = "zircon",
    .kernel_path = "zircon/kernel.elf",
    .expectations =
        RIBON_PROTOCOL_EXPECT_MEMORY_MAP |
        RIBON_PROTOCOL_EXPECT_KERNEL_IMAGE_LAYOUT |
        RIBON_PROTOCOL_ALLOW_BOOT_MODULES,
    .supported_modes =
        RIBON_MODE_MASK(RIBON_MODE_NORMAL) |
        RIBON_MODE_MASK(RIBON_MODE_DIAGNOSTIC),
    .handoff_format = "zbi",
    .handoff_major = 1u,
    .ops = &zircon_ops,
};

const struct RibonPluginDescriptor ribon_zircon_protocol_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_zircon_protocol_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_BOOT_PROTOCOL,
    .phase = RIBON_PLUGIN_PHASE_BOOT,
    .id = "protocol.zircon",
    .provides =
        RIBON_CAP_BOOT_PROTOCOL |
        RIBON_CAP_HANDOFF |
        RIBON_CAP_ENTRY_CONTRACT |
        RIBON_CAP_BOOT_CONFIRMATION,
    .requires = RIBON_CAP_IMAGE_ELF64,
    .architecture_mask = RIBON_ARCH_MASK_AARCH64,
    .environment_mask = RIBON_ENV_MASK_RAW_FDT,
    .mode_mask =
        RIBON_MODE_MASK(RIBON_MODE_NORMAL) |
        RIBON_MODE_MASK(RIBON_MODE_DIAGNOSTIC),
    .arena_budget = 64ull * 1024ull,
    .input_budget = 64ull * 1024ull * 1024ull,
    .output_budget = 16ull * 1024ull * 1024ull,
    .deadline_ms = 30000u,
    .operations = &zircon_protocol,
    .operations_size = sizeof(zircon_protocol),
    .operations_abi = 1u,
    .validate_operations = ribon_protocol_plugin_operations_are_valid,
};
