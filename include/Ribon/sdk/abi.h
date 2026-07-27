#ifndef RIBON_SDK_ABI_H
#define RIBON_SDK_ABI_H

#include <stdint.h>

/** @brief 설치된 SDK ABI descriptor를 식별하는 magic이다. */
#define RIBON_SDK_ABI_MAGIC 0x52425341u

/** @brief External package contract와 host harness ABI version이다. */
#define RIBON_SDK_ABI_VERSION 2u

/** @brief 설치된 header와 세 library가 공유하는 ABI tuple이다. */
struct RibonSdkAbiDescriptor {
    uint32_t magic; /**< `RIBON_SDK_ABI_MAGIC`이어야 한다. */
    uint32_t size; /**< Descriptor byte 크기다. */
    uint32_t sdk_abi_version; /**< `RIBON_SDK_ABI_VERSION`이다. */
    uint32_t core_abi_version; /**< 설치된 Core ABI version이다. */
    uint16_t plugin_abi_major; /**< 설치된 plugin ABI major다. */
    uint16_t plugin_abi_minor; /**< 설치된 plugin ABI minor다. */
    uint32_t firmware_personality_abi; /**< Firmware personality ABI version이다. */
    uint32_t source_version_major; /**< Ribon source major version이다. */
    uint32_t source_version_minor; /**< Ribon source minor version이다. */
    uint32_t source_version_patch; /**< Ribon source patch version이다. */
};

/** @brief Process-global 변경 없이 immutable SDK ABI descriptor를 반환한다. */
const struct RibonSdkAbiDescriptor *ribon_sdk_abi_descriptor(void);

#endif
