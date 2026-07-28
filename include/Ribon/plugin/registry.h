#ifndef RIBON_PLUGIN_REGISTRY_H
#define RIBON_PLUGIN_REGISTRY_H

#include <stdint.h>

#include <Ribon/core/status.h>
#include <Ribon/plugin/descriptor.h>
#include <Ribon/plugin/manifest.h>

/** @brief Generated immutable plugin pointer array다. */
struct RibonPluginRegistry {
    uint32_t size; /**< 이 registry descriptor의 byte 크기다. */
    uint32_t abi_version; /**< `RIBON_CORE_ABI_VERSION`과 일치해야 한다. */
    const struct RibonPluginDescriptor *const *plugins; /**< Stable ID 순 pointer array다. */
    uint32_t plugin_count; /**< Plugin pointer element 수다. */
};

/** @brief Product graph가 collection plugin provider를 고정하는 selector다. */
struct RibonPluginSelection {
    enum RibonPluginKind kind; /**< 선택할 collection plugin kind다. */
    const char *id; /**< 선택한 plugin의 stable ID다. */
};

/** @brief Registry와 product tuple의 provider, phase, capability, budget을 검사한다. */
int ribon_plugin_registry_validate(
    const struct RibonPluginRegistry *registry,
    const struct RibonProductDescriptor *product,
    enum RibonMode mode);

/** @brief Stable plugin ID와 kind가 모두 일치하는 descriptor를 반환한다. */
const struct RibonPluginDescriptor *ribon_plugin_registry_find(
    const struct RibonPluginRegistry *registry,
    enum RibonPluginKind kind,
    const char *id);

/** @brief Product selector 또는 유일 provider로 선택된 plugin을 반환한다. */
const struct RibonPluginDescriptor *ribon_plugin_registry_find_selected(
    const struct RibonPluginRegistry *registry,
    const struct RibonProductDescriptor *product,
    enum RibonPluginKind kind);

/** @brief QStar가 생성한 product registry를 반환한다. */
const struct RibonPluginRegistry *ribon_generated_plugin_registry(void);

/** @brief QStar가 생성한 product descriptor를 반환한다. */
const struct RibonProductDescriptor *ribon_generated_product_descriptor(void);

#endif
