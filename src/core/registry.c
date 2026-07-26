#include <Ribon/plugin/registry.h>

/** @brief 두 stable plugin ID의 byte 순서를 비교한다. */
static int registry_strcmp(const char *lhs, const char *rhs) {
    while (*lhs != '\0' && *rhs != '\0' && *lhs == *rhs) {
        ++lhs;
        ++rhs;
    }
    return (int)(unsigned char)*lhs - (int)(unsigned char)*rhs;
}

/** @brief Stable plugin ID와 kind가 모두 일치하는 descriptor를 반환한다. */
const struct RibonPluginDescriptor *ribon_plugin_registry_find(
    const struct RibonPluginRegistry *registry,
    enum RibonPluginKind kind,
    const char *id) {
    if (registry == 0 || id == 0 || registry->plugins == 0) {
        return 0;
    }
    for (uint32_t index = 0; index < registry->plugin_count; ++index) {
        const struct RibonPluginDescriptor *plugin = registry->plugins[index];
        if (plugin != 0 && plugin->kind == kind &&
            registry_strcmp(plugin->id, id) == 0) {
            return plugin;
        }
    }
    return 0;
}

/** @brief Capability bit의 유일 provider index를 반환한다. */
static int registry_provider(
    const struct RibonPluginRegistry *registry,
    uint64_t capability,
    uint32_t *index_out) {
    uint32_t found = RIBON_PLUGIN_REGISTRY_LIMIT;
    for (uint32_t index = 0; index < registry->plugin_count; ++index) {
        if ((registry->plugins[index]->provides & capability) == 0u) {
            continue;
        }
        if (found != RIBON_PLUGIN_REGISTRY_LIMIT) {
            return RIBON_CORE_STATUS_DUPLICATE_PROVIDER;
        }
        found = index;
    }
    if (found == RIBON_PLUGIN_REGISTRY_LIMIT) {
        return RIBON_CORE_STATUS_MISSING_CAPABILITY;
    }
    *index_out = found;
    return RIBON_CORE_STATUS_OK;
}

/** @brief Registry와 product tuple의 provider, phase, capability, budget을 검사한다. */
int ribon_plugin_registry_validate(
    const struct RibonPluginRegistry *registry,
    const struct RibonProductDescriptor *product,
    enum RibonMode mode) {
    uint32_t indegree[RIBON_PLUGIN_REGISTRY_LIMIT] = {0};
    uint64_t dependencies[RIBON_PLUGIN_REGISTRY_LIMIT] = {0};
    uint32_t ready[RIBON_PLUGIN_REGISTRY_LIMIT] = {0};
    uint32_t ready_count = 0u;
    uint32_t visited = 0u;
    uint64_t aggregate = 0u;
    uint64_t arena_budget = 0u;

    if (registry == 0 ||
        registry->size != sizeof(*registry) ||
        registry->abi_version != RIBON_CORE_ABI_VERSION ||
        registry->plugins == 0 ||
        registry->plugin_count == 0u ||
        !ribon_product_descriptor_is_valid(product) ||
        mode < RIBON_MODE_NORMAL ||
        mode > RIBON_MODE_DIAGNOSTIC ||
        registry->plugin_count > product->max_plugins) {
        return RIBON_CORE_STATUS_INVALID_DESCRIPTOR;
    }

    for (uint32_t index = 0; index < registry->plugin_count; ++index) {
        const struct RibonPluginDescriptor *plugin = registry->plugins[index];
        if (!ribon_plugin_descriptor_is_valid(plugin) ||
            (plugin->architecture_mask & product->architecture_mask) == 0u ||
            (plugin->environment_mask & product->environment_mask) == 0u ||
            (plugin->mode_mask & RIBON_MODE_MASK(mode)) == 0u ||
            (plugin->provides & ~product->allowed_capabilities) != 0u ||
            plugin->input_budget > product->limits.max_input_bytes ||
            plugin->output_budget > product->limits.max_handoff_bytes ||
            plugin->deadline_ms > product->limits.operation_deadline_ms ||
            plugin->phase == RIBON_PLUGIN_PHASE_RUNTIME ||
            (index != 0u &&
             registry_strcmp(registry->plugins[index - 1u]->id, plugin->id) >= 0)) {
            return RIBON_CORE_STATUS_INVALID_DESCRIPTOR;
        }
        if ((aggregate & plugin->provides) != 0u) {
            return RIBON_CORE_STATUS_DUPLICATE_PROVIDER;
        }
        if (arena_budget > UINT64_MAX - plugin->arena_budget) {
            return RIBON_CORE_STATUS_BAD_LIMIT;
        }
        arena_budget += plugin->arena_budget;
        aggregate |= plugin->provides;
    }

    if (arena_budget > product->limits.arena_bytes ||
        (aggregate & product->required_capabilities) !=
            product->required_capabilities) {
        return RIBON_CORE_STATUS_MISSING_CAPABILITY;
    }

    for (uint32_t consumer = 0; consumer < registry->plugin_count; ++consumer) {
        uint64_t required = registry->plugins[consumer]->requires;
        for (uint32_t bit = 0; bit < 64u; ++bit) {
            uint64_t capability = 1ull << bit;
            uint32_t provider = 0u;
            int status;
            if ((required & capability) == 0u) {
                continue;
            }
            status = registry_provider(registry, capability, &provider);
            if (status != RIBON_CORE_STATUS_OK) {
                return status;
            }
            if (provider == consumer ||
                registry->plugins[provider]->phase >
                    registry->plugins[consumer]->phase) {
                return RIBON_CORE_STATUS_DEPENDENCY_CYCLE;
            }
            if ((dependencies[provider] & (1ull << consumer)) == 0u) {
                dependencies[provider] |= 1ull << consumer;
                ++indegree[consumer];
            }
        }
    }

    for (uint32_t index = 0; index < registry->plugin_count; ++index) {
        if (indegree[index] == 0u) {
            ready[ready_count++] = index;
        }
    }
    while (ready_count != 0u) {
        uint32_t provider = ready[--ready_count];
        uint64_t consumers = dependencies[provider];
        ++visited;
        for (uint32_t consumer = 0; consumer < registry->plugin_count; ++consumer) {
            if ((consumers & (1ull << consumer)) == 0u) {
                continue;
            }
            --indegree[consumer];
            if (indegree[consumer] == 0u) {
                ready[ready_count++] = consumer;
            }
        }
    }
    return visited == registry->plugin_count ?
        RIBON_CORE_STATUS_OK :
        RIBON_CORE_STATUS_DEPENDENCY_CYCLE;
}
