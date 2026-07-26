#ifndef RIBON_SDK_PACKAGE_H
#define RIBON_SDK_PACKAGE_H

#include <stdint.h>

#include <Ribon/plugin/descriptor.h>
#include <Ribon/sdk/abi.h>

/** @brief External plugin package descriptor를 식별하는 magic이다. */
#define RIBON_SDK_PLUGIN_PACKAGE_MAGIC 0x52425350u

/** @brief Source package와 compiled descriptor를 연결하는 immutable 계약이다. */
struct RibonSdkPluginPackage {
    uint32_t magic; /**< `RIBON_SDK_PLUGIN_PACKAGE_MAGIC`이어야 한다. */
    uint32_t size; /**< Descriptor byte 크기다. */
    uint32_t sdk_abi_version; /**< `RIBON_SDK_ABI_VERSION`과 일치해야 한다. */
    const char *package_id; /**< Manifest의 stable package ID다. */
    const char *contract_id; /**< Package 문서가 구현하는 stable contract ID다. */
    const struct RibonPluginDescriptor *plugin; /**< Package가 제공하는 단일 plugin이다. */
};

/** @brief External package descriptor와 포함된 plugin ABI를 검사한다. */
int ribon_sdk_plugin_package_is_valid(
    const struct RibonSdkPluginPackage *package);

#endif
