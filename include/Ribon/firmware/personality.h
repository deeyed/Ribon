#ifndef RIBON_FIRMWARE_PERSONALITY_H
#define RIBON_FIRMWARE_PERSONALITY_H

#include <stdint.h>

/** @brief Firmware provider personality operation table ABI다. */
#define RIBON_FIRMWARE_PERSONALITY_ABI_VERSION 1u

/** @brief Consumer environment와 반대 방향인 firmware provider 종류다. */
enum RibonFirmwarePersonalityKind {
    RIBON_FIRMWARE_PERSONALITY_UEFI_COMPATIBLE = 0,
    RIBON_FIRMWARE_PERSONALITY_BIOS_COMPATIBLE = 1,
};

/** @brief Firmware provider product가 publish할 personality descriptor다. */
struct RibonFirmwarePersonality {
    uint32_t size; /**< Descriptor byte 크기다. */
    uint32_t abi_version; /**< `RIBON_FIRMWARE_PERSONALITY_ABI_VERSION`이다. */
    enum RibonFirmwarePersonalityKind kind; /**< Provider ABI 종류다. */
    const char *id; /**< Stable personality ID다. */
    uint64_t published_services; /**< 외부 firmware ABI로 publish할 service bit다. */
    uint64_t runtime_services; /**< OS entry 뒤 보존할 service bit다. */
};

#endif
