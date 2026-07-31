#include "ribos/host/allocator.h"
#include "ribos/host/artifact_emitter.h"
#include "ribos/ir/builder.h"
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static RibosIrModule *
build_policy_module(void)
{
    RibosIrModule *module =
        ribos_ir_module_create(ribos_host_allocator());
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
        memcpy(mutated, first, first_size);
        mutated[RIBOS_ENVELOPE_RESERVED_OFFSET] = 1;
        passed = ribos_artifact_open_v1(
            mutated,
            first_size,
            &view) == RIBOS_ARTIFACT_INVALID_FORMAT;
    }
    if (passed) {
        memcpy(mutated, first, first_size);
        mutated[RIBOS_ENVELOPE_MINOR_OFFSET] = 1;
        passed = ribos_artifact_open_v1(
            mutated,
            first_size,
            &view) == RIBOS_ARTIFACT_UNSUPPORTED_VERSION;
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
    static const uint8_t trust_domain[32] =
        "RIBON-TRUST-MESSAGE-V1";
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
    RibosArtifactTrustContextV1 trust = {
        .size = sizeof(trust),
        .trust_major = RIBOS_ARTIFACT_TRUST_MESSAGE_V1_MAJOR,
        .trust_minor = RIBOS_ARTIFACT_TRUST_MESSAGE_V1_MINOR,
        .mode = RIBOS_ARTIFACT_TRUST_MODE_NORMAL,
        .key_usage = RIBOS_ARTIFACT_KEY_USAGE_POLICY_NORMAL,
        .sequence = 7,
        .product_digest = {0x21},
        .rollback_domain_digest = {0x42},
    };
    uint8_t message[RIBOS_ARTIFACT_TRUST_MESSAGE_V1_BYTES];
    uint8_t key_id_hash[RIBOS_SCHEMA_DIGEST_BYTES];
    uint8_t *artifact = NULL;
    size_t artifact_size = 0;
    size_t required = 0;
    uint64_t message_sequence = 0;
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
        ribos_artifact_trust_message_v1(
            &view,
            view.signature_algorithm,
            view.key_id,
            view.key_id_length,
            &trust,
            message) == RIBOS_ARTIFACT_TRUST_OK;
    if (passed) {
        ribos_artifact_sha256(
            key_id,
            sizeof(key_id) - 1,
            key_id_hash);
        passed =
            memcmp(
                message,
                trust_domain,
                sizeof(trust_domain)) == 0 &&
            message[32] == 1 && message[33] == 0 &&
            message[34] == 0 && message[35] == 0 &&
            message[36] == 1 && message[37] == 0 &&
            message[38] == 0 && message[39] == 0 &&
            message[40] == 1 && message[41] == 0 &&
            message[42] == 0 && message[43] == 0 &&
            message[44] == 1 && message[45] == 0 &&
            message[46] == 0 && message[47] == 0 &&
            message[48] == 1 && message[49] == 0 &&
            message[50] == 1 && message[51] == 0 &&
            message[52] == 1 && message[53] == 0 &&
            message[54] == 1 && message[55] == 0 &&
            ribos_artifact_reader_u64(
                message,
                sizeof(message),
                56,
                &message_sequence) &&
            message_sequence == trust.sequence &&
            ribos_artifact_reader_u64(
                message,
                sizeof(message),
                64,
                &message_payload_length) &&
            message_payload_length == view.payload_length &&
            memcmp(
                message + 72,
                view.artifact_hash,
                RIBOS_SCHEMA_DIGEST_BYTES) == 0 &&
            memcmp(
                message + 104,
                trust.product_digest,
                RIBOS_SCHEMA_DIGEST_BYTES) == 0 &&
            memcmp(
                message + 136,
                view.schema_digest,
                RIBOS_SCHEMA_DIGEST_BYTES) == 0 &&
            memcmp(
                message + 168,
                trust.rollback_domain_digest,
                RIBOS_SCHEMA_DIGEST_BYTES) == 0 &&
            memcmp(
                message + 200,
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

/** Cross-tool 고정 vector의 view와 product trust tuple을 구성한다. */
static void
build_trust_message_vector(
    RibosArtifactView *view,
    RibosArtifactTrustContextV1 *trust)
{
    size_t index;

    memset(view, 0, sizeof(*view));
    memset(trust, 0, sizeof(*trust));
    view->envelope_major = RIBOS_ARTIFACT_ENVELOPE_V1_MAJOR;
    view->envelope_minor = RIBOS_ARTIFACT_ENVELOPE_V1_MINOR;
    view->vm_abi_major = RIBOS_VM_ABI_V1_MAJOR;
    view->vm_abi_minor = RIBOS_VM_ABI_V1_MINOR;
    view->isa_major = RIBOS_BYTECODE_ISA_V1_MAJOR;
    view->isa_minor = RIBOS_BYTECODE_ISA_V1_MINOR;
    view->hash_algorithm = RIBOS_ARTIFACT_HASH_SHA256;
    view->payload_length = 0x11223344u;
    trust->size = sizeof(*trust);
    trust->trust_major = RIBOS_ARTIFACT_TRUST_MESSAGE_V1_MAJOR;
    trust->trust_minor = RIBOS_ARTIFACT_TRUST_MESSAGE_V1_MINOR;
    trust->mode = RIBOS_ARTIFACT_TRUST_MODE_NORMAL;
    trust->key_usage = RIBOS_ARTIFACT_KEY_USAGE_POLICY_NORMAL;
    trust->sequence = UINT64_C(0x0102030405060708);
    for (index = 0; index < RIBOS_SCHEMA_DIGEST_BYTES; ++index) {
        view->artifact_hash[index] = (uint8_t)index;
        trust->product_digest[index] = (uint8_t)(0x20u + index);
        view->schema_digest[index] = (uint8_t)(0x40u + index);
        trust->rollback_domain_digest[index] =
            (uint8_t)(0x60u + index);
    }
}

/** Trust message의 stable failure registry와 genesis sequence를 검사한다. */
static int
test_trust_message_failures(void)
{
    static const uint8_t key_id[] =
        "ribon-production-normal-2026q3";
    RibosArtifactView view;
    RibosArtifactTrustContextV1 trust;
    uint8_t message[RIBOS_ARTIFACT_TRUST_MESSAGE_V1_BYTES];
    int passed;

    build_trust_message_vector(&view, &trust);
    passed = ribos_artifact_trust_message_v1(
        &view,
        RIBOS_ARTIFACT_SIGNATURE_ED25519,
        key_id,
        sizeof(key_id) - 1,
        &trust,
        message) == RIBOS_ARTIFACT_TRUST_OK;
    trust.trust_major = 2;
    passed = passed && ribos_artifact_trust_message_v1(
        &view,
        RIBOS_ARTIFACT_SIGNATURE_ED25519,
        key_id,
        sizeof(key_id) - 1,
        &trust,
        message) == RIBOS_ARTIFACT_TRUST_UNSUPPORTED_VERSION;
    build_trust_message_vector(&view, &trust);
    view.hash_algorithm = (RibosArtifactHashAlgorithm)0;
    passed = passed && ribos_artifact_trust_message_v1(
        &view,
        RIBOS_ARTIFACT_SIGNATURE_ED25519,
        key_id,
        sizeof(key_id) - 1,
        &trust,
        message) == RIBOS_ARTIFACT_TRUST_UNSUPPORTED_ALGORITHM;
    build_trust_message_vector(&view, &trust);
    trust.mode = 0;
    passed = passed && ribos_artifact_trust_message_v1(
        &view,
        RIBOS_ARTIFACT_SIGNATURE_ED25519,
        key_id,
        sizeof(key_id) - 1,
        &trust,
        message) == RIBOS_ARTIFACT_TRUST_INVALID_MODE;
    build_trust_message_vector(&view, &trust);
    trust.key_usage = 0;
    passed = passed && ribos_artifact_trust_message_v1(
        &view,
        RIBOS_ARTIFACT_SIGNATURE_ED25519,
        key_id,
        sizeof(key_id) - 1,
        &trust,
        message) == RIBOS_ARTIFACT_TRUST_INVALID_USAGE;
    build_trust_message_vector(&view, &trust);
    trust.key_usage = RIBOS_ARTIFACT_KEY_USAGE_UPDATE_MANIFEST;
    passed = passed && ribos_artifact_trust_message_v1(
        &view,
        RIBOS_ARTIFACT_SIGNATURE_ED25519,
        key_id,
        sizeof(key_id) - 1,
        &trust,
        message) == RIBOS_ARTIFACT_TRUST_MODE_USAGE_MISMATCH;
    build_trust_message_vector(&view, &trust);
    memset(trust.product_digest, 0, sizeof(trust.product_digest));
    passed = passed && ribos_artifact_trust_message_v1(
        &view,
        RIBOS_ARTIFACT_SIGNATURE_ED25519,
        key_id,
        sizeof(key_id) - 1,
        &trust,
        message) == RIBOS_ARTIFACT_TRUST_INVALID_IDENTITY;
    build_trust_message_vector(&view, &trust);
    trust.flags = 1;
    passed = passed && ribos_artifact_trust_message_v1(
        &view,
        RIBOS_ARTIFACT_SIGNATURE_ED25519,
        key_id,
        sizeof(key_id) - 1,
        &trust,
        message) == RIBOS_ARTIFACT_TRUST_RESERVED_NONZERO;
    build_trust_message_vector(&view, &trust);
    trust.sequence = 0;
    passed = passed && ribos_artifact_trust_message_v1(
        &view,
        RIBOS_ARTIFACT_SIGNATURE_ED25519,
        key_id,
        sizeof(key_id) - 1,
        &trust,
        message) == RIBOS_ARTIFACT_TRUST_OK;
    return passed;
}

/** C codec가 만든 canonical cross-tool vector를 lowercase hex로 출력한다. */
static int
dump_trust_message_vector(void)
{
    static const uint8_t key_id[] =
        "ribon-production-normal-2026q3";
    RibosArtifactView view;
    RibosArtifactTrustContextV1 trust;
    uint8_t message[RIBOS_ARTIFACT_TRUST_MESSAGE_V1_BYTES];
    size_t index;

    build_trust_message_vector(&view, &trust);
    if (ribos_artifact_trust_message_v1(
            &view,
            RIBOS_ARTIFACT_SIGNATURE_ED25519,
            key_id,
            sizeof(key_id) - 1,
            &trust,
            message) != RIBOS_ARTIFACT_TRUST_OK) {
        return 1;
    }
    for (index = 0; index < sizeof(message); ++index) {
        (void)printf("%02x", message[index]);
    }
    (void)putchar('\n');
    return 0;
}

int
main(int argc, char **argv)
{
    RibosIrModule *module = build_policy_module();
    int passed = module != NULL;

    if (argc == 2 && strcmp(argv[1], "--dump-trust-vector") == 0) {
        ribos_ir_module_destroy(module);
        return dump_trust_message_vector();
    }
    if (argc != 1) {
        ribos_ir_module_destroy(module);
        return 2;
    }

    passed = passed && test_unsigned_and_mutations(module);
    passed = passed && test_optional_source_map(module);
    passed = passed && test_signed_envelope(module);
    passed = passed && test_trust_message_failures();
    ribos_ir_module_destroy(module);
    if (!passed) {
        (void)fprintf(stderr, "RIBOS-ARTIFACT-TEST-FAIL\n");
        return 1;
    }
    (void)printf(
        "RIBOS-ARTIFACT-TEST-OK le=1 hash=sha256 "
        "signed-envelope=1 trust-message=product-bound "
        "source-map=optional mutations=7\n");
    return 0;
}
