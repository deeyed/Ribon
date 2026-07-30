#ifndef RIBOS_SCHEMA_SCHEMA_H
#define RIBOS_SCHEMA_SCHEMA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file schema.h
 * @brief Ribos compiler, verifier와 VM이 공유하는 product schema 계약.
 */

#define RIBOS_SCHEMA_DIGEST_BYTES 32u
#define RIBOS_SCHEMA_MAX_PARAMETERS 4u
#define RIBOS_SCHEMA_V1_MAJOR 1u
#define RIBOS_SCHEMA_V1_MINOR 0u

/** Product descriptor와 policy artifact가 공유하는 capability 비트다. */
typedef enum RibosCapability {
    RIBOS_CAPABILITY_INSPECT = 1u << 0,
    RIBOS_CAPABILITY_DEVICE = 1u << 1,
    RIBOS_CAPABILITY_STATE = 1u << 2,
    RIBOS_CAPABILITY_NETWORK = 1u << 3,
    RIBOS_CAPABILITY_FLASH = 1u << 4,
    RIBOS_CAPABILITY_HANDOFF = 1u << 5,
    RIBOS_CAPABILITY_BOOT = 1u << 6,
    RIBOS_CAPABILITY_DIAGNOSTIC = 1u << 7
} RibosCapability;

/** Schema가 선언하는 named type의 안정된 분류다. */
typedef enum RibosSchemaTypeClass {
    RIBOS_SCHEMA_TYPE_VALUE = 0,
    RIBOS_SCHEMA_TYPE_OPAQUE_HANDLE,
    RIBOS_SCHEMA_TYPE_FACT,
    RIBOS_SCHEMA_TYPE_ENUM
} RibosSchemaTypeClass;

/** Product가 source name으로 공개하는 한 named type이다. */
typedef struct RibosSchemaType {
    uint32_t stable_id;
    RibosSchemaTypeClass type_class;
    const char *name;
} RibosSchemaType;

/** Product fact 또는 typed struct member 하나의 schema다. */
typedef struct RibosSchemaMember {
    uint32_t stable_id;
    const char *owner_type;
    const char *name;
    const char *result_type;
    const char *collection_element_type;
    uint32_t collection_bound;
} RibosSchemaMember;

/** Semantic helper parameter 하나의 name과 type이다. */
typedef struct RibosSchemaParameter {
    const char *name;
    const char *type;
} RibosSchemaParameter;

/**
 * Semantic helper 하나의 source ABI다.
 *
 * error_type이 NULL이 아니면 source result는
 * Result[result_type, error_type]이다.
 */
typedef struct RibosSchemaHelper {
    uint32_t stable_id;
    const char *path;
    uint32_t capabilities;
    const char *result_type;
    const char *error_type;
    RibosSchemaParameter parameters[RIBOS_SCHEMA_MAX_PARAMETERS];
    size_t parameter_count;
} RibosSchemaHelper;

/** Typed handoff key 하나와 그 value type이다. */
typedef struct RibosSchemaHandoffField {
    uint32_t stable_id;
    const char *key;
    const char *value_type;
} RibosSchemaHandoffField;

/**
 * 한 Ribon product가 Ribos에 공개하는 immutable schema다.
 *
 * 모든 table은 stable_id 오름차순이어야 한다. Schema identity는 pointer나
 * C layout이 아니라 이 descriptor의 canonical byte encoding에서 계산한다.
 */
typedef struct RibosProductSchema {
    uint16_t format_major;
    uint16_t format_minor;
    const char *product_id;
    const RibosSchemaType *types;
    size_t type_count;
    const RibosSchemaMember *members;
    size_t member_count;
    const RibosSchemaHelper *helpers;
    size_t helper_count;
    const RibosSchemaHandoffField *handoff_fields;
    size_t handoff_field_count;
} RibosProductSchema;

/** Schema operation의 안정된 종료 상태다. */
typedef enum RibosSchemaStatus {
    RIBOS_SCHEMA_OK = 0,
    RIBOS_SCHEMA_INVALID_ARGUMENT,
    RIBOS_SCHEMA_INVALID_DESCRIPTOR,
    RIBOS_SCHEMA_CAPACITY_EXCEEDED
} RibosSchemaStatus;

/** Generic host corpus가 사용하는 versioned reference schema를 반환한다. */
const RibosProductSchema *ribos_schema_reference_v1(void);

/** Descriptor ordering, stable ID와 필수 문자열을 검증한다. */
RibosSchemaStatus ribos_schema_validate(const RibosProductSchema *schema);

/**
 * Schema를 canonical little-endian byte sequence로 기록한다.
 *
 * output이 NULL이면 required_size만 계산한다.
 */
RibosSchemaStatus ribos_schema_encode(
    const RibosProductSchema *schema,
    uint8_t *output,
    size_t output_capacity,
    size_t *required_size);

/** Canonical schema byte sequence의 SHA-256 identity를 계산한다. */
RibosSchemaStatus ribos_schema_compute_identity(
    const RibosProductSchema *schema,
    uint8_t digest[RIBOS_SCHEMA_DIGEST_BYTES]);

/** Source name과 일치하는 product-generated type을 반환한다. */
const RibosSchemaType *ribos_schema_find_type(
    const RibosProductSchema *schema,
    const char *name,
    size_t name_length);

/** Source path와 일치하는 semantic helper를 반환한다. */
const RibosSchemaHelper *ribos_schema_find_helper(
    const RibosProductSchema *schema,
    const char *path,
    size_t path_length);

/** Owner type과 member spelling이 일치하는 fact/member를 반환한다. */
const RibosSchemaMember *ribos_schema_find_member(
    const RibosProductSchema *schema,
    const char *owner_type,
    size_t owner_type_length,
    const char *member,
    size_t member_length);

/** Handoff key spelling과 일치하는 typed field를 반환한다. */
const RibosSchemaHandoffField *ribos_schema_find_handoff_field(
    const RibosProductSchema *schema,
    const char *key,
    size_t key_length);

#ifdef __cplusplus
}
#endif

#endif
