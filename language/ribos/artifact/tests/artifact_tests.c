#include "ribos/artifact/emitter.h"
#include "ribos/ir/builder.h"
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static RibosIrModule *
build_policy_module(void)
{
    RibosIrModule *module = ribos_ir_module_create();
    uint8_t schema_digest[RIBOS_SCHEMA_DIGEST_BYTES] = {1};
    RibosIrType unit_type = {
        .kind = RIBOS_IR_TYPE_UNIT,
    };
    RibosIrFunction function = {
        .return_type = 0,
        .parameter_start = 0,
        .declared_instruction_budget = 2,
        .declared_helper_budget = 0,
        .flags = RIBOS_IR_FUNCTION_POLICY,
    };
    RibosIrSourceMap source_map = {
        .ast_node_id = 1,
        .start_byte = 0,
        .end_byte = 1,
        .start_line = 1,
        .start_column = 1,
        .end_line = 1,
        .end_column = 2,
    };
    RibosIrBlock block = {
        .function_id = 0,
        .flags = 1,
    };
    RibosIrSlot slot = {
        .function_id = 0,
        .type_id = 0,
        .source_map_id = 0,
    };
    RibosIrInstruction value = {
        .opcode = RIBOS_IR_OP_CONST_UNIT,
        .block_id = 0,
        .result_slot = 0,
        .target = RIBOS_IR_INVALID_ID,
        .alternate = RIBOS_IR_INVALID_ID,
        .source_map_id = 0,
    };
    RibosIrInstruction terminal = {
        .opcode = RIBOS_IR_OP_RETURN,
        .block_id = 0,
        .result_slot = RIBOS_IR_INVALID_ID,
        .target = RIBOS_IR_INVALID_ID,
        .alternate = RIBOS_IR_INVALID_ID,
        .source_map_id = 0,
    };
    uint32_t id;
    uint32_t return_operand = 0;

    if (module == NULL ||
        ribos_ir_builder_set_schema_identity(
            module,
            schema_digest) != RIBOS_IR_OK ||
        ribos_ir_builder_add_type(
            module,
            &unit_type,
            &id) != RIBOS_IR_OK ||
        ribos_ir_builder_add_function(
            module,
            &function,
            &id) != RIBOS_IR_OK ||
        ribos_ir_builder_add_source_map(
            module,
            &source_map,
            &id) != RIBOS_IR_OK ||
        ribos_ir_builder_add_block(
            module,
            &block,
            &id) != RIBOS_IR_OK ||
        ribos_ir_builder_add_slot(
            module,
            &slot,
            &id) != RIBOS_IR_OK ||
        ribos_ir_builder_add_instruction(
            module,
            &value,
            NULL,
            0,
            &id) != RIBOS_IR_OK ||
        ribos_ir_builder_add_instruction(
            module,
            &terminal,
            &return_operand,
            1,
            &id) != RIBOS_IR_OK) {
        ribos_ir_module_destroy(module);
        return NULL;
    }
    function = (RibosIrFunction){
        .id = 0,
        .return_type = 0,
        .entry_block = 0,
        .first_block = 0,
        .block_count = 1,
        .first_slot = 0,
        .slot_count = 1,
        .parameter_start = 0,
        .declared_instruction_budget = 2,
        .declared_helper_budget = 0,
        .flags = RIBOS_IR_FUNCTION_POLICY,
    };
    if (ribos_ir_builder_update_function(
            module,
            &function) != RIBOS_IR_OK) {
        ribos_ir_module_destroy(module);
        return NULL;
    }
    return module;
}

static int
emit_artifact(
    const RibosIrModule *module,
    const RibosArtifactEmitOptions *options,
    uint8_t **bytes,
    size_t *byte_count)
{
    size_t required = 0;

    if (ribos_artifact_emit_v1(
            module,
            options,
            NULL,
            0,
            &required) != RIBOS_ARTIFACT_OK) {
        return 0;
    }
    *bytes = malloc(required);
    if (*bytes == NULL) {
        return 0;
    }
    *byte_count = required;
    if (ribos_artifact_emit_v1(
            module,
            options,
            *bytes,
            required,
            &required) != RIBOS_ARTIFACT_OK ||
        required != *byte_count) {
        free(*bytes);
        *bytes = NULL;
        *byte_count = 0;
        return 0;
    }
    return 1;
}

static int
rehash_payload(uint8_t *artifact, size_t artifact_size)
{
    uint64_t payload_length;
    uint8_t hash[RIBOS_SCHEMA_DIGEST_BYTES];

    if (!ribos_artifact_reader_u64(
            artifact,
            artifact_size,
            RIBOS_ENVELOPE_PAYLOAD_LENGTH_OFFSET,
            &payload_length) ||
        payload_length > artifact_size -
            RIBOS_ARTIFACT_ENVELOPE_BYTES) {
        return 0;
    }
    ribos_artifact_sha256(
        artifact + RIBOS_ARTIFACT_ENVELOPE_BYTES,
        (size_t)payload_length,
        hash);
    memcpy(
        artifact + RIBOS_ENVELOPE_HASH_OFFSET,
        hash,
        sizeof(hash));
    return 1;
}

static int
test_unsigned_and_mutations(const RibosIrModule *module)
{
    RibosArtifactEmitOptions options = {
        .include_source_map = 1,
    };
    RibosArtifactView view;
    uint8_t *first = NULL;
    uint8_t *second = NULL;
    uint8_t *mutated = NULL;
    size_t first_size = 0;
    size_t second_size = 0;
    size_t required = 0;
    int passed =
        emit_artifact(module, &options, &first, &first_size) &&
        emit_artifact(module, &options, &second, &second_size);

    passed = passed && first_size == second_size &&
        memcmp(first, second, first_size) == 0 &&
        ribos_artifact_open_v1(
            first,
            first_size,
            &view) == RIBOS_ARTIFACT_OK &&
        view.vm_abi_major == 1 &&
        view.isa_major == 1 &&
        view.entry_function == 0 &&
        view.virtual_register_count == 1 &&
        view.slot_count == 1 &&
        view.instruction_budget == 2 &&
        view.instruction_upper_bound == 2 &&
        view.helper_budget == 0 &&
        view.helper_upper_bound == 0 &&
        view.maximum_call_depth == 1 &&
        view.signature_algorithm ==
            RIBOS_ARTIFACT_SIGNATURE_NONE &&
        ribos_artifact_find_section(
            &view,
            RIBOS_ARTIFACT_SECTION_INSTRUCTIONS)->count == 2 &&
        ribos_artifact_find_section(
            &view,
            RIBOS_ARTIFACT_SECTION_SOURCE_MAPS)->count == 1;
    mutated = passed ? malloc(first_size) : NULL;
    passed = passed && mutated != NULL;
    if (passed) {
        memcpy(mutated, first, first_size);
        mutated[RIBOS_ARTIFACT_ENVELOPE_BYTES + 20] ^= 1;
        passed = ribos_artifact_open_v1(
            mutated,
            first_size,
            &view) == RIBOS_ARTIFACT_HASH_MISMATCH;
    }
    if (passed) {
        memcpy(mutated, first, first_size);
        memset(mutated + 32, 0xff, 8);
        passed = ribos_artifact_open_v1(
            mutated,
            first_size,
            &view) == RIBOS_ARTIFACT_INVALID_FORMAT;
    }
    if (passed) {
        size_t first_descriptor =
            RIBOS_ARTIFACT_ENVELOPE_BYTES +
            RIBOS_ARTIFACT_PAYLOAD_HEADER_BYTES;

        memcpy(mutated, first, first_size);
        memset(
            mutated + first_descriptor +
                RIBOS_SECTION_DATA_OFFSET_OFFSET,
            0xff,
            8);
        passed = rehash_payload(mutated, first_size) &&
            ribos_artifact_open_v1(
                mutated,
                first_size,
                &view) == RIBOS_ARTIFACT_INVALID_FORMAT;
    }
    if (passed) {
        size_t first_descriptor =
            RIBOS_ARTIFACT_ENVELOPE_BYTES +
            RIBOS_ARTIFACT_PAYLOAD_HEADER_BYTES;

        memcpy(mutated, first, first_size);
        memset(
            mutated + first_descriptor +
                RIBOS_SECTION_DATA_LENGTH_OFFSET,
            0xff,
            8);
        passed = rehash_payload(mutated, first_size) &&
            ribos_artifact_open_v1(
                mutated,
                first_size,
                &view) == RIBOS_ARTIFACT_INVALID_FORMAT;
    }
    if (passed) {
        memcpy(mutated, first, first_size);
        memset(
            mutated + RIBOS_ARTIFACT_ENVELOPE_BYTES +
                RIBOS_PAYLOAD_DIRECTORY_LENGTH_OFFSET,
            0xff,
            8);
        passed = rehash_payload(mutated, first_size) &&
            ribos_artifact_open_v1(
                mutated,
                first_size,
                &view) == RIBOS_ARTIFACT_INVALID_FORMAT;
    }
    if (passed) {
        passed = ribos_artifact_emit_v1(
            module,
            &options,
            first,
            first_size - 1,
            &required) == RIBOS_ARTIFACT_CAPACITY_EXCEEDED &&
            required == first_size;
    }
    free(mutated);
    free(second);
    free(first);
    return passed;
}

static int
test_optional_source_map(const RibosIrModule *module)
{
    RibosArtifactEmitOptions options = {0};
    RibosArtifactView view;
    const RibosArtifactSectionView *instructions;
    uint8_t *artifact = NULL;
    size_t artifact_size = 0;
    int passed =
        emit_artifact(
            module,
            &options,
            &artifact,
            &artifact_size) &&
        ribos_artifact_open_v1(
            artifact,
            artifact_size,
            &view) == RIBOS_ARTIFACT_OK;

    instructions = passed ?
        ribos_artifact_find_section(
            &view,
            RIBOS_ARTIFACT_SECTION_INSTRUCTIONS) : NULL;
    passed = passed &&
        (view.payload_flags & RIBOS_ARTIFACT_HAS_SOURCE_MAP) == 0 &&
        ribos_artifact_find_section(
            &view,
            RIBOS_ARTIFACT_SECTION_SOURCE_MAPS) == NULL &&
        instructions != NULL &&
        instructions->bytes[28] == 0xff &&
        instructions->bytes[29] == 0xff &&
        instructions->bytes[30] == 0xff &&
        instructions->bytes[31] == 0xff;
    free(artifact);
    return passed;
}

static int
test_signed_envelope(const RibosIrModule *module)
{
    static const uint8_t key_id[] = "factory-policy-key";
    static const uint8_t signature_domain[32] =
        "RIBOS-ARTIFACT-SIGNATURE-V1";
    uint8_t signature[RIBOS_ARTIFACT_ED25519_SIGNATURE_BYTES];
    RibosArtifactEmitOptions options = {
        .include_source_map = 1,
        .signature = {
            .algorithm = RIBOS_ARTIFACT_SIGNATURE_ED25519,
            .key_id = key_id,
            .key_id_length = sizeof(key_id) - 1,
            .signature = signature,
            .signature_length = sizeof(signature),
        },
    };
    RibosArtifactView view;
    uint8_t message[RIBOS_ARTIFACT_SIGNATURE_MESSAGE_BYTES];
    uint8_t key_id_hash[RIBOS_SCHEMA_DIGEST_BYTES];
    uint8_t *artifact = NULL;
    size_t artifact_size = 0;
    size_t required = 0;
    uint64_t message_payload_length = 0;
    int passed;

    memset(signature, 0xa5, sizeof(signature));
    passed =
        emit_artifact(
            module,
            &options,
            &artifact,
            &artifact_size) &&
        ribos_artifact_open_v1(
            artifact,
            artifact_size,
            &view) == RIBOS_ARTIFACT_OK &&
        view.signature_algorithm ==
            RIBOS_ARTIFACT_SIGNATURE_ED25519 &&
        view.key_id_length == sizeof(key_id) - 1 &&
        view.signature_length == sizeof(signature) &&
        memcmp(view.key_id, key_id, sizeof(key_id) - 1) == 0 &&
        memcmp(view.signature, signature, sizeof(signature)) == 0 &&
        ribos_artifact_signature_message_v1(
            view.signature_algorithm,
            view.key_id,
            view.key_id_length,
            view.payload_length,
            view.artifact_hash,
            message) == RIBOS_ARTIFACT_OK;
    if (passed) {
        ribos_artifact_sha256(
            key_id,
            sizeof(key_id) - 1,
            key_id_hash);
        passed =
            memcmp(
                message,
                signature_domain,
                sizeof(signature_domain)) == 0 &&
            message[32] == 1 && message[33] == 0 &&
            message[34] == 0 && message[35] == 0 &&
            message[36] == 1 && message[37] == 0 &&
            message[38] == 0 && message[39] == 0 &&
            ribos_artifact_reader_u64(
                message,
                sizeof(message),
                40,
                &message_payload_length) &&
            message_payload_length == view.payload_length &&
            memcmp(
                message + 48,
                view.artifact_hash,
                RIBOS_SCHEMA_DIGEST_BYTES) == 0 &&
            memcmp(
                message + 80,
                key_id_hash,
                RIBOS_SCHEMA_DIGEST_BYTES) == 0;
    }
    options.signature.signature_length = sizeof(signature) - 1;
    passed = passed && ribos_artifact_emit_v1(
        module,
        &options,
        NULL,
        0,
        &required) ==
            RIBOS_ARTIFACT_INVALID_SIGNATURE_ENVELOPE;
    free(artifact);
    return passed;
}

int
main(void)
{
    RibosIrModule *module = build_policy_module();
    int passed = module != NULL;

    passed = passed && test_unsigned_and_mutations(module);
    passed = passed && test_optional_source_map(module);
    passed = passed && test_signed_envelope(module);
    ribos_ir_module_destroy(module);
    if (!passed) {
        (void)fprintf(stderr, "RIBOS-ARTIFACT-TEST-FAIL\n");
        return 1;
    }
    (void)printf(
        "RIBOS-ARTIFACT-TEST-OK le=1 hash=sha256 "
        "signed-envelope=1 source-map=optional mutations=5\n");
    return 0;
}
