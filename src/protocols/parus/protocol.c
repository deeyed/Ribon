#include <Ribon/arch/entry.h>
#include <Ribon/boot/plan.h>
#include <Ribon/plugin/descriptor.h>
#include <Ribon/protocols/parus/rph1.h>

/** @brief 두 stable ID가 같은 byte sequence인지 검사한다. */
static int parus_streq(const char *lhs, const char *rhs) {
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

/** @brief Manifest가 Parus Boot Protocol ABI v1을 허용하는지 검사한다. */
static int parus_match(const struct RibonManifestView *manifest) {
    if (manifest == 0 || !parus_streq(manifest->protocol_id, "parus")) {
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

/** @brief Parus manifest가 정확히 한 kernel과 허용 component만 가지는지 검사한다. */
static int parus_validate_components(const struct RibonManifestView *manifest) {
    uint32_t kernel_count = 0u;

    if (parus_match(manifest) != RIBON_PROTOCOL_STATUS_OK ||
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

/** @brief Parus가 허용하는 OS image format을 반환한다. */
static uint64_t parus_select_image_formats(void) {
    return RIBON_IMAGE_FORMAT_MASK(RIBON_EXECUTABLE_FORMAT_ELF64);
}

/** @brief Architecture에 맞는 Parus register ABI와 entry flag를 선택한다. */
static int parus_select_entry_contract(
    const struct RibonArchDescriptor *arch,
    struct RibonEntryContract *out) {
    if (arch == 0 || out == 0) {
        return RIBON_PROTOCOL_STATUS_BAD_ARGUMENT;
    }
    *out = (struct RibonEntryContract){
        .required_entry_flags = RIBON_KERNEL_ENTRY_FLAG_RPH1,
        .supported_entry_flags =
            RIBON_KERNEL_ENTRY_FLAG_RPH1 |
            RIBON_KERNEL_ENTRY_FLAG_ENTERED_HIGH |
            RIBON_KERNEL_ENTRY_FLAG_DIRECT_HIGH,
    };
    switch (arch->id) {
    case RIBON_ARCHITECTURE_X86_64:
        out->register_abi = RIBON_REGISTER_ABI_X86_64_RDI_RSI;
        return RIBON_PROTOCOL_STATUS_OK;
    case RIBON_ARCHITECTURE_AARCH64:
        out->register_abi = RIBON_REGISTER_ABI_AARCH64_X0_X1;
        return RIBON_PROTOCOL_STATUS_OK;
    case RIBON_ARCHITECTURE_RISCV64:
        out->register_abi = RIBON_REGISTER_ABI_RISCV64_A0_A1;
        out->supported_entry_flags = RIBON_KERNEL_ENTRY_FLAG_RPH1;
        return RIBON_PROTOCOL_STATUS_OK;
    default:
        *out = (struct RibonEntryContract){0};
        return RIBON_PROTOCOL_STATUS_BAD_ENTRY_CONTRACT;
    }
}

/** @brief 두 nonce를 전체 byte를 방문해 비교한다. */
static int parus_nonce_equal(const uint8_t *lhs, const uint8_t *rhs, uint32_t size) {
    uint8_t difference = 0u;
    for (uint32_t index = 0; index < size; ++index) {
        difference |= (uint8_t)(lhs[index] ^ rhs[index]);
    }
    return difference == 0u;
}

/** @brief Parus confirmation을 generation과 32-byte nonce에 묶어 검증한다. */
static int parus_validate_confirmation(
    const struct RibonBootConfirmation *confirmation,
    const struct RibonBootConfirmationExpectation *expected) {
    if (confirmation == 0 ||
        expected == 0 ||
        !parus_streq(confirmation->protocol_id, "parus") ||
        confirmation->result != RIBON_BOOT_CONFIRMATION_HEALTHY ||
        confirmation->generation != expected->generation ||
        confirmation->nonce_size != RIBON_BOOT_CONFIRMATION_NONCE_SIZE ||
        expected->nonce_size != RIBON_BOOT_CONFIRMATION_NONCE_SIZE ||
        !parus_nonce_equal(
            confirmation->nonce,
            expected->nonce,
            RIBON_BOOT_CONFIRMATION_NONCE_SIZE)) {
        return RIBON_PROTOCOL_STATUS_BAD_CONFIRMATION;
    }
    return RIBON_PROTOCOL_STATUS_OK;
}

static const struct RibonBootProtocolOps parus_ops = {
    .size = sizeof(parus_ops),
    .abi_version = RIBON_BOOT_PROTOCOL_OPS_ABI_VERSION,
    .match = parus_match,
    .validate_components = parus_validate_components,
    .select_image_formats = parus_select_image_formats,
    .prepare_handoff = ribon_parus_build_rph1,
    .select_entry_contract = parus_select_entry_contract,
    .validate_confirmation = parus_validate_confirmation,
};

static const struct RibonBootProtocol parus_protocol = {
    .size = sizeof(parus_protocol),
    .abi_version = 1u,
    .id = "parus",
    .kernel_path = "kernel/kernel.elf",
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
    .handoff_format = "rph1",
    .handoff_major = RIBON_PARUS_RPH1_VERSION_MAJOR,
    .ops = &parus_ops,
};

/** @brief Parus Boot Protocol plugin descriptor다. */
const struct RibonPluginDescriptor ribon_parus_protocol_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_parus_protocol_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_BOOT_PROTOCOL,
    .phase = RIBON_PLUGIN_PHASE_BOOT,
    .id = "protocol.parus",
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
    .output_budget = RIBON_PARUS_RPH1_MAX_TOTAL_SIZE,
    .deadline_ms = 30000u,
    .operations = &parus_protocol,
    .operations_size = sizeof(parus_protocol),
    .operations_abi = 1u,
    .validate_operations = ribon_protocol_plugin_operations_are_valid,
};
