#include <Ribon/boot/module_bundle.h>
#include <Ribon/service/directory.h>

#include <stddef.h>
#include <stdint.h>

/** @brief Base+size range가 non-wrapping인지 검사한다. */
static int module_range_end(uint64_t base, uint64_t size, uint64_t *end_out) {
    if (end_out == 0 || size == 0u || base > UINT64_MAX - size) {
        return 0;
    }
    *end_out = base + size;
    return 1;
}

/** @brief 두 half-open physical range가 겹치는지 검사한다. */
static int module_ranges_overlap(
    uint64_t left_base,
    uint64_t left_end,
    uint64_t right_base,
    uint64_t right_end) {
    return left_base < right_end && right_base < left_end;
}

/** @brief Stable logical name이 bounded non-empty ASCII인지 검사한다. */
static int module_name_is_valid(const char *name) {
    if (name == 0) {
        return 0;
    }
    for (uint32_t index = 0u; index < 64u; ++index) {
        const unsigned char value = (unsigned char)name[index];
        if (value == '\0') {
            return index != 0u;
        }
        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '.' ||
              value == '_' || value == '-')) {
            return 0;
        }
    }
    return 0;
}

/** @brief Generated exact spans를 semantic module과 page backing으로 검증 투영한다. */
int ribon_boot_module_bundle_materialize(
    const struct RibonBootModuleBundle *bundle,
    const struct RibonBootModuleBundleLayout *layout,
    struct RibonBootModule *modules,
    struct RibonBootModuleBackingRange *backings,
    uint32_t capacity,
    uint32_t *count_out) {
    uint64_t section_end = 0u;
    uint64_t bootloader_end = 0u;
    uint64_t kernel_end = 0u;
    uint32_t initial_images = 0u;

    if (count_out == 0) {
        return RIBON_BOOT_MODULE_BUNDLE_STATUS_BAD_ARGUMENT;
    }
    *count_out = 0u;
    if (bundle == 0) {
        return RIBON_BOOT_MODULE_BUNDLE_STATUS_OK;
    }
    if (bundle->size != sizeof(*bundle) ||
        bundle->abi_version != RIBON_BOOT_MODULE_BUNDLE_ABI_VERSION ||
        bundle->reserved != 0u) {
        return RIBON_BOOT_MODULE_BUNDLE_STATUS_BAD_ABI;
    }
    if (bundle->component_count == 0u) {
        return bundle->components == 0 ? RIBON_BOOT_MODULE_BUNDLE_STATUS_OK :
            RIBON_BOOT_MODULE_BUNDLE_STATUS_BAD_COMPONENT;
    }
    if (layout == 0 || modules == 0 || backings == 0 ||
        bundle->components == 0 ||
        bundle->component_count > RIBON_BOOT_MODULE_CAPACITY ||
        bundle->component_count > capacity) {
        return RIBON_BOOT_MODULE_BUNDLE_STATUS_OUT_OF_CAPACITY;
    }
    if (layout->alignment == 0u ||
        (layout->alignment & (layout->alignment - 1u)) != 0u ||
        !module_range_end(layout->section_base, layout->section_size, &section_end) ||
        !module_range_end(
            layout->bootloader_base, layout->bootloader_size, &bootloader_end) ||
        !module_range_end(layout->kernel_base, layout->kernel_size, &kernel_end) ||
        (layout->section_base & (layout->alignment - 1u)) != 0u ||
        (section_end & (layout->alignment - 1u)) != 0u) {
        return RIBON_BOOT_MODULE_BUNDLE_STATUS_BAD_ARGUMENT;
    }

    for (uint32_t index = 0u; index < bundle->component_count; ++index) {
        const struct RibonBootModuleComponent *component =
            &bundle->components[index];
        const uint64_t start = (uint64_t)(uintptr_t)component->start;
        const uint64_t exact_end = (uint64_t)(uintptr_t)component->end;
        uint64_t backing_end;

        if (!module_name_is_valid(component->name) ||
            start == 0u || exact_end <= start ||
            start < layout->section_base || exact_end > section_end ||
            (start & (layout->alignment - 1u)) != 0u ||
            (component->role != RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE &&
             component->role != RIBON_BOOT_MODULE_ROLE_AUXILIARY) ||
            exact_end > UINT64_MAX - (layout->alignment - 1u)) {
            return RIBON_BOOT_MODULE_BUNDLE_STATUS_BAD_COMPONENT;
        }
        if (component->role == RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE &&
            ++initial_images > 1u) {
            return RIBON_BOOT_MODULE_BUNDLE_STATUS_BAD_COMPONENT;
        }
        backing_end = (exact_end + layout->alignment - 1u) &
            ~(layout->alignment - 1u);
        if (backing_end <= start || backing_end > section_end ||
            module_ranges_overlap(
                start, backing_end, layout->bootloader_base, bootloader_end) ||
            module_ranges_overlap(
                start, backing_end, layout->kernel_base, kernel_end)) {
            return RIBON_BOOT_MODULE_BUNDLE_STATUS_OVERLAP;
        }
        for (uint32_t previous = 0u; previous < index; ++previous) {
            const uint64_t previous_end =
                backings[previous].base + backings[previous].size;
            if (module_ranges_overlap(
                    start, backing_end, backings[previous].base, previous_end)) {
                return RIBON_BOOT_MODULE_BUNDLE_STATUS_OVERLAP;
            }
        }
        modules[index] = (struct RibonBootModule){
            .name = component->name,
            .physical_address = start,
            .size = exact_end - start,
            .role = component->role,
        };
        backings[index] = (struct RibonBootModuleBackingRange){
            .base = start,
            .size = backing_end - start,
        };
    }
    *count_out = bundle->component_count;
    return RIBON_BOOT_MODULE_BUNDLE_STATUS_OK;
}

/** @brief Generated data-only module service의 exact ABI를 검사한다. */
int ribon_boot_module_bundle_service_operations_are_valid(
    const struct RibonServiceDescriptor *descriptor) {
    const struct RibonBootModuleBundleServiceOperations *operations;
    const struct RibonBootModuleBundle *bundle;
    uint32_t initial_images = 0u;
    if (descriptor == 0 ||
        descriptor->kind != RIBON_SERVICE_KIND_BOOT_MODULE_BUNDLE ||
        descriptor->provides != RIBON_CAP_BOOT_MODULE_BUNDLE ||
        descriptor->operations_size != sizeof(*operations) ||
        descriptor->operations_abi != RIBON_SERVICE_ABI_VERSION) {
        return 0;
    }
    operations = descriptor->operations;
    if (operations == 0 || operations->size != sizeof(*operations) ||
        operations->abi_version != RIBON_SERVICE_ABI_VERSION ||
        operations->bundle == 0 || operations->section_start == 0 ||
        operations->section_end <= operations->section_start) {
        return 0;
    }
    bundle = operations->bundle;
    if (bundle->size != sizeof(*bundle) ||
        bundle->abi_version != RIBON_BOOT_MODULE_BUNDLE_ABI_VERSION ||
        bundle->reserved != 0u || bundle->components == 0 ||
        bundle->component_count == 0u ||
        bundle->component_count > RIBON_BOOT_MODULE_CAPACITY) {
        return 0;
    }
    for (uint32_t index = 0u; index < bundle->component_count; ++index) {
        const struct RibonBootModuleComponent *component =
            &bundle->components[index];
        const uintptr_t component_start = (uintptr_t)component->start;
        const uintptr_t component_end = (uintptr_t)component->end;
        const uintptr_t section_start = (uintptr_t)operations->section_start;
        const uintptr_t section_end = (uintptr_t)operations->section_end;
        if (!module_name_is_valid(component->name) ||
            component_start < section_start ||
            component_end <= component_start || component_end > section_end ||
            (component->role != RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE &&
             component->role != RIBON_BOOT_MODULE_ROLE_AUXILIARY)) {
            return 0;
        }
        if (component->role == RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE &&
            ++initial_images > 1u) {
            return 0;
        }
    }
    return 1;
}
