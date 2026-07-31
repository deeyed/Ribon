#include "ribos/vm/terminal.h"

#include "internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RIBOS_HOST_CONTEXT_MAGIC "RBCTX1\0\0"
#define RIBOS_HOST_TRANSCRIPT_MAGIC "RBTRN1\0\0"
#define RIBOS_HOST_FIXTURE_MAGIC_BYTES 8u
#define RIBOS_HOST_CONTEXT_HEADER_BYTES 128u
#define RIBOS_HOST_TRANSCRIPT_HEADER_BYTES 192u
#define RIBOS_HOST_TRANSCRIPT_ROW_BYTES 128u
#define RIBOS_HOST_CONTEXT_MAX_PAYLOAD (1024u * 1024u)
#define RIBOS_HOST_TRANSCRIPT_MAX_BYTES (16u * 1024u * 1024u)
#define RIBOS_HOST_TRANSCRIPT_MAX_ROWS 65536u
#define RIBOS_HOST_MAX_OPERATIONS UINT64_C(1000000)
#define RIBOS_HOST_MAX_DURATION_NS UINT64_C(1000000000000)
#define RIBOS_HOST_MAX_HANDLES 64u
#define RIBOS_HOST_REPORT_BYTES 16384u

typedef enum RibosHostTranscriptResult {
    RIBOS_HOST_TRANSCRIPT_RESULT_NONE = 0,
    RIBOS_HOST_TRANSCRIPT_RESULT_VALUE = 1,
    RIBOS_HOST_TRANSCRIPT_RESULT_HANDLE = 2,
    RIBOS_HOST_TRANSCRIPT_RESULT_POLICY_ERROR = 3
} RibosHostTranscriptResult;

typedef struct RibosHostFile {
    uint8_t *bytes;
    size_t size;
} RibosHostFile;

typedef struct RibosHostContextFixture {
    uint32_t context_type_id;
    uint32_t selected_mode;
    uint32_t selected_phase;
    uint64_t generation;
    const uint8_t *payload;
    size_t payload_size;
    uint8_t payload_digest[RIBOS_VM_DIGEST_BYTES];
    uint8_t snapshot_digest[RIBOS_VM_DIGEST_BYTES];
} RibosHostContextFixture;

typedef struct RibosHostTranscriptFixture {
    const uint8_t *rows;
    const uint8_t *payload;
    uint32_t row_count;
    size_t payload_size;
    uint8_t artifact_hash[RIBOS_VM_DIGEST_BYTES];
    uint8_t context_digest[RIBOS_VM_DIGEST_BYTES];
    uint8_t digest[RIBOS_VM_DIGEST_BYTES];
} RibosHostTranscriptFixture;

typedef struct RibosHostTranscriptRow {
    uint64_t sequence;
    uint32_t helper_id;
    uint32_t callback_status;
    uint32_t result_kind;
    uint64_t operations;
    uint64_t polls;
    uint64_t elapsed_ns;
    const uint8_t *payload;
    size_t payload_size;
    uint32_t journal_state;
    uint8_t journal_digest[RIBOS_VM_DIGEST_BYTES];
    uint64_t object_id;
} RibosHostTranscriptRow;

typedef struct RibosHostBinding {
    uint32_t stable_id;
    uint32_t success_type_id;
    uint32_t success_type_size;
    uint32_t success_type_class;
    uint32_t success_ownership;
    uint32_t error_type_id;
    uint32_t error_type_size;
} RibosHostBinding;

typedef struct RibosHostObject {
    uint64_t object_id;
    uint32_t dropped;
} RibosHostObject;

typedef struct RibosHostAuthority {
    uint8_t helper_digest[RIBOS_VM_DIGEST_BYTES];
} RibosHostAuthority;

typedef struct RibosHostProgram {
    RibosVmHelperBinding *bindings;
    RibosHostBinding *binding_metadata;
    uint32_t binding_count;
    RibosVmHelperContract contract;
    RibosVmLimits limits;
    RibosHostAuthority authority;
    void *authorized_workspace;
    const RibosAuthorizedArtifact *authorized;
    void *prepared_workspace;
    const RibosPreparedProgram *prepared;
    RibosVerifierReport verifier;
} RibosHostProgram;

typedef struct RibosHostExecution {
    const RibosHostTranscriptFixture *transcript;
    const RibosHostProgram *program;
    RibosHostObject *objects;
    uint32_t object_capacity;
    uint32_t object_count;
    uint32_t next_row;
    uint32_t replay_error;
    uint32_t recovery_calls;
    uint64_t now_ns;
    RibosVmFaultReceipt recovery_receipt;
} RibosHostExecution;

typedef struct RibosHostReport {
    char bytes[RIBOS_HOST_REPORT_BYTES];
    size_t size;
    int failed;
} RibosHostReport;

static uint16_t
ribos_host_read_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] |
        ((uint16_t)bytes[1] << 8);
}

static uint32_t
ribos_host_read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

static uint64_t
ribos_host_read_u64(const uint8_t *bytes)
{
    return (uint64_t)bytes[0] |
        ((uint64_t)bytes[1] << 8) |
        ((uint64_t)bytes[2] << 16) |
        ((uint64_t)bytes[3] << 24) |
        ((uint64_t)bytes[4] << 32) |
        ((uint64_t)bytes[5] << 40) |
        ((uint64_t)bytes[6] << 48) |
        ((uint64_t)bytes[7] << 56);
}

static int
ribos_host_bytes_are_zero(const uint8_t *bytes, size_t size)
{
    size_t index;
    uint8_t combined = 0;

    for (index = 0; index < size; ++index) {
        combined |= bytes[index];
    }
    return combined == 0;
}

static int
ribos_host_digest_is_zero(
    const uint8_t digest[RIBOS_VM_DIGEST_BYTES])
{
    return ribos_host_bytes_are_zero(
        digest,
        RIBOS_VM_DIGEST_BYTES);
}

static int
ribos_host_range(
    size_t total,
    uint64_t offset,
    uint64_t length,
    size_t *native_offset,
    size_t *native_length)
{
    if (offset > SIZE_MAX || length > SIZE_MAX ||
        (size_t)offset > total ||
        (size_t)length > total - (size_t)offset) {
        return 0;
    }
    *native_offset = (size_t)offset;
    *native_length = (size_t)length;
    return 1;
}

static int
ribos_host_read_file(
    const char *path,
    size_t maximum_size,
    RibosHostFile *file)
{
    FILE *stream;
    long measured;
    uint8_t *bytes;

    if (path == NULL || file == NULL) {
        return 0;
    }
    memset(file, 0, sizeof(*file));
    stream = fopen(path, "rb");
    if (stream == NULL ||
        fseek(stream, 0, SEEK_END) != 0 ||
        (measured = ftell(stream)) <= 0 ||
        (uint64_t)measured > maximum_size ||
        fseek(stream, 0, SEEK_SET) != 0) {
        if (stream != NULL) {
            (void)fclose(stream);
        }
        return 0;
    }
    bytes = malloc((size_t)measured);
    if (bytes == NULL ||
        fread(bytes, 1, (size_t)measured, stream) !=
            (size_t)measured ||
        fclose(stream) != 0) {
        free(bytes);
        return 0;
    }
    file->bytes = bytes;
    file->size = (size_t)measured;
    return 1;
}

static void
ribos_host_release_file(RibosHostFile *file)
{
    if (file == NULL) {
        return;
    }
    free(file->bytes);
    memset(file, 0, sizeof(*file));
}

static int
ribos_host_parse_context(
    const RibosHostFile *file,
    RibosHostContextFixture *context)
{
    RibosArtifactSha256 hash;
    uint8_t snapshot_digest[RIBOS_VM_DIGEST_BYTES];
    size_t payload_offset;
    size_t payload_size;
    uint64_t total_size;

    if (file == NULL || context == NULL ||
        file->size < RIBOS_HOST_CONTEXT_HEADER_BYTES ||
        memcmp(
            file->bytes,
            RIBOS_HOST_CONTEXT_MAGIC,
            RIBOS_HOST_FIXTURE_MAGIC_BYTES) != 0 ||
        ribos_host_read_u16(file->bytes + 8) != 1 ||
        ribos_host_read_u16(file->bytes + 10) != 0 ||
        ribos_host_read_u32(file->bytes + 12) !=
            RIBOS_HOST_CONTEXT_HEADER_BYTES ||
        ribos_host_read_u32(file->bytes + 16) != 0 ||
        ribos_host_read_u32(file->bytes + 20) ==
            RIBOS_VM_INVALID_ID ||
        ribos_host_read_u32(file->bytes + 24) >= 64 ||
        ribos_host_read_u32(file->bytes + 28) >= 64 ||
        ribos_host_read_u64(file->bytes + 32) == 0 ||
        ribos_host_read_u64(file->bytes + 40) !=
            RIBOS_HOST_CONTEXT_HEADER_BYTES ||
        !ribos_host_bytes_are_zero(file->bytes + 96, 32)) {
        return 0;
    }
    total_size = ribos_host_read_u64(file->bytes + 56);
    if (total_size != file->size ||
        !ribos_host_range(
            file->size,
            ribos_host_read_u64(file->bytes + 40),
            ribos_host_read_u64(file->bytes + 48),
            &payload_offset,
            &payload_size) ||
        payload_offset != RIBOS_HOST_CONTEXT_HEADER_BYTES ||
        payload_size > RIBOS_HOST_CONTEXT_MAX_PAYLOAD ||
        payload_offset + payload_size != file->size) {
        return 0;
    }
    ribos_artifact_sha256_initialize(&hash);
    ribos_artifact_sha256_update(&hash, file->bytes, 64);
    ribos_artifact_sha256_update(
        &hash,
        file->bytes + payload_offset,
        payload_size);
    ribos_artifact_sha256_finish(&hash, snapshot_digest);
    if (memcmp(
            snapshot_digest,
            file->bytes + 64,
            RIBOS_VM_DIGEST_BYTES) != 0) {
        return 0;
    }
    memset(context, 0, sizeof(*context));
    context->context_type_id =
        ribos_host_read_u32(file->bytes + 20);
    context->selected_mode =
        ribos_host_read_u32(file->bytes + 24);
    context->selected_phase =
        ribos_host_read_u32(file->bytes + 28);
    context->generation =
        ribos_host_read_u64(file->bytes + 32);
    context->payload = file->bytes + payload_offset;
    context->payload_size = payload_size;
    ribos_artifact_sha256(
        context->payload,
        context->payload_size,
        context->payload_digest);
    memcpy(
        context->snapshot_digest,
        snapshot_digest,
        RIBOS_VM_DIGEST_BYTES);
    return 1;
}

static int
ribos_host_parse_transcript(
    const RibosHostFile *file,
    RibosHostTranscriptFixture *transcript)
{
    uint8_t digest[RIBOS_VM_DIGEST_BYTES];
    uint32_t row_count;
    uint64_t rows_length;
    uint64_t payload_offset_wire;
    size_t rows_offset;
    size_t rows_size;
    size_t payload_offset;
    size_t payload_size;
    size_t expected_payload_offset = 0;
    uint64_t elapsed_total = 0;
    uint32_t index;

    if (file == NULL || transcript == NULL ||
        file->size < RIBOS_HOST_TRANSCRIPT_HEADER_BYTES ||
        file->size > RIBOS_HOST_TRANSCRIPT_MAX_BYTES ||
        memcmp(
            file->bytes,
            RIBOS_HOST_TRANSCRIPT_MAGIC,
            RIBOS_HOST_FIXTURE_MAGIC_BYTES) != 0 ||
        ribos_host_read_u16(file->bytes + 8) != 1 ||
        ribos_host_read_u16(file->bytes + 10) != 0 ||
        ribos_host_read_u32(file->bytes + 12) !=
            RIBOS_HOST_TRANSCRIPT_HEADER_BYTES ||
        ribos_host_read_u32(file->bytes + 16) != 0 ||
        ribos_host_read_u32(file->bytes + 24) !=
            RIBOS_HOST_TRANSCRIPT_ROW_BYTES ||
        ribos_host_read_u32(file->bytes + 28) != 0 ||
        ribos_host_read_u64(file->bytes + 32) !=
            RIBOS_HOST_TRANSCRIPT_HEADER_BYTES ||
        ribos_host_read_u64(file->bytes + 64) != file->size ||
        !ribos_host_bytes_are_zero(file->bytes + 168, 24)) {
        return 0;
    }
    row_count = ribos_host_read_u32(file->bytes + 20);
    rows_length = ribos_host_read_u64(file->bytes + 40);
    payload_offset_wire = ribos_host_read_u64(file->bytes + 48);
    if (row_count > RIBOS_HOST_TRANSCRIPT_MAX_ROWS ||
        rows_length !=
            (uint64_t)row_count *
                RIBOS_HOST_TRANSCRIPT_ROW_BYTES ||
        payload_offset_wire !=
            (uint64_t)RIBOS_HOST_TRANSCRIPT_HEADER_BYTES +
                rows_length ||
        !ribos_host_range(
            file->size,
            ribos_host_read_u64(file->bytes + 32),
            rows_length,
            &rows_offset,
            &rows_size) ||
        !ribos_host_range(
            file->size,
            payload_offset_wire,
            ribos_host_read_u64(file->bytes + 56),
            &payload_offset,
            &payload_size) ||
        rows_offset != RIBOS_HOST_TRANSCRIPT_HEADER_BYTES ||
        payload_offset + payload_size != file->size) {
        return 0;
    }
    ribos_artifact_sha256(
        file->bytes + RIBOS_HOST_TRANSCRIPT_HEADER_BYTES,
        file->size - RIBOS_HOST_TRANSCRIPT_HEADER_BYTES,
        digest);
    if (memcmp(
            digest,
            file->bytes + 136,
            RIBOS_VM_DIGEST_BYTES) != 0) {
        return 0;
    }
    for (index = 0; index < row_count; ++index) {
        const uint8_t *row = file->bytes + rows_offset +
            (size_t)index * RIBOS_HOST_TRANSCRIPT_ROW_BYTES;
        uint32_t callback_status = ribos_host_read_u32(row + 12);
        uint32_t result_kind = ribos_host_read_u32(row + 16);
        uint64_t operations = ribos_host_read_u64(row + 24);
        uint64_t polls = ribos_host_read_u64(row + 32);
        uint64_t elapsed = ribos_host_read_u64(row + 40);
        uint64_t row_payload_offset = ribos_host_read_u64(row + 48);
        uint64_t row_payload_size = ribos_host_read_u64(row + 56);
        uint32_t journal_state = ribos_host_read_u32(row + 64);
        uint64_t object_id = ribos_host_read_u64(row + 104);
        size_t native_offset;
        size_t native_size;

        if (ribos_host_read_u64(row) != (uint64_t)index + 1 ||
            ribos_host_read_u32(row + 8) == RIBOS_VM_INVALID_ID ||
            callback_status >
                RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT ||
            result_kind > RIBOS_HOST_TRANSCRIPT_RESULT_POLICY_ERROR ||
            ribos_host_read_u32(row + 20) != 0 ||
            operations > RIBOS_HOST_MAX_OPERATIONS ||
            polls > RIBOS_HOST_MAX_OPERATIONS ||
            elapsed > RIBOS_HOST_MAX_DURATION_NS ||
            journal_state >
                RIBOS_VM_JOURNAL_RECEIPT_UNCERTAIN ||
            ribos_host_read_u32(row + 68) != 0 ||
            !ribos_host_bytes_are_zero(row + 112, 16) ||
            !ribos_host_range(
                payload_size,
                row_payload_offset,
                row_payload_size,
                &native_offset,
                &native_size) ||
            native_offset != expected_payload_offset ||
            elapsed_total > RIBOS_HOST_MAX_DURATION_NS - elapsed) {
            return 0;
        }
        expected_payload_offset += native_size;
        elapsed_total += elapsed;
        if ((journal_state ==
                RIBOS_VM_JOURNAL_RECEIPT_NONE) !=
            ribos_host_digest_is_zero(row + 72)) {
            return 0;
        }
        if (callback_status == RIBOS_VM_HELPER_CALLBACK_OK) {
            if (result_kind !=
                    RIBOS_HOST_TRANSCRIPT_RESULT_VALUE &&
                result_kind !=
                    RIBOS_HOST_TRANSCRIPT_RESULT_HANDLE) {
                return 0;
            }
        } else if (callback_status ==
                       RIBOS_VM_HELPER_CALLBACK_POLICY_ERROR) {
            if (result_kind !=
                RIBOS_HOST_TRANSCRIPT_RESULT_POLICY_ERROR) {
                return 0;
            }
        } else if (result_kind !=
                       RIBOS_HOST_TRANSCRIPT_RESULT_NONE ||
                   native_size != 0) {
            return 0;
        }
        if ((result_kind ==
                RIBOS_HOST_TRANSCRIPT_RESULT_HANDLE) !=
            (object_id != 0)) {
            return 0;
        }
    }
    if (expected_payload_offset != payload_size) {
        return 0;
    }
    memset(transcript, 0, sizeof(*transcript));
    transcript->rows = file->bytes + rows_offset;
    transcript->payload = file->bytes + payload_offset;
    transcript->row_count = row_count;
    transcript->payload_size = payload_size;
    memcpy(
        transcript->artifact_hash,
        file->bytes + 72,
        RIBOS_VM_DIGEST_BYTES);
    memcpy(
        transcript->context_digest,
        file->bytes + 104,
        RIBOS_VM_DIGEST_BYTES);
    memcpy(transcript->digest, digest, RIBOS_VM_DIGEST_BYTES);
    return 1;
}

static int
ribos_host_transcript_row(
    const RibosHostTranscriptFixture *transcript,
    uint32_t index,
    RibosHostTranscriptRow *decoded)
{
    const uint8_t *row;
    uint64_t payload_offset;
    uint64_t payload_size;

    if (transcript == NULL || decoded == NULL ||
        index >= transcript->row_count) {
        return 0;
    }
    row = transcript->rows +
        (size_t)index * RIBOS_HOST_TRANSCRIPT_ROW_BYTES;
    payload_offset = ribos_host_read_u64(row + 48);
    payload_size = ribos_host_read_u64(row + 56);
    if (payload_offset > transcript->payload_size ||
        payload_size >
            transcript->payload_size - (size_t)payload_offset) {
        return 0;
    }
    memset(decoded, 0, sizeof(*decoded));
    decoded->sequence = ribos_host_read_u64(row);
    decoded->helper_id = ribos_host_read_u32(row + 8);
    decoded->callback_status = ribos_host_read_u32(row + 12);
    decoded->result_kind = ribos_host_read_u32(row + 16);
    decoded->operations = ribos_host_read_u64(row + 24);
    decoded->polls = ribos_host_read_u64(row + 32);
    decoded->elapsed_ns = ribos_host_read_u64(row + 40);
    decoded->payload =
        transcript->payload + (size_t)payload_offset;
    decoded->payload_size = (size_t)payload_size;
    decoded->journal_state = ribos_host_read_u32(row + 64);
    memcpy(
        decoded->journal_digest,
        row + 72,
        RIBOS_VM_DIGEST_BYTES);
    decoded->object_id = ribos_host_read_u64(row + 104);
    return 1;
}

static const uint8_t *
ribos_host_section_row(
    const RibosArtifactSectionView *section,
    uint32_t index)
{
    size_t offset;

    if (section == NULL || index >= section->count ||
        section->row_size == 0 ||
        (size_t)index > SIZE_MAX / section->row_size) {
        return NULL;
    }
    offset = (size_t)index * section->row_size;
    if (offset > section->byte_length ||
        section->row_size > section->byte_length - offset) {
        return NULL;
    }
    return section->bytes + offset;
}

static int
ribos_host_type_by_name(
    const RibosArtifactView *view,
    const char *name,
    uint32_t *type_id,
    uint32_t *byte_size)
{
    const RibosArtifactSectionView *types =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_TYPES);
    uint16_t expected_kind = UINT16_MAX;
    uint16_t expected_width = 0;
    size_t name_size;
    uint32_t index;

    if (view == NULL || name == NULL ||
        type_id == NULL || byte_size == NULL ||
        types == NULL) {
        return 0;
    }
    name_size = strlen(name);
    if (strcmp(name, "Unit") == 0) {
        expected_kind = RIBOS_BC_TYPE_UNIT;
    } else if (strcmp(name, "bool") == 0) {
        expected_kind = RIBOS_BC_TYPE_BOOL;
    } else if (name_size >= 2 &&
               (name[0] == 'u' || name[0] == 'i')) {
        expected_kind = name[0] == 'u' ?
            RIBOS_BC_TYPE_UNSIGNED : RIBOS_BC_TYPE_SIGNED;
        expected_width = (uint16_t)strtoul(name + 1, NULL, 10);
    }
    for (index = 0; index < types->count; ++index) {
        const uint8_t *row = ribos_host_section_row(types, index);
        uint16_t kind;
        uint32_t length;

        if (row == NULL) {
            return 0;
        }
        kind = ribos_host_read_u16(row + 4);
        if (expected_kind != UINT16_MAX) {
            if (kind == expected_kind &&
                (expected_width == 0 ||
                 ribos_host_read_u16(row + 6) ==
                    expected_width)) {
                *type_id = index;
                *byte_size = ribos_host_read_u32(row + 40);
                return 1;
            }
            continue;
        }
        length = ribos_host_read_u32(row + 56);
        if (length == name_size &&
            length <= RIBOS_ARTIFACT_TYPE_ROW_BYTES - 60u &&
            memcmp(row + 60, name, length) == 0) {
            *type_id = index;
            *byte_size = ribos_host_read_u32(row + 40);
            return 1;
        }
    }
    return 0;
}

static uint32_t
ribos_host_effect(const RibosSchemaHelper *helper)
{
    if ((helper->flags &
         RIBOS_SCHEMA_HELPER_TERMINAL_BOOT_ACTION) != 0) {
        return RIBOS_VM_HELPER_EFFECT_TERMINAL;
    }
    if ((helper->capabilities &
         (RIBOS_CAPABILITY_DEVICE |
          RIBOS_CAPABILITY_STATE |
          RIBOS_CAPABILITY_FLASH |
          RIBOS_CAPABILITY_HANDOFF)) != 0) {
        return RIBOS_VM_HELPER_EFFECT_JOURNALED;
    }
    if ((helper->capabilities &
         (RIBOS_CAPABILITY_NETWORK |
          RIBOS_CAPABILITY_DIAGNOSTIC)) != 0) {
        return RIBOS_VM_HELPER_EFFECT_EPHEMERAL;
    }
    return RIBOS_VM_HELPER_EFFECT_PURE;
}

static uint32_t
ribos_host_durability(uint32_t effect)
{
    switch (effect) {
    case RIBOS_VM_HELPER_EFFECT_PURE:
        return RIBOS_VM_HELPER_DURABILITY_NONE;
    case RIBOS_VM_HELPER_EFFECT_EPHEMERAL:
        return RIBOS_VM_HELPER_DURABILITY_VOLATILE;
    case RIBOS_VM_HELPER_EFFECT_JOURNALED:
        return RIBOS_VM_HELPER_DURABILITY_JOURNAL_RECEIPT;
    case RIBOS_VM_HELPER_EFFECT_TERMINAL:
        return RIBOS_VM_HELPER_DURABILITY_SEALED_INTENT;
    default:
        return UINT32_MAX;
    }
}

static uint32_t
ribos_host_transition(
    const RibosSchemaHelper *helper,
    uint32_t result_ownership)
{
    int terminal =
        (helper->flags &
         RIBOS_SCHEMA_HELPER_TERMINAL_BOOT_ACTION) != 0;
    int typestate =
        (helper->flags &
         RIBOS_SCHEMA_HELPER_TYPESTATE_TRANSITION) != 0;

    if (terminal) {
        return typestate ?
            RIBOS_VM_HANDLE_TRANSITION_TERMINAL_CONSUME :
            RIBOS_VM_HANDLE_TRANSITION_NONE;
    }
    if (typestate) {
        return strcmp(helper->result_type, "Unit") == 0 ?
            RIBOS_VM_HANDLE_TRANSITION_CONSUME :
            RIBOS_VM_HANDLE_TRANSITION_REPLACE;
    }
    if (result_ownership == RIBOS_SCHEMA_OWNERSHIP_AFFINE ||
        result_ownership == RIBOS_SCHEMA_OWNERSHIP_LINEAR) {
        return RIBOS_VM_HANDLE_TRANSITION_CREATE;
    }
    return RIBOS_VM_HANDLE_TRANSITION_NONE;
}

static const RibosSchemaType *
ribos_host_schema_type(
    const RibosProductSchema *schema,
    const char *name)
{
    return ribos_schema_find_type(schema, name, strlen(name));
}

static const RibosHostBinding *
ribos_host_find_binding(
    const RibosHostProgram *program,
    uint32_t stable_id)
{
    uint32_t left = 0;
    uint32_t right = program->binding_count;

    while (left < right) {
        uint32_t middle = left + (right - left) / 2;
        uint32_t candidate =
            program->binding_metadata[middle].stable_id;

        if (candidate == stable_id) {
            return &program->binding_metadata[middle];
        }
        if (candidate < stable_id) {
            left = middle + 1;
        } else {
            right = middle;
        }
    }
    return NULL;
}

static uint32_t
ribos_host_authorize(
    void *authority_context,
    const RibosArtifactAuthorizationRequest *request,
    RibosArtifactAuthorizationReceipt *receipt)
{
    RibosHostAuthority *authority = authority_context;

    if (authority == NULL || request == NULL ||
        receipt == NULL || request->envelope_flags != 0 ||
        request->signature_size != 0 ||
        request->key_id_size != 0) {
        return RIBOS_VM_STATUS_NOT_AUTHORIZED;
    }
    memset(receipt, 0, sizeof(*receipt));
    receipt->size = sizeof(*receipt);
    receipt->authorization_major =
        RIBOS_VM_AUTHORIZATION_V1_MAJOR;
    receipt->authorization_minor =
        RIBOS_VM_AUTHORIZATION_V1_MINOR;
    receipt->decision = RIBOS_ARTIFACT_AUTHORIZATION_GRANTED;
    receipt->authority_generation = 1;
    receipt->manifest_sequence = 1;
    receipt->rollback_floor = 1;
    receipt->policy_identity_digest[0] = 0x52;
    memcpy(
        receipt->artifact_hash,
        request->artifact_hash,
        RIBOS_VM_DIGEST_BYTES);
    memcpy(
        receipt->schema_digest,
        request->schema_digest,
        RIBOS_VM_DIGEST_BYTES);
    memcpy(
        receipt->helper_execution_digest,
        authority->helper_digest,
        RIBOS_VM_DIGEST_BYTES);
    return RIBOS_VM_STATUS_OK;
}

static uint32_t
ribos_host_object_drop(
    void *drop_context,
    void *trusted_object)
{
    RibosHostObject *object = trusted_object;

    (void)drop_context;
    if (object == NULL || object->dropped != 0) {
        return RIBOS_VM_HANDLE_DROP_FAILED;
    }
    object->dropped = 1;
    return RIBOS_VM_HANDLE_DROP_COMPLETE;
}

static uint32_t
ribos_host_now(void *embedder_context, uint64_t *now_ns)
{
    RibosHostExecution *execution = embedder_context;

    if (execution == NULL || now_ns == NULL) {
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    *now_ns = execution->now_ns;
    return RIBOS_VM_HELPER_CALLBACK_OK;
}

static void
ribos_host_recovery(
    void *embedder_context,
    const RibosVmFaultReceipt *receipt)
{
    RibosHostExecution *execution = embedder_context;

    if (execution == NULL || receipt == NULL) {
        return;
    }
    ++execution->recovery_calls;
    execution->recovery_receipt = *receipt;
}

static uint32_t
ribos_host_helper(
    void *embedder_context,
    RibosVmHelperCall *call)
{
    RibosHostExecution *execution = embedder_context;
    RibosVmHelperCallInfo info;
    RibosHostTranscriptRow row;
    const RibosHostBinding *binding;
    RibosVmStatus status;

    if (execution == NULL ||
        ribos_vm_helper_call_info_v1(call, &info) !=
            RIBOS_VM_STATUS_OK ||
        !ribos_host_transcript_row(
            execution->transcript,
            execution->next_row,
            &row) ||
        row.helper_id != info.stable_id ||
        row.sequence != (uint64_t)execution->next_row + 1) {
        if (execution != NULL) {
            execution->replay_error = 1;
        }
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    binding = ribos_host_find_binding(
        execution->program,
        info.stable_id);
    if (binding == NULL ||
        execution->now_ns >
            RIBOS_HOST_MAX_DURATION_NS - row.elapsed_ns) {
        execution->replay_error = 1;
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    ++execution->next_row;
    status = ribos_vm_helper_call_consume_operations_v1(
        call,
        row.operations);
    if (status == RIBOS_VM_STATUS_OK && row.polls != 0) {
        status = ribos_vm_helper_call_consume_polls_v1(
            call,
            row.polls);
    }
    if (status != RIBOS_VM_STATUS_OK) {
        execution->replay_error = 1;
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    if (info.effect == RIBOS_VM_HELPER_EFFECT_JOURNALED) {
        if (row.journal_state ==
                RIBOS_VM_JOURNAL_RECEIPT_NONE ||
            ribos_vm_helper_call_set_journal_receipt_v1(
                call,
                row.journal_state,
                row.journal_digest) != RIBOS_VM_STATUS_OK) {
            execution->replay_error = 1;
            return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
        }
    } else if (row.journal_state !=
               RIBOS_VM_JOURNAL_RECEIPT_NONE) {
        execution->replay_error = 1;
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    execution->now_ns += row.elapsed_ns;
    if (row.callback_status ==
            RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT) {
        return row.callback_status;
    }
    if (row.callback_status ==
            RIBOS_VM_HELPER_CALLBACK_POLICY_ERROR) {
        if (binding->error_type_id == RIBOS_VM_INVALID_ID ||
            row.result_kind !=
                RIBOS_HOST_TRANSCRIPT_RESULT_POLICY_ERROR ||
            row.payload_size != binding->error_type_size ||
            ribos_vm_helper_call_set_policy_error_v1(
                call,
                binding->error_type_id,
                row.payload_size == 0 ? NULL : row.payload,
                row.payload_size) != RIBOS_VM_STATUS_OK) {
            execution->replay_error = 1;
            return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
        }
        return row.callback_status;
    }
    if (row.result_kind == RIBOS_HOST_TRANSCRIPT_RESULT_VALUE) {
        if (row.payload_size != binding->success_type_size ||
            ribos_vm_helper_call_set_success_value_v1(
                call,
                binding->success_type_id,
                row.payload_size == 0 ? NULL : row.payload,
                row.payload_size) != RIBOS_VM_STATUS_OK) {
            execution->replay_error = 1;
            return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
        }
        return RIBOS_VM_HELPER_CALLBACK_OK;
    }
    if (row.result_kind == RIBOS_HOST_TRANSCRIPT_RESULT_HANDLE &&
        execution->object_count < execution->object_capacity) {
        RibosHostObject *object =
            &execution->objects[execution->object_count++];

        object->object_id = row.object_id;
        object->dropped = 0;
        if (row.payload_size == 0 &&
            ribos_vm_helper_call_set_success_handle_v1(
                call,
                binding->success_type_id,
                object,
                ribos_host_object_drop,
                NULL) == RIBOS_VM_STATUS_OK) {
            return RIBOS_VM_HELPER_CALLBACK_OK;
        }
    }
    execution->replay_error = 1;
    return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
}

static int
ribos_host_build_contract(
    const RibosArtifactView *view,
    RibosHostProgram *program)
{
    const RibosProductSchema *schema =
        ribos_schema_reference_v1();
    const RibosArtifactSectionView *imports =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_HELPER_IMPORTS);
    uint32_t index;

    if (view == NULL || program == NULL ||
        imports == NULL ||
        imports->count > RIBOS_VM_MAX_HELPER_BINDINGS) {
        return 0;
    }
    /*
     * Helper contracts are non-empty by ABI. A helper-free policy therefore
     * receives only the terminal recovery descriptor; it remains unreachable
     * and cannot increase the artifact's declared/reachable capabilities.
     */
    program->binding_count =
        imports->count == 0 ? 1 : imports->count;
    program->bindings = calloc(
        program->binding_count == 0 ? 1 : program->binding_count,
        sizeof(*program->bindings));
    program->binding_metadata = calloc(
        program->binding_count == 0 ? 1 : program->binding_count,
        sizeof(*program->binding_metadata));
    if (program->bindings == NULL ||
        program->binding_metadata == NULL) {
        return 0;
    }
    for (index = 0; index < program->binding_count; ++index) {
        const uint8_t *import =
            imports->count == 0 ?
                NULL : ribos_host_section_row(imports, index);
        const RibosSchemaHelper *helper = NULL;
        const RibosSchemaType *result_schema;
        RibosHostBinding *metadata =
            &program->binding_metadata[index];
        RibosVmHelperExecutionDescriptor *execution =
            &program->bindings[index].execution;
        uint32_t result_ownership;
        uint32_t effect;
        uint32_t transition;
        size_t schema_index;

        if (imports->count != 0 && import == NULL) {
            return 0;
        }
        metadata->stable_id =
            imports->count == 0 ?
                22u : ribos_host_read_u32(import);
        for (schema_index = 0;
             schema_index < schema->helper_count;
             ++schema_index) {
            if (schema->helpers[schema_index].stable_id ==
                metadata->stable_id) {
                helper = &schema->helpers[schema_index];
                break;
            }
        }
        if (helper == NULL) {
            return 0;
        }
        result_schema =
            ribos_host_schema_type(schema, helper->result_type);
        result_ownership = result_schema == NULL ?
            RIBOS_SCHEMA_OWNERSHIP_COPY :
            result_schema->ownership;
        effect = ribos_host_effect(helper);
        transition =
            ribos_host_transition(helper, result_ownership);
        metadata->error_type_id = RIBOS_VM_INVALID_ID;
        if (!ribos_host_type_by_name(
                view,
                helper->result_type,
                &metadata->success_type_id,
                &metadata->success_type_size)) {
            return 0;
        }
        metadata->success_type_class = result_schema == NULL ?
            RIBOS_SCHEMA_TYPE_VALUE :
            result_schema->type_class;
        metadata->success_ownership = result_ownership;
        if (helper->error_type != NULL &&
            !ribos_host_type_by_name(
                view,
                helper->error_type,
                &metadata->error_type_id,
                &metadata->error_type_size)) {
            return 0;
        }
        *execution = (RibosVmHelperExecutionDescriptor){
            .size = sizeof(*execution),
            .contract_major =
                RIBOS_VM_HELPER_EXECUTION_V1_MAJOR,
            .contract_minor =
                RIBOS_VM_HELPER_EXECUTION_V1_MINOR,
            .stable_id = helper->stable_id,
            .required_capabilities = helper->capabilities,
            .effect = effect,
            .execution_mode =
                RIBOS_VM_HELPER_EXECUTION_SYNCHRONOUS,
            .durability = ribos_host_durability(effect),
            .handle_transition = transition,
            .transition_parameter =
                transition ==
                    RIBOS_VM_HANDLE_TRANSITION_CONSUME ||
                transition ==
                    RIBOS_VM_HANDLE_TRANSITION_REPLACE ||
                transition ==
                    RIBOS_VM_HANDLE_TRANSITION_TERMINAL_CONSUME ?
                        helper->transition_parameter :
                        RIBOS_VM_INVALID_ID,
            .allowed_mode_mask = UINT64_MAX,
            .allowed_phase_mask = UINT64_MAX,
            .maximum_input_bytes =
                RIBOS_HOST_CONTEXT_MAX_PAYLOAD,
            .maximum_output_bytes =
                RIBOS_HOST_CONTEXT_MAX_PAYLOAD,
            .maximum_operations =
                RIBOS_HOST_MAX_OPERATIONS,
            .maximum_polls = RIBOS_HOST_MAX_OPERATIONS,
            .maximum_duration_ns =
                RIBOS_HOST_MAX_DURATION_NS,
        };
        program->bindings[index].invoke = ribos_host_helper;
    }
    program->contract = (RibosVmHelperContract){
        .size = sizeof(program->contract),
        .contract_major =
            RIBOS_VM_HELPER_EXECUTION_V1_MAJOR,
        .contract_minor =
            RIBOS_VM_HELPER_EXECUTION_V1_MINOR,
        .binding_count = program->binding_count,
        .bindings = program->bindings,
    };
    if (ribos_vm_helper_contract_compute_identity_v1(
            &program->contract,
            program->contract.digest) != RIBOS_VM_STATUS_OK) {
        return 0;
    }
    memcpy(
        program->authority.helper_digest,
        program->contract.digest,
        RIBOS_VM_DIGEST_BYTES);
    return 1;
}

static int
ribos_host_prepare(
    const RibosHostFile *artifact,
    const RibosArtifactView *view,
    RibosHostProgram *program)
{
    RibosArtifactAuthorizer authorizer = {
        .size = sizeof(authorizer),
        .authorization_major =
            RIBOS_VM_AUTHORIZATION_V1_MAJOR,
        .authorization_minor =
            RIBOS_VM_AUTHORIZATION_V1_MINOR,
        .authority_context = &program->authority,
        .authorize = ribos_host_authorize,
    };
    size_t workspace_size;

    if (!ribos_host_build_contract(view, program) ||
        view->instruction_upper_bound == 0 ||
        view->maximum_stack_bytes == 0 ||
        view->maximum_call_depth == 0 ||
        view->helper_upper_bound > RIBOS_HOST_MAX_OPERATIONS) {
        return 0;
    }
    program->limits = (RibosVmLimits){
        .size = sizeof(program->limits),
        .runtime_abi_major = RIBOS_VM_RUNTIME_ABI_V1_MAJOR,
        .runtime_abi_minor = RIBOS_VM_RUNTIME_ABI_V1_MINOR,
        .maximum_instructions = view->instruction_upper_bound,
        .maximum_helper_calls =
            view->helper_upper_bound == 0 ?
                1 : view->helper_upper_bound,
        .maximum_stack_bytes = view->maximum_stack_bytes,
        .maximum_arena_bytes =
            RIBOS_VM_RUNTIME_MAX_ARENA_BYTES_V1,
        .maximum_input_bytes =
            RIBOS_HOST_TRANSCRIPT_MAX_BYTES,
        .maximum_output_bytes =
            RIBOS_HOST_TRANSCRIPT_MAX_BYTES,
        .maximum_operations = RIBOS_HOST_MAX_OPERATIONS,
        .maximum_polls = RIBOS_HOST_MAX_OPERATIONS,
        .maximum_execution_duration_ns =
            RIBOS_HOST_MAX_DURATION_NS,
        .maximum_helper_duration_ns =
            RIBOS_HOST_MAX_DURATION_NS,
        .maximum_call_depth = view->maximum_call_depth,
        .maximum_handles = RIBOS_HOST_MAX_HANDLES,
        .maximum_trace_records = 16,
    };
    if (ribos_authorized_artifact_workspace_size_v1(
            artifact->size,
            &workspace_size) != RIBOS_VM_STATUS_OK) {
        return 0;
    }
    program->authorized_workspace = malloc(workspace_size);
    if (program->authorized_workspace == NULL ||
        ribos_authorize_artifact_v1(
            artifact->bytes,
            artifact->size,
            &authorizer,
            program->authorized_workspace,
            workspace_size,
            &program->authorized) != RIBOS_VM_STATUS_OK ||
        ribos_prepared_program_workspace_size_v1(
            program->authorized,
            &program->contract,
            &workspace_size) != RIBOS_VM_STATUS_OK) {
        return 0;
    }
    program->prepared_workspace = malloc(workspace_size);
    if (program->prepared_workspace == NULL ||
        ribos_prepare_program_v1(
            program->authorized,
            ribos_schema_reference_v1(),
            &program->contract,
            &program->limits,
            program->prepared_workspace,
            workspace_size,
            &program->verifier,
            &program->prepared) != RIBOS_VM_STATUS_OK) {
        return 0;
    }
    return 1;
}

static void
ribos_host_release_program(RibosHostProgram *program)
{
    if (program == NULL) {
        return;
    }
    free(program->prepared_workspace);
    free(program->authorized_workspace);
    free(program->binding_metadata);
    free(program->bindings);
    memset(program, 0, sizeof(*program));
}

static void
ribos_host_report_append(
    RibosHostReport *report,
    const char *format,
    ...)
{
    va_list arguments;
    int length;

    if (report == NULL || format == NULL || report->failed) {
        return;
    }
    va_start(arguments, format);
    length = vsnprintf(
        report->bytes + report->size,
        sizeof(report->bytes) - report->size,
        format,
        arguments);
    va_end(arguments);
    if (length < 0 ||
        (size_t)length >= sizeof(report->bytes) - report->size) {
        report->failed = 1;
        return;
    }
    report->size += (size_t)length;
}

static void
ribos_host_hex(
    const uint8_t digest[RIBOS_VM_DIGEST_BYTES],
    char output[RIBOS_VM_DIGEST_BYTES * 2u + 1u])
{
    static const char digits[] = "0123456789abcdef";
    uint32_t index;

    for (index = 0; index < RIBOS_VM_DIGEST_BYTES; ++index) {
        output[index * 2] = digits[digest[index] >> 4];
        output[index * 2 + 1] = digits[digest[index] & 0x0f];
    }
    output[RIBOS_VM_DIGEST_BYTES * 2u] = '\0';
}

static uint32_t
ribos_host_report_source_map(
    const RibosArtifactView *view,
    uint32_t source_map_id,
    uint64_t *start_byte,
    uint64_t *end_byte,
    uint32_t *start_line,
    uint32_t *start_column,
    uint32_t *end_line,
    uint32_t *end_column)
{
    const RibosArtifactSectionView *maps =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_SOURCE_MAPS);
    const uint8_t *row =
        ribos_host_section_row(maps, source_map_id);

    *start_byte = 0;
    *end_byte = 0;
    *start_line = 0;
    *start_column = 0;
    *end_line = 0;
    *end_column = 0;
    if (row == NULL ||
        ribos_host_read_u32(row) != source_map_id) {
        return RIBOS_VM_INVALID_ID;
    }
    *start_byte = ribos_host_read_u64(row + 8);
    *end_byte = ribos_host_read_u64(row + 16);
    *start_line = ribos_host_read_u32(row + 24);
    *start_column = ribos_host_read_u32(row + 28);
    *end_line = ribos_host_read_u32(row + 32);
    *end_column = ribos_host_read_u32(row + 36);
    return source_map_id;
}

static int
ribos_host_emit_report(
    const RibosArtifactView *view,
    const RibosHostContextFixture *context_fixture,
    const RibosHostTranscriptFixture *transcript,
    const RibosHostProgram *program,
    const RibosHostExecution *host_execution,
    const RibosVmOutcome *outcome,
    const RibosVmInterpreterSnapshot *interpreter,
    const RibosVmHelperExecutionSnapshot *helper,
    const RibosVmTerminalSnapshot *terminal)
{
    RibosHostReport report = {{0}, 0, 0};
    uint8_t payload_digest[RIBOS_VM_DIGEST_BYTES] = {0};
    uint8_t outcome_receipt[RIBOS_VM_DIGEST_BYTES] = {0};
    uint8_t report_digest[RIBOS_VM_DIGEST_BYTES];
    const uint8_t *prepared_binding =
        ribos_prepared_program_binding_digest_v1(
            program->prepared);
    const uint8_t *payload = NULL;
    size_t payload_size = 0;
    uint32_t outcome_type = RIBOS_VM_INVALID_ID;
    uint32_t outcome_code = 0;
    uint32_t fault_code = 0;
    uint32_t fault_subject = 0;
    uint32_t fault_instruction = RIBOS_VM_INVALID_ID;
    uint32_t fault_helper = RIBOS_VM_INVALID_ID;
    uint32_t source_map_id = terminal->source_map_id;
    uint64_t source_start;
    uint64_t source_end;
    uint32_t source_start_line;
    uint32_t source_start_column;
    uint32_t source_end_line;
    uint32_t source_end_column;
    char artifact_hex[65];
    char schema_hex[65];
    char binding_hex[65];
    char context_hex[65];
    char transcript_hex[65];
    char payload_hex[65];
    char receipt_hex[65];
    char journal_hex[65];
    char trace_hex[65];
    char report_hex[65];

    if (outcome->kind == RIBOS_VM_OUTCOME_BOOT_ACTION) {
        payload = outcome->value.boot_action.payload;
        payload_size =
            (size_t)outcome->value.boot_action.payload_size;
        outcome_type = outcome->value.boot_action.action_type_id;
        memcpy(
            outcome_receipt,
            outcome->value.boot_action.receipt_digest,
            RIBOS_VM_DIGEST_BYTES);
    } else if (outcome->kind ==
               RIBOS_VM_OUTCOME_POLICY_ERROR) {
        payload = outcome->value.policy_error.payload;
        payload_size =
            (size_t)outcome->value.policy_error.payload_size;
        outcome_type = outcome->value.policy_error.error_type_id;
        outcome_code = outcome->value.policy_error.stable_code;
    } else if (outcome->kind == RIBOS_VM_OUTCOME_VM_FAULT) {
        fault_code = outcome->value.vm_fault.fault_code;
        fault_subject = outcome->value.vm_fault.subject;
        fault_instruction =
            outcome->value.vm_fault.instruction_id;
        fault_helper = outcome->value.vm_fault.helper_id;
        memcpy(
            outcome_receipt,
            outcome->value.vm_fault.trace_digest,
            RIBOS_VM_DIGEST_BYTES);
        source_map_id = interpreter->source_map_id;
    } else {
        return 0;
    }
    ribos_artifact_sha256(payload, payload_size, payload_digest);
    source_map_id = ribos_host_report_source_map(
        view,
        source_map_id,
        &source_start,
        &source_end,
        &source_start_line,
        &source_start_column,
        &source_end_line,
        &source_end_column);
    ribos_host_hex(view->artifact_hash, artifact_hex);
    ribos_host_hex(view->schema_digest, schema_hex);
    ribos_host_hex(prepared_binding, binding_hex);
    ribos_host_hex(context_fixture->snapshot_digest, context_hex);
    ribos_host_hex(transcript->digest, transcript_hex);
    ribos_host_hex(payload_digest, payload_hex);
    ribos_host_hex(outcome_receipt, receipt_hex);
    ribos_host_hex(terminal->journal_chain_digest, journal_hex);
    ribos_host_hex(terminal->trace_digest, trace_hex);
    ribos_host_report_append(
        &report,
        "format=RIBOS-RUN-REPORT-V1\n"
        "artifact.sha256=%s\n"
        "schema.sha256=%s\n"
        "binding.sha256=%s\n"
        "context.sha256=%s\n"
        "transcript.sha256=%s\n"
        "outcome=%s\n"
        "outcome.type=%u\n"
        "outcome.code=%u\n"
        "outcome.payload.bytes=%zu\n"
        "outcome.payload.sha256=%s\n"
        "outcome.receipt.sha256=%s\n"
        "instructions.actual=%llu\n"
        "instructions.upper=%llu\n"
        "helpers.actual=%llu\n"
        "helpers.upper=%llu\n"
        "operations.actual=%llu\n"
        "polls.actual=%llu\n"
        "stack.bytes=%llu\n"
        "stack.upper=%llu\n"
        "frame.depth=%u\n"
        "call-depth.upper=%u\n"
        "source.id=%u\n"
        "source.start=%llu\n"
        "source.end=%llu\n"
        "source.start.line=%u\n"
        "source.start.column=%u\n"
        "source.end.line=%u\n"
        "source.end.column=%u\n"
        "terminal.state=%u\n"
        "terminal.action-consumed=%u\n"
        "terminal.journal.state=%u\n"
        "terminal.journal.count=%llu\n"
        "terminal.journal.sha256=%s\n"
        "terminal.trace.sha256=%s\n"
        "fault.code=%s\n"
        "fault.code.id=%u\n"
        "fault.subject=%u\n"
        "fault.instruction=%u\n"
        "fault.helper=%u\n"
        "recovery.calls=%u\n"
        "transcript.rows=%u\n"
        "transcript.consumed=%u\n",
        artifact_hex,
        schema_hex,
        binding_hex,
        context_hex,
        transcript_hex,
        ribos_vm_outcome_kind_name(outcome->kind),
        outcome_type,
        outcome_code,
        payload_size,
        payload_hex,
        receipt_hex,
        (unsigned long long)interpreter->consumed_instructions,
        (unsigned long long)
            program->verifier.recomputed_instruction_upper_bound,
        (unsigned long long)helper->consumed_helper_calls,
        (unsigned long long)
            program->verifier.recomputed_helper_upper_bound,
        (unsigned long long)helper->consumed_operations,
        (unsigned long long)helper->consumed_polls,
        (unsigned long long)interpreter->stack_bytes,
        (unsigned long long)
            program->verifier.recomputed_stack_bytes,
        interpreter->frame_depth,
        program->verifier.recomputed_call_depth,
        source_map_id,
        (unsigned long long)source_start,
        (unsigned long long)source_end,
        source_start_line,
        source_start_column,
        source_end_line,
        source_end_column,
        terminal->state,
        terminal->action_consumed,
        terminal->journal_state,
        (unsigned long long)terminal->journal_count,
        journal_hex,
        trace_hex,
        ribos_vm_fault_code_name(fault_code),
        fault_code,
        fault_subject,
        fault_instruction,
        fault_helper,
        host_execution->recovery_calls,
        transcript->row_count,
        host_execution->next_row);
    if (report.failed) {
        return 0;
    }
    ribos_artifact_sha256(
        (const uint8_t *)report.bytes,
        report.size,
        report_digest);
    ribos_host_hex(report_digest, report_hex);
    ribos_host_report_append(
        &report,
        "report.sha256=%s\n",
        report_hex);
    if (report.failed ||
        fwrite(report.bytes, 1, report.size, stdout) != report.size ||
        fflush(stdout) != 0) {
        return 0;
    }
    return 1;
}

static int
ribos_host_execute(
    const RibosArtifactView *view,
    const RibosHostContextFixture *context_fixture,
    const RibosHostTranscriptFixture *transcript,
    RibosHostProgram *program)
{
    RibosVmStoragePlan plan;
    RibosVmStorage *storage = NULL;
    RibosVmHandleHostTable handle_table;
    RibosVmHandleHostEntry *handle_entries = NULL;
    RibosHostObject *objects = NULL;
    RibosHostExecution host_execution;
    RibosVmContext context;
    RibosVmEmbedder embedder;
    RibosVmHelperEnvironment environment;
    RibosVmOutcome outcome;
    RibosVmInterpreterSnapshot interpreter;
    RibosVmHelperExecutionSnapshot helper;
    RibosVmTerminalSnapshot terminal;
    void *arena = NULL;
    size_t arena_size;
    RibosVmStatus status;
    int success = 0;

    status = ribos_vm_runtime_size_v1(
            program->prepared,
            &plan,
            &arena_size);
    if (status != RIBOS_VM_STATUS_OK) {
        (void)fprintf(
            stderr,
            "RIBOS-RUN-DIAGNOSTIC phase=runtime-size vm-status=%s\n",
            ribos_vm_status_name(status));
        return 0;
    }
    arena = malloc(arena_size);
    handle_entries = calloc(
        RIBOS_HOST_MAX_HANDLES,
        sizeof(*handle_entries));
    objects = calloc(
        transcript->row_count == 0 ? 1 : transcript->row_count,
        sizeof(*objects));
    if (arena == NULL || handle_entries == NULL ||
        objects == NULL ||
        (status = ribos_vm_storage_initialize_v1(
            program->prepared,
            &plan,
            arena,
            arena_size,
            0,
            &storage)) != RIBOS_VM_STATUS_OK ||
        (status = ribos_vm_handle_host_table_initialize_v1(
            &handle_table,
            handle_entries,
            RIBOS_HOST_MAX_HANDLES)) != RIBOS_VM_STATUS_OK) {
        (void)fprintf(
            stderr,
            "RIBOS-RUN-DIAGNOSTIC phase=runtime-init vm-status=%s\n",
            ribos_vm_status_name(status));
        goto done;
    }
    memset(&host_execution, 0, sizeof(host_execution));
    host_execution.transcript = transcript;
    host_execution.program = program;
    host_execution.objects = objects;
    host_execution.object_capacity = transcript->row_count;
    host_execution.now_ns = 1;
    context = (RibosVmContext){
        .size = sizeof(context),
        .runtime_abi_major = RIBOS_VM_RUNTIME_ABI_V1_MAJOR,
        .runtime_abi_minor = RIBOS_VM_RUNTIME_ABI_V1_MINOR,
        .context_type_id = context_fixture->context_type_id,
        .selected_mode = context_fixture->selected_mode,
        .selected_phase = context_fixture->selected_phase,
        .generation = context_fixture->generation,
        .bytes = context_fixture->payload,
        .byte_size = context_fixture->payload_size,
    };
    memcpy(
        context.digest,
        context_fixture->payload_digest,
        RIBOS_VM_DIGEST_BYTES);
    embedder = (RibosVmEmbedder){
        .size = sizeof(embedder),
        .runtime_abi_major = RIBOS_VM_RUNTIME_ABI_V1_MAJOR,
        .runtime_abi_minor = RIBOS_VM_RUNTIME_ABI_V1_MINOR,
        .selected_mode = context_fixture->selected_mode,
        .selected_phase = context_fixture->selected_phase,
        .granted_capabilities = view->declared_capabilities,
        .helper_contract = &program->contract,
        .embedder_context = &host_execution,
        .monotonic_now_ns = ribos_host_now,
        .factory_recovery = ribos_host_recovery,
    };
    environment = (RibosVmHelperEnvironment){
        .size = sizeof(environment),
        .helpers_major = RIBOS_VM_HELPERS_V1_MAJOR,
        .helpers_minor = RIBOS_VM_HELPERS_V1_MINOR,
        .embedder = &embedder,
        .handle_table = &handle_table,
    };
    status = ribos_vm_policy_execute_v1(
        program->prepared,
        &context,
        &environment,
        storage,
        arena_size,
        &outcome);
    if (status != RIBOS_VM_STATUS_OK) {
        (void)fprintf(
            stderr,
            "RIBOS-RUN-DIAGNOSTIC phase=policy-execute vm-status=%s\n",
            ribos_vm_status_name(status));
        goto done;
    }
    if (host_execution.replay_error != 0 ||
        host_execution.next_row != transcript->row_count) {
        (void)fprintf(
            stderr,
            "RIBOS-RUN-DIAGNOSTIC phase=transcript-consumption "
            "vm-status=invalid-state\n");
        goto done;
    }
    status = ribos_vm_interpreter_snapshot_v1(
            program->prepared,
            storage,
            arena_size,
            &interpreter);
    if (status == RIBOS_VM_STATUS_OK) {
        status = ribos_vm_helper_execution_snapshot_v1(
            program->prepared,
            storage,
            arena_size,
            &helper);
    }
    if (status == RIBOS_VM_STATUS_OK) {
        status = ribos_vm_terminal_snapshot_v1(
            program->prepared,
            storage,
            arena_size,
            &terminal);
    }
    if (status != RIBOS_VM_STATUS_OK) {
        (void)fprintf(
            stderr,
            "RIBOS-RUN-DIAGNOSTIC phase=snapshot vm-status=%s\n",
            ribos_vm_status_name(status));
        goto done;
    }
    if (interpreter.consumed_instructions >
            program->verifier.recomputed_instruction_upper_bound ||
        helper.consumed_helper_calls >
            program->verifier.recomputed_helper_upper_bound ||
        (outcome.kind == RIBOS_VM_OUTCOME_VM_FAULT ?
            host_execution.recovery_calls != 1 :
            host_execution.recovery_calls != 0)) {
        (void)fprintf(
            stderr,
            "RIBOS-RUN-DIAGNOSTIC phase=resource-or-recovery "
            "vm-status=invalid-state\n");
        goto done;
    }
    success = ribos_host_emit_report(
        view,
        context_fixture,
        transcript,
        program,
        &host_execution,
        &outcome,
        &interpreter,
        &helper,
        &terminal);

done:
    free(objects);
    free(handle_entries);
    free(arena);
    return success;
}

static void
ribos_host_usage(FILE *stream, const char *program)
{
    (void)fprintf(
        stream,
        "usage: %s --context CONTEXT.rbctx "
        "--transcript HELPERS.rbtr POLICY.rba\n",
        program);
}

int
main(int argc, char **argv)
{
    RibosHostFile artifact = {0};
    RibosHostFile context_file = {0};
    RibosHostFile transcript_file = {0};
    RibosHostContextFixture context;
    RibosHostTranscriptFixture transcript;
    RibosHostProgram program;
    RibosArtifactView view;
    const char *context_path;
    const char *transcript_path;
    const char *artifact_path;
    const char *failure = NULL;
    int success = 0;

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        ribos_host_usage(stdout, argv[0]);
        return 0;
    }
    if (argc != 6 ||
        strcmp(argv[1], "--context") != 0 ||
        strcmp(argv[3], "--transcript") != 0) {
        ribos_host_usage(stderr, argv[0]);
        return 64;
    }
    context_path = argv[2];
    transcript_path = argv[4];
    artifact_path = argv[5];
    memset(&program, 0, sizeof(program));
    if (!ribos_host_read_file(
            artifact_path,
            RIBOS_ARTIFACT_MAX_BYTES,
            &artifact)) {
        failure = "artifact-io";
    } else if (!ribos_host_read_file(
            context_path,
            RIBOS_HOST_CONTEXT_HEADER_BYTES +
                RIBOS_HOST_CONTEXT_MAX_PAYLOAD,
            &context_file)) {
        failure = "context-io";
    } else if (!ribos_host_read_file(
            transcript_path,
            RIBOS_HOST_TRANSCRIPT_MAX_BYTES,
            &transcript_file)) {
        failure = "transcript-io";
    } else if (ribos_artifact_open_v1(
            artifact.bytes,
            artifact.size,
            &view) != RIBOS_ARTIFACT_OK) {
        failure = "artifact-open";
    } else if (!ribos_host_parse_context(&context_file, &context)) {
        failure = "context-decode";
    } else if (!ribos_host_parse_transcript(
            &transcript_file,
            &transcript)) {
        failure = "transcript-decode";
    } else if (memcmp(
            transcript.artifact_hash,
            view.artifact_hash,
            RIBOS_VM_DIGEST_BYTES) != 0) {
        failure = "artifact-binding";
    } else if (memcmp(
            transcript.context_digest,
            context.snapshot_digest,
            RIBOS_VM_DIGEST_BYTES) != 0) {
        failure = "context-binding";
    } else if (!ribos_host_prepare(&artifact, &view, &program)) {
        failure = "prepare";
    } else if (!ribos_host_execute(
            &view,
            &context,
            &transcript,
            &program)) {
        failure = "execute";
    }
    if (failure != NULL) {
        (void)fprintf(
            stderr,
            "RIBOS-RUN-FAIL status=%s\n",
            failure);
        goto done;
    }
    success = 1;

done:
    ribos_host_release_program(&program);
    ribos_host_release_file(&transcript_file);
    ribos_host_release_file(&context_file);
    ribos_host_release_file(&artifact);
    return success ? 0 : 2;
}
