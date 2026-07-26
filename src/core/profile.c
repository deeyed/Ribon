#include <Ribon/profile.h>

/** @brief 두 C 문자열이 같은지 검사한다. */
static int ribon_streq(const char *lhs, const char *rhs) {
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

/** @brief 이름과 일치하는 builtin profile을 반환한다. */
const struct RibonProfile *ribon_find_builtin_profile(const char *name) {
    if (ribon_streq(name, "parus")) {
        return ribon_profile_parus();
    }
    return 0;
}

/** @brief handoff 종류의 고정 문자열을 반환한다. */
const char *ribon_handoff_name(enum RibonHandoffKind handoff) {
    switch (handoff) {
    case RIBON_HANDOFF_NONE:
        return "none";
    case RIBON_HANDOFF_PROFILE_DEFINED:
        return "profile-defined";
    default:
        return "unknown";
    }
}

/** @brief Profile이 요청한 expectation bit를 모두 가지는지 검사한다. */
int ribon_profile_has_expectation(const struct RibonProfile *profile, uint32_t expectation) {
    if (profile == 0) {
        return 0;
    }
    return (profile->expectations & expectation) == expectation;
}

/** @brief Profile이 요청한 capability bit를 모두 가지는지 검사한다. */
int ribon_profile_has_capability(const struct RibonProfile *profile, uint64_t capability) {
    if (profile == 0 || (capability & ~RIBON_PROFILE_CAP_ALL) != 0u) {
        return 0;
    }
    return (profile->capabilities & capability) == capability;
}

/** @brief 한 capability bit와 callback 존재 여부가 같은지 검사한다. */
static int capability_matches_callback(
    const struct RibonProfile *profile,
    uint64_t capability,
    int callback_present) {
    return (((profile->capabilities & capability) != 0u) != (callback_present != 0)) ? 0 : 1;
}

/** @brief Profile descriptor, capability, operation table의 일관성을 검사한다. */
int ribon_profile_is_valid(const struct RibonProfile *profile) {
    if (profile == 0 ||
        profile->name == 0 ||
        profile->description == 0 ||
        profile->kernel_path == 0 ||
        profile->supported_modes == 0u ||
        (profile->supported_modes & ~RIBON_MODE_MASK_ALL) != 0u ||
        (profile->capabilities & ~RIBON_PROFILE_CAP_ALL) != 0u ||
        profile->ops == 0 ||
        profile->ops->abi_version != RIBON_PROFILE_OPS_ABI_VERSION) {
        return 0;
    }
    if (profile->handoff != RIBON_HANDOFF_NONE &&
        (profile->handoff_name == 0 || profile->handoff_major == 0u)) {
        return 0;
    }
    if ((profile->handoff != RIBON_HANDOFF_NONE) !=
        ((profile->capabilities & RIBON_PROFILE_CAP_HANDOFF) != 0u)) {
        return 0;
    }
    return capability_matches_callback(
               profile,
               RIBON_PROFILE_CAP_MANIFEST_MATCH,
               profile->ops->match_manifest != 0) &&
           capability_matches_callback(
               profile,
               RIBON_PROFILE_CAP_COMPONENT_VALIDATION,
               profile->ops->validate_components != 0) &&
           capability_matches_callback(
               profile,
               RIBON_PROFILE_CAP_ENTRY_CONTRACT,
               profile->ops->select_entry_contract != 0) &&
           capability_matches_callback(
               profile,
               RIBON_PROFILE_CAP_HANDOFF,
               profile->ops->build_handoff != 0) &&
           capability_matches_callback(
               profile,
               RIBON_PROFILE_CAP_BOOT_CONFIRMATION,
               profile->ops->validate_confirmation != 0);
}

/** @brief Profile operation을 통해 boot confirmation을 fail-closed로 검증한다. */
int ribon_profile_validate_confirmation(
    const struct RibonProfile *profile,
    const struct RibonBootConfirmation *confirmation,
    const struct RibonBootConfirmationExpectation *expected) {
    if (!ribon_profile_is_valid(profile) ||
        !ribon_profile_has_capability(
            profile,
            RIBON_PROFILE_CAP_BOOT_CONFIRMATION)) {
        return RIBON_PROFILE_STATUS_UNSUPPORTED;
    }
    return profile->ops->validate_confirmation(confirmation, expected);
}
