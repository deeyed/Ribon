#include <Ribon/plugin/registry.h>

/** @brief 두 stable plugin ID의 byte 순서를 비교한다. */
static int registry_strcmp(const char *lhs, const char *rhs) {
    while (*lhs != '\0' && *rhs != '\0' && *lhs == *rhs) {
        ++lhs;
        ++rhs;
    }
    return (int)(unsigned char)*lhs - (int)(unsigned char)*rhs;
}

/** @brief Capability가 one-authority provider를 요구하는지 판별한다. */
static int registry_capability_is_authority(uint64_t capability) {
    return capability == RIBON_CAP_ARCHITECTURE ||
           capability == RIBON_CAP_FIRMWARE_PERSONALITY ||
           capability == RIBON_CAP_FIRMWARE_SERVICE_DIRECTORY;
}

/** @brief Capability가 immutable service directory authority인지 판별한다. */
static int registry_capability_is_service(uint64_t capability) {
    switch (capability) {
    case RIBON_CAP_BOOT_SOURCE_READ:
    case RIBON_CAP_INACTIVE_SLOT_WRITE:
    case RIBON_CAP_INACTIVE_SLOT_ERASE:
    case RIBON_CAP_STORAGE_FLUSH:
    case RIBON_CAP_MONOTONIC_TIMER:
    case RIBON_CAP_WATCHDOG:
    case RIBON_CAP_RESET:
    case RIBON_CAP_PERSISTENT_METADATA:
    case RIBON_CAP_NETWORK_TRANSPORT:
    case RIBON_CAP_RANDOM_NONCE:
    case RIBON_CAP_DIAGNOSTIC_SINK:
    case RIBON_CAP_ENVIRONMENT_QUIESCE:
    case RIBON_CAP_MACHINE_DESCRIPTION:
    case RIBON_CAP_PAYLOAD_PLACEMENT:
    case RIBON_CAP_BOOT_MODULE_BUNDLE:
        return 1;
    default:
        return 0;
    }
}

/** @brief Product selection이 capability provider를 정확히 지정하는지 검사한다. */
static int registry_is_selected_provider(
    const struct RibonProductDescriptor *product,
    const struct RibonPluginDescriptor *plugin,
    uint64_t capability) {
    for (uint32_t index = 0u; index < product->plugin_selection_count; ++index) {
        const struct RibonPluginSelection *selection =
            &product->plugin_selections[index];
        if (selection->kind == plugin->kind && selection->id != 0 &&
            registry_strcmp(selection->id, plugin->id) == 0 &&
            (plugin->provides & capability) != 0u) {
            return 1;
        }
    }
    return 0;
}

/** @brief Product의 plugin collection selector가 graph에 대해 유효한지 검사한다. */
static int registry_plugin_selections_are_valid(
    const struct RibonPluginRegistry *registry,
    const struct RibonProductDescriptor *product) {
    for (uint32_t index = 0u; index < product->plugin_selection_count; ++index) {
        const struct RibonPluginSelection *selection =
            &product->plugin_selections[index];
        if (selection->kind < RIBON_PLUGIN_KIND_ARCHITECTURE ||
            selection->kind > RIBON_PLUGIN_KIND_SERVICE ||
            selection->id == 0 || selection->id[0] == '\0' ||
            (index != 0u &&
             product->plugin_selections[index - 1u].kind >= selection->kind) ||
            ribon_plugin_registry_find(registry, selection->kind, selection->id) == 0) {
            return 0;
        }
    }
    return 1;
}

/** @brief Stable plugin ID와 kind가 모두 일치하는 descriptor를 반환한다. */
const struct RibonPluginDescriptor *ribon_plugin_registry_find(
    const struct RibonPluginRegistry *registry,
    enum RibonPluginKind kind,
    const char *id) {
    if (registry == 0 || id == 0 || registry->plugins == 0) {
        return 0;
    }
    for (uint32_t index = 0u; index < registry->plugin_count; ++index) {
        const struct RibonPluginDescriptor *plugin = registry->plugins[index];
        if (plugin != 0 && plugin->kind == kind &&
            registry_strcmp(plugin->id, id) == 0) {
            return plugin;
        }
    }
    return 0;
}

/** @brief Product selector 또는 유일 provider로 선택된 plugin을 반환한다. */
const struct RibonPluginDescriptor *ribon_plugin_registry_find_selected(
    const struct RibonPluginRegistry *registry,
    const struct RibonProductDescriptor *product,
    enum RibonPluginKind kind) {
    const struct RibonPluginDescriptor *found = 0;
    if (registry == 0 || product == 0) {
        return 0;
    }
    for (uint32_t index = 0u; index < product->plugin_selection_count; ++index) {
        const struct RibonPluginSelection *selection =
            &product->plugin_selections[index];
        if (selection->kind == kind) {
            return ribon_plugin_registry_find(registry, kind, selection->id);
        }
    }
    for (uint32_t index = 0u; index < registry->plugin_count; ++index) {
        const struct RibonPluginDescriptor *candidate = registry->plugins[index];
        if (candidate->kind != kind) {
            continue;
        }
        if (found != 0) {
            return 0;
        }
        found = candidate;
    }
    return found;
}

/** @brief Capability dependency의 authority 또는 selected collection provider를 찾는다. */
static int registry_provider(
    const struct RibonPluginRegistry *registry,
    const struct RibonProductDescriptor *product,
    uint64_t capability,
    uint32_t *index_out) {
    uint32_t found = RIBON_PLUGIN_REGISTRY_LIMIT;
    uint32_t selected = RIBON_PLUGIN_REGISTRY_LIMIT;
    uint32_t count = 0u;

    for (uint32_t index = 0u; index < registry->plugin_count; ++index) {
        const struct RibonPluginDescriptor *plugin = registry->plugins[index];
        if ((plugin->provides & capability) == 0u) {
            continue;
        }
        ++count;
        if (registry_is_selected_provider(product, plugin, capability)) {
            if (selected != RIBON_PLUGIN_REGISTRY_LIMIT) {
                return RIBON_CORE_STATUS_AMBIGUOUS_SELECTION;
            }
            selected = index;
        }
        if (found == RIBON_PLUGIN_REGISTRY_LIMIT) {
            found = index;
        }
    }
    if (count == 0u) {
        return RIBON_CORE_STATUS_MISSING_CAPABILITY;
    }
    if (registry_capability_is_authority(capability)) {
        if (count != 1u) {
            return RIBON_CORE_STATUS_DUPLICATE_PROVIDER;
        }
        *index_out = found;
        return RIBON_CORE_STATUS_OK;
    }
    if (selected != RIBON_PLUGIN_REGISTRY_LIMIT) {
        *index_out = selected;
        return RIBON_CORE_STATUS_OK;
    }
    if (count != 1u) {
        return RIBON_CORE_STATUS_AMBIGUOUS_SELECTION;
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
    uint32_t architecture_count = 0u;
    uint32_t environment_count = 0u;
    uint32_t personality_count = 0u;

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

    for (uint32_t index = 0u; index < registry->plugin_count; ++index) {
        const struct RibonPluginDescriptor *plugin = registry->plugins[index];
        const int frontend_matches =
            product->kind == RIBON_PRODUCT_KIND_FIRMWARE ?
                (plugin != 0 &&
                 (plugin->personality_mask & product->personality_mask) != 0u) :
                (plugin != 0 &&
                 (plugin->environment_mask & product->environment_mask) != 0u);
        if (!ribon_plugin_descriptor_is_valid(plugin) ||
            (plugin->architecture_mask & product->architecture_mask) == 0u ||
            !frontend_matches ||
            (plugin->mode_mask & RIBON_MODE_MASK(mode)) == 0u ||
            (plugin->provides & ~product->allowed_capabilities) != 0u ||
            plugin->input_budget > product->limits.max_input_bytes ||
            plugin->output_budget > product->limits.max_handoff_bytes ||
            plugin->deadline_ms > product->limits.operation_deadline_ms ||
            (plugin->phase == RIBON_PLUGIN_PHASE_RUNTIME &&
             product->kind != RIBON_PRODUCT_KIND_FIRMWARE) ||
            (plugin->kind == RIBON_PLUGIN_KIND_FIRMWARE_PERSONALITY &&
             product->kind != RIBON_PRODUCT_KIND_FIRMWARE) ||
            (plugin->kind == RIBON_PLUGIN_KIND_ENVIRONMENT &&
             product->kind == RIBON_PRODUCT_KIND_FIRMWARE) ||
            (index != 0u &&
             registry_strcmp(registry->plugins[index - 1u]->id, plugin->id) >= 0)) {
            return RIBON_CORE_STATUS_INVALID_DESCRIPTOR;
        }
        if (arena_budget > UINT64_MAX - plugin->arena_budget) {
            return RIBON_CORE_STATUS_BAD_LIMIT;
        }
        arena_budget += plugin->arena_budget;
        aggregate |= plugin->provides;
        architecture_count +=
            plugin->kind == RIBON_PLUGIN_KIND_ARCHITECTURE ? 1u : 0u;
        environment_count +=
            plugin->kind == RIBON_PLUGIN_KIND_ENVIRONMENT ? 1u : 0u;
        personality_count +=
            plugin->kind == RIBON_PLUGIN_KIND_FIRMWARE_PERSONALITY ? 1u : 0u;
    }

    if (!registry_plugin_selections_are_valid(registry, product)) {
        return RIBON_CORE_STATUS_INVALID_DESCRIPTOR;
    }
    for (uint32_t bit = 0u; bit < 64u; ++bit) {
        const uint64_t capability = 1ull << bit;
        uint32_t providers = 0u;
        if (!registry_capability_is_authority(capability)) {
            continue;
        }
        for (uint32_t index = 0u; index < registry->plugin_count; ++index) {
            providers +=
                (registry->plugins[index]->provides & capability) != 0u ? 1u : 0u;
        }
        if (providers > 1u) {
            return RIBON_CORE_STATUS_DUPLICATE_PROVIDER;
        }
    }
    if (arena_budget > product->limits.arena_bytes ||
        (product->kind == RIBON_PRODUCT_KIND_BOOTLOADER &&
         (architecture_count != 1u ||
          environment_count != 1u ||
          personality_count != 0u)) ||
        (product->kind == RIBON_PRODUCT_KIND_FIRMWARE &&
         (architecture_count != 1u ||
          personality_count != 1u ||
          environment_count != 0u))) {
        return RIBON_CORE_STATUS_MISSING_CAPABILITY;
    }
    for (uint32_t bit = 0u; bit < 64u; ++bit) {
        const uint64_t capability = 1ull << bit;
        if ((product->required_capabilities & capability) != 0u &&
            !registry_capability_is_service(capability) &&
            (aggregate & capability) == 0u) {
            return RIBON_CORE_STATUS_MISSING_CAPABILITY;
        }
    }

    for (uint32_t consumer = 0u; consumer < registry->plugin_count; ++consumer) {
        uint64_t required = registry->plugins[consumer]->requires;
        for (uint32_t bit = 0u; bit < 64u; ++bit) {
            const uint64_t capability = 1ull << bit;
            uint32_t provider = 0u;
            int status;
            if ((required & capability) == 0u) {
                continue;
            }
            status = registry_provider(registry, product, capability, &provider);
            if (status != RIBON_CORE_STATUS_OK) {
                return status;
            }
            if (provider == consumer) {
                return RIBON_CORE_STATUS_DEPENDENCY_CYCLE;
            }
            if (registry->plugins[provider]->phase >
                registry->plugins[consumer]->phase) {
                return RIBON_CORE_STATUS_PHASE_INVERSION;
            }
            if ((dependencies[provider] & (1ull << consumer)) == 0u) {
                dependencies[provider] |= 1ull << consumer;
                ++indegree[consumer];
            }
        }
    }

    for (uint32_t index = 0u; index < registry->plugin_count; ++index) {
        if (indegree[index] == 0u) {
            ready[ready_count++] = index;
        }
    }
    while (ready_count != 0u) {
        const uint32_t provider = ready[--ready_count];
        const uint64_t consumers = dependencies[provider];
        ++visited;
        for (uint32_t consumer = 0u; consumer < registry->plugin_count; ++consumer) {
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
