#include <Ribon/arch.h>
#include <Ribon/core.h>
#include <Ribon/platform.h>
#include <Ribon/profile.h>

/** @brief 두 C 문자열이 같은지 검사한다. */
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

/** @brief mode 열거값의 고정 문자열을 반환한다. */
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

/** @brief resource limit의 모든 필드가 유효한 상한인지 검사한다. */
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

/** @brief mode descriptor ABI, capability 집합, 자원 상한을 검사한다. */
int ribon_mode_descriptor_is_valid(const struct RibonModeDescriptor *mode) {
    if (mode == 0 ||
        mode->abi_version != RIBON_CORE_ABI_VERSION ||
        mode->mode < RIBON_MODE_NORMAL ||
        mode->mode > RIBON_MODE_DIAGNOSTIC ||
        mode->name == 0 ||
        !core_streq(mode->name, ribon_mode_name(mode->mode)) ||
        (mode->required_platform_capabilities & ~RIBON_PLATFORM_CAP_ALL) != 0u ||
        (mode->forbidden_platform_capabilities & ~RIBON_PLATFORM_CAP_ALL) != 0u ||
        (mode->required_platform_capabilities &
         mode->forbidden_platform_capabilities) != 0u ||
        (mode->required_arch_capabilities & ~RIBON_ARCH_CAP_ALL) != 0u ||
        (mode->forbidden_arch_capabilities & ~RIBON_ARCH_CAP_ALL) != 0u ||
        (mode->required_arch_capabilities &
         mode->forbidden_arch_capabilities) != 0u) {
        return 0;
    }
    return ribon_resource_limits_are_valid(&mode->limits);
}

/** @brief Core가 service를 호출하기 전에 전체 경계를 fail-closed로 검증한다. */
int ribon_core_context_validate(const struct RibonCoreContext *context) {
    struct RibonEntryContract entry_contract;
    uint64_t supported;

    if (context == 0 || context->mode == 0 || context->platform == 0 ||
        context->arch == 0 || context->profile == 0 || context->arena == 0) {
        return RIBON_CORE_STATUS_BAD_ARGUMENT;
    }
    if (context->mode->abi_version != RIBON_CORE_ABI_VERSION ||
        context->platform->abi_version != RIBON_PLATFORM_OPS_ABI_VERSION ||
        context->arch->abi_version != RIBON_ARCH_OPS_ABI_VERSION ||
        context->profile->ops == 0 ||
        context->profile->ops->abi_version != RIBON_PROFILE_OPS_ABI_VERSION) {
        return RIBON_CORE_STATUS_BAD_ABI;
    }
    if (!ribon_mode_descriptor_is_valid(context->mode)) {
        return RIBON_CORE_STATUS_BAD_MODE;
    }
    if (!ribon_platform_ops_are_valid(context->platform)) {
        return RIBON_CORE_STATUS_INVALID_OPERATION_TABLE;
    }
    if (!ribon_arch_ops_are_valid(context->arch)) {
        return RIBON_CORE_STATUS_INVALID_OPERATION_TABLE;
    }
    if (!ribon_profile_is_valid(context->profile)) {
        return RIBON_CORE_STATUS_INVALID_PROFILE;
    }
    if (context->arena->base == 0 ||
        context->arena->used != 0u ||
        context->arena->high_watermark != 0u ||
        context->arena->capacity < context->mode->limits.arena_bytes) {
        return RIBON_CORE_STATUS_BAD_LIMIT;
    }
    supported = context->platform->capabilities.supported;
    if ((supported & context->mode->required_platform_capabilities) !=
        context->mode->required_platform_capabilities) {
        return RIBON_CORE_STATUS_MISSING_CAPABILITY;
    }
    if ((supported & context->mode->forbidden_platform_capabilities) != 0u) {
        return RIBON_CORE_STATUS_FORBIDDEN_CAPABILITY;
    }
    if ((context->arch->capabilities &
         context->mode->required_arch_capabilities) !=
        context->mode->required_arch_capabilities) {
        return RIBON_CORE_STATUS_MISSING_CAPABILITY;
    }
    if ((context->arch->capabilities &
         context->mode->forbidden_arch_capabilities) != 0u) {
        return RIBON_CORE_STATUS_FORBIDDEN_CAPABILITY;
    }
    if ((context->profile->supported_modes &
         RIBON_MODE_MASK(context->mode->mode)) == 0u) {
        return RIBON_CORE_STATUS_INVALID_PROFILE;
    }
    if (ribon_profile_has_capability(
            context->profile,
            RIBON_PROFILE_CAP_ENTRY_CONTRACT)) {
        if (context->profile->ops->select_entry_contract(
                context->arch->descriptor,
                &entry_contract) != RIBON_PROFILE_STATUS_OK ||
            entry_contract.required_entry_flags == 0u ||
            (entry_contract.required_entry_flags &
             ~entry_contract.supported_entry_flags) != 0u) {
            return RIBON_CORE_STATUS_INVALID_PROFILE;
        }
    }
    return RIBON_CORE_STATUS_OK;
}
