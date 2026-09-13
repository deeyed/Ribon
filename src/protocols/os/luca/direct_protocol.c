#include <Ribon/boot/plan.h>
#include <Ribon/plugin/descriptor.h>
#include <Ribon/protocols/os/luca/direct_fdt.h>

/** @brief Stable identifier의 exact byte equality를 검사한다. */
static int luca_direct_streq(const char *lhs, const char *rhs) {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }
    while (*lhs != '\0' && *rhs != '\0' && *lhs == *rhs) {
        ++lhs;
        ++rhs;
    }
    return *lhs == *rhs;
}

/** @brief Explicit development protocol ID와 ABI v1만 허용한다. */
static int luca_direct_match(const struct RibonManifestView *manifest) {
    if (manifest == 0 ||
        !luca_direct_streq(manifest->protocol_id, "luca-direct-fdt-dev") ||
        manifest->protocol_abi_min == 0u ||
        manifest->protocol_abi_min > 1u || manifest->protocol_abi_max < 1u ||
        manifest->protocol_abi_max < manifest->protocol_abi_min) {
        return RIBON_PROTOCOL_STATUS_BAD_MANIFEST;
    }
    return RIBON_PROTOCOL_STATUS_OK;
}

/** @brief 정확히 한 kernel과 한 initial-image component만 허용한다. */
static int luca_direct_validate_components(
    const struct RibonManifestView *manifest) {
    uint32_t kernels = 0u;
    uint32_t initial_images = 0u;
    if (luca_direct_match(manifest) != RIBON_PROTOCOL_STATUS_OK ||
        manifest->components == 0 || manifest->component_count != 2u) {
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
        } else if (component->role == RIBON_COMPONENT_ROLE_BOOT_MODULE) {
            ++initial_images;
        } else {
            return RIBON_PROTOCOL_STATUS_BAD_COMPONENTS;
        }
    }
    return kernels == 1u && initial_images == 1u ?
        RIBON_PROTOCOL_STATUS_OK : RIBON_PROTOCOL_STATUS_BAD_COMPONENTS;
}

/**
 * @brief Return the executable image formats admitted by direct-FDT boot.
 *
 * @return A format mask containing only ELF64, the development protocol's
 * exact kernel executable representation.
 */
static uint64_t luca_direct_select_image_formats(void) {
    return RIBON_IMAGE_FORMAT_MASK(RIBON_EXECUTABLE_FORMAT_ELF64);
}

/**
 * @brief Test whether two physical half-open ranges overlap or overflow.
 *
 * Zero-sized and overflowing spans are invalid and therefore rejected through
 * the same true result as a real overlap.
 *
 * @param lhs_base Physical start address of the first span.
 * @param lhs_size Size in bytes of the first span.
 * @param rhs_base Physical start address of the second span.
 * @param rhs_size Size in bytes of the second span.
 * @return Nonzero when either span is invalid or their address sets overlap;
 * zero only for two valid, disjoint spans.
 */
static int luca_direct_ranges_overlap(
    uint64_t lhs_base,
    uint64_t lhs_size,
    uint64_t rhs_base,
    uint64_t rhs_size) {
    return lhs_size == 0u || rhs_size == 0u ||
           lhs_base > UINT64_MAX - lhs_size || rhs_base > UINT64_MAX - rhs_size ||
           (lhs_base < rhs_base + rhs_size && rhs_base < lhs_base + lhs_size);
}

/**
 * @brief Validate that a runtime-owned span is wholly inside direct-FDT RAM.
 *
 * The half-open range `[base, base + size)` must be nonempty, must not
 * overflow, and must remain inside the fixed development RAM window. Callers
 * use this predicate before reserving loader-owned buffers or publishing their
 * lifetime in the FDT; a false result is an admission failure and publishes no
 * partial handoff state.
 *
 * @param base Physical start address of the candidate span.
 * @param size Size in bytes of the candidate span.
 * @return Nonzero when the complete half-open range is admissible, otherwise
 * zero.
 */
int ribon_luca_direct_fdt_runtime_span_valid(uint64_t base, uint64_t size) {
    return size != 0u &&
           base >= RIBON_LUCA_DIRECT_FDT_RUNTIME_RAM_START &&
           base < RIBON_LUCA_DIRECT_FDT_RUNTIME_RAM_END &&
           size <= RIBON_LUCA_DIRECT_FDT_RUNTIME_RAM_END - base;
}

/** @brief Reservation을 sorted merged set에 추가한다. */
static int luca_direct_add_reservation(
    struct RibonMemoryRegion ranges[RIBON_LUCA_DIRECT_FDT_RESERVATION_CAPACITY],
    uint32_t *count,
    uint64_t base,
    uint64_t length) {
    uint32_t insert = 0u;
    if (count == 0 || length == 0u || base > UINT64_MAX - length) {
        return 0;
    }
    while (insert < *count && ranges[insert].base < base) {
        ++insert;
    }
    if (*count == RIBON_LUCA_DIRECT_FDT_RESERVATION_CAPACITY) {
        return 0;
    }
    for (uint32_t index = *count; index > insert; --index) {
        ranges[index] = ranges[index - 1u];
    }
    ranges[insert] = (struct RibonMemoryRegion){
        .base = base,
        .length = length,
        .kind = RIBON_MEMORY_REGION_RESERVED,
    };
    ++*count;
    for (uint32_t index = 1u; index < *count;) {
        struct RibonMemoryRegion *previous = &ranges[index - 1u];
        const uint64_t previous_end = previous->base + previous->length;
        const uint64_t current_end = ranges[index].base + ranges[index].length;
        if (ranges[index].base > previous_end) {
            ++index;
            continue;
        }
        if (current_end > previous_end) {
            previous->length = current_end - previous->base;
        }
        for (uint32_t move = index + 1u; move < *count; ++move) {
            ranges[move - 1u] = ranges[move];
        }
        --*count;
    }
    return 1;
}

/** @brief Final normalized map에서 exit 이후에도 reclaim할 수 없는 RAM을 수집한다. */
static int luca_direct_add_firmware_reservations(
    const struct RibonMutableMemoryMap *memory_map,
    struct RibonMemoryRegion ranges[RIBON_LUCA_DIRECT_FDT_RESERVATION_CAPACITY],
    uint32_t *count) {
    if (memory_map == 0 || memory_map->regions == 0 ||
        memory_map->region_count > memory_map->capacity) {
        return 0;
    }
    for (uint32_t index = 0u; index < memory_map->region_count; ++index) {
        const struct RibonMemoryRegion *region = &memory_map->regions[index];
        if (region->kind == RIBON_MEMORY_REGION_USABLE ||
            region->kind == RIBON_MEMORY_REGION_MMIO ||
            region->kind == RIBON_MEMORY_REGION_FRAMEBUFFER ||
            (region->attributes & RIBON_MEMORY_ATTR_BOOT_RECLAIMABLE) != 0u) {
            continue;
        }
        if (!luca_direct_add_reservation(
                ranges, count, region->base, region->length)) {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Build the bounded LUCA development direct-FDT handoff artifact.
 *
 * The function validates the exact kernel and initial-World components,
 * rejects overlaps and malformed input, records their half-open physical
 * ranges under `/chosen`, and reserves every live handoff buffer. It writes
 * only to caller-owned storage and performs no allocation or firmware service
 * call. On failure the returned status identifies the rejected contract and
 * the output must not be transferred to LUCA. On success the output remains
 * valid only while the caller preserves the component images, FDT storage, and
 * every range described by the emitted reservation set.
 *
 * @param plan Validated direct-FDT boot plan containing the exact kernel and
 * initial-World components.
 * @param environment Immutable platform FDT and runtime service inputs.
 * @param normalized_memory_map Final normalized firmware memory map whose live
 * reservations must remain unavailable to LUCA.
 * @param buffer Caller-owned storage that receives the serialized FDT.
 * @param capacity Available byte capacity of `buffer`.
 * @param out Caller-owned artifact descriptor receiving the serialized FDT
 * address, used size, entry point, and bounded reservation metadata.
 * @return `RIBON_PROTOCOL_STATUS_OK` on a complete artifact, or the precise
 * protocol status for invalid components, address ranges, capacity, or FDT
 * input.
 */
int ribon_luca_build_direct_fdt(
    const struct RibonBootPlan *plan,
    const struct RibonBootEnvironment *environment,
    const struct RibonMutableMemoryMap *normalized_memory_map,
    void *buffer,
    uint64_t capacity,
    struct RibonHandoffArtifact *out) {
    struct RibonMemoryRegion reservations[
        RIBON_LUCA_DIRECT_FDT_RESERVATION_CAPACITY];
    uint32_t reservation_count = 0u;
    uint64_t output_size = 0u;
    uint64_t kernel_start = UINT64_MAX;
    uint64_t kernel_end = 0u;
    const struct RibonBootModule *world;
    if (out != 0) {
        *out = (struct RibonHandoffArtifact){0};
    }
    if (plan == 0 || environment == 0 || buffer == 0 || out == 0 ||
        plan->arch == 0 || plan->arch->id != RIBON_ARCHITECTURE_AARCH64 ||
        plan->environment != RIBON_ENVIRONMENT_UEFI ||
        plan->kernel_runtime_entry_address == 0u ||
        plan->kernel_load_segments == 0 || plan->kernel_load_segment_count == 0u ||
        environment->architecture != RIBON_ARCHITECTURE_AARCH64 ||
        environment->kind != RIBON_ENVIRONMENT_UEFI ||
        environment->device_tree.data == 0 || environment->device_tree.size < 40u ||
        environment->boot_modules.modules == 0 ||
        environment->boot_modules.module_count != 1u ||
        environment->command_line.length != 0u ||
        (environment->flags & (RIBON_BOOT_ENV_HAS_MEMORY_MAP |
                               RIBON_BOOT_ENV_HAS_DEVICE_TREE |
                               RIBON_BOOT_ENV_HAS_BOOT_MODULES)) !=
            (RIBON_BOOT_ENV_HAS_MEMORY_MAP |
             RIBON_BOOT_ENV_HAS_DEVICE_TREE |
             RIBON_BOOT_ENV_HAS_BOOT_MODULES)) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_BAD_ARGUMENT;
    }
    world = &environment->boot_modules.modules[0];
    if (world->role != RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE ||
        world->physical_address == 0u || world->size == 0u ||
        !ribon_luca_direct_fdt_runtime_span_valid(
            world->physical_address, world->size) ||
        luca_direct_ranges_overlap(
            world->physical_address,
            world->size,
            (uint64_t)(uintptr_t)buffer,
            capacity)) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN;
    }
    for (uint32_t index = 0u; index < plan->kernel_load_segment_count; ++index) {
        const struct RibonLoadSegment *segment = &plan->kernel_load_segments[index];
        const uint64_t base = segment->runtime_address != 0u ?
            segment->runtime_address : segment->load_address;
        uint64_t end;
        if (segment->memory_size == 0u ||
            base > UINT64_MAX - segment->memory_size ||
            luca_direct_ranges_overlap(base, segment->memory_size,
                                       world->physical_address, world->size)) {
            return RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN;
        }
        end = base + segment->memory_size;
        if (base < kernel_start) {
            kernel_start = base;
        }
        if (end > kernel_end) {
            kernel_end = end;
        }
    }
    if (kernel_start == UINT64_MAX || kernel_end <= kernel_start ||
        !ribon_luca_direct_fdt_runtime_span_valid(
            kernel_start, kernel_end - kernel_start) ||
        plan->kernel_runtime_entry_address < kernel_start ||
        plan->kernel_runtime_entry_address >= kernel_end ||
        !luca_direct_add_reservation(
            reservations, &reservation_count,
            kernel_start, kernel_end - kernel_start)) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN;
    }
    if (!luca_direct_add_reservation(
            reservations, &reservation_count,
            world->physical_address, world->size) ||
        !luca_direct_add_reservation(
            reservations, &reservation_count,
            (uint64_t)(uintptr_t)buffer, capacity) ||
        !luca_direct_add_firmware_reservations(
            normalized_memory_map, reservations, &reservation_count)) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_OUT_OF_CAPACITY;
    }
    if (!ribon_luca_direct_fdt_build_blob(
            environment->device_tree.data,
            environment->device_tree.size,
            kernel_start,
            kernel_end - kernel_start,
            plan->kernel_runtime_entry_address,
            world->physical_address,
            world->size,
            reservations,
            reservation_count,
            buffer,
            capacity,
            &output_size)) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN;
    }
    *out = (struct RibonHandoffArtifact){
        .data = buffer,
        .size = output_size,
        .format = "fdt",
        .version_major = 17u,
        .section_count = 3u,
    };
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}

/** @brief AArch64 direct-FDT register and entry-state contract를 봉인한다. */
static int luca_direct_prepare_terminal(
    const struct RibonArchDescriptor *arch,
    const struct RibonBootPlan *plan,
    const struct RibonBootEnvironment *environment,
    const struct RibonHandoffArtifact *handoff,
    struct RibonTerminalRequest *out) {
    (void)environment;
    if (arch == 0 || arch->id != RIBON_ARCHITECTURE_AARCH64 || plan == 0 ||
        handoff == 0 || handoff->data == 0 || handoff->size < 40u || out == 0 ||
        plan->kernel_runtime_entry_address == 0u ||
        !ribon_luca_direct_fdt_runtime_span_valid(
            (uint64_t)(uintptr_t)handoff->data, handoff->size)) {
        return RIBON_PROTOCOL_STATUS_BAD_ENTRY_CONTRACT;
    }
    *out = (struct RibonTerminalRequest){
        .size = sizeof(*out),
        .abi_version = RIBON_TERMINAL_REQUEST_ABI_VERSION,
        .kind = RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY,
        .direct_entry = {
            .size = sizeof(struct RibonEntryInvocation),
            .abi_version = RIBON_ENTRY_INVOCATION_ABI_VERSION,
            .entry_address = plan->kernel_runtime_entry_address,
            .register_abi = RIBON_REGISTER_ABI_AARCH64_X0_X1_X2_X3,
            .argument_count = 1u,
            .arguments = {(uint64_t)(uintptr_t)handoff->data, 0u, 0u, 0u},
            .interrupts = RIBON_ENTRY_INTERRUPTS_MASKED,
            .privilege = RIBON_ENTRY_PRIVILEGE_AARCH64_EL1,
            .translation = RIBON_ENTRY_TRANSLATION_DISABLED,
        },
    };
    return RIBON_PROTOCOL_STATUS_OK;
}

static int luca_direct_validate_boot_health(
    const struct RibonBootHealthPayload *payload) {
    (void)payload;
    return RIBON_PROTOCOL_STATUS_UNSUPPORTED;
}

static const struct RibonBootProtocolOps luca_direct_ops = {
    .size = sizeof(luca_direct_ops),
    .abi_version = RIBON_BOOT_PROTOCOL_OPS_ABI_VERSION,
    .match = luca_direct_match,
    .validate_components = luca_direct_validate_components,
    .select_image_formats = luca_direct_select_image_formats,
    .prepare_handoff = ribon_luca_build_direct_fdt,
    .prepare_terminal = luca_direct_prepare_terminal,
    .validate_boot_health = luca_direct_validate_boot_health,
};

static const struct RibonBootProtocol luca_direct_protocol = {
    .size = sizeof(luca_direct_protocol),
    .abi_version = 1u,
    .id = "luca-direct-fdt-dev",
    .kernel_path = "kernel/luca.elf",
    .terminal_execution = RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY,
    .expectations =
        RIBON_PROTOCOL_EXPECT_MEMORY_MAP |
        RIBON_PROTOCOL_EXPECT_KERNEL_IMAGE_LAYOUT |
        RIBON_PROTOCOL_ALLOW_DEVICE_TREE |
        RIBON_PROTOCOL_ALLOW_BOOT_MODULES,
    .supported_modes = RIBON_MODE_MASK(RIBON_MODE_NORMAL),
    .handoff_format = "fdt",
    .handoff_major = 17u,
    .ops = &luca_direct_ops,
};

const struct RibonPluginDescriptor
ribon_luca_direct_fdt_protocol_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_luca_direct_fdt_protocol_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_BOOT_PROTOCOL,
    .phase = RIBON_PLUGIN_PHASE_BOOT,
    .id = "protocol.luca-direct-fdt-dev",
    .provides =
        RIBON_CAP_BOOT_PROTOCOL |
        RIBON_CAP_HANDOFF |
        RIBON_CAP_ENTRY_CONTRACT |
        RIBON_CAP_BOOT_CONFIRMATION,
    .requires = RIBON_CAP_IMAGE_ELF64,
    .architecture_mask = RIBON_ARCH_MASK_AARCH64,
    .environment_mask = RIBON_ENV_MASK_UEFI,
    .mode_mask = RIBON_MODE_MASK(RIBON_MODE_NORMAL),
    .arena_budget = 64ull * 1024ull,
    .input_budget = 64ull * 1024ull * 1024ull,
    .output_budget = 64ull * 1024ull,
    .deadline_ms = 30000u,
    .operations = &luca_direct_protocol,
    .operations_size = sizeof(luca_direct_protocol),
    .operations_abi = 1u,
    .validate_operations = ribon_protocol_plugin_operations_are_valid,
};
