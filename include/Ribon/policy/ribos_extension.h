#ifndef RIBON_POLICY_RIBOS_EXTENSION_H
#define RIBON_POLICY_RIBOS_EXTENSION_H

#include <stddef.h>
#include <stdint.h>

#include <Ribon/policy/ribos.h>

#include <ribos/schema/schema.h>
#include <ribos/vm/helpers.h>
#include <ribos/vm/prepared.h>
#include <ribos/vm/runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Installed SDK의 typed Ribos extension package ABI다. */
#define RIBON_RIBOS_EXTENSION_ABI_VERSION 1u

/** @brief Typed extension descriptor 검증의 stable fail-closed 결과다. */
enum RibonRibosExtensionStatus {
    RIBON_RIBOS_EXTENSION_STATUS_OK = 0,
    RIBON_RIBOS_EXTENSION_STATUS_BAD_ARGUMENT = -1,
    RIBON_RIBOS_EXTENSION_STATUS_BAD_ABI = -2,
    RIBON_RIBOS_EXTENSION_STATUS_BAD_SCHEMA = -3,
    RIBON_RIBOS_EXTENSION_STATUS_BAD_HELPER_CONTRACT = -4,
    RIBON_RIBOS_EXTENSION_STATUS_BAD_ROUTE = -5,
    RIBON_RIBOS_EXTENSION_STATUS_CAPABILITY_WIDENING = -6,
    RIBON_RIBOS_EXTENSION_STATUS_TYPESTATE_MISMATCH = -7,
    RIBON_RIBOS_EXTENSION_STATUS_BUDGET_EXCEEDED = -8,
    RIBON_RIBOS_EXTENSION_STATUS_DIGEST_MISMATCH = -9,
    RIBON_RIBOS_EXTENSION_STATUS_SCHEMA_ARTIFACT = -10,
};

/**
 * @brief 외부 C mechanism을 한 product-local Ribos semantic package로 묶는다.
 *
 * `schema`는 source type과 exact helper signature를, `helper_contract`는 effect,
 * capability, typestate와 operation bound를 소유한다. Callback pointer는 `routes`에만
 * 있으며 schema artifact와 두 canonical digest에 포함되지 않는다.
 */
struct RibonRibosExtensionDescriptor {
    uint32_t size;
    uint32_t abi_version;
    const char *package_id;
    const RibosProductSchema *schema;
    const RibosVmHelperContract *helper_contract;
    const struct RibonRibosHelperRoute *routes;
    uint32_t route_count;
    uint32_t selected_phase;
    uint32_t granted_ribos_capabilities;
    uint32_t helper_call_budget;
    uint8_t schema_digest[RIBOS_SCHEMA_DIGEST_BYTES];
    uint8_t helper_execution_digest[RIBOS_SCHEMA_DIGEST_BYTES];
    uint64_t reserved[4];
};

/** @brief Descriptor, schema, execution table와 route closure를 독립 검증한다. */
int ribon_ribos_extension_validate_v1(
    const struct RibonRibosExtensionDescriptor *extension);

/** @brief Canonical little-endian schema artifact의 exact byte 수를 반환한다. */
int ribon_ribos_extension_schema_size_v1(
    const struct RibonRibosExtensionDescriptor *extension,
    size_t *required_size);

/** @brief Extension schema를 callback pointer 없는 canonical artifact로 기록한다. */
int ribon_ribos_extension_schema_write_v1(
    const struct RibonRibosExtensionDescriptor *extension,
    uint8_t *output,
    size_t output_capacity,
    size_t *written_size);

/** @brief Canonical schema artifact를 descriptor와 field-wise 대조해 읽는다. */
int ribon_ribos_extension_schema_read_v1(
    const struct RibonRibosExtensionDescriptor *extension,
    const uint8_t *artifact,
    size_t artifact_size);

#ifdef __cplusplus
}
#endif

#endif
