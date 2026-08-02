#include <Ribon/boot/module_bundle.h>
#include <Ribon/service/directory.h>

#include <stdint.h>
#include <stdio.h>

#include "../../src/environments/raw-fdt/raw_fdt.h"

#define TEST_PAGE_SIZE 4096u
#define TEST_PAGE_COUNT 12u

static _Alignas(TEST_PAGE_SIZE) unsigned char module_space[
    TEST_PAGE_SIZE * TEST_PAGE_COUNT];

static int failures;

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "module_bundle_tests: %s\n", (message)); \
        ++failures; \
    } \
} while (0)

_Static_assert(
    RIBON_RAW_FDT_MAX_TARGET_RESERVATIONS == 10u,
    "target reservation closure must include 8 modules");
_Static_assert(
    RIBON_RAW_FDT_MAX_RESERVATIONS == 19u,
    "raw-FDT closure must include target, firmware, and FDT reservations");
_Static_assert(
    RIBON_RAW_FDT_MAX_MEMORY_REGIONS == 39u,
    "single-bank split closure must be 2N+1");

/** @brief Test module section 밖에 disjoint bootloader/kernel range를 만든다. */
static struct RibonBootModuleBundleLayout test_layout(void) {
    const uint64_t section = (uint64_t)(uintptr_t)module_space;
    return (struct RibonBootModuleBundleLayout){
        .section_base = section,
        .section_size = sizeof(module_space),
        .bootloader_base = section - (2u * TEST_PAGE_SIZE),
        .bootloader_size = TEST_PAGE_SIZE,
        .kernel_base = section + sizeof(module_space) + TEST_PAGE_SIZE,
        .kernel_size = TEST_PAGE_SIZE,
        .alignment = TEST_PAGE_SIZE,
    };
}

/** @brief 한 component table을 v1 generated bundle shape로 감싼다. */
static struct RibonBootModuleBundle test_bundle(
    const struct RibonBootModuleComponent *components,
    uint32_t count) {
    return (struct RibonBootModuleBundle){
        .size = sizeof(struct RibonBootModuleBundle),
        .abi_version = RIBON_BOOT_MODULE_BUNDLE_ABI_VERSION,
        .components = components,
        .component_count = count,
    };
}

/** @brief Materialization 결과와 order/exact/backing 의미를 검사한다. */
static void test_positive_shapes(void) {
    const struct RibonBootModuleBundle empty = test_bundle(0, 0u);
    const struct RibonBootModuleComponent components[] = {
        {
            .name = "initial-user",
            .start = module_space,
            .end = module_space + 37u,
            .role = RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE,
        },
        {
            .name = "firmware-aux",
            .start = module_space + (2u * TEST_PAGE_SIZE),
            .end = module_space + (2u * TEST_PAGE_SIZE) + 123u,
            .role = RIBON_BOOT_MODULE_ROLE_AUXILIARY,
        },
    };
    const struct RibonBootModuleBundle bundle = test_bundle(components, 2u);
    struct RibonBootModule modules[RIBON_BOOT_MODULE_CAPACITY];
    struct RibonBootModuleBackingRange backings[RIBON_BOOT_MODULE_CAPACITY];
    struct RibonBootModuleBundleLayout layout = test_layout();
    uint32_t count = UINT32_MAX;

    CHECK(
        ribon_boot_module_bundle_materialize(
            &empty, 0, 0, 0, 0u, &count) ==
            RIBON_BOOT_MODULE_BUNDLE_STATUS_OK && count == 0u,
        "empty bundle must preserve module-free semantics");
    CHECK(
        ribon_boot_module_bundle_materialize(
            &bundle, &layout, modules, backings,
            RIBON_BOOT_MODULE_CAPACITY, &count) ==
            RIBON_BOOT_MODULE_BUNDLE_STATUS_OK,
        "mixed module bundle must materialize");
    CHECK(count == 2u, "mixed module count must be preserved");
    CHECK(
        modules[0].role == RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE &&
        modules[0].physical_address == (uint64_t)(uintptr_t)module_space &&
        modules[0].size == 37u &&
        modules[1].role == RIBON_BOOT_MODULE_ROLE_AUXILIARY &&
        modules[1].size == 123u,
        "source order, roles, and exact byte spans must be preserved");
    CHECK(
        backings[0].base == modules[0].physical_address &&
        backings[0].size == TEST_PAGE_SIZE &&
        backings[1].size == TEST_PAGE_SIZE,
        "semantic spans must project to page-aligned backing reservations");

    {
        const struct RibonBootModuleBundle one = test_bundle(&components[1], 1u);
        CHECK(
            ribon_boot_module_bundle_materialize(
                &one, &layout, modules, backings,
                RIBON_BOOT_MODULE_CAPACITY, &count) ==
                RIBON_BOOT_MODULE_BUNDLE_STATUS_OK &&
            modules[0].role == RIBON_BOOT_MODULE_ROLE_AUXILIARY,
            "auxiliary-only bundle must be valid");
    }
}

/** @brief Duplicate, capacity, range, role와 overlap을 fail-closed 검사한다. */
static void test_negative_shapes(void) {
    struct RibonBootModuleComponent components[9];
    struct RibonBootModule modules[RIBON_BOOT_MODULE_CAPACITY];
    struct RibonBootModuleBackingRange backings[RIBON_BOOT_MODULE_CAPACITY];
    struct RibonBootModuleBundleLayout layout = test_layout();
    struct RibonBootModuleBundle bundle;
    uint32_t count;

    for (uint32_t index = 0u; index < 9u; ++index) {
        components[index] = (struct RibonBootModuleComponent){
            .name = "aux",
            .start = module_space + index * TEST_PAGE_SIZE,
            .end = module_space + index * TEST_PAGE_SIZE + 1u,
            .role = RIBON_BOOT_MODULE_ROLE_AUXILIARY,
        };
    }
    bundle = test_bundle(components, 9u);
    CHECK(
        ribon_boot_module_bundle_materialize(
            &bundle, &layout, modules, backings,
            RIBON_BOOT_MODULE_CAPACITY, &count) ==
            RIBON_BOOT_MODULE_BUNDLE_STATUS_OUT_OF_CAPACITY,
        "ninth module must be rejected");

    components[0].name = "initial-a";
    components[0].role = RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE;
    components[1].name = "initial-b";
    components[1].role = RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE;
    bundle = test_bundle(components, 2u);
    CHECK(
        ribon_boot_module_bundle_materialize(
            &bundle, &layout, modules, backings, 8u, &count) ==
            RIBON_BOOT_MODULE_BUNDLE_STATUS_BAD_COMPONENT,
        "duplicate initial image must be rejected");

    components[0].role = (enum RibonBootModuleRole)99;
    bundle = test_bundle(components, 1u);
    CHECK(
        ribon_boot_module_bundle_materialize(
            &bundle, &layout, modules, backings, 8u, &count) ==
            RIBON_BOOT_MODULE_BUNDLE_STATUS_BAD_COMPONENT,
        "unknown role must be rejected");

    components[0].role = RIBON_BOOT_MODULE_ROLE_AUXILIARY;
    components[0].start = module_space;
    components[0].end = module_space;
    CHECK(
        ribon_boot_module_bundle_materialize(
            &bundle, &layout, modules, backings, 8u, &count) ==
            RIBON_BOOT_MODULE_BUNDLE_STATUS_BAD_COMPONENT,
        "zero-size component must be rejected");
    components[0].start = (const unsigned char *)(uintptr_t)(UINTPTR_MAX - 1u);
    components[0].end = (const unsigned char *)(uintptr_t)1u;
    CHECK(
        ribon_boot_module_bundle_materialize(
            &bundle, &layout, modules, backings, 8u, &count) ==
            RIBON_BOOT_MODULE_BUNDLE_STATUS_BAD_COMPONENT,
        "wrapping component range must be rejected");

    components[0].start = module_space;
    components[0].end = module_space + 1u;
    components[1] = (struct RibonBootModuleComponent){
        .name = "overlap",
        .start = module_space,
        .end = module_space + 2u,
        .role = RIBON_BOOT_MODULE_ROLE_AUXILIARY,
    };
    bundle = test_bundle(components, 2u);
    CHECK(
        ribon_boot_module_bundle_materialize(
            &bundle, &layout, modules, backings, 8u, &count) ==
            RIBON_BOOT_MODULE_BUNDLE_STATUS_OVERLAP,
        "module backing overlap must be rejected");

    bundle = test_bundle(components, 1u);
    layout.kernel_base = (uint64_t)(uintptr_t)module_space;
    CHECK(
        ribon_boot_module_bundle_materialize(
            &bundle, &layout, modules, backings, 8u, &count) ==
            RIBON_BOOT_MODULE_BUNDLE_STATUS_OVERLAP,
        "kernel/module overlap must be rejected");
    layout = test_layout();
    layout.bootloader_base = (uint64_t)(uintptr_t)module_space;
    CHECK(
        ribon_boot_module_bundle_materialize(
            &bundle, &layout, modules, backings, 8u, &count) ==
            RIBON_BOOT_MODULE_BUNDLE_STATUS_OVERLAP,
        "bootloader/module overlap must be rejected");
}

/** @brief Generated data-only provider가 Core service validation을 통과하는지 검사한다. */
static void test_service_provider(void) {
    const struct RibonBootModuleComponent component = {
        .name = "initial-user",
        .start = module_space,
        .end = module_space + 1u,
        .role = RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE,
    };
    const struct RibonBootModuleBundle bundle = test_bundle(&component, 1u);
    const struct RibonBootModuleBundleServiceOperations operations = {
        .size = sizeof(operations),
        .abi_version = RIBON_SERVICE_ABI_VERSION,
        .bundle = &bundle,
        .section_start = module_space,
        .section_end = module_space + sizeof(module_space),
    };
    const struct RibonServiceDescriptor descriptor = {
        .kind = RIBON_SERVICE_KIND_BOOT_MODULE_BUNDLE,
        .provides = RIBON_CAP_BOOT_MODULE_BUNDLE,
        .operations = &operations,
        .operations_size = sizeof(operations),
        .operations_abi = RIBON_SERVICE_ABI_VERSION,
    };
    CHECK(
        ribon_boot_module_bundle_service_operations_are_valid(&descriptor),
        "data-only generated service shape must validate");
}

int main(void) {
    test_positive_shapes();
    test_negative_shapes();
    test_service_provider();
    if (failures != 0) {
        return 1;
    }
    puts("RIBON-RAW-FDT-BOOT-MODULES-OK");
    return 0;
}
