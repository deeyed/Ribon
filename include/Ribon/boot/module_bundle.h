#ifndef RIBON_BOOT_MODULE_BUNDLE_H
#define RIBON_BOOT_MODULE_BUNDLE_H

#include <stdint.h>

#include <Ribon/firmware/environment.h>

struct RibonServiceDescriptor;

/** @brief Generated boot-module component table ABI다. */
#define RIBON_BOOT_MODULE_BUNDLE_ABI_VERSION 1u

/** @brief Generated component의 exact immutable byte span이다. */
struct RibonBootModuleComponent {
    const char *name; /**< Stable logical name이다. */
    const unsigned char *start; /**< Exact first byte다. */
    const unsigned char *end; /**< Exact one-past-last byte다. */
    enum RibonBootModuleRole role; /**< Architecture/OS-neutral semantic role다. */
};

/** @brief Product build가 생성한 immutable component table다. */
struct RibonBootModuleBundle {
    uint32_t size;
    uint32_t abi_version;
    const struct RibonBootModuleComponent *components;
    uint32_t component_count;
    uint32_t reserved;
};

/** @brief 한 component를 보존하는 page-aligned physical backing range다. */
struct RibonBootModuleBackingRange {
    uint64_t base;
    uint64_t size;
};

/** @brief Materializer가 검증할 linked-image physical 경계다. */
struct RibonBootModuleBundleLayout {
    uint64_t section_base;
    uint64_t section_size;
    uint64_t bootloader_base;
    uint64_t bootloader_size;
    uint64_t kernel_base;
    uint64_t kernel_size;
    uint64_t alignment;
};

/** @brief Boot-module materialization 결과다. */
enum RibonBootModuleBundleStatus {
    RIBON_BOOT_MODULE_BUNDLE_STATUS_OK = 0,
    RIBON_BOOT_MODULE_BUNDLE_STATUS_BAD_ARGUMENT = -1,
    RIBON_BOOT_MODULE_BUNDLE_STATUS_BAD_ABI = -2,
    RIBON_BOOT_MODULE_BUNDLE_STATUS_BAD_COMPONENT = -3,
    RIBON_BOOT_MODULE_BUNDLE_STATUS_OUT_OF_CAPACITY = -4,
    RIBON_BOOT_MODULE_BUNDLE_STATUS_OVERLAP = -5,
};

/**
 * @brief Generated exact spans를 semantic module과 page backing으로 검증 투영한다.
 *
 * 빈 bundle은 valid no-module 결과다. Non-empty bundle의 모든 component는 linked
 * module section 안에 있어야 하며 bootloader runtime, kernel placement 또는 다른
 * component와 겹치면 fail-closed한다.
 */
int ribon_boot_module_bundle_materialize(
    const struct RibonBootModuleBundle *bundle,
    const struct RibonBootModuleBundleLayout *layout,
    struct RibonBootModule *modules,
    struct RibonBootModuleBackingRange *backings,
    uint32_t capacity,
    uint32_t *count_out);

/** @brief Generated data-only module service의 exact ABI를 검사한다. */
int ribon_boot_module_bundle_service_operations_are_valid(
    const struct RibonServiceDescriptor *descriptor);

#endif
