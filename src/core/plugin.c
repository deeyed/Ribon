#include <Ribon/plugin/descriptor.h>
#include <Ribon/plugin/manifest.h>

#include <stddef.h>

/** @brief Plugin phase의 안정적인 이름을 반환한다. */
const char *ribon_plugin_phase_name(enum RibonPluginPhase phase) {
    switch (phase) {
    case RIBON_PLUGIN_PHASE_EARLY:
        return "early";
    case RIBON_PLUGIN_PHASE_FOUNDATION:
        return "foundation";
    case RIBON_PLUGIN_PHASE_DRIVER:
        return "driver";
    case RIBON_PLUGIN_PHASE_BOOT:
        return "boot";
    case RIBON_PLUGIN_PHASE_QUIESCE:
        return "quiesce";
    case RIBON_PLUGIN_PHASE_RUNTIME:
        return "runtime";
    default:
        return "unknown";
    }
}

/** @brief Plugin kind의 안정적인 이름을 반환한다. */
const char *ribon_plugin_kind_name(enum RibonPluginKind kind) {
    switch (kind) {
    case RIBON_PLUGIN_KIND_ARCHITECTURE:
        return "architecture";
    case RIBON_PLUGIN_KIND_ENVIRONMENT:
        return "environment";
    case RIBON_PLUGIN_KIND_IMAGE_FORMAT:
        return "image-format";
    case RIBON_PLUGIN_KIND_BOOT_PROTOCOL:
        return "boot-protocol";
    case RIBON_PLUGIN_KIND_POLICY:
        return "policy";
    case RIBON_PLUGIN_KIND_FIRMWARE_PERSONALITY:
        return "firmware-personality";
    case RIBON_PLUGIN_KIND_PLATFORM:
        return "platform";
    case RIBON_PLUGIN_KIND_SERVICE:
        return "service";
    default:
        return "unknown";
    }
}

/** @brief Product kind의 안정적인 이름을 반환한다. */
const char *ribon_product_kind_name(enum RibonProductKind kind) {
    switch (kind) {
    case RIBON_PRODUCT_KIND_LIBRARY:
        return "library";
    case RIBON_PRODUCT_KIND_BOOTLOADER:
        return "bootloader";
    case RIBON_PRODUCT_KIND_FIRMWARE:
        return "firmware";
    default:
        return "invalid";
    }
}

/** @brief 한 32-bit 값이 정확히 한 compatibility bit인지 검사한다. */
static int plugin_one_bit(uint32_t value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}

/** @brief Plugin descriptor의 독립 field와 typed operation table을 검사한다. */
int ribon_plugin_descriptor_is_valid(const struct RibonPluginDescriptor *descriptor) {
    if (descriptor == 0 ||
        descriptor->magic != RIBON_PLUGIN_DESCRIPTOR_MAGIC ||
        descriptor->size != sizeof(*descriptor) ||
        descriptor->abi_major != RIBON_PLUGIN_ABI_MAJOR ||
        descriptor->abi_minor > RIBON_PLUGIN_ABI_MINOR ||
        descriptor->kind < RIBON_PLUGIN_KIND_ARCHITECTURE ||
        descriptor->kind > RIBON_PLUGIN_KIND_SERVICE ||
        descriptor->phase < RIBON_PLUGIN_PHASE_EARLY ||
        descriptor->phase > RIBON_PLUGIN_PHASE_RUNTIME ||
        descriptor->id == 0 ||
        descriptor->id[0] == '\0' ||
        descriptor->provides == 0u ||
        (descriptor->provides & ~RIBON_CAP_ALL) != 0u ||
        (descriptor->requires & ~RIBON_CAP_ALL) != 0u ||
        (descriptor->architecture_mask & ~RIBON_ARCH_MASK_ALL) != 0u ||
        descriptor->architecture_mask == 0u ||
        (descriptor->environment_mask & ~RIBON_ENV_MASK_ALL) != 0u ||
        (descriptor->personality_mask & ~RIBON_PERSONALITY_MASK_ALL) != 0u ||
        (descriptor->environment_mask == 0u &&
         descriptor->personality_mask == 0u) ||
        (descriptor->mode_mask & ~RIBON_MODE_MASK_ALL) != 0u ||
        descriptor->mode_mask == 0u ||
        descriptor->reserved != 0u ||
        descriptor->arena_budget == 0u ||
        descriptor->input_budget == 0u ||
        descriptor->output_budget == 0u ||
        descriptor->deadline_ms == 0u ||
        descriptor->operations == 0 ||
        descriptor->operations_size == 0u ||
        descriptor->operations_abi == 0u ||
        descriptor->validate_operations == 0) {
        return 0;
    }
    return descriptor->validate_operations(descriptor);
}

/** @brief Product descriptor의 tuple, capability와 budget을 검사한다. */
int ribon_product_descriptor_is_valid(const struct RibonProductDescriptor *product) {
    int frontend_is_valid;

    if (product == 0) {
        return 0;
    }
    frontend_is_valid =
        ((product->kind == RIBON_PRODUCT_KIND_LIBRARY ||
          product->kind == RIBON_PRODUCT_KIND_BOOTLOADER) &&
         plugin_one_bit(product->environment_mask) &&
         product->personality_mask == 0u) ||
        (product->kind == RIBON_PRODUCT_KIND_FIRMWARE &&
         product->environment_mask == 0u &&
         plugin_one_bit(product->personality_mask));
    if (product == 0 ||
        product->magic != RIBON_PRODUCT_DESCRIPTOR_MAGIC ||
        product->size != sizeof(*product) ||
        product->abi_version != RIBON_CORE_ABI_VERSION ||
        product->id == 0 ||
        product->id[0] == '\0' ||
        product->kind <= RIBON_PRODUCT_KIND_INVALID ||
        product->kind > RIBON_PRODUCT_KIND_FIRMWARE ||
        !plugin_one_bit(product->architecture_mask) ||
        (product->architecture_mask & ~RIBON_ARCH_MASK_ALL) != 0u ||
        (product->environment_mask & ~RIBON_ENV_MASK_ALL) != 0u ||
        (product->personality_mask & ~RIBON_PERSONALITY_MASK_ALL) != 0u ||
        !frontend_is_valid ||
        product->mode_mask == 0u ||
        (product->mode_mask & ~RIBON_MODE_MASK_ALL) != 0u ||
        product->max_plugins == 0u ||
        product->max_plugins > RIBON_PLUGIN_REGISTRY_LIMIT ||
        (product->required_capabilities & ~RIBON_CAP_ALL) != 0u ||
        (product->allowed_capabilities & ~RIBON_CAP_ALL) != 0u ||
        (product->required_capabilities & ~product->allowed_capabilities) != 0u ||
        product->service_selection_count > RIBON_SERVICE_DIRECTORY_LIMIT ||
        (product->service_selection_count != 0u &&
         product->service_selections == 0) ||
        product->plugin_selection_count > RIBON_PLUGIN_REGISTRY_LIMIT ||
        (product->plugin_selection_count != 0u &&
         product->plugin_selections == 0)) {
        return 0;
    }
    return ribon_resource_limits_are_valid(&product->limits);
}
