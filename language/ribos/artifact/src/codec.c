#include "ribos/artifact/format.h"

#include "internal.h"

#include <string.h>

static int
ribos_artifact_u64_to_size(uint64_t value, size_t *result)
{
    if (result == NULL || value > SIZE_MAX) {
        return 0;
    }
    *result = (size_t)value;
    return 1;
}

static uint32_t
ribos_artifact_expected_row_size(RibosArtifactSectionKind kind)
{
    switch (kind) {
    case RIBOS_ARTIFACT_SECTION_TYPES:
        return RIBOS_ARTIFACT_TYPE_ROW_BYTES;
    case RIBOS_ARTIFACT_SECTION_SHAPES:
        return RIBOS_ARTIFACT_SHAPE_ROW_BYTES;
    case RIBOS_ARTIFACT_SECTION_CONSTANTS:
        return RIBOS_ARTIFACT_CONSTANT_ROW_BYTES;
    case RIBOS_ARTIFACT_SECTION_CONSTANT_BYTES:
        return 1;
    case RIBOS_ARTIFACT_SECTION_FUNCTIONS:
        return RIBOS_ARTIFACT_FUNCTION_ROW_BYTES;
    case RIBOS_ARTIFACT_SECTION_BLOCKS:
        return RIBOS_ARTIFACT_BLOCK_ROW_BYTES;
    case RIBOS_ARTIFACT_SECTION_LOOPS:
        return RIBOS_ARTIFACT_LOOP_ROW_BYTES;
    case RIBOS_ARTIFACT_SECTION_SLOTS:
        return RIBOS_ARTIFACT_SLOT_ROW_BYTES;
    case RIBOS_ARTIFACT_SECTION_INSTRUCTIONS:
        return RIBOS_ARTIFACT_INSTRUCTION_ROW_BYTES;
    case RIBOS_ARTIFACT_SECTION_OPERANDS:
        return RIBOS_ARTIFACT_OPERAND_ROW_BYTES;
    case RIBOS_ARTIFACT_SECTION_HELPER_IMPORTS:
        return RIBOS_ARTIFACT_HELPER_IMPORT_ROW_BYTES;
    case RIBOS_ARTIFACT_SECTION_HELPER_BOUNDS:
        return RIBOS_ARTIFACT_HELPER_BOUND_ROW_BYTES;
    case RIBOS_ARTIFACT_SECTION_SOURCE_MAPS:
        return RIBOS_ARTIFACT_SOURCE_MAP_ROW_BYTES;
    default:
        return 0;
    }
}

static uint32_t
ribos_artifact_section_max_count(RibosArtifactSectionKind kind)
{
    switch (kind) {
    case RIBOS_ARTIFACT_SECTION_TYPES:
        return 256;
    case RIBOS_ARTIFACT_SECTION_SHAPES:
        return 4096;
    case RIBOS_ARTIFACT_SECTION_CONSTANTS:
        return 4096;
    case RIBOS_ARTIFACT_SECTION_CONSTANT_BYTES:
        return 1024u * 1024u;
    case RIBOS_ARTIFACT_SECTION_FUNCTIONS:
        return 64;
    case RIBOS_ARTIFACT_SECTION_BLOCKS:
        return 4096;
    case RIBOS_ARTIFACT_SECTION_LOOPS:
        return 1024;
    case RIBOS_ARTIFACT_SECTION_SLOTS:
        return RIBOS_ARTIFACT_MAX_SLOTS;
    case RIBOS_ARTIFACT_SECTION_INSTRUCTIONS:
        return 65536;
    case RIBOS_ARTIFACT_SECTION_OPERANDS:
        return 131072;
    case RIBOS_ARTIFACT_SECTION_HELPER_IMPORTS:
        return RIBOS_ARTIFACT_MAX_HELPER_IMPORTS;
    case RIBOS_ARTIFACT_SECTION_HELPER_BOUNDS:
        return 16384;
    case RIBOS_ARTIFACT_SECTION_SOURCE_MAPS:
        return 65536;
    default:
        return 0;
    }
}

static int
ribos_artifact_validate_signature_shape(
    uint32_t flags,
    uint16_t algorithm,
    size_t key_id_length,
    size_t signature_length)
{
    if ((flags & ~RIBOS_ARTIFACT_ENVELOPE_SIGNED) != 0) {
        return 0;
    }
    if (algorithm == RIBOS_ARTIFACT_SIGNATURE_NONE) {
        return flags == 0 && key_id_length == 0 &&
            signature_length == 0;
    }
    if (algorithm == RIBOS_ARTIFACT_SIGNATURE_ED25519) {
        return flags == RIBOS_ARTIFACT_ENVELOPE_SIGNED &&
            key_id_length > 0 &&
            key_id_length <= RIBOS_ARTIFACT_MAX_KEY_ID_BYTES &&
            signature_length ==
                RIBOS_ARTIFACT_ED25519_SIGNATURE_BYTES;
    }
    return 0;
}

static int
ribos_artifact_validate_instruction_opcodes(
    const RibosArtifactSectionView *section)
{
    size_t index;

    if (section == NULL) {
        return 0;
    }
    for (index = 0; index < section->count; ++index) {
        uint8_t opcode =
            section->bytes[index * section->row_size];

        if (opcode < RIBOS_BC_PARAMETER ||
            opcode > RIBOS_BC_TRAP) {
            return 0;
        }
    }
    return 1;
}

static RibosArtifactStatus
ribos_artifact_open_payload(
    const uint8_t *payload,
    size_t payload_length,
    RibosArtifactView *view)
{
    const uint8_t *magic;
    const uint8_t *reserved;
    uint32_t header_bytes;
    uint32_t section_count;
    uint64_t directory_offset_u64;
    uint64_t directory_length_u64;
    uint64_t encoded_payload_length;
    size_t directory_offset;
    size_t directory_length;
    size_t expected_directory_length;
    size_t expected_data_offset;
    size_t index;
    uint32_t expected_sections;

    if (payload_length < RIBOS_ARTIFACT_PAYLOAD_HEADER_BYTES ||
        !ribos_artifact_reader_bytes(
            payload,
            payload_length,
            RIBOS_PAYLOAD_MAGIC_OFFSET,
            RIBOS_ARTIFACT_MAGIC_BYTES,
            &magic) ||
        memcmp(
            magic,
            RIBOS_ARTIFACT_PAYLOAD_MAGIC,
            RIBOS_ARTIFACT_MAGIC_BYTES) != 0 ||
        !ribos_artifact_reader_u16(
            payload,
            payload_length,
            RIBOS_PAYLOAD_VM_MAJOR_OFFSET,
            &view->vm_abi_major) ||
        !ribos_artifact_reader_u16(
            payload,
            payload_length,
            RIBOS_PAYLOAD_VM_MINOR_OFFSET,
            &view->vm_abi_minor) ||
        !ribos_artifact_reader_u16(
            payload,
            payload_length,
            RIBOS_PAYLOAD_ISA_MAJOR_OFFSET,
            &view->isa_major) ||
        !ribos_artifact_reader_u16(
            payload,
            payload_length,
            RIBOS_PAYLOAD_ISA_MINOR_OFFSET,
            &view->isa_minor) ||
        !ribos_artifact_reader_u32(
            payload,
            payload_length,
            RIBOS_PAYLOAD_HEADER_BYTES_OFFSET,
            &header_bytes) ||
        !ribos_artifact_reader_u32(
            payload,
            payload_length,
            RIBOS_PAYLOAD_FLAGS_OFFSET,
            &view->payload_flags) ||
        !ribos_artifact_reader_u32(
            payload,
            payload_length,
            RIBOS_PAYLOAD_SECTION_COUNT_OFFSET,
            &section_count) ||
        !ribos_artifact_reader_u32(
            payload,
            payload_length,
            RIBOS_PAYLOAD_ENTRY_FUNCTION_OFFSET,
            &view->entry_function) ||
        !ribos_artifact_reader_u32(
            payload,
            payload_length,
            RIBOS_PAYLOAD_REGISTER_COUNT_OFFSET,
            &view->virtual_register_count) ||
        !ribos_artifact_reader_u32(
            payload,
            payload_length,
            RIBOS_PAYLOAD_SLOT_COUNT_OFFSET,
            &view->slot_count) ||
        !ribos_artifact_reader_u32(
            payload,
            payload_length,
            RIBOS_PAYLOAD_DECLARED_CAPS_OFFSET,
            &view->declared_capabilities) ||
        !ribos_artifact_reader_u32(
            payload,
            payload_length,
            RIBOS_PAYLOAD_REQUIRED_CAPS_OFFSET,
            &view->required_capabilities) ||
        !ribos_artifact_reader_u64(
            payload,
            payload_length,
            RIBOS_PAYLOAD_INSTRUCTION_BUDGET_OFFSET,
            &view->instruction_budget) ||
        !ribos_artifact_reader_u64(
            payload,
            payload_length,
            RIBOS_PAYLOAD_INSTRUCTION_UPPER_OFFSET,
            &view->instruction_upper_bound) ||
        !ribos_artifact_reader_u64(
            payload,
            payload_length,
            RIBOS_PAYLOAD_HELPER_BUDGET_OFFSET,
            &view->helper_budget) ||
        !ribos_artifact_reader_u64(
            payload,
            payload_length,
            RIBOS_PAYLOAD_HELPER_UPPER_OFFSET,
            &view->helper_upper_bound) ||
        !ribos_artifact_reader_u64(
            payload,
            payload_length,
            RIBOS_PAYLOAD_STACK_BYTES_OFFSET,
            &view->maximum_stack_bytes) ||
        !ribos_artifact_reader_u32(
            payload,
            payload_length,
            RIBOS_PAYLOAD_CALL_DEPTH_OFFSET,
            &view->maximum_call_depth) ||
        !ribos_artifact_reader_bytes(
            payload,
            payload_length,
            RIBOS_PAYLOAD_SCHEMA_DIGEST_OFFSET,
            RIBOS_SCHEMA_DIGEST_BYTES,
            &magic) ||
        !ribos_artifact_reader_u64(
            payload,
            payload_length,
            RIBOS_PAYLOAD_DIRECTORY_OFFSET_OFFSET,
            &directory_offset_u64) ||
        !ribos_artifact_reader_u64(
            payload,
            payload_length,
            RIBOS_PAYLOAD_DIRECTORY_LENGTH_OFFSET,
            &directory_length_u64) ||
        !ribos_artifact_reader_u64(
            payload,
            payload_length,
            RIBOS_PAYLOAD_LENGTH_OFFSET,
            &encoded_payload_length)) {
        return RIBOS_ARTIFACT_INVALID_FORMAT;
    }
    memcpy(
        view->schema_digest,
        magic,
        RIBOS_SCHEMA_DIGEST_BYTES);
    if (view->vm_abi_major != RIBOS_VM_ABI_V1_MAJOR ||
        view->vm_abi_minor != RIBOS_VM_ABI_V1_MINOR ||
        view->isa_major != RIBOS_BYTECODE_ISA_V1_MAJOR ||
        view->isa_minor != RIBOS_BYTECODE_ISA_V1_MINOR) {
        return RIBOS_ARTIFACT_UNSUPPORTED_VERSION;
    }
    if (header_bytes != RIBOS_ARTIFACT_PAYLOAD_HEADER_BYTES ||
        (view->payload_flags & ~RIBOS_ARTIFACT_HAS_SOURCE_MAP) != 0 ||
        encoded_payload_length != payload_length ||
        view->virtual_register_count != view->slot_count ||
        view->slot_count > RIBOS_ARTIFACT_MAX_SLOTS ||
        (view->required_capabilities &
         ~view->declared_capabilities) != 0 ||
        view->instruction_upper_bound > view->instruction_budget ||
        view->helper_upper_bound > view->helper_budget ||
        view->maximum_stack_bytes > 64u * 1024u * 1024u ||
        view->maximum_call_depth == 0 ||
        ribos_artifact_bytes_are_zero(
            view->schema_digest,
            RIBOS_SCHEMA_DIGEST_BYTES)) {
        return RIBOS_ARTIFACT_INVALID_FORMAT;
    }
    if (!ribos_artifact_reader_bytes(
            payload, payload_length, 92, 4, &reserved) ||
        !ribos_artifact_bytes_are_zero(reserved, 4) ||
        !ribos_artifact_reader_bytes(
            payload,
            payload_length,
            RIBOS_PAYLOAD_RESERVED_OFFSET,
            8,
            &reserved) ||
        !ribos_artifact_bytes_are_zero(reserved, 8) ||
        !ribos_artifact_u64_to_size(
            directory_offset_u64,
            &directory_offset) ||
        !ribos_artifact_u64_to_size(
            directory_length_u64,
            &directory_length) ||
        !ribos_artifact_size_multiply(
            section_count,
            RIBOS_ARTIFACT_SECTION_DESCRIPTOR_BYTES,
            &expected_directory_length) ||
        directory_offset != RIBOS_ARTIFACT_PAYLOAD_HEADER_BYTES ||
        directory_length != expected_directory_length ||
        !ribos_artifact_size_add(
            directory_offset,
            directory_length,
            &expected_data_offset) ||
        expected_data_offset > payload_length) {
        return RIBOS_ARTIFACT_INVALID_FORMAT;
    }
    expected_sections =
        (view->payload_flags & RIBOS_ARTIFACT_HAS_SOURCE_MAP) != 0 ?
            13u : 12u;
    if (section_count != expected_sections) {
        return RIBOS_ARTIFACT_INVALID_FORMAT;
    }
    for (index = 0; index < section_count; ++index) {
        size_t descriptor =
            directory_offset +
            index * RIBOS_ARTIFACT_SECTION_DESCRIPTOR_BYTES;
        uint16_t kind_u16;
        uint16_t flags;
        uint32_t row_size;
        uint64_t data_offset_u64;
        uint64_t data_length_u64;
        uint32_t count;
        uint32_t descriptor_reserved;
        size_t data_offset;
        size_t data_length;
        size_t expected_length;
        size_t aligned_offset;
        const uint8_t *bytes;
        RibosArtifactSectionKind kind;

        if (!ribos_artifact_reader_u16(
                payload, payload_length, descriptor, &kind_u16) ||
            !ribos_artifact_reader_u16(
                payload,
                payload_length,
                descriptor + RIBOS_SECTION_FLAGS_OFFSET,
                &flags) ||
            !ribos_artifact_reader_u32(
                payload,
                payload_length,
                descriptor + RIBOS_SECTION_ROW_SIZE_OFFSET,
                &row_size) ||
            !ribos_artifact_reader_u64(
                payload,
                payload_length,
                descriptor + RIBOS_SECTION_DATA_OFFSET_OFFSET,
                &data_offset_u64) ||
            !ribos_artifact_reader_u64(
                payload,
                payload_length,
                descriptor + RIBOS_SECTION_DATA_LENGTH_OFFSET,
                &data_length_u64) ||
            !ribos_artifact_reader_u32(
                payload,
                payload_length,
                descriptor + RIBOS_SECTION_COUNT_OFFSET,
                &count) ||
            !ribos_artifact_reader_u32(
                payload,
                payload_length,
                descriptor + RIBOS_SECTION_RESERVED_OFFSET,
                &descriptor_reserved)) {
            return RIBOS_ARTIFACT_INVALID_FORMAT;
        }
        kind = (RibosArtifactSectionKind)kind_u16;
        if (kind_u16 != index + 1 ||
            flags != 0 || descriptor_reserved != 0 ||
            row_size != ribos_artifact_expected_row_size(kind) ||
            count > ribos_artifact_section_max_count(kind) ||
            !ribos_artifact_u64_to_size(
                data_offset_u64,
                &data_offset) ||
            !ribos_artifact_u64_to_size(
                data_length_u64,
                &data_length) ||
            !ribos_artifact_size_multiply(
                count,
                row_size,
                &expected_length) ||
            data_length != expected_length ||
            !ribos_artifact_size_align(
                expected_data_offset,
                8,
                &aligned_offset) ||
            data_offset != aligned_offset ||
            !ribos_artifact_reader_bytes(
                payload,
                payload_length,
                expected_data_offset,
                data_offset - expected_data_offset,
                &reserved) ||
            !ribos_artifact_bytes_are_zero(
                reserved,
                data_offset - expected_data_offset) ||
            !ribos_artifact_reader_bytes(
                payload,
                payload_length,
                data_offset,
                data_length,
                &bytes) ||
            !ribos_artifact_size_add(
                data_offset,
                data_length,
                &expected_data_offset)) {
            return RIBOS_ARTIFACT_INVALID_FORMAT;
        }
        view->sections[kind] = (RibosArtifactSectionView){
            .kind = kind,
            .row_size = row_size,
            .count = count,
            .bytes = bytes,
            .byte_length = data_length,
        };
        ++view->section_count;
    }
    if (expected_data_offset != payload_length ||
        view->entry_function >=
            view->sections[
                RIBOS_ARTIFACT_SECTION_FUNCTIONS
            ].count ||
        view->slot_count !=
            view->sections[RIBOS_ARTIFACT_SECTION_SLOTS].count ||
        !ribos_artifact_validate_instruction_opcodes(
            &view->sections[
                RIBOS_ARTIFACT_SECTION_INSTRUCTIONS])) {
        return RIBOS_ARTIFACT_INVALID_FORMAT;
    }
    view->payload = payload;
    view->payload_length = payload_length;
    return RIBOS_ARTIFACT_OK;
}

RibosArtifactStatus
ribos_artifact_open_v1(
    const uint8_t *artifact,
    size_t artifact_size,
    RibosArtifactView *view)
{
    const uint8_t *magic;
    const uint8_t *reserved;
    const uint8_t *encoded_hash;
    const uint8_t *payload;
    const uint8_t *key_id;
    const uint8_t *signature;
    uint32_t header_bytes;
    uint16_t hash_algorithm;
    uint16_t signature_algorithm;
    uint64_t payload_offset_u64;
    uint64_t payload_length_u64;
    uint64_t key_id_offset_u64;
    uint64_t signature_offset_u64;
    uint64_t total_length_u64;
    uint32_t key_id_length_u32;
    uint32_t signature_length_u32;
    size_t payload_offset;
    size_t payload_length;
    size_t key_id_offset;
    size_t signature_offset;
    size_t total_length;
    size_t expected_offset;
    uint8_t computed_hash[RIBOS_SCHEMA_DIGEST_BYTES];
    RibosArtifactStatus status;

    if (artifact == NULL || view == NULL) {
        return RIBOS_ARTIFACT_INVALID_ARGUMENT;
    }
    memset(view, 0, sizeof(*view));
    if (artifact_size < RIBOS_ARTIFACT_ENVELOPE_BYTES ||
        artifact_size > RIBOS_ARTIFACT_MAX_BYTES ||
        !ribos_artifact_reader_bytes(
            artifact,
            artifact_size,
            RIBOS_ENVELOPE_MAGIC_OFFSET,
            RIBOS_ARTIFACT_MAGIC_BYTES,
            &magic) ||
        memcmp(
            magic,
            RIBOS_ARTIFACT_ENVELOPE_MAGIC,
            RIBOS_ARTIFACT_MAGIC_BYTES) != 0 ||
        !ribos_artifact_reader_u16(
            artifact,
            artifact_size,
            RIBOS_ENVELOPE_MAJOR_OFFSET,
            &view->envelope_major) ||
        !ribos_artifact_reader_u16(
            artifact,
            artifact_size,
            RIBOS_ENVELOPE_MINOR_OFFSET,
            &view->envelope_minor) ||
        !ribos_artifact_reader_u32(
            artifact,
            artifact_size,
            RIBOS_ENVELOPE_HEADER_BYTES_OFFSET,
            &header_bytes) ||
        !ribos_artifact_reader_u32(
            artifact,
            artifact_size,
            RIBOS_ENVELOPE_FLAGS_OFFSET,
            &view->envelope_flags) ||
        !ribos_artifact_reader_u16(
            artifact,
            artifact_size,
            RIBOS_ENVELOPE_HASH_ALGORITHM_OFFSET,
            &hash_algorithm) ||
        !ribos_artifact_reader_u16(
            artifact,
            artifact_size,
            RIBOS_ENVELOPE_SIGNATURE_ALGORITHM_OFFSET,
            &signature_algorithm) ||
        !ribos_artifact_reader_u64(
            artifact,
            artifact_size,
            RIBOS_ENVELOPE_PAYLOAD_OFFSET_OFFSET,
            &payload_offset_u64) ||
        !ribos_artifact_reader_u64(
            artifact,
            artifact_size,
            RIBOS_ENVELOPE_PAYLOAD_LENGTH_OFFSET,
            &payload_length_u64) ||
        !ribos_artifact_reader_u64(
            artifact,
            artifact_size,
            RIBOS_ENVELOPE_KEY_ID_OFFSET_OFFSET,
            &key_id_offset_u64) ||
        !ribos_artifact_reader_u32(
            artifact,
            artifact_size,
            RIBOS_ENVELOPE_KEY_ID_LENGTH_OFFSET,
            &key_id_length_u32) ||
        !ribos_artifact_reader_u32(
            artifact,
            artifact_size,
            RIBOS_ENVELOPE_SIGNATURE_LENGTH_OFFSET,
            &signature_length_u32) ||
        !ribos_artifact_reader_u64(
            artifact,
            artifact_size,
            RIBOS_ENVELOPE_SIGNATURE_OFFSET_OFFSET,
            &signature_offset_u64) ||
        !ribos_artifact_reader_u64(
            artifact,
            artifact_size,
            RIBOS_ENVELOPE_TOTAL_LENGTH_OFFSET,
            &total_length_u64) ||
        !ribos_artifact_reader_bytes(
            artifact,
            artifact_size,
            RIBOS_ENVELOPE_HASH_OFFSET,
            RIBOS_SCHEMA_DIGEST_BYTES,
            &encoded_hash) ||
        !ribos_artifact_reader_bytes(
            artifact,
            artifact_size,
            RIBOS_ENVELOPE_RESERVED_OFFSET,
            RIBOS_ARTIFACT_ENVELOPE_BYTES -
                RIBOS_ENVELOPE_RESERVED_OFFSET,
            &reserved)) {
        return RIBOS_ARTIFACT_INVALID_FORMAT;
    }
    if (view->envelope_major !=
            RIBOS_ARTIFACT_ENVELOPE_V1_MAJOR ||
        view->envelope_minor !=
            RIBOS_ARTIFACT_ENVELOPE_V1_MINOR) {
        return RIBOS_ARTIFACT_UNSUPPORTED_VERSION;
    }
    if (header_bytes != RIBOS_ARTIFACT_ENVELOPE_BYTES ||
        hash_algorithm != RIBOS_ARTIFACT_HASH_SHA256 ||
        !ribos_artifact_bytes_are_zero(
            reserved,
            RIBOS_ARTIFACT_ENVELOPE_BYTES -
                RIBOS_ENVELOPE_RESERVED_OFFSET) ||
        !ribos_artifact_u64_to_size(
            payload_offset_u64,
            &payload_offset) ||
        !ribos_artifact_u64_to_size(
            payload_length_u64,
            &payload_length) ||
        !ribos_artifact_u64_to_size(
            key_id_offset_u64,
            &key_id_offset) ||
        !ribos_artifact_u64_to_size(
            signature_offset_u64,
            &signature_offset) ||
        !ribos_artifact_u64_to_size(
            total_length_u64,
            &total_length)) {
        return RIBOS_ARTIFACT_INVALID_FORMAT;
    }
    if (!ribos_artifact_validate_signature_shape(
            view->envelope_flags,
            signature_algorithm,
            key_id_length_u32,
            signature_length_u32)) {
        return RIBOS_ARTIFACT_INVALID_SIGNATURE_ENVELOPE;
    }
    expected_offset = RIBOS_ARTIFACT_ENVELOPE_BYTES;
    if (payload_offset != expected_offset ||
        !ribos_artifact_size_add(
            expected_offset,
            payload_length,
            &expected_offset) ||
        key_id_offset != expected_offset ||
        !ribos_artifact_size_add(
            expected_offset,
            key_id_length_u32,
            &expected_offset) ||
        signature_offset != expected_offset ||
        !ribos_artifact_size_add(
            expected_offset,
            signature_length_u32,
            &expected_offset) ||
        total_length != expected_offset ||
        total_length != artifact_size ||
        !ribos_artifact_reader_bytes(
            artifact,
            artifact_size,
            payload_offset,
            payload_length,
            &payload) ||
        !ribos_artifact_reader_bytes(
            artifact,
            artifact_size,
            key_id_offset,
            key_id_length_u32,
            &key_id) ||
        !ribos_artifact_reader_bytes(
            artifact,
            artifact_size,
            signature_offset,
            signature_length_u32,
            &signature)) {
        return RIBOS_ARTIFACT_INVALID_FORMAT;
    }
    ribos_artifact_sha256(payload, payload_length, computed_hash);
    if (memcmp(
            computed_hash,
            encoded_hash,
            RIBOS_SCHEMA_DIGEST_BYTES) != 0) {
        return RIBOS_ARTIFACT_HASH_MISMATCH;
    }
    view->hash_algorithm = (RibosArtifactHashAlgorithm)hash_algorithm;
    view->signature_algorithm =
        (RibosArtifactSignatureAlgorithm)signature_algorithm;
    memcpy(
        view->artifact_hash,
        encoded_hash,
        RIBOS_SCHEMA_DIGEST_BYTES);
    view->key_id = key_id;
    view->key_id_length = key_id_length_u32;
    view->signature = signature;
    view->signature_length = signature_length_u32;
    status = ribos_artifact_open_payload(
        payload,
        payload_length,
        view);
    if (status != RIBOS_ARTIFACT_OK) {
        memset(view, 0, sizeof(*view));
    }
    return status;
}

const RibosArtifactSectionView *
ribos_artifact_find_section(
    const RibosArtifactView *view,
    RibosArtifactSectionKind kind)
{
    if (view == NULL || kind <= 0 ||
        kind >= RIBOS_ARTIFACT_SECTION_KIND_COUNT ||
        view->sections[kind].kind != kind) {
        return NULL;
    }
    return &view->sections[kind];
}

RibosArtifactStatus
ribos_artifact_signature_message_v1(
    RibosArtifactSignatureAlgorithm algorithm,
    const uint8_t *key_id,
    size_t key_id_length,
    uint64_t payload_length,
    const uint8_t artifact_hash[RIBOS_SCHEMA_DIGEST_BYTES],
    uint8_t output[RIBOS_ARTIFACT_SIGNATURE_MESSAGE_BYTES])
{
    static const uint8_t domain[32] =
        "RIBOS-ARTIFACT-SIGNATURE-V1";
    RibosArtifactWriter writer;
    uint8_t key_id_hash[RIBOS_SCHEMA_DIGEST_BYTES];

    if (algorithm != RIBOS_ARTIFACT_SIGNATURE_ED25519 ||
        key_id == NULL || key_id_length == 0 ||
        key_id_length > RIBOS_ARTIFACT_MAX_KEY_ID_BYTES ||
        artifact_hash == NULL || output == NULL) {
        return RIBOS_ARTIFACT_INVALID_ARGUMENT;
    }
    ribos_artifact_sha256(key_id, key_id_length, key_id_hash);
    memset(output, 0, RIBOS_ARTIFACT_SIGNATURE_MESSAGE_BYTES);
    writer = (RibosArtifactWriter){
        .output = output,
        .capacity = RIBOS_ARTIFACT_SIGNATURE_MESSAGE_BYTES,
    };
    ribos_artifact_writer_bytes(&writer, 0, domain, sizeof(domain));
    ribos_artifact_writer_u16(
        &writer, 32, RIBOS_ARTIFACT_ENVELOPE_V1_MAJOR);
    ribos_artifact_writer_u16(
        &writer, 34, RIBOS_ARTIFACT_ENVELOPE_V1_MINOR);
    ribos_artifact_writer_u16(&writer, 36, (uint16_t)algorithm);
    ribos_artifact_writer_u64(&writer, 40, payload_length);
    ribos_artifact_writer_bytes(
        &writer, 48, artifact_hash, RIBOS_SCHEMA_DIGEST_BYTES);
    ribos_artifact_writer_bytes(
        &writer, 80, key_id_hash, RIBOS_SCHEMA_DIGEST_BYTES);
    return writer.failed ?
        RIBOS_ARTIFACT_INVALID_ARGUMENT : RIBOS_ARTIFACT_OK;
}

const char *
ribos_bytecode_opcode_name(RibosBytecodeOpcode opcode)
{
    switch (opcode) {
    case RIBOS_BC_PARAMETER:
        return "parameter";
    case RIBOS_BC_CONST_UNIT:
        return "const-unit";
    case RIBOS_BC_CONST_BOOL:
        return "const-bool";
    case RIBOS_BC_CONST_INTEGER:
        return "const-integer";
    case RIBOS_BC_CONST_STRING:
        return "const-string";
    case RIBOS_BC_CONST_SYMBOL:
        return "const-symbol";
    case RIBOS_BC_MOVE:
        return "move";
    case RIBOS_BC_CHECKED_UNARY:
        return "checked-unary";
    case RIBOS_BC_CHECKED_BINARY:
        return "checked-binary";
    case RIBOS_BC_BUILD_LIST:
        return "build-list";
    case RIBOS_BC_BUILD_MAP:
        return "build-map";
    case RIBOS_BC_BUILD_STRUCT:
        return "build-struct";
    case RIBOS_BC_BUILD_VARIANT:
        return "build-variant";
    case RIBOS_BC_MEMBER:
        return "member";
    case RIBOS_BC_INDEX:
        return "index";
    case RIBOS_BC_COLLECTION_LENGTH:
        return "collection-length";
    case RIBOS_BC_VARIANT_TAG:
        return "variant-tag";
    case RIBOS_BC_VARIANT_PAYLOAD:
        return "variant-payload";
    case RIBOS_BC_CALL_DIRECT:
        return "call-direct";
    case RIBOS_BC_CALL_HELPER:
        return "call-helper";
    case RIBOS_BC_JUMP:
        return "jump";
    case RIBOS_BC_BRANCH:
        return "branch";
    case RIBOS_BC_RETURN:
        return "return";
    case RIBOS_BC_TRAP:
        return "trap";
    default:
        return "invalid";
    }
}
