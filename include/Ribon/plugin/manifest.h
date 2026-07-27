#ifndef RIBON_PLUGIN_MANIFEST_H
#define RIBON_PLUGIN_MANIFEST_H

#include <stdint.h>

#include <Ribon/core/capability.h>
#include <Ribon/service/directory.h>

struct RibonPluginSelection;

/** @brief Product descriptor를 식별하는 magic이다. */
#define RIBON_PRODUCT_DESCRIPTOR_MAGIC 0x52425044u

/** @brief Static object graph가 만드는 product의 권한 class다. */
enum RibonProductKind {
    RIBON_PRODUCT_KIND_INVALID = 0,
    RIBON_PRODUCT_KIND_LIBRARY = 1,
    RIBON_PRODUCT_KIND_BOOTLOADER = 2,
    RIBON_PRODUCT_KIND_FIRMWARE = 3,
};

/** @brief Generated registry와 함께 검증하는 immutable product descriptor다. */
struct RibonProductDescriptor {
    uint32_t magic; /**< `RIBON_PRODUCT_DESCRIPTOR_MAGIC`이어야 한다. */
    uint32_t size; /**< 이 descriptor의 byte 크기다. */
    uint32_t abi_version; /**< `RIBON_CORE_ABI_VERSION`과 일치해야 한다. */
    const char *id; /**< Stable product ID다. */
    enum RibonProductKind kind; /**< Library, bootloader, firmware 권한 class다. */
    uint32_t architecture_mask; /**< 정확히 한 architecture bit다. */
    uint32_t environment_mask; /**< Consumer product의 정확히 한 environment bit다. */
    uint32_t personality_mask; /**< Firmware product의 정확히 한 personality bit다. */
    uint32_t mode_mask; /**< 허용 mode bitset이다. */
    uint32_t max_plugins; /**< Registry plugin 수 상한이다. */
    uint64_t required_capabilities; /**< Product가 요구하는 capability다. */
    uint64_t allowed_capabilities; /**< Product가 허용하는 capability다. */
    const struct RibonServiceSelection *service_selections; /**< Collection owner selection이다. */
    uint32_t service_selection_count; /**< Service selection element 수다. */
    const struct RibonPluginSelection *plugin_selections; /**< Collection plugin owner selection이다. */
    uint32_t plugin_selection_count; /**< Plugin selection element 수다. */
    struct RibonResourceLimits limits; /**< Product 자원 상한이다. */
};

/** @brief Product kind의 안정적인 이름을 반환한다. */
const char *ribon_product_kind_name(enum RibonProductKind kind);

/** @brief Product descriptor의 tuple, capability와 budget을 검사한다. */
int ribon_product_descriptor_is_valid(const struct RibonProductDescriptor *product);

#endif
