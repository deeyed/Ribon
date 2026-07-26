#ifndef RIBON_EXAMPLES_DIAGNOSTIC_SINK_H
#define RIBON_EXAMPLES_DIAGNOSTIC_SINK_H

#include <stdint.h>

#include <Ribon/sdk/package.h>

/** @brief Example sink가 caller-owned byte budget을 추적하는 context다. */
struct RibonExampleDiagnosticSink {
    uint64_t byte_limit; /**< 한 contract run에서 허용할 byte 상한이다. */
    uint64_t bytes_written; /**< 성공적으로 승인한 누적 byte 수다. */
};

/** @brief Example service plugin의 operation table ABI다. */
#define RIBON_EXAMPLE_DIAGNOSTIC_SINK_ABI_VERSION 1u

/** @brief Bounded diagnostic sink operation table이다. */
struct RibonExampleDiagnosticSinkOperations {
    uint32_t size; /**< Operation table byte 크기다. */
    uint32_t abi_version; /**< Example operation ABI version이다. */
    int (*write)(
        struct RibonExampleDiagnosticSink *,
        const void *,
        uint64_t); /**< Caller buffer를 소유하지 않고 byte budget만 소비한다. */
};

/** @brief External package가 제공하는 service plugin descriptor다. */
extern const struct RibonPluginDescriptor
    ribon_example_diagnostic_sink_plugin_descriptor;

/** @brief Manifest와 compiled plugin을 연결하는 package descriptor다. */
extern const struct RibonSdkPluginPackage
    ribon_example_diagnostic_sink_package;

#endif
