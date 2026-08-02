#include <Ribon/boot/plan.h>
#include <Ribon/plugin/descriptor.h>
#include <Ribon/protocols/os/linux/boot.h>

/** @brief Linux protocol stable ID를 allocation 없이 비교한다. */
static int linux_streq(const char *lhs, const char *rhs) {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }
    while (*lhs != '\0' && *rhs != '\0' && *lhs == *rhs) {
        ++lhs;
        ++rhs;
    }
    return *lhs == *rhs;
}

/** @brief Manifest가 Linux protocol ABI v1을 선택했는지 검사한다. */
static int linux_match(const struct RibonManifestView *manifest) {
    return manifest != 0 &&
           linux_streq(manifest->protocol_id, "linux") &&
           manifest->protocol_abi_min <= 1u &&
           manifest->protocol_abi_max >= 1u ?
        RIBON_PROTOCOL_STATUS_OK :
        RIBON_PROTOCOL_STATUS_BAD_MANIFEST;
}

/** @brief Linux kernel과 선택적 FDT 구성을 bounded하게 검사한다. */
static int linux_validate_components(const struct RibonManifestView *manifest) {
    uint32_t kernels = 0u;
    uint32_t device_trees = 0u;
    if (linux_match(manifest) != RIBON_PROTOCOL_STATUS_OK ||
        manifest->components == 0 ||
        manifest->component_count == 0u ||
        manifest->component_count > 32u) {
        return RIBON_PROTOCOL_STATUS_BAD_COMPONENTS;
    }
    for (uint32_t index = 0u; index < manifest->component_count; ++index) {
        const struct RibonComponentDescriptor *component =
            &manifest->components[index];
        if (component->name == 0 || component->size == 0u ||
            component->flags != RIBON_COMPONENT_FLAGS_NONE) {
            return RIBON_PROTOCOL_STATUS_BAD_COMPONENTS;
        }
        if (component->role == RIBON_COMPONENT_ROLE_KERNEL) {
            ++kernels;
        } else if (component->role == RIBON_COMPONENT_ROLE_DEVICE_TREE) {
            ++device_trees;
        } else {
            return RIBON_PROTOCOL_STATUS_BAD_COMPONENTS;
        }
    }
    return kernels == 1u && device_trees <= 1u ?
        RIBON_PROTOCOL_STATUS_OK :
        RIBON_PROTOCOL_STATUS_BAD_COMPONENTS;
}

/** @brief Experimental Linux wrapper가 허용하는 image format을 반환한다. */
static uint64_t linux_select_image_formats(void) {
    return RIBON_IMAGE_FORMAT_MASK(RIBON_EXECUTABLE_FORMAT_ELF64);
}

/** @brief Validated FDT bytes를 caller-owned Linux handoff storage에 복사한다. */
static int linux_prepare_handoff(
    const struct RibonBootPlan *plan,
    const struct RibonBootEnvironment *environment,
    const struct RibonMutableMemoryMap *normalized_memory_map,
    void *buffer,
    uint64_t capacity,
    struct RibonHandoffArtifact *out) {
    uint8_t *destination = buffer;
    const uint8_t *source;
    (void)plan;
    (void)normalized_memory_map;
    if (environment == 0 || buffer == 0 || out == 0 ||
        (environment->flags & RIBON_BOOT_ENV_HAS_DEVICE_TREE) == 0u ||
        environment->device_tree.data == 0 ||
        environment->device_tree.size == 0u ||
        environment->device_tree.size > capacity) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN;
    }
    source = environment->device_tree.data;
    for (uint64_t index = 0u; index < environment->device_tree.size; ++index) {
        destination[index] = source[index];
    }
    *out = (struct RibonHandoffArtifact){
        .data = buffer,
        .size = environment->device_tree.size,
        .format = "linux-fdt",
        .version_major = 1u,
        .section_count = 1u,
    };
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}

/** @brief Linux AArch64/RISC-V raw-FDT entry register를 봉인한다. */
static int linux_prepare_entry_invocation(
    const struct RibonArchDescriptor *arch,
    const struct RibonBootPlan *plan,
    const struct RibonBootEnvironment *environment,
    const struct RibonHandoffArtifact *handoff,
    struct RibonEntryInvocation *out) {
    if (arch == 0 || plan == 0 || environment == 0 || handoff == 0 ||
        handoff->data == 0 || out == 0 ||
        plan->kernel_runtime_entry_address == 0u ||
        (environment->flags & RIBON_BOOT_ENV_HAS_BOOT_CPU_ID) == 0u) {
        return RIBON_PROTOCOL_STATUS_BAD_ARGUMENT;
    }
    *out = (struct RibonEntryInvocation){
        .size = sizeof(*out),
        .abi_version = RIBON_ENTRY_INVOCATION_ABI_VERSION,
        .entry_address = plan->kernel_runtime_entry_address,
        .interrupts = RIBON_ENTRY_INTERRUPTS_MASKED,
        .privilege = RIBON_ENTRY_PRIVILEGE_CURRENT_SUPERVISOR,
        .translation = RIBON_ENTRY_TRANSLATION_PRESERVE_REACHABLE,
    };
    if (arch->id == RIBON_ARCHITECTURE_AARCH64) {
        out->register_abi = RIBON_REGISTER_ABI_AARCH64_X0_X1_X2_X3;
        out->argument_count = 1u;
        out->arguments[0] = (uint64_t)(uintptr_t)handoff->data;
    } else if (arch->id == RIBON_ARCHITECTURE_RISCV64) {
        out->register_abi = RIBON_REGISTER_ABI_RISCV64_A0_A1_A2_A3;
        out->argument_count = 2u;
        out->arguments[0] = environment->boot_cpu_id;
        out->arguments[1] = (uint64_t)(uintptr_t)handoff->data;
    } else {
        *out = (struct RibonEntryInvocation){0};
        return RIBON_PROTOCOL_STATUS_UNSUPPORTED;
    }
    return RIBON_PROTOCOL_STATUS_OK;
}

/** @brief Linux companion이 정의할 health payload를 아직 거부한다. */
static int linux_validate_boot_health(const struct RibonBootHealthPayload *payload) {
    (void)payload;
    return RIBON_PROTOCOL_STATUS_UNSUPPORTED;
}

static const struct RibonBootProtocolOps linux_ops = {
    .size = sizeof(linux_ops),
    .abi_version = RIBON_BOOT_PROTOCOL_OPS_ABI_VERSION,
    .match = linux_match,
    .validate_components = linux_validate_components,
    .select_image_formats = linux_select_image_formats,
    .prepare_handoff = linux_prepare_handoff,
    .prepare_entry_invocation = linux_prepare_entry_invocation,
    .validate_boot_health = linux_validate_boot_health,
};

static const struct RibonBootProtocol linux_protocol = {
    .size = sizeof(linux_protocol),
    .abi_version = 1u,
    .id = "linux",
    .kernel_path = "linux/kernel.elf",
    .expectations =
        RIBON_PROTOCOL_EXPECT_MEMORY_MAP |
        RIBON_PROTOCOL_EXPECT_KERNEL_IMAGE_LAYOUT |
        RIBON_PROTOCOL_ALLOW_DEVICE_TREE,
    .supported_modes =
        RIBON_MODE_MASK(RIBON_MODE_NORMAL) |
        RIBON_MODE_MASK(RIBON_MODE_DIAGNOSTIC),
    .handoff_format = "linux-fdt",
    .handoff_major = 1u,
    .ops = &linux_ops,
};

const struct RibonPluginDescriptor ribon_linux_protocol_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_linux_protocol_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_BOOT_PROTOCOL,
    .phase = RIBON_PLUGIN_PHASE_BOOT,
    .id = "protocol.linux",
    .provides =
        RIBON_CAP_BOOT_PROTOCOL |
        RIBON_CAP_HANDOFF |
        RIBON_CAP_ENTRY_CONTRACT |
        RIBON_CAP_BOOT_CONFIRMATION,
    .requires = RIBON_CAP_IMAGE_ELF64,
    .architecture_mask = RIBON_ARCH_MASK_AARCH64 | RIBON_ARCH_MASK_RISCV64,
    .environment_mask = RIBON_ENV_MASK_RAW_FDT | RIBON_ENV_MASK_SBI,
    .mode_mask =
        RIBON_MODE_MASK(RIBON_MODE_NORMAL) |
        RIBON_MODE_MASK(RIBON_MODE_DIAGNOSTIC),
    .arena_budget = 64ull * 1024ull,
    .input_budget = 64ull * 1024ull * 1024ull,
    .output_budget = 2ull * 1024ull * 1024ull,
    .deadline_ms = 30000u,
    .operations = &linux_protocol,
    .operations_size = sizeof(linux_protocol),
    .operations_abi = 1u,
    .validate_operations = ribon_protocol_plugin_operations_are_valid,
};
