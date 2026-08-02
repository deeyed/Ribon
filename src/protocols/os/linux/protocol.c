#include <Ribon/boot/plan.h>
#include <Ribon/plugin/descriptor.h>
#include <Ribon/protocols/os/linux/boot.h>
#include <Ribon/protocols/os/linux/fdt.h>

#define RIBON_LINUX_INITRAMFS_NAME "initramfs"
#define RIBON_LINUX_INITRD_WINDOW_SIZE (32ull * 1024ull * 1024ull * 1024ull)

/** @brief Linux stable ID와 module 이름을 allocation 없이 비교한다. */
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
    return manifest != 0 && linux_streq(manifest->protocol_id, "linux") &&
           manifest->protocol_abi_min != 0u &&
           manifest->protocol_abi_min <= 1u &&
           manifest->protocol_abi_max >= 1u &&
           manifest->protocol_abi_max >= manifest->protocol_abi_min ?
        RIBON_PROTOCOL_STATUS_OK : RIBON_PROTOCOL_STATUS_BAD_MANIFEST;
}

/** @brief Linux Image, FDT, 선택적 initramfs tuple을 bounded하게 검사한다. */
static int linux_validate_components(const struct RibonManifestView *manifest) {
    uint32_t kernels = 0u;
    uint32_t device_trees = 0u;
    uint32_t initramfs = 0u;
    if (linux_match(manifest) != RIBON_PROTOCOL_STATUS_OK ||
        manifest->components == 0 || manifest->component_count == 0u ||
        manifest->component_count > 3u) {
        return RIBON_PROTOCOL_STATUS_BAD_COMPONENTS;
    }
    for (uint32_t index = 0u; index < manifest->component_count; ++index) {
        const struct RibonComponentDescriptor *component = &manifest->components[index];
        if (component->name == 0 || component->size == 0u ||
            component->flags != RIBON_COMPONENT_FLAGS_NONE) {
            return RIBON_PROTOCOL_STATUS_BAD_COMPONENTS;
        }
        if (component->role == RIBON_COMPONENT_ROLE_KERNEL) {
            ++kernels;
        } else if (component->role == RIBON_COMPONENT_ROLE_DEVICE_TREE) {
            ++device_trees;
        } else if (component->role == RIBON_COMPONENT_ROLE_BOOT_MODULE &&
                   linux_streq(component->name, RIBON_LINUX_INITRAMFS_NAME)) {
            ++initramfs;
        } else {
            return RIBON_PROTOCOL_STATUS_BAD_COMPONENTS;
        }
    }
    return kernels == 1u && device_trees == 1u && initramfs <= 1u ?
        RIBON_PROTOCOL_STATUS_OK : RIBON_PROTOCOL_STATUS_BAD_COMPONENTS;
}

/** @brief Linux AArch64 raw Image만 허용한다. */
static uint64_t linux_select_image_formats(void) {
    return RIBON_IMAGE_FORMAT_MASK(RIBON_EXECUTABLE_FORMAT_LINUX_AARCH64);
}

/** @brief Half-open range가 서로 겹치는지 검사한다. */
static int linux_ranges_overlap(
    uint64_t lhs_base,
    uint64_t lhs_size,
    uint64_t rhs_base,
    uint64_t rhs_size) {
    uint64_t lhs_end;
    uint64_t rhs_end;
    if (lhs_size == 0u || rhs_size == 0u || lhs_base > UINT64_MAX - lhs_size ||
        rhs_base > UINT64_MAX - rhs_size) {
        return 1;
    }
    lhs_end = lhs_base + lhs_size;
    rhs_end = rhs_base + rhs_size;
    return lhs_base < rhs_end && rhs_base < lhs_end;
}

/** @brief Module range가 normalized BOOT_MODULE reservation으로 덮이는지 검사한다. */
static int linux_module_is_reserved(
    const struct RibonMutableMemoryMap *memory_map,
    uint64_t base,
    uint64_t size) {
    uint64_t end;
    if (memory_map == 0 || memory_map->regions == 0 || size == 0u ||
        base > UINT64_MAX - size) {
        return 0;
    }
    end = base + size;
    for (uint32_t index = 0u; index < memory_map->region_count; ++index) {
        const struct RibonMemoryRegion *region = &memory_map->regions[index];
        uint64_t region_end;
        if (region->kind == RIBON_MEMORY_REGION_BOOT_MODULE &&
            ribon_memory_region_end(region, &region_end) == RIBON_MEMORY_STATUS_OK &&
            region->base <= base && region_end >= end) {
            return 1;
        }
    }
    return 0;
}

/** @brief Validated FDT를 compact handoff로 만들고 선택적 initramfs를 게시한다. */
static int linux_prepare_handoff(
    const struct RibonBootPlan *plan,
    const struct RibonBootEnvironment *environment,
    const struct RibonMutableMemoryMap *normalized_memory_map,
    void *buffer,
    uint64_t capacity,
    struct RibonHandoffArtifact *out) {
    uint64_t initrd_start = 0u;
    uint64_t initrd_size = 0u;
    uint64_t output_size;
    if (plan == 0 || environment == 0 || buffer == 0 || out == 0 ||
        plan->arch == 0 || plan->arch->id != RIBON_ARCHITECTURE_AARCH64 ||
        plan->kernel_format != RIBON_EXECUTABLE_FORMAT_LINUX_AARCH64 ||
        plan->kernel_runtime_entry_address == 0u ||
        plan->kernel_runtime_load_end <= plan->kernel_runtime_load_base ||
        (environment->flags & RIBON_BOOT_ENV_HAS_DEVICE_TREE) == 0u ||
        environment->device_tree.data == 0 || environment->device_tree.size == 0u ||
        environment->boot_modules.module_count > 1u) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN;
    }
    if (environment->boot_modules.module_count == 1u) {
        const struct RibonBootModule *module = &environment->boot_modules.modules[0];
        const uint64_t window_base =
            plan->kernel_runtime_load_base & ~(RIBON_LINUX_INITRD_WINDOW_SIZE - 1u);
        uint64_t window_end;
        if (module->role != RIBON_BOOT_MODULE_ROLE_AUXILIARY ||
            !linux_streq(module->name, RIBON_LINUX_INITRAMFS_NAME) ||
            module->physical_address == 0u || module->size == 0u ||
            module->physical_address > UINT64_MAX - module->size ||
            window_base > UINT64_MAX - RIBON_LINUX_INITRD_WINDOW_SIZE ||
            (window_end = window_base + RIBON_LINUX_INITRD_WINDOW_SIZE) <
                module->physical_address + module->size ||
            module->physical_address < window_base ||
            linux_ranges_overlap(
                module->physical_address,
                module->size,
                plan->kernel_runtime_load_base,
                plan->kernel_runtime_load_end - plan->kernel_runtime_load_base) ||
            !linux_module_is_reserved(
                normalized_memory_map, module->physical_address, module->size)) {
            return RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN;
        }
        initrd_start = module->physical_address;
        initrd_size = module->size;
    }
    if (!ribon_linux_fdt_build(
            environment->device_tree.data,
            environment->device_tree.size,
            initrd_start,
            initrd_size,
            buffer,
            capacity,
            &output_size)) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN;
    }
    *out = (struct RibonHandoffArtifact){
        .data = buffer,
        .size = output_size,
        .format = "linux-fdt",
        .version_major = 1u,
        .section_count = 1u,
    };
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}

/** @brief Linux AArch64 entry의 x0=FDT, x1..x3=0 계약을 봉인한다. */
static int linux_prepare_entry_invocation(
    const struct RibonArchDescriptor *arch,
    const struct RibonBootPlan *plan,
    const struct RibonBootEnvironment *environment,
    const struct RibonHandoffArtifact *handoff,
    struct RibonEntryInvocation *out) {
    (void)environment;
    if (arch == 0 || plan == 0 || handoff == 0 || handoff->data == 0 || out == 0 ||
        arch->id != RIBON_ARCHITECTURE_AARCH64 ||
        plan->kernel_format != RIBON_EXECUTABLE_FORMAT_LINUX_AARCH64 ||
        plan->kernel_runtime_entry_address == 0u || handoff->size == 0u) {
        return RIBON_PROTOCOL_STATUS_BAD_ARGUMENT;
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

/** @brief Linux health producer 계약이 열리기 전에는 payload를 거부한다. */
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
    .kernel_path = "linux/Image",
    .expectations =
        RIBON_PROTOCOL_EXPECT_MEMORY_MAP |
        RIBON_PROTOCOL_EXPECT_KERNEL_IMAGE_LAYOUT |
        RIBON_PROTOCOL_ALLOW_DEVICE_TREE |
        RIBON_PROTOCOL_ALLOW_BOOT_MODULES,
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
    .requires = RIBON_CAP_IMAGE_LINUX_AARCH64,
    .architecture_mask = RIBON_ARCH_MASK_AARCH64,
    .environment_mask = RIBON_ENV_MASK_RAW_FDT,
    .mode_mask =
        RIBON_MODE_MASK(RIBON_MODE_NORMAL) |
        RIBON_MODE_MASK(RIBON_MODE_DIAGNOSTIC),
    .arena_budget = 64ull * 1024ull,
    .input_budget = 64ull * 1024ull * 1024ull,
    .output_budget = 64ull * 1024ull,
    .deadline_ms = 30000u,
    .operations = &linux_protocol,
    .operations_size = sizeof(linux_protocol),
    .operations_abi = 1u,
    .validate_operations = ribon_protocol_plugin_operations_are_valid,
};
