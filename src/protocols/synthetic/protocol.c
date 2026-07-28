#include <Ribon/boot/plan.h>
#include <Ribon/plugin/descriptor.h>

/** @brief 두 stable ID가 같은 byte sequence인지 검사한다. */
static int synthetic_streq(const char *lhs, const char *rhs) {
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

/** @brief Synthetic manifest가 protocol ABI v1을 허용하는지 검사한다. */
static int synthetic_match(const struct RibonManifestView *manifest) {
    return manifest != 0 &&
           synthetic_streq(manifest->protocol_id, "synthetic") &&
           manifest->protocol_abi_min != 0u &&
           manifest->protocol_abi_min <= 1u &&
           manifest->protocol_abi_max >= 1u &&
           manifest->protocol_abi_max >= manifest->protocol_abi_min ?
        RIBON_PROTOCOL_STATUS_OK :
        RIBON_PROTOCOL_STATUS_BAD_MANIFEST;
}

/** @brief Synthetic fixture가 정확히 한 kernel component를 가지는지 검사한다. */
static int synthetic_validate_components(const struct RibonManifestView *manifest) {
    uint32_t kernels = 0u;
    if (synthetic_match(manifest) != RIBON_PROTOCOL_STATUS_OK ||
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
        if (component->role == RIBON_COMPONENT_ROLE_KERNEL) {
            ++kernels;
        } else if (component->role != RIBON_COMPONENT_ROLE_BOOT_MODULE &&
                   component->role != RIBON_COMPONENT_ROLE_DEVICE_TREE) {
            return RIBON_PROTOCOL_STATUS_BAD_COMPONENTS;
        }
    }
    return kernels == 1u ?
        RIBON_PROTOCOL_STATUS_OK :
        RIBON_PROTOCOL_STATUS_BAD_COMPONENTS;
}

/** @brief Synthetic protocol이 허용하는 image format을 반환한다. */
static uint64_t synthetic_select_image_formats(void) {
    return RIBON_IMAGE_FORMAT_MASK(RIBON_EXECUTABLE_FORMAT_ELF64);
}

/** @brief Synthetic handoff를 architecture-neutral register invocation으로 봉인한다. */
static int synthetic_prepare_entry_invocation(
    const struct RibonArchDescriptor *arch,
    const struct RibonBootPlan *plan,
    const struct RibonBootEnvironment *environment,
    const struct RibonHandoffArtifact *handoff,
    struct RibonEntryInvocation *out) {
    enum RibonRegisterAbi register_abi;
    (void)environment;
    if (arch == 0 || plan == 0 || handoff == 0 || handoff->data == 0 ||
        handoff->size == 0u || out == 0 ||
        plan->kernel_runtime_entry_address == 0u) {
        return RIBON_PROTOCOL_STATUS_BAD_ARGUMENT;
    }
    switch (arch->id) {
    case RIBON_ARCHITECTURE_X86_64:
        register_abi = RIBON_REGISTER_ABI_X86_64_RDI_RSI_RDX_RCX;
        break;
    case RIBON_ARCHITECTURE_AARCH64:
        register_abi = RIBON_REGISTER_ABI_AARCH64_X0_X1_X2_X3;
        break;
    case RIBON_ARCHITECTURE_RISCV64:
        register_abi = RIBON_REGISTER_ABI_RISCV64_A0_A1_A2_A3;
        break;
    default:
        *out = (struct RibonEntryInvocation){0};
        return RIBON_PROTOCOL_STATUS_BAD_ENTRY_CONTRACT;
    }
    *out = (struct RibonEntryInvocation){
        .size = sizeof(*out),
        .abi_version = RIBON_ENTRY_INVOCATION_ABI_VERSION,
        .entry_address = plan->kernel_runtime_entry_address,
        .register_abi = register_abi,
        .argument_count = 1u,
        .arguments = {(uint64_t)(uintptr_t)handoff->data},
        .interrupts = RIBON_ENTRY_INTERRUPTS_MASKED,
        .privilege = RIBON_ENTRY_PRIVILEGE_CURRENT_SUPERVISOR,
        .translation = RIBON_ENTRY_TRANSLATION_PRESERVE_REACHABLE,
    };
    return RIBON_PROTOCOL_STATUS_OK;
}

/** @brief Little-endian 64-bit fixture field를 기록한다. */
static void synthetic_write_u64(unsigned char *bytes, uint64_t value) {
    for (uint32_t index = 0; index < 8u; ++index) {
        bytes[index] = (unsigned char)((value >> (index * 8u)) & 0xffu);
    }
}

/** @brief Caller-owned 32-byte buffer에 deterministic synthetic handoff를 만든다. */
static int synthetic_prepare_handoff(
    const struct RibonBootPlan *plan,
    const struct RibonBootEnvironment *environment,
    const struct RibonMutableMemoryMap *normalized_memory_map,
    void *buffer,
    uint64_t capacity,
    struct RibonHandoffArtifact *out) {
    unsigned char *bytes = (unsigned char *)buffer;
    if (plan == 0 ||
        environment == 0 ||
        normalized_memory_map == 0 ||
        buffer == 0 ||
        capacity < 32u ||
        out == 0) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_BAD_ARGUMENT;
    }
    bytes[0] = 'S';
    bytes[1] = 'Y';
    bytes[2] = 'N';
    bytes[3] = '1';
    bytes[4] = 1u;
    bytes[5] = 0u;
    bytes[6] = 0u;
    bytes[7] = 0u;
    synthetic_write_u64(bytes + 8u, plan->kernel_runtime_entry_address);
    synthetic_write_u64(bytes + 16u, plan->usable_memory_bytes);
    synthetic_write_u64(bytes + 24u, normalized_memory_map->region_count);
    *out = (struct RibonHandoffArtifact){
        .data = buffer,
        .size = 32u,
        .format = "synthetic-v1",
        .version_major = 1u,
        .section_count = 1u,
    };
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}

/** @brief Synthetic confirmation을 generation과 nonce에 묶어 검증한다. */
static int synthetic_validate_confirmation(
    const struct RibonBootConfirmation *confirmation,
    const struct RibonBootConfirmationExpectation *expected) {
    uint8_t difference = 0u;
    if (confirmation == 0 ||
        expected == 0 ||
        !synthetic_streq(confirmation->protocol_id, "synthetic") ||
        confirmation->result != RIBON_BOOT_CONFIRMATION_HEALTHY ||
        confirmation->generation != expected->generation ||
        confirmation->nonce_size != RIBON_BOOT_CONFIRMATION_NONCE_SIZE ||
        expected->nonce_size != RIBON_BOOT_CONFIRMATION_NONCE_SIZE) {
        return RIBON_PROTOCOL_STATUS_BAD_CONFIRMATION;
    }
    for (uint32_t index = 0; index < RIBON_BOOT_CONFIRMATION_NONCE_SIZE; ++index) {
        difference |= (uint8_t)(confirmation->nonce[index] ^ expected->nonce[index]);
    }
    return difference == 0u ?
        RIBON_PROTOCOL_STATUS_OK :
        RIBON_PROTOCOL_STATUS_BAD_CONFIRMATION;
}

static const struct RibonBootProtocolOps synthetic_ops = {
    .size = sizeof(synthetic_ops),
    .abi_version = RIBON_BOOT_PROTOCOL_OPS_ABI_VERSION,
    .match = synthetic_match,
    .validate_components = synthetic_validate_components,
    .select_image_formats = synthetic_select_image_formats,
    .prepare_handoff = synthetic_prepare_handoff,
    .prepare_entry_invocation = synthetic_prepare_entry_invocation,
    .validate_confirmation = synthetic_validate_confirmation,
};

static const struct RibonBootProtocol synthetic_protocol = {
    .size = sizeof(synthetic_protocol),
    .abi_version = 1u,
    .id = "synthetic",
    .kernel_path = "kernel/kernel.elf",
    .expectations =
        RIBON_PROTOCOL_EXPECT_MEMORY_MAP |
        RIBON_PROTOCOL_EXPECT_KERNEL_IMAGE_LAYOUT,
    .supported_modes = RIBON_MODE_MASK(RIBON_MODE_NORMAL),
    .handoff_format = "synthetic-v1",
    .handoff_major = 1u,
    .ops = &synthetic_ops,
};

/** @brief Synthetic contract-test Boot Protocol plugin descriptor다. */
const struct RibonPluginDescriptor ribon_synthetic_protocol_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_synthetic_protocol_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_BOOT_PROTOCOL,
    .phase = RIBON_PLUGIN_PHASE_BOOT,
    .id = "protocol.synthetic",
    .provides =
        RIBON_CAP_BOOT_PROTOCOL |
        RIBON_CAP_HANDOFF |
        RIBON_CAP_ENTRY_CONTRACT |
        RIBON_CAP_BOOT_CONFIRMATION,
    .requires = RIBON_CAP_IMAGE_ELF64,
    .architecture_mask = RIBON_ARCH_MASK_ALL,
    .environment_mask = RIBON_ENV_MASK_HOST,
    .mode_mask = RIBON_MODE_MASK(RIBON_MODE_NORMAL),
    .arena_budget = 4096u,
    .input_budget = 64ull * 1024ull * 1024ull,
    .output_budget = 64ull * 1024ull,
    .deadline_ms = 30000u,
    .operations = &synthetic_protocol,
    .operations_size = sizeof(synthetic_protocol),
    .operations_abi = 1u,
    .validate_operations = ribon_protocol_plugin_operations_are_valid,
};
