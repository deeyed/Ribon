#include <Ribon/core/context.h>
#include <Ribon/firmware/personality.h>
#include <Ribon/sdk/abi.h>
#include <Ribon/sdk/host.h>

/** @brief 설치된 public header와 세 library가 공유하는 ABI tuple이다. */
static const struct RibonSdkAbiDescriptor sdk_abi = {
    .magic = RIBON_SDK_ABI_MAGIC,
    .size = sizeof(sdk_abi),
    .sdk_abi_version = RIBON_SDK_ABI_VERSION,
    .core_abi_version = RIBON_CORE_ABI_VERSION,
    .plugin_abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .plugin_abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .firmware_personality_abi = RIBON_FIRMWARE_PERSONALITY_ABI_VERSION,
    .source_version_major = RIBON_VERSION_MAJOR,
    .source_version_minor = RIBON_VERSION_MINOR,
    .source_version_patch = RIBON_VERSION_PATCH,
};

/** @brief Process-global 변경 없이 immutable SDK ABI descriptor를 반환한다. */
const struct RibonSdkAbiDescriptor *ribon_sdk_abi_descriptor(void) {
    return &sdk_abi;
}

/** @brief 두 required string이 비어 있지 않은지 검사한다. */
static int sdk_string_is_valid(const char *value) {
    return value != 0 && value[0] != '\0';
}

/** @brief External package descriptor와 포함된 plugin ABI를 검사한다. */
int ribon_sdk_plugin_package_is_valid(
    const struct RibonSdkPluginPackage *package) {
    return package != 0 &&
           package->magic == RIBON_SDK_PLUGIN_PACKAGE_MAGIC &&
           package->size == sizeof(*package) &&
           package->sdk_abi_version == RIBON_SDK_ABI_VERSION &&
           sdk_string_is_valid(package->package_id) &&
           sdk_string_is_valid(package->contract_id) &&
           ribon_plugin_descriptor_is_valid(package->plugin);
}

/** @brief Host harness에서 package kind와 capability를 검사한다. */
int ribon_sdk_host_validate_package(
    const struct RibonSdkPluginPackage *package,
    enum RibonPluginKind expected_kind,
    uint64_t required_provides,
    uint64_t forbidden_requires) {
    if (!ribon_sdk_plugin_package_is_valid(package) ||
        expected_kind < RIBON_PLUGIN_KIND_ARCHITECTURE ||
        expected_kind > RIBON_PLUGIN_KIND_SERVICE ||
        required_provides == 0u ||
        (required_provides & ~RIBON_CAP_ALL) != 0u ||
        (forbidden_requires & ~RIBON_CAP_ALL) != 0u ||
        package->plugin->kind != expected_kind ||
        (package->plugin->provides & required_provides) != required_provides ||
        (package->plugin->requires & forbidden_requires) != 0u) {
        return RIBON_CORE_STATUS_INVALID_DESCRIPTOR;
    }
    return RIBON_CORE_STATUS_OK;
}
