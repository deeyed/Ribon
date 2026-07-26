#include <Ribon/profile.h>
#include <Ribon/arch.h>
#include <Ribon/profiles/parus/rph1.h>

/** @brief 두 C 문자열이 같은지 검사한다. */
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

/** @brief Manifest가 Parus profile ABI v1을 허용하는지 검사한다. */
static int parus_match_manifest(const struct RibonManifestView *manifest) {
    if (manifest == 0 || !parus_streq(manifest->profile_id, "parus")) {
        return RIBON_PROFILE_STATUS_BAD_MANIFEST;
    }
    if (manifest->profile_abi_min == 0u ||
        manifest->profile_abi_min > 1u ||
        manifest->profile_abi_max < 1u ||
        manifest->profile_abi_max < manifest->profile_abi_min) {
        return RIBON_PROFILE_STATUS_BAD_MANIFEST;
    }
    return RIBON_PROFILE_STATUS_OK;
}

/** @brief Parus manifest가 정확히 한 kernel과 허용 component만 가지는지 검사한다. */
static int parus_validate_components(const struct RibonManifestView *manifest) {
    uint32_t kernel_count = 0u;

    if (parus_match_manifest(manifest) != RIBON_PROFILE_STATUS_OK ||
        manifest->components == 0 ||
        manifest->component_count == 0u ||
        manifest->component_count > 32u) {
        return RIBON_PROFILE_STATUS_BAD_COMPONENTS;
    }
    for (uint32_t index = 0; index < manifest->component_count; ++index) {
        const struct RibonComponentDescriptor *component = &manifest->components[index];
        if (component->name == 0 ||
            component->size == 0u ||
            component->flags != RIBON_COMPONENT_FLAGS_NONE) {
            return RIBON_PROFILE_STATUS_BAD_COMPONENTS;
        }
        switch (component->role) {
        case RIBON_COMPONENT_ROLE_KERNEL:
            ++kernel_count;
            break;
        case RIBON_COMPONENT_ROLE_BOOT_MODULE:
        case RIBON_COMPONENT_ROLE_DEVICE_TREE:
            break;
        default:
            return RIBON_PROFILE_STATUS_BAD_COMPONENTS;
        }
    }
    return kernel_count == 1u ?
        RIBON_PROFILE_STATUS_OK :
        RIBON_PROFILE_STATUS_BAD_COMPONENTS;
}

/** @brief Architecture 이름에 맞는 Parus register ABI와 entry flag 집합을 선택한다. */
static int parus_select_entry_contract(
    const struct RibonArchDescriptor *arch,
    struct RibonEntryContract *out) {
    if (arch == 0 || arch->canonical_name == 0 || out == 0) {
        return RIBON_PROFILE_STATUS_BAD_ARGUMENT;
    }
    *out = (struct RibonEntryContract){
        .required_entry_flags = RIBON_KERNEL_ENTRY_FLAG_RPH1,
        .supported_entry_flags =
            RIBON_KERNEL_ENTRY_FLAG_RPH1 |
            RIBON_KERNEL_ENTRY_FLAG_ENTERED_HIGH |
            RIBON_KERNEL_ENTRY_FLAG_DIRECT_HIGH,
    };
    if (parus_streq(arch->canonical_name, "x86_64")) {
        out->register_abi = RIBON_REGISTER_ABI_X86_64_RDI_RSI;
        return RIBON_PROFILE_STATUS_OK;
    }
    if (parus_streq(arch->canonical_name, "aarch64")) {
        out->register_abi = RIBON_REGISTER_ABI_AARCH64_X0_X1;
        return RIBON_PROFILE_STATUS_OK;
    }
    if (parus_streq(arch->canonical_name, "riscv64")) {
        out->register_abi = RIBON_REGISTER_ABI_RISCV64_A0_A1;
        out->supported_entry_flags = RIBON_KERNEL_ENTRY_FLAG_RPH1;
        return RIBON_PROFILE_STATUS_OK;
    }
    *out = (struct RibonEntryContract){0};
    return RIBON_PROFILE_STATUS_BAD_ENTRY_CONTRACT;
}

/** @brief 두 고정 길이 nonce를 전체 byte를 방문해 비교한다. */
static int parus_nonce_equal(const uint8_t *lhs, const uint8_t *rhs, uint32_t size) {
    uint8_t difference = 0u;
    for (uint32_t index = 0; index < size; ++index) {
        difference |= (uint8_t)(lhs[index] ^ rhs[index]);
    }
    return difference == 0u;
}

/** @brief Parus boot confirmation을 generation과 32-byte nonce에 묶어 검증한다. */
static int parus_validate_confirmation(
    const struct RibonBootConfirmation *confirmation,
    const struct RibonBootConfirmationExpectation *expected) {
    if (confirmation == 0 || expected == 0 ||
        !parus_streq(confirmation->profile_id, "parus") ||
        confirmation->result != RIBON_BOOT_CONFIRMATION_HEALTHY ||
        confirmation->generation != expected->generation ||
        confirmation->nonce_size != RIBON_BOOT_CONFIRMATION_NONCE_SIZE ||
        expected->nonce_size != RIBON_BOOT_CONFIRMATION_NONCE_SIZE ||
        !parus_nonce_equal(
            confirmation->nonce,
            expected->nonce,
            RIBON_BOOT_CONFIRMATION_NONCE_SIZE)) {
        return RIBON_PROFILE_STATUS_BAD_CONFIRMATION;
    }
    return RIBON_PROFILE_STATUS_OK;
}

static const struct RibonProfileOps parus_ops = {
    .abi_version = RIBON_PROFILE_OPS_ABI_VERSION,
    .match_manifest = parus_match_manifest,
    .validate_components = parus_validate_components,
    .select_entry_contract = parus_select_entry_contract,
    .build_handoff = ribon_parus_build_rph1,
    .validate_confirmation = parus_validate_confirmation,
};

static const struct RibonProfile parus_profile = {
    .name = "parus",
    .description = "Parus kernel boot contract profile",
    .kernel_path = "kernel/kernel.elf",
    .expectations =
        RIBON_PROFILE_EXPECT_MEMORY_MAP |
        RIBON_PROFILE_EXPECT_KERNEL_IMAGE_LAYOUT |
        RIBON_PROFILE_ALLOW_DEVICE_TREE |
        RIBON_PROFILE_ALLOW_ACPI |
        RIBON_PROFILE_ALLOW_BOOT_MODULES,
    .supported_modes =
        RIBON_MODE_MASK(RIBON_MODE_NORMAL) |
        RIBON_MODE_MASK(RIBON_MODE_RECOVERY) |
        RIBON_MODE_MASK(RIBON_MODE_DIAGNOSTIC),
    .capabilities = RIBON_PROFILE_CAP_ALL,
    .handoff = RIBON_HANDOFF_PROFILE_DEFINED,
    .handoff_name = "rph1",
    .handoff_major = RIBON_PARUS_RPH1_VERSION_MAJOR,
    .ops = &parus_ops,
};

/** @brief Builtin Parus profile descriptor를 반환한다. */
const struct RibonProfile *ribon_profile_parus(void) {
    return &parus_profile;
}
