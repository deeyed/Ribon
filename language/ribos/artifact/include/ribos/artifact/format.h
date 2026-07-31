#ifndef RIBOS_ARTIFACT_FORMAT_H
#define RIBOS_ARTIFACT_FORMAT_H

#include <stddef.h>
#include <stdint.h>

#include "ribos/schema/schema.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file format.h
 * @brief Ribos bytecode ISA와 signed artifact envelope v1의 wire 계약.
 */

#define RIBOS_VM_ABI_V1_MAJOR 1u
#define RIBOS_VM_ABI_V1_MINOR 0u
#define RIBOS_BYTECODE_ISA_V1_MAJOR 1u
#define RIBOS_BYTECODE_ISA_V1_MINOR 0u
#define RIBOS_ARTIFACT_ENVELOPE_V1_MAJOR 1u
#define RIBOS_ARTIFACT_ENVELOPE_V1_MINOR 0u

#define RIBOS_ARTIFACT_ENVELOPE_BYTES 128u
#define RIBOS_ARTIFACT_PAYLOAD_HEADER_BYTES 160u
#define RIBOS_ARTIFACT_SECTION_DESCRIPTOR_BYTES 32u
#define RIBOS_ARTIFACT_MAX_BYTES (32u * 1024u * 1024u)
#define RIBOS_ARTIFACT_MAX_VIRTUAL_REGISTERS 16384u
#define RIBOS_ARTIFACT_MAX_SLOTS 16384u
#define RIBOS_ARTIFACT_MAX_HELPER_IMPORTS 256u
#define RIBOS_ARTIFACT_MAX_KEY_ID_BYTES 64u
#define RIBOS_ARTIFACT_ED25519_SIGNATURE_BYTES 64u
#define RIBOS_ARTIFACT_SIGNATURE_MESSAGE_BYTES 112u
#define RIBOS_ARTIFACT_INVALID_ID UINT32_MAX
#define RIBOS_BYTECODE_FRAME_ALIGNMENT_V1 8u

#define RIBOS_ARTIFACT_TYPE_ROW_BYTES 128u
#define RIBOS_ARTIFACT_SHAPE_ROW_BYTES 32u
#define RIBOS_ARTIFACT_CONSTANT_ROW_BYTES 32u
#define RIBOS_ARTIFACT_FUNCTION_ROW_BYTES 104u
#define RIBOS_ARTIFACT_BLOCK_ROW_BYTES 32u
#define RIBOS_ARTIFACT_LOOP_ROW_BYTES 32u
#define RIBOS_ARTIFACT_SLOT_ROW_BYTES 32u
#define RIBOS_ARTIFACT_INSTRUCTION_ROW_BYTES 48u
#define RIBOS_ARTIFACT_OPERAND_ROW_BYTES 4u
#define RIBOS_ARTIFACT_HELPER_IMPORT_ROW_BYTES 16u
#define RIBOS_ARTIFACT_HELPER_BOUND_ROW_BYTES 16u
#define RIBOS_ARTIFACT_SOURCE_MAP_ROW_BYTES 40u

/** Artifact encoder와 allocation-free reader의 안정된 결과다. */
typedef enum RibosArtifactStatus {
    RIBOS_ARTIFACT_OK = 0,
    RIBOS_ARTIFACT_INVALID_ARGUMENT,
    RIBOS_ARTIFACT_INVALID_POLICY_IR,
    RIBOS_ARTIFACT_RESOURCE_CLOSURE_FAILED,
    RIBOS_ARTIFACT_CAPACITY_EXCEEDED,
    RIBOS_ARTIFACT_INVALID_FORMAT,
    RIBOS_ARTIFACT_UNSUPPORTED_VERSION,
    RIBOS_ARTIFACT_HASH_MISMATCH,
    RIBOS_ARTIFACT_INVALID_SIGNATURE_ENVELOPE
} RibosArtifactStatus;

/** Payload hash algorithm registry다. */
typedef enum RibosArtifactHashAlgorithm {
    RIBOS_ARTIFACT_HASH_SHA256 = 1
} RibosArtifactHashAlgorithm;

/** Signature envelope algorithm registry다. */
typedef enum RibosArtifactSignatureAlgorithm {
    RIBOS_ARTIFACT_SIGNATURE_NONE = 0,
    RIBOS_ARTIFACT_SIGNATURE_ED25519 = 1
} RibosArtifactSignatureAlgorithm;

/** Envelope flag의 안정된 의미 비트다. */
typedef enum RibosArtifactEnvelopeFlags {
    RIBOS_ARTIFACT_ENVELOPE_SIGNED = 1u << 0
} RibosArtifactEnvelopeFlags;

/**
 * Caller-owned bytes의 SHA-256 identity를 allocation 없이 계산한다.
 *
 * `input_size == 0`이면 `input`은 NULL일 수 있다. 이 함수는 context와 product
 * adapter가 immutable value identity를 만들 때 artifact hash 구현을 공유한다.
 */
void ribos_artifact_digest_bytes_v1(
    const uint8_t *input,
    size_t input_size,
    uint8_t digest[RIBOS_SCHEMA_DIGEST_BYTES]);

/** Executable payload flag의 안정된 의미 비트다. */
typedef enum RibosArtifactPayloadFlags {
    RIBOS_ARTIFACT_HAS_SOURCE_MAP = 1u << 0
} RibosArtifactPayloadFlags;

/**
 * Ribos ISA v1 opcode다.
 *
 * 값은 wire byte 자체이며 Policy IR enum 값과 독립적으로 고정된다.
 */
typedef enum RibosBytecodeOpcode {
    RIBOS_BC_PARAMETER = 0x01,
    RIBOS_BC_CONST_UNIT = 0x02,
    RIBOS_BC_CONST_BOOL = 0x03,
    RIBOS_BC_CONST_INTEGER = 0x04,
    RIBOS_BC_CONST_STRING = 0x05,
    RIBOS_BC_CONST_SYMBOL = 0x06,
    RIBOS_BC_MOVE = 0x07,
    RIBOS_BC_CHECKED_UNARY = 0x08,
    RIBOS_BC_CHECKED_BINARY = 0x09,
    RIBOS_BC_BUILD_LIST = 0x0a,
    RIBOS_BC_BUILD_MAP = 0x0b,
    RIBOS_BC_BUILD_STRUCT = 0x0c,
    RIBOS_BC_BUILD_VARIANT = 0x0d,
    RIBOS_BC_MEMBER = 0x0e,
    RIBOS_BC_INDEX = 0x0f,
    RIBOS_BC_COLLECTION_LENGTH = 0x10,
    RIBOS_BC_VARIANT_TAG = 0x11,
    RIBOS_BC_VARIANT_PAYLOAD = 0x12,
    RIBOS_BC_CALL_DIRECT = 0x13,
    RIBOS_BC_CALL_HELPER = 0x14,
    RIBOS_BC_JUMP = 0x15,
    RIBOS_BC_BRANCH = 0x16,
    RIBOS_BC_RETURN = 0x17,
    RIBOS_BC_TRAP = 0x18
} RibosBytecodeOpcode;

/** Type table `kind` field의 VM ABI v1 wire registry다. */
typedef enum RibosBytecodeTypeKind {
    RIBOS_BC_TYPE_ERROR = 0,
    RIBOS_BC_TYPE_UNKNOWN = 1,
    RIBOS_BC_TYPE_UNIT = 2,
    RIBOS_BC_TYPE_BOOL = 3,
    RIBOS_BC_TYPE_UNSIGNED = 4,
    RIBOS_BC_TYPE_SIGNED = 5,
    RIBOS_BC_TYPE_STRING_LITERAL = 6,
    RIBOS_BC_TYPE_NAMED = 7,
    RIBOS_BC_TYPE_ARRAY = 8,
    RIBOS_BC_TYPE_LIST = 9,
    RIBOS_BC_TYPE_FROZEN_MAP = 10,
    RIBOS_BC_TYPE_DICT = 11,
    RIBOS_BC_TYPE_OPTION = 12,
    RIBOS_BC_TYPE_RESULT = 13,
    RIBOS_BC_TYPE_STRUCT = 14,
    RIBOS_BC_TYPE_ENUM = 15
} RibosBytecodeTypeKind;

/** Type table `storage kind` field의 VM ABI v1 wire registry다. */
typedef enum RibosBytecodeStorageKind {
    RIBOS_BC_STORAGE_SCALAR = 0,
    RIBOS_BC_STORAGE_OPAQUE = 1,
    RIBOS_BC_STORAGE_INLINE_ARRAY = 2,
    RIBOS_BC_STORAGE_INLINE_LIST = 3,
    RIBOS_BC_STORAGE_SORTED_MAP = 4,
    RIBOS_BC_STORAGE_TAGGED_UNION = 5,
    RIBOS_BC_STORAGE_INLINE_STRUCT = 6
} RibosBytecodeStorageKind;

/** Shape table `kind` field의 VM ABI v1 wire registry다. */
typedef enum RibosBytecodeShapeKind {
    RIBOS_BC_SHAPE_STRUCT_FIELD = 0,
    RIBOS_BC_SHAPE_ENUM_VARIANT = 1,
    RIBOS_BC_SHAPE_ENUM_PAYLOAD = 2
} RibosBytecodeShapeKind;

/** Constant table `kind` field의 VM ABI v1 wire registry다. */
typedef enum RibosBytecodeConstantKind {
    RIBOS_BC_CONSTANT_STRING = 0,
    RIBOS_BC_CONSTANT_SYMBOL = 1
} RibosBytecodeConstantKind;

/** Checked unary/binary instruction `target` field의 ISA v1 registry다. */
typedef enum RibosBytecodeCheckedOperator {
    RIBOS_BC_CHECK_NOT = 1,
    RIBOS_BC_CHECK_EQUAL = 2,
    RIBOS_BC_CHECK_NOT_EQUAL = 3,
    RIBOS_BC_CHECK_LESS = 4,
    RIBOS_BC_CHECK_LESS_EQUAL = 5,
    RIBOS_BC_CHECK_GREATER = 6,
    RIBOS_BC_CHECK_GREATER_EQUAL = 7,
    RIBOS_BC_CHECK_IN = 8,
    RIBOS_BC_CHECK_NOT_IN = 9,
    RIBOS_BC_CHECK_BIT_OR = 10,
    RIBOS_BC_CHECK_BIT_XOR = 11,
    RIBOS_BC_CHECK_BIT_AND = 12,
    RIBOS_BC_CHECK_SHIFT_LEFT = 13,
    RIBOS_BC_CHECK_SHIFT_RIGHT = 14,
    RIBOS_BC_CHECK_ADD = 15,
    RIBOS_BC_CHECK_SUBTRACT = 16,
    RIBOS_BC_CHECK_MULTIPLY = 17,
    RIBOS_BC_CHECK_DIVIDE = 18,
    RIBOS_BC_CHECK_REMAINDER = 19,
    RIBOS_BC_CHECK_POSITIVE = 20,
    RIBOS_BC_CHECK_NEGATIVE = 21,
    RIBOS_BC_CHECK_BIT_NOT = 22
} RibosBytecodeCheckedOperator;

/** Function descriptor flag의 VM ABI v1 wire registry다. */
typedef enum RibosBytecodeFunctionFlags {
    RIBOS_BC_FUNCTION_POLICY = 1u << 0,
    RIBOS_BC_FUNCTION_PURE = 1u << 1
} RibosBytecodeFunctionFlags;

/** Slot descriptor flag의 VM ABI v1 wire registry다. */
typedef enum RibosBytecodeSlotFlags {
    RIBOS_BC_SLOT_PARAMETER = 1u << 0,
    RIBOS_BC_SLOT_BINDING = 1u << 1,
    RIBOS_BC_SLOT_MUTABLE = 1u << 2
} RibosBytecodeSlotFlags;

/** Block descriptor flag의 VM ABI v1 wire registry다. */
typedef enum RibosBytecodeBlockFlags {
    RIBOS_BC_BLOCK_ENTRY = 1u << 0
} RibosBytecodeBlockFlags;

/** Function terminal mask의 VM ABI v1 wire registry다. */
typedef enum RibosBytecodeTerminalMask {
    RIBOS_BC_TERMINAL_RETURN = 1u << 0,
    RIBOS_BC_TERMINAL_TRAP = 1u << 1
} RibosBytecodeTerminalMask;

/** Payload section directory의 canonical 순서와 kind다. */
typedef enum RibosArtifactSectionKind {
    RIBOS_ARTIFACT_SECTION_TYPES = 1,
    RIBOS_ARTIFACT_SECTION_SHAPES = 2,
    RIBOS_ARTIFACT_SECTION_CONSTANTS = 3,
    RIBOS_ARTIFACT_SECTION_CONSTANT_BYTES = 4,
    RIBOS_ARTIFACT_SECTION_FUNCTIONS = 5,
    RIBOS_ARTIFACT_SECTION_BLOCKS = 6,
    RIBOS_ARTIFACT_SECTION_LOOPS = 7,
    RIBOS_ARTIFACT_SECTION_SLOTS = 8,
    RIBOS_ARTIFACT_SECTION_INSTRUCTIONS = 9,
    RIBOS_ARTIFACT_SECTION_OPERANDS = 10,
    RIBOS_ARTIFACT_SECTION_HELPER_IMPORTS = 11,
    RIBOS_ARTIFACT_SECTION_HELPER_BOUNDS = 12,
    RIBOS_ARTIFACT_SECTION_SOURCE_MAPS = 13,
    RIBOS_ARTIFACT_SECTION_KIND_COUNT = 14
} RibosArtifactSectionKind;

/** 검증된 section 하나의 borrowed byte view다. */
typedef struct RibosArtifactSectionView {
    RibosArtifactSectionKind kind;
    uint32_t row_size;
    uint32_t count;
    const uint8_t *bytes;
    size_t byte_length;
} RibosArtifactSectionView;

/**
 * Hash와 wire range가 검증된 artifact의 allocation-free borrowed view다.
 *
 * Signature bytes의 암호학적 검증과 bytecode semantic verification은 이 view를
 * 만드는 함수의 책임이 아니다.
 */
typedef struct RibosArtifactView {
    uint16_t envelope_major;
    uint16_t envelope_minor;
    uint16_t vm_abi_major;
    uint16_t vm_abi_minor;
    uint16_t isa_major;
    uint16_t isa_minor;
    uint32_t envelope_flags;
    uint32_t payload_flags;
    RibosArtifactHashAlgorithm hash_algorithm;
    RibosArtifactSignatureAlgorithm signature_algorithm;
    uint32_t entry_function;
    uint32_t virtual_register_count;
    uint32_t slot_count;
    uint32_t declared_capabilities;
    uint32_t required_capabilities;
    uint64_t instruction_budget;
    uint64_t instruction_upper_bound;
    uint64_t helper_budget;
    uint64_t helper_upper_bound;
    uint64_t maximum_stack_bytes;
    uint32_t maximum_call_depth;
    uint8_t schema_digest[RIBOS_SCHEMA_DIGEST_BYTES];
    uint8_t artifact_hash[RIBOS_SCHEMA_DIGEST_BYTES];
    const uint8_t *payload;
    size_t payload_length;
    const uint8_t *key_id;
    size_t key_id_length;
    const uint8_t *signature;
    size_t signature_length;
    RibosArtifactSectionView
        sections[RIBOS_ARTIFACT_SECTION_KIND_COUNT];
    size_t section_count;
} RibosArtifactView;

/**
 * Little-endian range, section directory와 payload SHA-256을 검증한다.
 *
 * 함수는 allocation하지 않으며 성공한 view의 pointer는 input artifact lifetime
 * 동안만 유효하다.
 */
RibosArtifactStatus ribos_artifact_open_v1(
    const uint8_t *artifact,
    size_t artifact_size,
    RibosArtifactView *view);

/** Kind로 검증된 borrowed section view를 찾는다. */
const RibosArtifactSectionView *ribos_artifact_find_section(
    const RibosArtifactView *view,
    RibosArtifactSectionKind kind);

/**
 * Ed25519 signer와 verifier가 공유하는 canonical 112-byte message를 만든다.
 *
 * Message는 domain, envelope version, algorithm, payload length, payload hash와
 * SHA-256(key ID)를 결합한다.
 */
RibosArtifactStatus ribos_artifact_signature_message_v1(
    RibosArtifactSignatureAlgorithm algorithm,
    const uint8_t *key_id,
    size_t key_id_length,
    uint64_t payload_length,
    const uint8_t artifact_hash[RIBOS_SCHEMA_DIGEST_BYTES],
    uint8_t output[RIBOS_ARTIFACT_SIGNATURE_MESSAGE_BYTES]);

/** Stable bytecode opcode의 ASCII spelling을 반환한다. */
const char *ribos_bytecode_opcode_name(RibosBytecodeOpcode opcode);

#ifdef __cplusplus
}
#endif

#endif
