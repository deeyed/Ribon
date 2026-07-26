#ifndef RIBON_CORE_CONTEXT_H
#define RIBON_CORE_CONTEXT_H

#include <stdint.h>

#include <Ribon/core/capability.h>
#include <Ribon/core/memory.h>
#include <Ribon/plugin/registry.h>

/** @brief Ribon source release version이다. */
#define RIBON_VERSION_MAJOR 0u
#define RIBON_VERSION_MINOR 3u
#define RIBON_VERSION_PATCH 0u

/** @brief 초기화된 Core context의 immutable library ABI다. */
struct RibonCoreContext {
    uint32_t size; /**< 이 context의 byte 크기다. */
    uint32_t abi_version; /**< `RIBON_CORE_ABI_VERSION`과 일치해야 한다. */
    const struct RibonProductDescriptor *product; /**< Generated product tuple이다. */
    const struct RibonPluginRegistry *registry; /**< Generated immutable registry다. */
    const struct RibonModeDescriptor *mode; /**< Link-time mode policy다. */
    struct RibonArena *arena; /**< Caller-owned fixed arena다. */
};

/**
 * @brief Product, registry, mode와 빈 arena를 검증해 immutable context를 만든다.
 *
 * 실패 전후로 service 또는 plugin callback을 호출하지 않는다.
 */
int ribon_context_initialize(
    struct RibonCoreContext *out,
    const struct RibonProductDescriptor *product,
    const struct RibonPluginRegistry *registry,
    const struct RibonModeDescriptor *mode,
    struct RibonArena *arena);

/** @brief 이미 초기화된 Core context의 전체 불변식을 재검증한다. */
int ribon_core_context_validate(const struct RibonCoreContext *context);

/** @brief Ribon library ABI version의 안정적인 문자열을 반환한다. */
const char *ribon_version_string(void);

#endif
