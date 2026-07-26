#ifndef RIBON_PLUGIN_MANIFEST_H
#define RIBON_PLUGIN_MANIFEST_H

#include <stdint.h>

#include <Ribon/core/capability.h>

/** @brief Product descriptor를 식별하는 magic이다. */
#define RIBON_PRODUCT_DESCRIPTOR_MAGIC 0x52425044u

/** @brief Generated registry와 함께 검증하는 immutable product descriptor다. */
struct RibonProductDescriptor {
    uint32_t magic; /**< `RIBON_PRODUCT_DESCRIPTOR_MAGIC`이어야 한다. */
    uint32_t size; /**< 이 descriptor의 byte 크기다. */
    uint32_t abi_version; /**< `RIBON_CORE_ABI_VERSION`과 일치해야 한다. */
    const char *id; /**< Stable product ID다. */
    uint32_t architecture_mask; /**< 정확히 한 architecture bit다. */
    uint32_t environment_mask; /**< 정확히 한 entry environment bit다. */
    uint32_t mode_mask; /**< 허용 mode bitset이다. */
    uint32_t max_plugins; /**< Registry plugin 수 상한이다. */
    uint64_t required_capabilities; /**< Product가 요구하는 capability다. */
    uint64_t allowed_capabilities; /**< Product가 허용하는 capability다. */
    struct RibonResourceLimits limits; /**< Product 자원 상한이다. */
};

/** @brief Product descriptor의 tuple, capability와 budget을 검사한다. */
int ribon_product_descriptor_is_valid(const struct RibonProductDescriptor *product);

#endif
