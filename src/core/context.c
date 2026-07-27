#include <Ribon/core/context.h>

/** @brief 두 stable ID가 같은 byte sequence인지 검사한다. */
static int core_streq(const char *lhs, const char *rhs) {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }
    while (*lhs != '\0' && *rhs != '\0') {
        if (*lhs != *rhs) {
            return 0;
        }
        ++lhs;
        ++rhs;
    }
    return *lhs == *rhs;
}

/** @brief Ribon library ABI version의 안정적인 문자열을 반환한다. */
const char *ribon_version_string(void) {
    return "0.4.0";
}

/** @brief Mode 값의 안정적인 이름을 반환한다. */
const char *ribon_mode_name(enum RibonMode mode) {
    switch (mode) {
    case RIBON_MODE_NORMAL:
        return "normal";
    case RIBON_MODE_RECOVERY:
        return "recovery";
    case RIBON_MODE_PROVISIONING:
        return "provisioning";
    case RIBON_MODE_DIAGNOSTIC:
        return "diagnostic";
    default:
        return "unknown";
    }
}

/** @brief Resource limit의 내부 일관성을 검사한다. */
int ribon_resource_limits_are_valid(const struct RibonResourceLimits *limits) {
    return limits != 0 &&
           limits->max_memory_regions != 0u &&
           limits->max_load_segments != 0u &&
           limits->max_components != 0u &&
           limits->max_retries != 0u &&
           limits->max_input_bytes != 0u &&
           limits->max_handoff_bytes != 0u &&
           limits->arena_bytes != 0u &&
           limits->operation_deadline_ms != 0u &&
           limits->max_load_segments <= limits->max_components &&
           limits->max_handoff_bytes <= limits->arena_bytes &&
           limits->max_handoff_bytes <= limits->max_input_bytes;
}

/** @brief Mode descriptor ABI, capability와 limit를 fail-closed로 검사한다. */
int ribon_mode_descriptor_is_valid(const struct RibonModeDescriptor *mode) {
    if (mode == 0 ||
        mode->size != sizeof(*mode) ||
        mode->abi_version != RIBON_CORE_ABI_VERSION ||
        mode->mode < RIBON_MODE_NORMAL ||
        mode->mode > RIBON_MODE_DIAGNOSTIC ||
        mode->name == 0 ||
        !core_streq(mode->name, ribon_mode_name(mode->mode)) ||
        (mode->required_capabilities & ~RIBON_CAP_ALL) != 0u ||
        (mode->forbidden_capabilities & ~RIBON_CAP_ALL) != 0u ||
        (mode->required_capabilities & mode->forbidden_capabilities) != 0u) {
        return 0;
    }
    return ribon_resource_limits_are_valid(&mode->limits);
}

/** @brief 이미 초기화된 Core context의 전체 불변식을 재검증한다. */
int ribon_core_context_validate(const struct RibonCoreContext *context) {
    if (context == 0 ||
        context->size != sizeof(*context) ||
        context->abi_version != RIBON_CORE_ABI_VERSION ||
        context->product == 0 ||
        context->registry == 0 ||
        context->services == 0 ||
        context->mode == 0 ||
        context->arena == 0) {
        return RIBON_CORE_STATUS_BAD_ARGUMENT;
    }
    if (!ribon_mode_descriptor_is_valid(context->mode) ||
        (context->product->mode_mask & RIBON_MODE_MASK(context->mode->mode)) == 0u) {
        return RIBON_CORE_STATUS_BAD_MODE;
    }
    if (context->arena->base == 0 ||
        context->arena->used != 0u ||
        context->arena->high_watermark != 0u ||
        context->arena->capacity < context->mode->limits.arena_bytes ||
        context->arena->capacity < context->product->limits.arena_bytes) {
        return RIBON_CORE_STATUS_BAD_LIMIT;
    }
    {
        int status = ribon_plugin_registry_validate(
            context->registry,
            context->product,
            context->mode->mode);
        if (status != RIBON_CORE_STATUS_OK) {
            return status;
        }
        return ribon_service_directory_validate(
            context->services,
            context->product,
            context->mode->mode);
    }
}

/** @brief Product, registry, mode와 빈 arena를 검증해 immutable context를 만든다. */
int ribon_context_initialize(
    struct RibonCoreContext *out,
    const struct RibonProductDescriptor *product,
    const struct RibonPluginRegistry *registry,
    const struct RibonServiceDirectory *services,
    const struct RibonModeDescriptor *mode,
    struct RibonArena *arena) {
    struct RibonCoreContext candidate;
    int status;

    if (out == 0) {
        return RIBON_CORE_STATUS_BAD_ARGUMENT;
    }
    candidate = (struct RibonCoreContext){
        .size = sizeof(candidate),
        .abi_version = RIBON_CORE_ABI_VERSION,
        .product = product,
        .registry = registry,
        .services = services,
        .mode = mode,
        .arena = arena,
    };
    status = ribon_core_context_validate(&candidate);
    if (status != RIBON_CORE_STATUS_OK) {
        *out = (struct RibonCoreContext){0};
        return status;
    }
    *out = candidate;
    return RIBON_CORE_STATUS_OK;
}
