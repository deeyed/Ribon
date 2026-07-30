#include "ribos/vm/verifier.h"

#include <limits.h>
#include <string.h>

typedef struct RibosVerifierArena {
    uint8_t *bytes;
    size_t capacity;
    size_t used;
    int failed;
} RibosVerifierArena;

typedef struct RibosVerifierTypeLayout {
    uint32_t storage_kind;
    uint32_t byte_size;
    uint32_t alignment;
    uint32_t element_stride;
    uint32_t payload_offset;
    uint32_t capacity;
} RibosVerifierTypeLayout;

typedef struct RibosVerifierPathBound {
    uint64_t stop;
    uint64_t returned;
    uint64_t trapped;
    uint8_t has_stop;
    uint8_t has_return;
    uint8_t has_trap;
} RibosVerifierPathBound;

typedef enum RibosVerifierMetric {
    RIBOS_VERIFIER_METRIC_INSTRUCTION = 0,
    RIBOS_VERIFIER_METRIC_HELPER_TOTAL,
    RIBOS_VERIFIER_METRIC_HELPER_ID
} RibosVerifierMetric;

typedef struct RibosVerifierPathContext {
    struct RibosVerifierContext *verifier;
    uint32_t function_id;
    uint32_t stop_block;
    RibosVerifierMetric metric;
    uint32_t helper_import;
    uint32_t depth;
    int failed;
} RibosVerifierPathContext;

typedef struct RibosVerifierContext {
    RibosArtifactView artifact;
    const RibosProductSchema *schema;
    RibosVerifierReport *report;
    const RibosArtifactSectionView *types;
    const RibosArtifactSectionView *shapes;
    const RibosArtifactSectionView *constants;
    const RibosArtifactSectionView *constant_bytes;
    const RibosArtifactSectionView *functions;
    const RibosArtifactSectionView *blocks;
    const RibosArtifactSectionView *loops;
    const RibosArtifactSectionView *slots;
    const RibosArtifactSectionView *instructions;
    const RibosArtifactSectionView *operands;
    const RibosArtifactSectionView *helper_imports;
    const RibosArtifactSectionView *helper_bounds;
    const RibosArtifactSectionView *source_maps;
    RibosVerifierTypeLayout *type_layouts;
    uint8_t *type_states;
    uint8_t *instruction_seen;
    uint8_t *block_reachable;
    uint32_t *predecessor_counts;
    uint32_t *predecessor_starts;
    uint32_t *predecessor_cursor;
    uint32_t *predecessors;
    uint64_t *definition_bits;
    uint64_t *out_bits;
    uint64_t *temporary_bits;
    uint8_t *call_edges;
    uint8_t *call_states;
    uint32_t *call_depths;
    uint32_t *frame_bytes;
    uint32_t *terminal_masks;
    uint64_t *stack_bytes;
    uint8_t *type_ownership;
    uint8_t *resource_states;
    uint32_t *reachable_capabilities;
    uint64_t *instruction_bounds;
    uint64_t *helper_total_bounds;
    uint64_t *helper_matrix;
    uint8_t *path_states;
    RibosVerifierPathBound *path_memo;
    uint8_t *action_masks;
    size_t path_matrix_entries;
    size_t bit_matrix_words;
} RibosVerifierContext;

static uint16_t
ribos_verifier_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t
ribos_verifier_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

static uint64_t
ribos_verifier_u64(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static const uint8_t *
ribos_verifier_row(
    const RibosArtifactSectionView *section,
    uint32_t index)
{
    if (section == NULL || index >= section->count) {
        return NULL;
    }
    return section->bytes + (size_t)index * section->row_size;
}

static int
ribos_verifier_range_u32(
    uint32_t start,
    uint32_t count,
    uint32_t limit)
{
    return start <= limit && count <= limit - start;
}

static int
ribos_verifier_add_u32(
    uint32_t left,
    uint32_t right,
    uint32_t *result)
{
    if (result == NULL || left > UINT32_MAX - right) {
        return 0;
    }
    *result = left + right;
    return 1;
}

static int
ribos_verifier_add_u64(
    uint64_t left,
    uint64_t right,
    uint64_t *result)
{
    if (result == NULL || right > UINT64_MAX - left) {
        return 0;
    }
    *result = left + right;
    return 1;
}

static int
ribos_verifier_multiply_u64(
    uint64_t left,
    uint64_t right,
    uint64_t *result)
{
    if (result == NULL ||
        (left != 0 && right > UINT64_MAX / left)) {
        return 0;
    }
    *result = left * right;
    return 1;
}

static int
ribos_verifier_multiply_u32(
    uint32_t left,
    uint32_t right,
    uint32_t *result)
{
    uint64_t value = (uint64_t)left * right;

    if (result == NULL || value > UINT32_MAX) {
        return 0;
    }
    *result = (uint32_t)value;
    return 1;
}

static int
ribos_verifier_align_u32(
    uint32_t value,
    uint32_t alignment,
    uint32_t *result)
{
    uint32_t mask;

    if (result == NULL || alignment == 0 ||
        (alignment & (alignment - 1)) != 0) {
        return 0;
    }
    mask = alignment - 1;
    if (value > UINT32_MAX - mask) {
        return 0;
    }
    *result = (value + mask) & ~mask;
    return 1;
}

static int
ribos_verifier_size_add(size_t *value, size_t amount)
{
    if (value == NULL || *value > SIZE_MAX - amount) {
        return 0;
    }
    *value += amount;
    return 1;
}

static int
ribos_verifier_size_multiply(
    size_t left,
    size_t right,
    size_t *result)
{
    if (result == NULL || (left != 0 && right > SIZE_MAX / left)) {
        return 0;
    }
    *result = left * right;
    return 1;
}

static int
ribos_verifier_workspace_add(
    size_t *size,
    size_t alignment,
    size_t count,
    size_t element_size)
{
    size_t padding;
    size_t bytes;

    if (size == NULL || alignment == 0 ||
        (alignment & (alignment - 1)) != 0 ||
        !ribos_verifier_size_multiply(count, element_size, &bytes)) {
        return 0;
    }
    padding = (alignment - (*size & (alignment - 1))) &
        (alignment - 1);
    return ribos_verifier_size_add(size, padding) &&
        ribos_verifier_size_add(size, bytes);
}

static void *
ribos_verifier_arena_take(
    RibosVerifierArena *arena,
    size_t alignment,
    size_t count,
    size_t element_size)
{
    size_t padding;
    size_t bytes;
    uint8_t *result;
    uintptr_t cursor;

    if (arena == NULL || alignment == 0 ||
        (alignment & (alignment - 1)) != 0 ||
        !ribos_verifier_size_multiply(count, element_size, &bytes)) {
        if (arena != NULL) {
            arena->failed = 1;
        }
        return NULL;
    }
    cursor = (uintptr_t)arena->bytes + arena->used;
    padding = (alignment - (cursor & (alignment - 1))) &
        (alignment - 1);
    if (arena->used > arena->capacity ||
        padding > arena->capacity - arena->used ||
        bytes > arena->capacity - arena->used - padding) {
        arena->failed = 1;
        return NULL;
    }
    arena->used += padding;
    result = arena->bytes + arena->used;
    arena->used += bytes;
    if (bytes != 0) {
        memset(result, 0, bytes);
    }
    return result;
}

static RibosVerifierStatus
ribos_verifier_fail(
    RibosVerifierContext *context,
    RibosVerifierStatus status,
    RibosVerifierSubject subject,
    uint32_t subject_id,
    uint32_t detail)
{
    if (context != NULL && context->report != NULL) {
        context->report->status = status;
        context->report->subject = subject;
        context->report->subject_id = subject_id;
        context->report->detail = detail;
    }
    return status;
}

static void
ribos_verifier_initialize_report(RibosVerifierReport *report)
{
    if (report != NULL) {
        memset(report, 0, sizeof(*report));
        report->verifier_major = RIBOS_VERIFIER_V1_MAJOR;
        report->verifier_minor = RIBOS_VERIFIER_V1_MINOR;
        report->status = RIBOS_VERIFIER_INVALID_ARGUMENT;
        report->subject = RIBOS_VERIFIER_SUBJECT_ARTIFACT;
        report->subject_id = RIBOS_ARTIFACT_INVALID_ID;
    }
}

static uint64_t
ribos_verifier_fnv1a64(const uint8_t *bytes, size_t length)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t index;

    for (index = 0; index < length; ++index) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int
ribos_verifier_zero(const uint8_t *bytes, size_t length)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        if (bytes[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static int
ribos_verifier_type_layout(
    RibosVerifierContext *context,
    uint32_t type_id)
{
    const uint8_t *row;
    RibosVerifierTypeLayout *layout;
    uint32_t kind;
    uint32_t first;
    uint32_t second;
    uint32_t bound;
    uint32_t shape_start;
    uint32_t shape_count;
    uint32_t size = 0;
    uint32_t alignment = 1;

    if (type_id >= context->types->count) {
        return 0;
    }
    if (context->type_states[type_id] == 2) {
        return 1;
    }
    if (context->type_states[type_id] == 1) {
        return 0;
    }
    context->type_states[type_id] = 1;
    row = ribos_verifier_row(context->types, type_id);
    layout = &context->type_layouts[type_id];
    kind = ribos_verifier_u16(row + 4);
    first = ribos_verifier_u32(row + 8);
    second = ribos_verifier_u32(row + 12);
    bound = ribos_verifier_u32(row + 16);
    shape_start = ribos_verifier_u32(row + 20);
    shape_count = ribos_verifier_u32(row + 24);
    layout->storage_kind = RIBOS_BC_STORAGE_SCALAR;
    layout->capacity = bound;

    switch (kind) {
    case RIBOS_BC_TYPE_ERROR:
    case RIBOS_BC_TYPE_UNKNOWN:
    case RIBOS_BC_TYPE_UNIT:
        break;
    case RIBOS_BC_TYPE_BOOL:
        size = 1;
        break;
    case RIBOS_BC_TYPE_UNSIGNED:
    case RIBOS_BC_TYPE_SIGNED: {
        uint32_t bits = ribos_verifier_u16(row + 6);

        if (bits != 8 && bits != 16 && bits != 32 && bits != 64) {
            return 0;
        }
        size = bits / 8;
        alignment = size;
        break;
    }
    case RIBOS_BC_TYPE_STRING_LITERAL:
        size = 8;
        alignment = 4;
        layout->storage_kind = RIBOS_BC_STORAGE_OPAQUE;
        break;
    case RIBOS_BC_TYPE_NAMED:
        size = ribos_verifier_u32(row + 28);
        alignment = ribos_verifier_u32(row + 32);
        layout->storage_kind = RIBOS_BC_STORAGE_OPAQUE;
        if (size == 0 || alignment == 0 ||
            (alignment & (alignment - 1)) != 0 ||
            alignment > 8 || size % alignment != 0) {
            return 0;
        }
        break;
    case RIBOS_BC_TYPE_ARRAY:
    case RIBOS_BC_TYPE_LIST: {
        const RibosVerifierTypeLayout *element;
        uint32_t payload = 0;
        uint32_t bytes;

        if (first >= context->types->count ||
            !ribos_verifier_type_layout(context, first)) {
            return 0;
        }
        element = &context->type_layouts[first];
        if (!ribos_verifier_align_u32(
                element->byte_size,
                element->alignment,
                &layout->element_stride)) {
            return 0;
        }
        alignment = element->alignment;
        if (kind == RIBOS_BC_TYPE_LIST) {
            if (!ribos_verifier_align_u32(
                    4,
                    element->alignment,
                    &payload)) {
                return 0;
            }
            if (alignment < 4) {
                alignment = 4;
            }
            layout->storage_kind = RIBOS_BC_STORAGE_INLINE_LIST;
        } else {
            layout->storage_kind = RIBOS_BC_STORAGE_INLINE_ARRAY;
        }
        layout->payload_offset = payload;
        if (!ribos_verifier_multiply_u32(
                layout->element_stride,
                bound,
                &bytes) ||
            !ribos_verifier_add_u32(payload, bytes, &size) ||
            !ribos_verifier_align_u32(size, alignment, &size)) {
            return 0;
        }
        break;
    }
    case RIBOS_BC_TYPE_FROZEN_MAP:
    case RIBOS_BC_TYPE_DICT: {
        const RibosVerifierTypeLayout *key;
        const RibosVerifierTypeLayout *value;
        uint32_t entry_alignment;
        uint32_t value_offset;
        uint32_t entry_size;
        uint32_t bytes;

        if (first >= context->types->count ||
            second >= context->types->count ||
            !ribos_verifier_type_layout(context, first) ||
            !ribos_verifier_type_layout(context, second)) {
            return 0;
        }
        key = &context->type_layouts[first];
        value = &context->type_layouts[second];
        entry_alignment =
            key->alignment > value->alignment ?
                key->alignment : value->alignment;
        if (!ribos_verifier_align_u32(
                key->byte_size,
                value->alignment,
                &value_offset) ||
            !ribos_verifier_add_u32(
                value_offset,
                value->byte_size,
                &entry_size) ||
            !ribos_verifier_align_u32(
                entry_size,
                entry_alignment,
                &layout->element_stride) ||
            !ribos_verifier_align_u32(
                4,
                entry_alignment,
                &layout->payload_offset) ||
            !ribos_verifier_multiply_u32(
                layout->element_stride,
                bound,
                &bytes) ||
            !ribos_verifier_add_u32(
                layout->payload_offset,
                bytes,
                &size)) {
            return 0;
        }
        alignment = entry_alignment > 4 ? entry_alignment : 4;
        if (!ribos_verifier_align_u32(size, alignment, &size)) {
            return 0;
        }
        layout->storage_kind = RIBOS_BC_STORAGE_SORTED_MAP;
        break;
    }
    case RIBOS_BC_TYPE_OPTION:
    case RIBOS_BC_TYPE_RESULT: {
        const RibosVerifierTypeLayout *left;
        const RibosVerifierTypeLayout *right = NULL;
        uint32_t maximum_size;

        if (first >= context->types->count ||
            !ribos_verifier_type_layout(context, first)) {
            return 0;
        }
        left = &context->type_layouts[first];
        alignment = left->alignment;
        maximum_size = left->byte_size;
        if (kind == RIBOS_BC_TYPE_RESULT) {
            if (second >= context->types->count ||
                !ribos_verifier_type_layout(context, second)) {
                return 0;
            }
            right = &context->type_layouts[second];
            if (right->alignment > alignment) {
                alignment = right->alignment;
            }
            if (right->byte_size > maximum_size) {
                maximum_size = right->byte_size;
            }
        }
        if (!ribos_verifier_align_u32(
                1,
                alignment,
                &layout->payload_offset) ||
            !ribos_verifier_add_u32(
                layout->payload_offset,
                maximum_size,
                &size) ||
            !ribos_verifier_align_u32(size, alignment, &size)) {
            return 0;
        }
        layout->storage_kind = RIBOS_BC_STORAGE_TAGGED_UNION;
        break;
    }
    case RIBOS_BC_TYPE_STRUCT: {
        uint32_t shape;

        layout->storage_kind = RIBOS_BC_STORAGE_INLINE_STRUCT;
        if (!ribos_verifier_range_u32(
                shape_start,
                shape_count,
                context->shapes->count)) {
            return 0;
        }
        for (shape = shape_start; shape < shape_start + shape_count;
             ++shape) {
            const uint8_t *field =
                ribos_verifier_row(context->shapes, shape);
            uint32_t field_type = ribos_verifier_u32(field + 20);
            const RibosVerifierTypeLayout *field_layout;

            if (ribos_verifier_u32(field + 4) !=
                    RIBOS_BC_SHAPE_STRUCT_FIELD ||
                ribos_verifier_u32(field + 8) != type_id ||
                field_type >= context->types->count ||
                !ribos_verifier_type_layout(context, field_type)) {
                return 0;
            }
            field_layout = &context->type_layouts[field_type];
            if (!ribos_verifier_align_u32(
                    size,
                    field_layout->alignment,
                    &size) ||
                !ribos_verifier_add_u32(
                    size,
                    field_layout->byte_size,
                    &size)) {
                return 0;
            }
            if (field_layout->alignment > alignment) {
                alignment = field_layout->alignment;
            }
        }
        if (!ribos_verifier_align_u32(size, alignment, &size)) {
            return 0;
        }
        break;
    }
    case RIBOS_BC_TYPE_ENUM: {
        uint32_t shape;
        uint32_t maximum_payload = 0;
        uint32_t maximum_alignment = 1;

        if (!ribos_verifier_range_u32(
                shape_start,
                shape_count,
                context->shapes->count)) {
            return 0;
        }
        for (shape = shape_start; shape < shape_start + shape_count;
             ++shape) {
            const uint8_t *variant =
                ribos_verifier_row(context->shapes, shape);
            uint32_t variant_size = 0;
            uint32_t variant_alignment = 1;
            uint32_t payload;

            if (ribos_verifier_u32(variant + 4) !=
                RIBOS_BC_SHAPE_ENUM_VARIANT) {
                continue;
            }
            if (ribos_verifier_u32(variant + 8) != type_id ||
                ribos_verifier_u32(variant + 12) > UINT8_MAX) {
                return 0;
            }
            for (payload = shape_start;
                 payload < shape_start + shape_count;
                 ++payload) {
                const uint8_t *field =
                    ribos_verifier_row(context->shapes, payload);
                uint32_t field_type;
                const RibosVerifierTypeLayout *field_layout;

                if (ribos_verifier_u32(field + 4) !=
                        RIBOS_BC_SHAPE_ENUM_PAYLOAD ||
                    ribos_verifier_u32(field + 12) !=
                        ribos_verifier_u32(variant + 12)) {
                    continue;
                }
                field_type = ribos_verifier_u32(field + 20);
                if (ribos_verifier_u32(field + 8) != type_id ||
                    field_type >= context->types->count ||
                    !ribos_verifier_type_layout(context, field_type)) {
                    return 0;
                }
                field_layout = &context->type_layouts[field_type];
                if (!ribos_verifier_align_u32(
                        variant_size,
                        field_layout->alignment,
                        &variant_size) ||
                    !ribos_verifier_add_u32(
                        variant_size,
                        field_layout->byte_size,
                        &variant_size)) {
                    return 0;
                }
                if (field_layout->alignment > variant_alignment) {
                    variant_alignment = field_layout->alignment;
                }
            }
            if (!ribos_verifier_align_u32(
                    variant_size,
                    variant_alignment,
                    &variant_size)) {
                return 0;
            }
            if (variant_size > maximum_payload) {
                maximum_payload = variant_size;
            }
            if (variant_alignment > maximum_alignment) {
                maximum_alignment = variant_alignment;
            }
        }
        alignment = maximum_alignment;
        if (!ribos_verifier_align_u32(
                1,
                alignment,
                &layout->payload_offset) ||
            !ribos_verifier_add_u32(
                layout->payload_offset,
                maximum_payload,
                &size) ||
            !ribos_verifier_align_u32(size, alignment, &size)) {
            return 0;
        }
        layout->storage_kind = RIBOS_BC_STORAGE_TAGGED_UNION;
        break;
    }
    default:
        return 0;
    }
    if (size > RIBOS_VERIFIER_MAX_VALUE_BYTES ||
        alignment == 0 || alignment > 8) {
        return 0;
    }
    layout->byte_size = size;
    layout->alignment = alignment;
    context->type_states[type_id] = 2;
    return 1;
}

static int
ribos_verifier_type_is_integer(
    RibosVerifierContext *context,
    uint32_t type_id)
{
    const uint8_t *row = ribos_verifier_row(context->types, type_id);
    uint32_t kind;

    if (row == NULL) {
        return 0;
    }
    kind = ribos_verifier_u16(row + 4);
    return kind == RIBOS_BC_TYPE_UNSIGNED ||
        kind == RIBOS_BC_TYPE_SIGNED;
}

static int
ribos_verifier_type_is_unsigned_integer(
    RibosVerifierContext *context,
    uint32_t type_id)
{
    const uint8_t *row = ribos_verifier_row(context->types, type_id);

    return row != NULL &&
        ribos_verifier_u16(row + 4) == RIBOS_BC_TYPE_UNSIGNED;
}

static int
ribos_verifier_type_is_ordered(
    RibosVerifierContext *context,
    uint32_t type_id)
{
    const uint8_t *row = ribos_verifier_row(context->types, type_id);
    const RibosSchemaType *schema_type;
    uint32_t name_length;

    if (row == NULL) {
        return 0;
    }
    if (ribos_verifier_type_is_integer(context, type_id)) {
        return 1;
    }
    if (ribos_verifier_u16(row + 4) != RIBOS_BC_TYPE_NAMED) {
        return 0;
    }
    name_length = ribos_verifier_u32(row + 56);
    schema_type = ribos_schema_find_type(
        context->schema,
        (const char *)(row + 60),
        name_length);
    return schema_type != NULL &&
        schema_type->type_class == RIBOS_SCHEMA_TYPE_ENUM;
}

static uint32_t
ribos_verifier_slot_type(
    RibosVerifierContext *context,
    uint32_t slot_id)
{
    const uint8_t *row = ribos_verifier_row(context->slots, slot_id);

    return row == NULL ? RIBOS_ARTIFACT_INVALID_ID :
        ribos_verifier_u32(row + 8);
}

static uint32_t
ribos_verifier_operand_slot(
    RibosVerifierContext *context,
    uint32_t instruction_id,
    uint32_t ordinal)
{
    const uint8_t *instruction =
        ribos_verifier_row(context->instructions, instruction_id);
    uint32_t start;
    uint32_t count;
    const uint8_t *operand;

    if (instruction == NULL) {
        return RIBOS_ARTIFACT_INVALID_ID;
    }
    start = ribos_verifier_u32(instruction + 16);
    count = ribos_verifier_u16(instruction + 2);
    if (ordinal >= count ||
        !ribos_verifier_range_u32(
            start,
            count,
            context->operands->count)) {
        return RIBOS_ARTIFACT_INVALID_ID;
    }
    operand = ribos_verifier_row(context->operands, start + ordinal);
    return ribos_verifier_u32(operand);
}

static int
ribos_verifier_type_name(
    RibosVerifierContext *context,
    uint32_t type_id,
    const char **name,
    size_t *name_length)
{
    const uint8_t *row = ribos_verifier_row(context->types, type_id);
    uint32_t length;

    if (row == NULL || name == NULL || name_length == NULL) {
        return 0;
    }
    length = ribos_verifier_u32(row + 56);
    if (length > 63) {
        return 0;
    }
    *name = (const char *)(row + 60);
    *name_length = length;
    return 1;
}

static int
ribos_verifier_spelling_equal(
    const char *left,
    size_t left_length,
    const char *right)
{
    size_t right_length;

    if (left == NULL || right == NULL) {
        return 0;
    }
    right_length = strlen(right);
    return left_length == right_length &&
        memcmp(left, right, left_length) == 0;
}

static int
ribos_verifier_type_matches_spelling(
    RibosVerifierContext *context,
    uint32_t type_id,
    const char *spelling)
{
    const uint8_t *row = ribos_verifier_row(context->types, type_id);
    const char *name;
    size_t name_length;

    if (row == NULL || spelling == NULL) {
        return 0;
    }
    if (strcmp(spelling, "*") == 0) {
        return 1;
    }
    if (!ribos_verifier_type_name(
            context,
            type_id,
            &name,
            &name_length)) {
        return 0;
    }
    return ribos_verifier_spelling_equal(
        name,
        name_length,
        spelling);
}

static const RibosSchemaType *
ribos_verifier_schema_type(
    RibosVerifierContext *context,
    uint32_t type_id)
{
    const uint8_t *row = ribos_verifier_row(context->types, type_id);
    const char *name;
    size_t name_length;

    if (row == NULL ||
        ribos_verifier_u16(row + 4) != RIBOS_BC_TYPE_NAMED ||
        !ribos_verifier_type_name(
            context,
            type_id,
            &name,
            &name_length)) {
        return NULL;
    }
    return ribos_schema_find_type(
        context->schema,
        name,
        name_length);
}

static int
ribos_verifier_type_ownership(
    RibosVerifierContext *context,
    uint32_t type_id)
{
    const uint8_t *row = ribos_verifier_row(context->types, type_id);
    uint32_t kind;
    int ownership = RIBOS_SCHEMA_OWNERSHIP_COPY;

    if (row == NULL) {
        return -1;
    }
    if (context->type_ownership[type_id] == UINT8_MAX) {
        return -1;
    }
    if (context->type_ownership[type_id] != 0) {
        return context->type_ownership[type_id] - 1;
    }
    context->type_ownership[type_id] = UINT8_MAX;
    kind = ribos_verifier_u16(row + 4);
    if (kind == RIBOS_BC_TYPE_NAMED) {
        const RibosSchemaType *type =
            ribos_verifier_schema_type(context, type_id);

        if (type == NULL) {
            return -1;
        }
        ownership = type->ownership;
    } else if (kind == RIBOS_BC_TYPE_ARRAY ||
        kind == RIBOS_BC_TYPE_LIST ||
        kind == RIBOS_BC_TYPE_OPTION) {
        ownership = ribos_verifier_type_ownership(
            context,
            ribos_verifier_u32(row + 8));
    } else if (kind == RIBOS_BC_TYPE_FROZEN_MAP ||
        kind == RIBOS_BC_TYPE_DICT ||
        kind == RIBOS_BC_TYPE_RESULT) {
        int left = ribos_verifier_type_ownership(
            context,
            ribos_verifier_u32(row + 8));
        int right = ribos_verifier_type_ownership(
            context,
            ribos_verifier_u32(row + 12));

        ownership = left > right ? left : right;
    } else if (kind == RIBOS_BC_TYPE_STRUCT ||
        kind == RIBOS_BC_TYPE_ENUM) {
        uint32_t start = ribos_verifier_u32(row + 20);
        uint32_t count = ribos_verifier_u32(row + 24);
        uint32_t index;

        for (index = 0; index < count; ++index) {
            const uint8_t *shape =
                ribos_verifier_row(context->shapes, start + index);
            uint32_t value_type = ribos_verifier_u32(shape + 20);
            int nested;

            if (value_type == RIBOS_ARTIFACT_INVALID_ID) {
                continue;
            }
            nested = ribos_verifier_type_ownership(
                context,
                value_type);
            if (nested > ownership) {
                ownership = nested;
            }
        }
    }
    if (ownership < RIBOS_SCHEMA_OWNERSHIP_COPY ||
        ownership > RIBOS_SCHEMA_OWNERSHIP_LINEAR) {
        return -1;
    }
    context->type_ownership[type_id] = (uint8_t)ownership + 1;
    return ownership;
}

static const RibosSchemaHelper *
ribos_verifier_schema_helper(
    const RibosProductSchema *schema,
    uint32_t stable_id)
{
    size_t index;

    if (schema == NULL) {
        return NULL;
    }
    for (index = 0; index < schema->helper_count; ++index) {
        if (schema->helpers[index].stable_id == stable_id) {
            return &schema->helpers[index];
        }
    }
    return NULL;
}

static int
ribos_verifier_helper_result_type(
    RibosVerifierContext *context,
    uint32_t type_id,
    const RibosSchemaHelper *helper)
{
    const uint8_t *row;

    if (helper == NULL) {
        return 0;
    }
    if (helper->error_type == NULL) {
        return ribos_verifier_type_matches_spelling(
            context,
            type_id,
            helper->result_type);
    }
    row = ribos_verifier_row(context->types, type_id);
    return row != NULL &&
        ribos_verifier_u16(row + 4) == RIBOS_BC_TYPE_RESULT &&
        ribos_verifier_type_matches_spelling(
            context,
            ribos_verifier_u32(row + 8),
            helper->result_type) &&
        ribos_verifier_type_matches_spelling(
            context,
            ribos_verifier_u32(row + 12),
            helper->error_type);
}

static int
ribos_verifier_constant_slice(
    RibosVerifierContext *context,
    uint32_t constant_id,
    const uint8_t **bytes,
    size_t *length)
{
    const uint8_t *row =
        ribos_verifier_row(context->constants, constant_id);
    uint32_t offset;
    uint32_t count;

    if (row == NULL || bytes == NULL || length == NULL) {
        return 0;
    }
    offset = ribos_verifier_u32(row + 8);
    count = ribos_verifier_u32(row + 12);
    if (!ribos_verifier_range_u32(
            offset,
            count,
            context->constant_bytes->count)) {
        return 0;
    }
    *bytes = context->constant_bytes->bytes + offset;
    *length = count;
    return 1;
}

static RibosVerifierStatus
ribos_verifier_validate_source_maps(RibosVerifierContext *context)
{
    uint32_t index;

    for (index = 0; index < context->source_maps->count; ++index) {
        const uint8_t *row =
            ribos_verifier_row(context->source_maps, index);
        uint64_t start = ribos_verifier_u64(row + 8);
        uint64_t end = ribos_verifier_u64(row + 16);

        if (ribos_verifier_u32(row) != index ||
            start > end ||
            ribos_verifier_u32(row + 24) == 0 ||
            ribos_verifier_u32(row + 28) == 0 ||
            ribos_verifier_u32(row + 32) == 0 ||
            ribos_verifier_u32(row + 36) == 0) {
            return ribos_verifier_fail(
                context,
                RIBOS_VERIFIER_STRUCTURAL_ERROR,
                RIBOS_VERIFIER_SUBJECT_ARTIFACT,
                index,
                RIBOS_ARTIFACT_SECTION_SOURCE_MAPS);
        }
    }
    return RIBOS_VERIFIER_OK;
}

static int
ribos_verifier_source_reference(
    RibosVerifierContext *context,
    uint32_t source_id)
{
    if ((context->artifact.payload_flags &
            RIBOS_ARTIFACT_HAS_SOURCE_MAP) != 0) {
        return source_id < context->source_maps->count;
    }
    return source_id == RIBOS_ARTIFACT_INVALID_ID;
}

static RibosVerifierStatus
ribos_verifier_validate_types(RibosVerifierContext *context)
{
    uint32_t index;

    for (index = 0; index < context->shapes->count; ++index) {
        const uint8_t *row = ribos_verifier_row(context->shapes, index);
        uint32_t kind = ribos_verifier_u32(row + 4);
        uint32_t owner = ribos_verifier_u32(row + 8);
        uint32_t value = ribos_verifier_u32(row + 20);
        const uint8_t *owner_row =
            ribos_verifier_row(context->types, owner);
        uint32_t owner_kind = owner_row == NULL ?
            UINT32_MAX : ribos_verifier_u16(owner_row + 4);
        uint32_t owner_start = owner_row == NULL ? 0 :
            ribos_verifier_u32(owner_row + 20);
        uint32_t owner_count = owner_row == NULL ? 0 :
            ribos_verifier_u32(owner_row + 24);

        if (ribos_verifier_u32(row) != index ||
            kind > RIBOS_BC_SHAPE_ENUM_PAYLOAD ||
            owner >= context->types->count ||
            index < owner_start ||
            index - owner_start >= owner_count ||
            ((owner_kind == RIBOS_BC_TYPE_STRUCT) !=
                (kind == RIBOS_BC_SHAPE_STRUCT_FIELD)) ||
            (owner_kind != RIBOS_BC_TYPE_STRUCT &&
                owner_kind != RIBOS_BC_TYPE_ENUM) ||
            !ribos_verifier_zero(row + 24, 8) ||
            ((kind == RIBOS_BC_SHAPE_ENUM_VARIANT) !=
                (value == RIBOS_ARTIFACT_INVALID_ID)) ||
            (kind != RIBOS_BC_SHAPE_ENUM_VARIANT &&
                value >= context->types->count)) {
            return ribos_verifier_fail(
                context,
                RIBOS_VERIFIER_INVALID_TYPE,
                RIBOS_VERIFIER_SUBJECT_SHAPE,
                index,
                kind);
        }
    }
    for (index = 0; index < context->types->count; ++index) {
        const uint8_t *row = ribos_verifier_row(context->types, index);
        uint32_t kind = ribos_verifier_u16(row + 4);
        uint32_t name_length = ribos_verifier_u32(row + 56);
        uint32_t shape_start = ribos_verifier_u32(row + 20);
        uint32_t shape_count = ribos_verifier_u32(row + 24);
        uint32_t first = ribos_verifier_u32(row + 8);
        uint32_t second = ribos_verifier_u32(row + 12);
        uint32_t bound = ribos_verifier_u32(row + 16);
        uint32_t abi_size = ribos_verifier_u32(row + 28);
        uint32_t abi_alignment = ribos_verifier_u32(row + 32);
        uint32_t bits = ribos_verifier_u16(row + 6);
        const RibosVerifierTypeLayout *layout;
        int kind_fields_valid = 0;

        switch (kind) {
        case RIBOS_BC_TYPE_ERROR:
        case RIBOS_BC_TYPE_UNKNOWN:
        case RIBOS_BC_TYPE_UNIT:
            kind_fields_valid = bits == 0 && first == 0 &&
                second == 0 && bound == 0 && shape_count == 0 &&
                abi_size == 0 && abi_alignment == 0;
            break;
        case RIBOS_BC_TYPE_BOOL:
            kind_fields_valid = bits == 1 && first == 0 &&
                second == 0 && bound == 0 && shape_count == 0 &&
                abi_size == 0 && abi_alignment == 0;
            break;
        case RIBOS_BC_TYPE_UNSIGNED:
        case RIBOS_BC_TYPE_SIGNED:
            kind_fields_valid =
                (bits == 8 || bits == 16 ||
                 bits == 32 || bits == 64) &&
                first == 0 && second == 0 && bound == 0 &&
                shape_count == 0 && abi_size == 0 &&
                abi_alignment == 0;
            break;
        case RIBOS_BC_TYPE_STRING_LITERAL:
            kind_fields_valid = bits == 0 && first == 0 &&
                second == 0 && shape_count == 0 &&
                abi_size == 0 && abi_alignment == 0;
            break;
        case RIBOS_BC_TYPE_NAMED: {
            const RibosSchemaType *schema_type =
                ribos_schema_find_type(
                    context->schema,
                    (const char *)(row + 60),
                    name_length);
            uint32_t expected_size =
                schema_type != NULL &&
                schema_type->type_class == RIBOS_SCHEMA_TYPE_ENUM ?
                    4 : 8;
            uint32_t expected_alignment = expected_size;

            kind_fields_valid = bits == 0 && first == 0 &&
                second == 0 && bound == 0 && shape_count == 0 &&
                name_length != 0 && schema_type != NULL &&
                abi_size == expected_size &&
                abi_alignment == expected_alignment;
            break;
        }
        case RIBOS_BC_TYPE_ARRAY:
        case RIBOS_BC_TYPE_LIST:
            kind_fields_valid = bits == 0 &&
                first < context->types->count &&
                second == 0 && bound != 0 && shape_count == 0 &&
                abi_size == 0 && abi_alignment == 0 &&
                name_length == 0;
            break;
        case RIBOS_BC_TYPE_FROZEN_MAP:
        case RIBOS_BC_TYPE_DICT:
            kind_fields_valid = bits == 0 &&
                first < context->types->count &&
                second < context->types->count &&
                bound != 0 && shape_count == 0 &&
                abi_size == 0 && abi_alignment == 0 &&
                name_length == 0;
            break;
        case RIBOS_BC_TYPE_OPTION:
            kind_fields_valid = bits == 0 &&
                first < context->types->count &&
                second == 0 && bound == 0 && shape_count == 0 &&
                abi_size == 0 && abi_alignment == 0 &&
                name_length == 0;
            break;
        case RIBOS_BC_TYPE_RESULT:
            kind_fields_valid = bits == 0 &&
                first < context->types->count &&
                second < context->types->count &&
                bound == 0 && shape_count == 0 &&
                abi_size == 0 && abi_alignment == 0 &&
                name_length == 0;
            break;
        case RIBOS_BC_TYPE_STRUCT:
        case RIBOS_BC_TYPE_ENUM:
            kind_fields_valid = bits == 0 && first == 0 &&
                second == 0 && bound == 0 &&
                abi_size == 0 && abi_alignment == 0 &&
                name_length != 0;
            break;
        default:
            break;
        }

        if (ribos_verifier_u32(row) != index ||
            kind > RIBOS_BC_TYPE_ENUM ||
            name_length > 63 ||
            !ribos_verifier_zero(
                row + 60 + name_length,
                64 - name_length) ||
            !ribos_verifier_zero(row + 124, 4) ||
            !ribos_verifier_range_u32(
                shape_start,
                shape_count,
                context->shapes->count) ||
            !kind_fields_valid ||
            !ribos_verifier_type_layout(context, index)) {
            return ribos_verifier_fail(
                context,
                RIBOS_VERIFIER_INVALID_TYPE,
                RIBOS_VERIFIER_SUBJECT_TYPE,
                index,
                kind);
        }
        layout = &context->type_layouts[index];
        if (ribos_verifier_u32(row + 36) != layout->storage_kind ||
            ribos_verifier_u32(row + 40) != layout->byte_size ||
            ribos_verifier_u32(row + 44) != layout->element_stride ||
            ribos_verifier_u32(row + 48) != layout->payload_offset ||
            ribos_verifier_u32(row + 52) != layout->capacity) {
            return ribos_verifier_fail(
                context,
                RIBOS_VERIFIER_INVALID_TYPE,
                RIBOS_VERIFIER_SUBJECT_TYPE,
                index,
                ribos_verifier_u32(row + 40));
        }
    }
    context->report->verified_type_count = context->types->count;
    return RIBOS_VERIFIER_OK;
}

static RibosVerifierStatus
ribos_verifier_validate_constants(RibosVerifierContext *context)
{
    uint32_t index;

    for (index = 0; index < context->constants->count; ++index) {
        const uint8_t *row =
            ribos_verifier_row(context->constants, index);
        const uint8_t *bytes;
        size_t length;
        uint32_t kind = ribos_verifier_u16(row + 4);

        if (ribos_verifier_u32(row) != index ||
            kind > RIBOS_BC_CONSTANT_SYMBOL ||
            ribos_verifier_u16(row + 6) != 0 ||
            !ribos_verifier_zero(row + 24, 8) ||
            !ribos_verifier_constant_slice(
                context,
                index,
                &bytes,
                &length) ||
            ribos_verifier_fnv1a64(bytes, length) !=
                ribos_verifier_u64(row + 16)) {
            return ribos_verifier_fail(
                context,
                RIBOS_VERIFIER_INVALID_CONSTANT,
                RIBOS_VERIFIER_SUBJECT_CONSTANT,
                index,
                kind);
        }
    }
    return RIBOS_VERIFIER_OK;
}

static int
ribos_verifier_block_in_function(
    const uint8_t *function,
    uint32_t block_id)
{
    uint32_t first = ribos_verifier_u32(function + 16);
    uint32_t count = ribos_verifier_u32(function + 20);

    return block_id >= first && block_id - first < count;
}

static int
ribos_verifier_slot_in_function(
    const uint8_t *function,
    uint32_t slot_id)
{
    uint32_t first = ribos_verifier_u32(function + 24);
    uint32_t count = ribos_verifier_u32(function + 28);

    return slot_id >= first && slot_id - first < count;
}

static RibosVerifierStatus
ribos_verifier_validate_functions_and_frames(
    RibosVerifierContext *context)
{
    uint32_t expected_block = 0;
    uint32_t expected_slot = 0;
    uint32_t policy_function = RIBOS_ARTIFACT_INVALID_ID;
    uint32_t function_id;

    for (function_id = 0;
         function_id < context->functions->count;
         ++function_id) {
        const uint8_t *function =
            ribos_verifier_row(context->functions, function_id);
        uint32_t flags = ribos_verifier_u32(function + 4);
        uint32_t return_type = ribos_verifier_u32(function + 8);
        uint32_t entry_block = ribos_verifier_u32(function + 12);
        uint32_t first_block = ribos_verifier_u32(function + 16);
        uint32_t block_count = ribos_verifier_u32(function + 20);
        uint32_t first_slot = ribos_verifier_u32(function + 24);
        uint32_t slot_count = ribos_verifier_u32(function + 28);
        uint32_t parameter_start = ribos_verifier_u32(function + 32);
        uint32_t parameter_count = ribos_verifier_u32(function + 36);
        uint32_t frame_offset = 0;
        uint32_t frame_alignment =
            RIBOS_BYTECODE_FRAME_ALIGNMENT_V1;
        uint32_t local;

        if (ribos_verifier_u32(function) != function_id ||
            (flags & ~(RIBOS_BC_FUNCTION_POLICY |
                RIBOS_BC_FUNCTION_PURE)) != 0 ||
            flags == (RIBOS_BC_FUNCTION_POLICY |
                RIBOS_BC_FUNCTION_PURE) ||
            return_type >= context->types->count ||
            first_block != expected_block ||
            first_slot != expected_slot ||
            block_count == 0 ||
            !ribos_verifier_range_u32(
                first_block,
                block_count,
                context->blocks->count) ||
            !ribos_verifier_range_u32(
                first_slot,
                slot_count,
                context->slots->count) ||
            !ribos_verifier_range_u32(
                parameter_start,
                parameter_count,
                context->slots->count) ||
            (parameter_count != 0 &&
                (!ribos_verifier_slot_in_function(
                    function,
                    parameter_start) ||
                 parameter_start != first_slot)) ||
            !ribos_verifier_block_in_function(function, entry_block) ||
            (ribos_verifier_u32(function + 44) &
                ~ribos_verifier_u32(function + 40)) != 0 ||
            !ribos_verifier_zero(function + 100, 4)) {
            return ribos_verifier_fail(
                context,
                RIBOS_VERIFIER_INVALID_FUNCTION,
                RIBOS_VERIFIER_SUBJECT_FUNCTION,
                function_id,
                flags);
        }
        if ((flags & RIBOS_BC_FUNCTION_POLICY) != 0) {
            if (policy_function != RIBOS_ARTIFACT_INVALID_ID) {
                return ribos_verifier_fail(
                    context,
                    RIBOS_VERIFIER_INVALID_FUNCTION,
                    RIBOS_VERIFIER_SUBJECT_FUNCTION,
                    function_id,
                    RIBOS_BC_FUNCTION_POLICY);
            }
            policy_function = function_id;
        }
        for (local = 0; local < slot_count; ++local) {
            uint32_t slot_id = first_slot + local;
            const uint8_t *slot =
                ribos_verifier_row(context->slots, slot_id);
            uint32_t type_id = ribos_verifier_u32(slot + 8);
            const uint8_t *type =
                ribos_verifier_row(context->types, type_id);
            const RibosVerifierTypeLayout *layout;
            uint32_t flags_value = ribos_verifier_u32(slot + 28);

            if (ribos_verifier_u32(slot) != slot_id ||
                ribos_verifier_u32(slot + 4) != function_id ||
                type_id >= context->types->count ||
                ribos_verifier_u16(type + 4) <=
                    RIBOS_BC_TYPE_UNKNOWN ||
                (flags_value & ~(RIBOS_BC_SLOT_PARAMETER |
                    RIBOS_BC_SLOT_BINDING |
                    RIBOS_BC_SLOT_MUTABLE)) != 0 ||
                !ribos_verifier_source_reference(
                    context,
                    ribos_verifier_u32(slot + 24))) {
                return ribos_verifier_fail(
                    context,
                    RIBOS_VERIFIER_INVALID_SLOT,
                    RIBOS_VERIFIER_SUBJECT_SLOT,
                    slot_id,
                    type_id);
            }
            if (local < parameter_count) {
                if ((flags_value & RIBOS_BC_SLOT_PARAMETER) == 0) {
                    return ribos_verifier_fail(
                        context,
                        RIBOS_VERIFIER_INVALID_SLOT,
                        RIBOS_VERIFIER_SUBJECT_SLOT,
                        slot_id,
                        RIBOS_BC_SLOT_PARAMETER);
                }
            } else if ((flags_value & RIBOS_BC_SLOT_PARAMETER) != 0) {
                return ribos_verifier_fail(
                    context,
                    RIBOS_VERIFIER_INVALID_SLOT,
                    RIBOS_VERIFIER_SUBJECT_SLOT,
                    slot_id,
                    RIBOS_BC_SLOT_PARAMETER);
            }
            layout = &context->type_layouts[type_id];
            if (!ribos_verifier_align_u32(
                    frame_offset,
                    layout->alignment,
                    &frame_offset) ||
                ribos_verifier_u32(slot + 12) != frame_offset ||
                ribos_verifier_u32(slot + 16) != layout->byte_size ||
                ribos_verifier_u32(slot + 20) != layout->alignment ||
                !ribos_verifier_add_u32(
                    frame_offset,
                    layout->byte_size,
                    &frame_offset)) {
                return ribos_verifier_fail(
                    context,
                    RIBOS_VERIFIER_FRAME_MISMATCH,
                    RIBOS_VERIFIER_SUBJECT_SLOT,
                    slot_id,
                    frame_offset);
            }
            if (layout->alignment > frame_alignment) {
                frame_alignment = layout->alignment;
            }
        }
        if (!ribos_verifier_align_u32(
                frame_offset,
                frame_alignment,
                &frame_offset) ||
            frame_offset > RIBOS_VERIFIER_MAX_FRAME_BYTES ||
            ribos_verifier_u32(function + 88) != frame_offset) {
            return ribos_verifier_fail(
                context,
                RIBOS_VERIFIER_FRAME_MISMATCH,
                RIBOS_VERIFIER_SUBJECT_FUNCTION,
                function_id,
                frame_offset);
        }
        context->frame_bytes[function_id] = frame_offset;
        expected_block += block_count;
        expected_slot += slot_count;
    }
    if (expected_block != context->blocks->count ||
        expected_slot != context->slots->count ||
        policy_function == RIBOS_ARTIFACT_INVALID_ID ||
        policy_function != context->artifact.entry_function) {
        return ribos_verifier_fail(
            context,
            RIBOS_VERIFIER_INVALID_FUNCTION,
            RIBOS_VERIFIER_SUBJECT_FUNCTION,
            context->artifact.entry_function,
            policy_function);
    }
    context->report->verified_function_count =
        context->functions->count;
    return RIBOS_VERIFIER_OK;
}

static int
ribos_verifier_terminal_opcode(uint32_t opcode)
{
    return opcode == RIBOS_BC_JUMP ||
        opcode == RIBOS_BC_BRANCH ||
        opcode == RIBOS_BC_RETURN ||
        opcode == RIBOS_BC_TRAP;
}

static RibosVerifierStatus
ribos_verifier_validate_blocks_and_instruction_shape(
    RibosVerifierContext *context)
{
    uint32_t block_id;

    memset(
        context->instruction_seen,
        0,
        context->instructions->count);
    for (block_id = 0; block_id < context->blocks->count; ++block_id) {
        const uint8_t *block =
            ribos_verifier_row(context->blocks, block_id);
        uint32_t function_id = ribos_verifier_u32(block + 4);
        uint32_t first_instruction = ribos_verifier_u32(block + 8);
        uint32_t last_instruction = ribos_verifier_u32(block + 12);
        uint32_t instruction_count = ribos_verifier_u32(block + 16);
        uint32_t flags = ribos_verifier_u32(block + 28);
        const uint8_t *function =
            ribos_verifier_row(context->functions, function_id);
        uint32_t instruction_id = first_instruction;
        uint32_t ordinal;

        if (ribos_verifier_u32(block) != block_id ||
            function == NULL ||
            !ribos_verifier_block_in_function(function, block_id) ||
            instruction_count == 0 ||
            first_instruction >= context->instructions->count ||
            last_instruction >= context->instructions->count ||
            (flags & ~RIBOS_BC_BLOCK_ENTRY) != 0 ||
            ((flags & RIBOS_BC_BLOCK_ENTRY) != 0) !=
                (ribos_verifier_u32(function + 12) == block_id) ||
            ribos_verifier_u32(block + 20) != 0 ||
            ribos_verifier_u32(block + 24) != 0) {
            return ribos_verifier_fail(
                context,
                RIBOS_VERIFIER_INVALID_BLOCK,
                RIBOS_VERIFIER_SUBJECT_BLOCK,
                block_id,
                instruction_count);
        }
        for (ordinal = 0; ordinal < instruction_count; ++ordinal) {
            const uint8_t *instruction =
                ribos_verifier_row(context->instructions, instruction_id);
            uint32_t opcode;
            uint32_t next;
            uint32_t operand_start;
            uint32_t operand_count;

            if (instruction == NULL ||
                context->instruction_seen[instruction_id] != 0) {
                return ribos_verifier_fail(
                    context,
                    RIBOS_VERIFIER_INVALID_INSTRUCTION,
                    RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
                    instruction_id,
                    block_id);
            }
            context->instruction_seen[instruction_id] = 1;
            opcode = instruction[0];
            next = ribos_verifier_u32(instruction + 32);
            operand_count = ribos_verifier_u16(instruction + 2);
            operand_start = ribos_verifier_u32(instruction + 16);
            if (instruction[1] != 0 ||
                ribos_verifier_u32(instruction + 4) != instruction_id ||
                ribos_verifier_u32(instruction + 8) != block_id ||
                opcode < RIBOS_BC_PARAMETER ||
                opcode > RIBOS_BC_TRAP ||
                !ribos_verifier_range_u32(
                    operand_start,
                    operand_count,
                    context->operands->count) ||
                !ribos_verifier_zero(instruction + 36, 4) ||
                !ribos_verifier_source_reference(
                    context,
                    ribos_verifier_u32(instruction + 28))) {
                return ribos_verifier_fail(
                    context,
                    RIBOS_VERIFIER_INVALID_INSTRUCTION,
                    RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
                    instruction_id,
                    opcode);
            }
            if (ordinal + 1 == instruction_count) {
                if (instruction_id != last_instruction ||
                    !ribos_verifier_terminal_opcode(opcode) ||
                    next != RIBOS_ARTIFACT_INVALID_ID) {
                    return ribos_verifier_fail(
                        context,
                        RIBOS_VERIFIER_INVALID_BLOCK,
                        RIBOS_VERIFIER_SUBJECT_BLOCK,
                        block_id,
                        instruction_id);
                }
            } else {
                if (ribos_verifier_terminal_opcode(opcode) ||
                    next >= context->instructions->count ||
                    ribos_verifier_u32(
                        ribos_verifier_row(
                            context->instructions,
                            next) + 8) != block_id) {
                    return ribos_verifier_fail(
                        context,
                        RIBOS_VERIFIER_INVALID_INSTRUCTION,
                        RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
                        instruction_id,
                        next);
                }
                instruction_id = next;
            }
        }
    }
    for (block_id = 0;
         block_id < context->instructions->count;
         ++block_id) {
        if (context->instruction_seen[block_id] == 0) {
            return ribos_verifier_fail(
                context,
                RIBOS_VERIFIER_INVALID_INSTRUCTION,
                RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
                block_id,
                0);
        }
    }
    context->report->verified_block_count = context->blocks->count;
    context->report->verified_instruction_count =
        context->instructions->count;
    return RIBOS_VERIFIER_OK;
}

static int
ribos_verifier_bool_type(
    RibosVerifierContext *context,
    uint32_t type_id)
{
    const uint8_t *row = ribos_verifier_row(context->types, type_id);

    return row != NULL &&
        ribos_verifier_u16(row + 4) == RIBOS_BC_TYPE_BOOL;
}

static int
ribos_verifier_u32_type(
    RibosVerifierContext *context,
    uint32_t type_id)
{
    const uint8_t *row = ribos_verifier_row(context->types, type_id);

    return row != NULL &&
        ribos_verifier_u16(row + 4) == RIBOS_BC_TYPE_UNSIGNED &&
        ribos_verifier_u16(row + 6) == 32;
}

static int
ribos_verifier_member_path_type(
    RibosVerifierContext *context,
    uint32_t owner_type,
    const uint8_t *path,
    size_t path_length,
    uint32_t result_type)
{
    const char *owner;
    size_t owner_length;
    size_t cursor = 0;
    int skipped_root = 0;

    if (!ribos_verifier_type_name(
            context,
            owner_type,
            &owner,
            &owner_length)) {
        return 0;
    }
    while (cursor < path_length) {
        size_t start = cursor;
        const RibosSchemaMember *member;

        while (cursor < path_length && path[cursor] != '.') {
            ++cursor;
        }
        if (!skipped_root) {
            skipped_root = 1;
        } else {
            member = ribos_schema_find_member(
                context->schema,
                owner,
                owner_length,
                (const char *)path + start,
                cursor - start);
            if (member == NULL) {
                return 0;
            }
            if (member->result_type == NULL) {
                const uint8_t *result_row =
                    ribos_verifier_row(context->types, result_type);
                uint32_t kind = result_row == NULL ?
                    UINT32_MAX : ribos_verifier_u16(result_row + 4);

                return cursor == path_length &&
                    (kind == RIBOS_BC_TYPE_NAMED ||
                     ((kind == RIBOS_BC_TYPE_ARRAY ||
                       kind == RIBOS_BC_TYPE_LIST) &&
                      ribos_verifier_type_matches_spelling(
                          context,
                          ribos_verifier_u32(result_row + 8),
                          member->collection_element_type) &&
                      ribos_verifier_u32(result_row + 16) ==
                          member->collection_bound));
            }
            owner = member->result_type;
            owner_length = strlen(owner);
        }
        if (cursor < path_length) {
            ++cursor;
        }
    }
    return skipped_root &&
        ribos_verifier_spelling_equal(
            owner,
            owner_length,
            (const char *)(
                ribos_verifier_row(context->types, result_type) + 60)) &&
        owner_length ==
            ribos_verifier_u32(
                ribos_verifier_row(context->types, result_type) + 56);
}

static int
ribos_verifier_struct_member_type(
    RibosVerifierContext *context,
    uint32_t owner_type,
    uint32_t result_type,
    uint32_t ordinal)
{
    const uint8_t *type = ribos_verifier_row(context->types, owner_type);
    uint32_t start;
    uint32_t count;

    if (type == NULL ||
        ribos_verifier_u16(type + 4) != RIBOS_BC_TYPE_STRUCT) {
        return 0;
    }
    start = ribos_verifier_u32(type + 20);
    count = ribos_verifier_u32(type + 24);
    if (ordinal >= count) {
        return 0;
    }
    return ribos_verifier_u32(
            ribos_verifier_row(
                context->shapes,
                start + ordinal) + 4) ==
            RIBOS_BC_SHAPE_STRUCT_FIELD &&
        ribos_verifier_u32(
            ribos_verifier_row(
                context->shapes,
                start + ordinal) + 20) == result_type;
}

static int
ribos_verifier_variant_signature(
    RibosVerifierContext *context,
    uint32_t sum_type,
    uint32_t tag,
    uint32_t operand_count,
    uint32_t instruction_id,
    uint32_t result_payload_type,
    int extracting)
{
    const uint8_t *type = ribos_verifier_row(context->types, sum_type);
    uint32_t kind;

    if (type == NULL) {
        return 0;
    }
    kind = ribos_verifier_u16(type + 4);
    if (kind == RIBOS_BC_TYPE_OPTION) {
        uint32_t alternate = extracting ?
            ribos_verifier_u32(
                ribos_verifier_row(
                    context->instructions,
                    instruction_id) + 24) :
            RIBOS_ARTIFACT_INVALID_ID;

        if (extracting &&
            alternate != RIBOS_ARTIFACT_INVALID_ID &&
            alternate != 0) {
            return 0;
        }
        if (tag == 0) {
            return extracting ?
                result_payload_type == ribos_verifier_u32(type + 8) :
                operand_count == 1 &&
                ribos_verifier_slot_type(
                    context,
                    ribos_verifier_operand_slot(
                        context,
                        instruction_id,
                        0)) == ribos_verifier_u32(type + 8);
        }
        return tag == 1 && !extracting && operand_count == 0;
    }
    if (kind == RIBOS_BC_TYPE_RESULT) {
        uint32_t expected;
        uint32_t alternate = extracting ?
            ribos_verifier_u32(
                ribos_verifier_row(
                    context->instructions,
                    instruction_id) + 24) :
            RIBOS_ARTIFACT_INVALID_ID;

        if (tag > 1 ||
            (extracting &&
             alternate != RIBOS_ARTIFACT_INVALID_ID &&
             alternate != 0)) {
            return 0;
        }
        expected = ribos_verifier_u32(type + (tag == 0 ? 8 : 12));
        return extracting ? result_payload_type == expected :
            operand_count == 1 &&
            ribos_verifier_slot_type(
                context,
                ribos_verifier_operand_slot(
                    context,
                    instruction_id,
                    0)) == expected;
    }
    if (kind == RIBOS_BC_TYPE_ENUM) {
        uint32_t start = ribos_verifier_u32(type + 20);
        uint32_t count = ribos_verifier_u32(type + 24);
        uint32_t index;
        uint32_t payload_count = 0;
        uint32_t requested_ordinal = extracting ?
            ribos_verifier_u32(
                ribos_verifier_row(
                    context->instructions,
                    instruction_id) + 24) :
            RIBOS_ARTIFACT_INVALID_ID;

        for (index = 0; index < count; ++index) {
            const uint8_t *shape =
                ribos_verifier_row(context->shapes, start + index);

            if (ribos_verifier_u32(shape + 4) ==
                    RIBOS_BC_SHAPE_ENUM_PAYLOAD &&
                ribos_verifier_u32(shape + 12) == tag) {
                uint32_t expected_type =
                    ribos_verifier_u32(shape + 20);

                if (extracting) {
                    if (requested_ordinal !=
                            RIBOS_ARTIFACT_INVALID_ID &&
                        ribos_verifier_u32(shape + 16) ==
                            requested_ordinal) {
                        return result_payload_type == expected_type;
                    }
                } else if (payload_count >= operand_count ||
                    ribos_verifier_slot_type(
                        context,
                        ribos_verifier_operand_slot(
                            context,
                            instruction_id,
                            payload_count)) != expected_type) {
                    return 0;
                }
                ++payload_count;
            }
        }
        return extracting ? 0 : payload_count == operand_count;
    }
    return 0;
}

static int
ribos_verifier_collection_element_type(
    RibosVerifierContext *context,
    uint32_t collection_type,
    uint32_t *element_type)
{
    const uint8_t *row =
        ribos_verifier_row(context->types, collection_type);
    uint32_t kind;

    if (row == NULL || element_type == NULL) {
        return 0;
    }
    kind = ribos_verifier_u16(row + 4);
    if (kind != RIBOS_BC_TYPE_ARRAY && kind != RIBOS_BC_TYPE_LIST) {
        return 0;
    }
    *element_type = ribos_verifier_u32(row + 8);
    return 1;
}

static int
ribos_verifier_import_exists(
    RibosVerifierContext *context,
    uint32_t helper_id)
{
    uint32_t index;

    for (index = 0; index < context->helper_imports->count; ++index) {
        const uint8_t *row =
            ribos_verifier_row(context->helper_imports, index);

        if (ribos_verifier_u32(row) == helper_id) {
            return 1;
        }
    }
    return 0;
}

static RibosVerifierStatus
ribos_verifier_instruction_type_error(
    RibosVerifierContext *context,
    uint32_t instruction_id,
    uint32_t detail)
{
    return ribos_verifier_fail(
        context,
        RIBOS_VERIFIER_TYPE_MISMATCH,
        RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
        instruction_id,
        detail);
}

static RibosVerifierStatus
ribos_verifier_validate_instruction_semantics(
    RibosVerifierContext *context,
    uint32_t instruction_id,
    uint32_t function_id)
{
    const uint8_t *instruction =
        ribos_verifier_row(context->instructions, instruction_id);
    const uint8_t *function =
        ribos_verifier_row(context->functions, function_id);
    uint32_t opcode = instruction[0];
    uint32_t count = ribos_verifier_u16(instruction + 2);
    uint32_t result = ribos_verifier_u32(instruction + 12);
    uint32_t target = ribos_verifier_u32(instruction + 20);
    uint32_t alternate = ribos_verifier_u32(instruction + 24);
    uint64_t immediate = ribos_verifier_u64(instruction + 40);
    uint32_t result_type = result == RIBOS_ARTIFACT_INVALID_ID ?
        RIBOS_ARTIFACT_INVALID_ID :
        ribos_verifier_slot_type(context, result);
    uint32_t left_slot = count == 0 ? RIBOS_ARTIFACT_INVALID_ID :
        ribos_verifier_operand_slot(context, instruction_id, 0);
    uint32_t left_type =
        ribos_verifier_slot_type(context, left_slot);
    uint32_t right_slot = count < 2 ? RIBOS_ARTIFACT_INVALID_ID :
        ribos_verifier_operand_slot(context, instruction_id, 1);
    uint32_t right_type =
        ribos_verifier_slot_type(context, right_slot);
    uint32_t local;

    if ((opcode != RIBOS_BC_BRANCH &&
            opcode != RIBOS_BC_VARIANT_PAYLOAD &&
            opcode != RIBOS_BC_MEMBER &&
            alternate != RIBOS_ARTIFACT_INVALID_ID) ||
        (opcode != RIBOS_BC_CONST_BOOL &&
            opcode != RIBOS_BC_CONST_INTEGER &&
            opcode != RIBOS_BC_BRANCH &&
            immediate != 0) ||
        ((opcode == RIBOS_BC_CONST_UNIT ||
          opcode == RIBOS_BC_CONST_BOOL ||
          opcode == RIBOS_BC_CONST_INTEGER ||
          opcode == RIBOS_BC_MOVE ||
          opcode == RIBOS_BC_VARIANT_TAG ||
          opcode == RIBOS_BC_RETURN) &&
         target != RIBOS_ARTIFACT_INVALID_ID)) {
        return ribos_verifier_fail(
            context,
            RIBOS_VERIFIER_INVALID_INSTRUCTION,
            RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
            instruction_id,
            target);
    }
    if ((result != RIBOS_ARTIFACT_INVALID_ID &&
            !ribos_verifier_slot_in_function(function, result)) ||
        (result == RIBOS_ARTIFACT_INVALID_ID) !=
            ribos_verifier_terminal_opcode(opcode)) {
        return ribos_verifier_instruction_type_error(
            context,
            instruction_id,
            result);
    }
    for (local = 0; local < count; ++local) {
        uint32_t operand =
            ribos_verifier_operand_slot(context, instruction_id, local);

        if (!ribos_verifier_slot_in_function(function, operand)) {
            return ribos_verifier_fail(
                context,
                RIBOS_VERIFIER_INVALID_SLOT,
                RIBOS_VERIFIER_SUBJECT_OPERAND,
                instruction_id,
                operand);
        }
    }

    switch (opcode) {
    case RIBOS_BC_PARAMETER: {
        uint32_t parameter_count = ribos_verifier_u32(function + 36);
        uint32_t parameter_start = ribos_verifier_u32(function + 32);
        const uint8_t *slot = ribos_verifier_row(context->slots, result);

        if (count != 0 || target >= parameter_count ||
            result != parameter_start + target ||
            ribos_verifier_u32(instruction + 8) !=
                ribos_verifier_u32(function + 12) ||
            (ribos_verifier_u32(slot + 28) &
                RIBOS_BC_SLOT_PARAMETER) == 0) {
            return ribos_verifier_instruction_type_error(
                context,
                instruction_id,
                target);
        }
        break;
    }
    case RIBOS_BC_CONST_UNIT:
        if (count != 0 ||
            ribos_verifier_u16(
                ribos_verifier_row(context->types, result_type) + 4) !=
                RIBOS_BC_TYPE_UNIT) {
            return ribos_verifier_instruction_type_error(
                context,
                instruction_id,
                result_type);
        }
        break;
    case RIBOS_BC_CONST_BOOL:
        if (count != 0 || immediate > 1 ||
            !ribos_verifier_bool_type(context, result_type)) {
            return ribos_verifier_instruction_type_error(
                context,
                instruction_id,
                (uint32_t)immediate);
        }
        break;
    case RIBOS_BC_CONST_INTEGER:
        if (count != 0 ||
            !ribos_verifier_type_is_integer(context, result_type)) {
            return ribos_verifier_instruction_type_error(
                context,
                instruction_id,
                result_type);
        }
        {
            const uint8_t *type =
                ribos_verifier_row(context->types, result_type);
            uint32_t bits = ribos_verifier_u16(type + 6);
            uint32_t kind = ribos_verifier_u16(type + 4);
            uint64_t maximum =
                bits == 64 ? UINT64_MAX :
                ((UINT64_C(1) << bits) - 1);

            if (kind == RIBOS_BC_TYPE_SIGNED) {
                maximum >>= 1;
            }
            if (immediate > maximum) {
                return ribos_verifier_instruction_type_error(
                    context,
                    instruction_id,
                    result_type);
            }
        }
        break;
    case RIBOS_BC_CONST_STRING:
    case RIBOS_BC_CONST_SYMBOL: {
        const uint8_t *constant =
            ribos_verifier_row(context->constants, target);
        uint32_t expected = opcode == RIBOS_BC_CONST_STRING ?
            RIBOS_BC_CONSTANT_STRING : RIBOS_BC_CONSTANT_SYMBOL;

        if (count != 0 || constant == NULL ||
            ribos_verifier_u16(constant + 4) != expected ||
            (opcode == RIBOS_BC_CONST_STRING &&
             ribos_verifier_u16(
                ribos_verifier_row(context->types, result_type) + 4) !=
                RIBOS_BC_TYPE_STRING_LITERAL) ||
            (opcode == RIBOS_BC_CONST_SYMBOL &&
             ribos_verifier_u16(
                ribos_verifier_row(context->types, result_type) + 4) !=
                RIBOS_BC_TYPE_NAMED)) {
            return ribos_verifier_instruction_type_error(
                context,
                instruction_id,
                target);
        }
        break;
    }
    case RIBOS_BC_MOVE:
        if (count != 1 || result_type != left_type) {
            return ribos_verifier_instruction_type_error(
                context,
                instruction_id,
                left_type);
        }
        break;
    case RIBOS_BC_CHECKED_UNARY:
        if (count != 1 || result_type != left_type ||
            ((target == RIBOS_BC_CHECK_NOT) ?
                !ribos_verifier_bool_type(context, left_type) :
             (target == RIBOS_BC_CHECK_NEGATIVE) ?
                (ribos_verifier_u16(
                    ribos_verifier_row(
                        context->types,
                        left_type) + 4) != RIBOS_BC_TYPE_SIGNED) :
             (target == RIBOS_BC_CHECK_POSITIVE ||
              target == RIBOS_BC_CHECK_BIT_NOT) ?
                !ribos_verifier_type_is_integer(context, left_type) :
                1)) {
            return ribos_verifier_instruction_type_error(
                context,
                instruction_id,
                target);
        }
        break;
    case RIBOS_BC_CHECKED_BINARY:
        if (count != 2) {
            return ribos_verifier_instruction_type_error(
                context,
                instruction_id,
                count);
        }
        if (target == RIBOS_BC_CHECK_EQUAL ||
            target == RIBOS_BC_CHECK_NOT_EQUAL) {
            if (left_type != right_type ||
                !ribos_verifier_bool_type(context, result_type)) {
                return ribos_verifier_instruction_type_error(
                    context,
                    instruction_id,
                    target);
            }
        } else if (target >= RIBOS_BC_CHECK_LESS &&
            target <= RIBOS_BC_CHECK_GREATER_EQUAL) {
            if (left_type != right_type ||
                !ribos_verifier_type_is_ordered(
                    context,
                    left_type) ||
                !ribos_verifier_bool_type(context, result_type)) {
                return ribos_verifier_instruction_type_error(
                    context,
                    instruction_id,
                    target);
            }
        } else if (target == RIBOS_BC_CHECK_IN ||
            target == RIBOS_BC_CHECK_NOT_IN) {
            uint32_t element;
            const uint8_t *right =
                ribos_verifier_row(context->types, right_type);

            if (!ribos_verifier_bool_type(context, result_type) ||
                !((ribos_verifier_collection_element_type(
                        context,
                        right_type,
                        &element) &&
                    element == left_type) ||
                  (right != NULL &&
                   ribos_verifier_u16(right + 4) ==
                       RIBOS_BC_TYPE_NAMED))) {
                return ribos_verifier_instruction_type_error(
                    context,
                    instruction_id,
                    target);
            }
        } else if (target >= RIBOS_BC_CHECK_BIT_OR &&
            target <= RIBOS_BC_CHECK_REMAINDER) {
            if (!ribos_verifier_type_is_integer(context, left_type) ||
                !ribos_verifier_type_is_integer(context, right_type) ||
                left_type != right_type || result_type != left_type) {
                return ribos_verifier_instruction_type_error(
                    context,
                    instruction_id,
                    target);
            }
        } else {
            return ribos_verifier_instruction_type_error(
                context,
                instruction_id,
                target);
        }
        break;
    case RIBOS_BC_BUILD_LIST: {
        const uint8_t *type =
            ribos_verifier_row(context->types, result_type);
        uint32_t kind = type == NULL ?
            UINT32_MAX : ribos_verifier_u16(type + 4);
        uint32_t element = type == NULL ?
            RIBOS_ARTIFACT_INVALID_ID :
            ribos_verifier_u32(type + 8);
        uint32_t bound = type == NULL ? 0 :
            ribos_verifier_u32(type + 16);

        if ((kind != RIBOS_BC_TYPE_ARRAY &&
                kind != RIBOS_BC_TYPE_LIST) ||
            target != bound ||
            (kind == RIBOS_BC_TYPE_ARRAY && count != bound) ||
            (kind == RIBOS_BC_TYPE_LIST && count > bound)) {
            return ribos_verifier_instruction_type_error(
                context,
                instruction_id,
                result_type);
        }
        for (local = 0; local < count; ++local) {
            if (ribos_verifier_slot_type(
                    context,
                    ribos_verifier_operand_slot(
                        context,
                        instruction_id,
                        local)) != element) {
                return ribos_verifier_instruction_type_error(
                    context,
                    instruction_id,
                    local);
            }
        }
        break;
    }
    case RIBOS_BC_BUILD_MAP: {
        const uint8_t *type =
            ribos_verifier_row(context->types, result_type);
        uint32_t kind = type == NULL ?
            UINT32_MAX : ribos_verifier_u16(type + 4);
        uint32_t bound = type == NULL ? 0 :
            ribos_verifier_u32(type + 16);

        if ((kind != RIBOS_BC_TYPE_FROZEN_MAP &&
                kind != RIBOS_BC_TYPE_DICT) ||
            target != bound || (count & 1u) != 0 ||
            (kind == RIBOS_BC_TYPE_FROZEN_MAP &&
             count / 2 != bound) ||
            (kind == RIBOS_BC_TYPE_DICT &&
             count / 2 > bound)) {
            return ribos_verifier_instruction_type_error(
                context,
                instruction_id,
                result_type);
        }
        for (local = 0; local < count; ++local) {
            uint32_t expected =
                ribos_verifier_u32(type + ((local & 1u) ? 12 : 8));

            if (ribos_verifier_slot_type(
                    context,
                    ribos_verifier_operand_slot(
                        context,
                        instruction_id,
                        local)) != expected) {
                return ribos_verifier_instruction_type_error(
                    context,
                    instruction_id,
                    local);
            }
        }
        break;
    }
    case RIBOS_BC_BUILD_STRUCT: {
        const uint8_t *type =
            ribos_verifier_row(context->types, result_type);
        uint32_t start = type == NULL ? 0 :
            ribos_verifier_u32(type + 20);

        if (type == NULL ||
            ribos_verifier_u16(type + 4) != RIBOS_BC_TYPE_STRUCT ||
            target != result_type ||
            count != ribos_verifier_u32(type + 24)) {
            return ribos_verifier_instruction_type_error(
                context,
                instruction_id,
                result_type);
        }
        for (local = 0; local < count; ++local) {
            const uint8_t *shape =
                ribos_verifier_row(context->shapes, start + local);

            if (ribos_verifier_u32(shape + 4) !=
                    RIBOS_BC_SHAPE_STRUCT_FIELD ||
                ribos_verifier_slot_type(
                    context,
                    ribos_verifier_operand_slot(
                        context,
                        instruction_id,
                        local)) !=
                    ribos_verifier_u32(shape + 20)) {
                return ribos_verifier_instruction_type_error(
                    context,
                    instruction_id,
                    local);
            }
        }
        break;
    }
    case RIBOS_BC_BUILD_VARIANT:
        if (!ribos_verifier_variant_signature(
                context,
                result_type,
                target,
                count,
                instruction_id,
                RIBOS_ARTIFACT_INVALID_ID,
                0)) {
            return ribos_verifier_instruction_type_error(
                context,
                instruction_id,
                target);
        }
        break;
    case RIBOS_BC_MEMBER: {
        const uint8_t *constant =
            ribos_verifier_row(context->constants, target);
        const uint8_t *owner =
            ribos_verifier_row(context->types, left_type);
        const uint8_t *path;
        size_t path_length;
        int valid_member;

        if (count != 1 || constant == NULL ||
            ribos_verifier_u16(constant + 4) !=
                RIBOS_BC_CONSTANT_SYMBOL ||
            !ribos_verifier_constant_slice(
                context,
                target,
                &path,
                &path_length)) {
            return ribos_verifier_instruction_type_error(
                context,
                instruction_id,
                target);
        }
        if (owner != NULL &&
            ribos_verifier_u16(owner + 4) ==
                RIBOS_BC_TYPE_STRUCT) {
            valid_member = ribos_verifier_struct_member_type(
                context,
                left_type,
                result_type,
                alternate);
        } else {
            valid_member =
                alternate == RIBOS_ARTIFACT_INVALID_ID &&
                ribos_verifier_member_path_type(
                    context,
                    left_type,
                    path,
                    path_length,
                    result_type);
        }
        if (!valid_member) {
            return ribos_verifier_instruction_type_error(
                context,
                instruction_id,
                alternate);
        }
        break;
    }
    case RIBOS_BC_INDEX: {
        const uint8_t *collection =
            ribos_verifier_row(context->types, left_type);
        uint32_t kind = collection == NULL ?
            UINT32_MAX : ribos_verifier_u16(collection + 4);

        if (target == 0 &&
            (kind == RIBOS_BC_TYPE_ARRAY ||
             kind == RIBOS_BC_TYPE_LIST)) {
            if (count != 2 ||
                !ribos_verifier_type_is_unsigned_integer(
                    context,
                    right_type) ||
                result_type != ribos_verifier_u32(collection + 8)) {
                return ribos_verifier_instruction_type_error(
                    context,
                    instruction_id,
                    result_type);
            }
        } else if ((kind == RIBOS_BC_TYPE_FROZEN_MAP ||
                    kind == RIBOS_BC_TYPE_DICT) &&
            (target == 0 || target == 1)) {
            if ((target == 0 && count != 2) ||
                (target == 1 && count != 3) ||
                right_type != ribos_verifier_u32(collection + 8) ||
                result_type != ribos_verifier_u32(collection + 12) ||
                (target == 1 &&
                 ribos_verifier_slot_type(
                     context,
                     ribos_verifier_operand_slot(
                         context,
                         instruction_id,
                         2)) != result_type)) {
                return ribos_verifier_instruction_type_error(
                    context,
                    instruction_id,
                    result_type);
            }
            /*
             * A defaulted lookup conditionally selects either the collection
             * value or the fallback operand. v1 has no path-sensitive
             * ownership join, so accepting a non-copy result would let the
             * fallback remain statically available after a runtime move.
             */
            if (target == 1 &&
                ribos_verifier_type_ownership(
                    context,
                    result_type) >
                    RIBOS_SCHEMA_OWNERSHIP_COPY) {
                return ribos_verifier_fail(
                    context,
                    RIBOS_VERIFIER_TYPESTATE_VIOLATION,
                    RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
                    instruction_id,
                    result_type);
            }
        } else {
            return ribos_verifier_instruction_type_error(
                context,
                instruction_id,
                left_type);
        }
        break;
    }
    case RIBOS_BC_COLLECTION_LENGTH: {
        uint32_t element;
        const uint8_t *collection =
            ribos_verifier_row(context->types, left_type);

        if (count != 1 ||
            !ribos_verifier_collection_element_type(
                context,
                left_type,
                &element) ||
            target != ribos_verifier_u32(collection + 16) ||
            !ribos_verifier_u32_type(context, result_type)) {
            return ribos_verifier_instruction_type_error(
                context,
                instruction_id,
                target);
        }
        break;
    }
    case RIBOS_BC_VARIANT_TAG: {
        const uint8_t *sum =
            ribos_verifier_row(context->types, left_type);
        uint32_t kind = sum == NULL ?
            UINT32_MAX : ribos_verifier_u16(sum + 4);

        if (count != 1 ||
            (kind != RIBOS_BC_TYPE_OPTION &&
             kind != RIBOS_BC_TYPE_RESULT &&
             kind != RIBOS_BC_TYPE_ENUM) ||
            !ribos_verifier_u32_type(context, result_type)) {
            return ribos_verifier_instruction_type_error(
                context,
                instruction_id,
                left_type);
        }
        break;
    }
    case RIBOS_BC_VARIANT_PAYLOAD:
        if (count != 1 ||
            !ribos_verifier_variant_signature(
                context,
                left_type,
                target,
                0,
                instruction_id,
                result_type,
                1)) {
            return ribos_verifier_instruction_type_error(
                context,
                instruction_id,
                target);
        }
        break;
    case RIBOS_BC_CALL_DIRECT: {
        const uint8_t *callee =
            ribos_verifier_row(context->functions, target);
        uint32_t parameter_count;

        if (callee == NULL) {
            return ribos_verifier_fail(
                context,
                RIBOS_VERIFIER_INVALID_TARGET,
                RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
                instruction_id,
                target);
        }
        parameter_count = ribos_verifier_u32(callee + 36);
        if (count != parameter_count ||
            result_type != ribos_verifier_u32(callee + 8)) {
            return ribos_verifier_instruction_type_error(
                context,
                instruction_id,
                target);
        }
        for (local = 0; local < count; ++local) {
            uint32_t parameter =
                ribos_verifier_u32(callee + 32) + local;

            if (ribos_verifier_slot_type(
                    context,
                    ribos_verifier_operand_slot(
                        context,
                        instruction_id,
                        local)) !=
                ribos_verifier_slot_type(context, parameter)) {
                return ribos_verifier_instruction_type_error(
                    context,
                    instruction_id,
                    local);
            }
        }
        break;
    }
    case RIBOS_BC_CALL_HELPER: {
        const RibosSchemaHelper *helper =
            ribos_verifier_schema_helper(context->schema, target);

        if (helper == NULL ||
            !ribos_verifier_import_exists(context, target) ||
            count != helper->parameter_count ||
            !ribos_verifier_helper_result_type(
                context,
                result_type,
                helper)) {
            return ribos_verifier_instruction_type_error(
                context,
                instruction_id,
                target);
        }
        for (local = 0; local < count; ++local) {
            if (!ribos_verifier_type_matches_spelling(
                    context,
                    ribos_verifier_slot_type(
                        context,
                        ribos_verifier_operand_slot(
                            context,
                            instruction_id,
                            local)),
                    helper->parameters[local].type)) {
                return ribos_verifier_instruction_type_error(
                    context,
                    instruction_id,
                    local);
            }
        }
        break;
    }
    case RIBOS_BC_JUMP:
        if (count != 0 ||
            !ribos_verifier_block_in_function(function, target)) {
            return ribos_verifier_fail(
                context,
                RIBOS_VERIFIER_INVALID_TARGET,
                RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
                instruction_id,
                target);
        }
        break;
    case RIBOS_BC_BRANCH:
        if (!ribos_verifier_block_in_function(function, target) ||
            !ribos_verifier_block_in_function(function, alternate) ||
            target == alternate) {
            return ribos_verifier_fail(
                context,
                RIBOS_VERIFIER_INVALID_TARGET,
                RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
                instruction_id,
                target);
        }
        if (count != 1 ||
            !ribos_verifier_bool_type(context, left_type)) {
            return ribos_verifier_instruction_type_error(
                context,
                instruction_id,
                left_type);
        }
        break;
    case RIBOS_BC_RETURN:
        if (count != 1 ||
            left_type != ribos_verifier_u32(function + 8)) {
            return ribos_verifier_instruction_type_error(
                context,
                instruction_id,
                left_type);
        }
        break;
    case RIBOS_BC_TRAP:
        if (count != 0) {
            return ribos_verifier_instruction_type_error(
                context,
                instruction_id,
                count);
        }
        break;
    default:
        return ribos_verifier_fail(
            context,
            RIBOS_VERIFIER_INVALID_INSTRUCTION,
            RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
            instruction_id,
            opcode);
    }
    return RIBOS_VERIFIER_OK;
}

static void ribos_verifier_bits_set(uint64_t *bits, uint32_t index);
static int ribos_verifier_bits_test(
    const uint64_t *bits,
    uint32_t index);

static RibosVerifierStatus
ribos_verifier_validate_imports_and_instruction_types(
    RibosVerifierContext *context)
{
    uint32_t previous = 0;
    uint32_t import_id;
    uint32_t instruction_id;

    memset(
        context->temporary_bits,
        0,
        ((context->slots->count + 63u) / 64u) *
            sizeof(*context->temporary_bits));

    for (import_id = 0;
         import_id < context->helper_imports->count;
         ++import_id) {
        const uint8_t *row =
            ribos_verifier_row(context->helper_imports, import_id);
        uint32_t helper_id = ribos_verifier_u32(row);
        const RibosSchemaHelper *helper =
            ribos_verifier_schema_helper(context->schema, helper_id);
        uint32_t derived_call_sites = 0;

        if ((import_id != 0 && helper_id <= previous) ||
            helper == NULL ||
            ribos_verifier_u32(row + 4) != helper->capabilities ||
            ribos_verifier_u32(row + 12) != 0) {
            return ribos_verifier_fail(
                context,
                RIBOS_VERIFIER_INVALID_TARGET,
                RIBOS_VERIFIER_SUBJECT_ARTIFACT,
                helper_id,
                RIBOS_ARTIFACT_SECTION_HELPER_IMPORTS);
        }
        for (instruction_id = 0;
             instruction_id < context->instructions->count;
             ++instruction_id) {
            const uint8_t *instruction = ribos_verifier_row(
                context->instructions,
                instruction_id);

            if (instruction[0] == RIBOS_BC_CALL_HELPER &&
                ribos_verifier_u32(instruction + 20) == helper_id) {
                ++derived_call_sites;
            }
        }
        if (derived_call_sites == 0 ||
            ribos_verifier_u32(row + 8) != derived_call_sites) {
            return ribos_verifier_fail(
                context,
                RIBOS_VERIFIER_RESOURCE_MISMATCH,
                RIBOS_VERIFIER_SUBJECT_ARTIFACT,
                helper_id,
                derived_call_sites);
        }
        previous = helper_id;
    }
    for (instruction_id = 0;
         instruction_id < context->instructions->count;
         ++instruction_id) {
        const uint8_t *instruction =
            ribos_verifier_row(context->instructions, instruction_id);
        const uint8_t *block = ribos_verifier_row(
            context->blocks,
            ribos_verifier_u32(instruction + 8));
        RibosVerifierStatus status =
            ribos_verifier_validate_instruction_semantics(
                context,
                instruction_id,
                ribos_verifier_u32(block + 4));

        if (status != RIBOS_VERIFIER_OK) {
            return status;
        }
        if (instruction[0] == RIBOS_BC_PARAMETER) {
            uint32_t result = ribos_verifier_u32(instruction + 12);

            if (ribos_verifier_bits_test(
                    context->temporary_bits,
                    result)) {
                return ribos_verifier_fail(
                    context,
                    RIBOS_VERIFIER_INVALID_INSTRUCTION,
                    RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
                    instruction_id,
                    result);
            }
            ribos_verifier_bits_set(
                context->temporary_bits,
                result);
        }
    }
    for (import_id = 0;
         import_id < context->functions->count;
         ++import_id) {
        const uint8_t *function =
            ribos_verifier_row(context->functions, import_id);
        uint32_t parameter_start =
            ribos_verifier_u32(function + 32);
        uint32_t parameter_count =
            ribos_verifier_u32(function + 36);
        uint32_t parameter;

        for (parameter = 0;
             parameter < parameter_count;
             ++parameter) {
            if (!ribos_verifier_bits_test(
                    context->temporary_bits,
                    parameter_start + parameter)) {
                return ribos_verifier_fail(
                    context,
                    RIBOS_VERIFIER_UNINITIALIZED_SLOT,
                    RIBOS_VERIFIER_SUBJECT_FUNCTION,
                    import_id,
                    parameter_start + parameter);
            }
        }
    }
    return RIBOS_VERIFIER_OK;
}

static int
ribos_verifier_loop_back_edge(
    RibosVerifierContext *context,
    uint32_t function_id,
    uint32_t source_block,
    uint32_t target_block)
{
    uint32_t index;

    for (index = 0; index < context->loops->count; ++index) {
        const uint8_t *loop =
            ribos_verifier_row(context->loops, index);

        if (ribos_verifier_u32(loop + 4) == function_id &&
            ribos_verifier_u32(loop + 20) == source_block &&
            ribos_verifier_u32(loop + 8) == target_block) {
            return 1;
        }
    }
    return 0;
}

static RibosVerifierStatus
ribos_verifier_validate_loops(RibosVerifierContext *context)
{
    uint32_t index;

    for (index = 0; index < context->loops->count; ++index) {
        const uint8_t *loop = ribos_verifier_row(context->loops, index);
        uint32_t function_id = ribos_verifier_u32(loop + 4);
        const uint8_t *function =
            ribos_verifier_row(context->functions, function_id);
        uint32_t header_id = ribos_verifier_u32(loop + 8);
        uint32_t body_id = ribos_verifier_u32(loop + 12);
        uint32_t exit_id = ribos_verifier_u32(loop + 16);
        uint32_t latch_id = ribos_verifier_u32(loop + 20);
        uint32_t trip_count = ribos_verifier_u32(loop + 24);
        const uint8_t *header;
        const uint8_t *terminal;

        if (ribos_verifier_u32(loop) != index ||
            function == NULL ||
            trip_count == 0 ||
            !ribos_verifier_block_in_function(function, header_id) ||
            !ribos_verifier_block_in_function(function, body_id) ||
            !ribos_verifier_block_in_function(function, exit_id) ||
            !ribos_verifier_source_reference(
                context,
                ribos_verifier_u32(loop + 28))) {
            return ribos_verifier_fail(
                context,
                RIBOS_VERIFIER_INVALID_BLOCK,
                RIBOS_VERIFIER_SUBJECT_BLOCK,
                header_id,
                index);
        }
        header = ribos_verifier_row(context->blocks, header_id);
        terminal = ribos_verifier_row(
            context->instructions,
            ribos_verifier_u32(header + 12));
        if (terminal[0] != RIBOS_BC_BRANCH ||
            ribos_verifier_u32(terminal + 20) != body_id ||
            ribos_verifier_u32(terminal + 24) != exit_id ||
            ribos_verifier_u64(terminal + 40) != trip_count) {
            return ribos_verifier_fail(
                context,
                RIBOS_VERIFIER_INVALID_TARGET,
                RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
                ribos_verifier_u32(terminal + 4),
                header_id);
        }
        if (latch_id != RIBOS_ARTIFACT_INVALID_ID) {
            const uint8_t *latch;
            const uint8_t *latch_terminal;

            if (!ribos_verifier_block_in_function(function, latch_id)) {
                return ribos_verifier_fail(
                    context,
                    RIBOS_VERIFIER_INVALID_TARGET,
                    RIBOS_VERIFIER_SUBJECT_BLOCK,
                    latch_id,
                    header_id);
            }
            latch = ribos_verifier_row(context->blocks, latch_id);
            latch_terminal = ribos_verifier_row(
                context->instructions,
                ribos_verifier_u32(latch + 12));
            if (latch_terminal[0] != RIBOS_BC_JUMP ||
                ribos_verifier_u32(latch_terminal + 20) != header_id) {
                return ribos_verifier_fail(
                    context,
                    RIBOS_VERIFIER_INVALID_TARGET,
                    RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
                    ribos_verifier_u32(latch_terminal + 4),
                    header_id);
            }
        }
    }
    for (index = 0; index < context->instructions->count; ++index) {
        const uint8_t *instruction =
            ribos_verifier_row(context->instructions, index);
        uint64_t immediate = ribos_verifier_u64(instruction + 40);
        uint32_t loop_index;
        int found = 0;

        if (instruction[0] != RIBOS_BC_BRANCH || immediate == 0) {
            continue;
        }
        for (loop_index = 0;
             loop_index < context->loops->count;
             ++loop_index) {
            const uint8_t *loop =
                ribos_verifier_row(context->loops, loop_index);

            if (ribos_verifier_u32(loop + 8) ==
                    ribos_verifier_u32(instruction + 8) &&
                ribos_verifier_u32(loop + 24) == immediate) {
                found = 1;
                break;
            }
        }
        if (!found) {
            return ribos_verifier_fail(
                context,
                RIBOS_VERIFIER_INVALID_INSTRUCTION,
                RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
                index,
                (uint32_t)immediate);
        }
    }
    return RIBOS_VERIFIER_OK;
}

static uint32_t
ribos_verifier_successors(
    RibosVerifierContext *context,
    uint32_t block_id,
    uint32_t targets[2])
{
    const uint8_t *block = ribos_verifier_row(context->blocks, block_id);
    const uint8_t *instruction = ribos_verifier_row(
        context->instructions,
        ribos_verifier_u32(block + 12));

    if (instruction[0] == RIBOS_BC_JUMP) {
        targets[0] = ribos_verifier_u32(instruction + 20);
        return 1;
    }
    if (instruction[0] == RIBOS_BC_BRANCH) {
        targets[0] = ribos_verifier_u32(instruction + 20);
        targets[1] = ribos_verifier_u32(instruction + 24);
        return 2;
    }
    return 0;
}

static uint64_t *
ribos_verifier_block_bits(
    uint64_t *matrix,
    uint32_t local_block,
    size_t words)
{
    return matrix + (size_t)local_block * words;
}

static int
ribos_verifier_bits_equal(
    const uint64_t *left,
    const uint64_t *right,
    size_t words)
{
    return memcmp(left, right, words * sizeof(*left)) == 0;
}

static void
ribos_verifier_bits_copy(
    uint64_t *target,
    const uint64_t *source,
    size_t words)
{
    memcpy(target, source, words * sizeof(*target));
}

static void
ribos_verifier_bits_intersect(
    uint64_t *target,
    const uint64_t *source,
    size_t words)
{
    size_t index;

    for (index = 0; index < words; ++index) {
        target[index] &= source[index];
    }
}

static void
ribos_verifier_bits_union(
    uint64_t *target,
    const uint64_t *source,
    size_t words)
{
    size_t index;

    for (index = 0; index < words; ++index) {
        target[index] |= source[index];
    }
}

static void
ribos_verifier_bits_set(uint64_t *bits, uint32_t index)
{
    bits[index / 64] |= UINT64_C(1) << (index % 64);
}

static int
ribos_verifier_bits_test(const uint64_t *bits, uint32_t index)
{
    return (bits[index / 64] &
        (UINT64_C(1) << (index % 64))) != 0;
}

static RibosVerifierStatus
ribos_verifier_validate_function_cfg(
    RibosVerifierContext *context,
    uint32_t function_id)
{
    const uint8_t *function =
        ribos_verifier_row(context->functions, function_id);
    uint32_t first_block = ribos_verifier_u32(function + 16);
    uint32_t block_count = ribos_verifier_u32(function + 20);
    uint32_t first_slot = ribos_verifier_u32(function + 24);
    uint32_t slot_count = ribos_verifier_u32(function + 28);
    uint32_t entry =
        ribos_verifier_u32(function + 12) - first_block;
    size_t words = (slot_count + 63u) / 64u;
    uint32_t local;
    uint32_t edge_count = 0;
    uint32_t reachable_count = 0;
    uint32_t terminal_mask = 0;
    uint32_t queue_head = 0;
    uint32_t queue_tail = 0;
    uint32_t iteration;
    int changed = 0;

    memset(context->block_reachable, 0, block_count);
    memset(
        context->predecessor_counts,
        0,
        (size_t)block_count * sizeof(*context->predecessor_counts));
    memset(
        context->predecessor_starts,
        0,
        (size_t)block_count * sizeof(*context->predecessor_starts));
    memset(
        context->predecessor_cursor,
        0,
        (size_t)block_count * sizeof(*context->predecessor_cursor));
    memset(
        context->definition_bits,
        0,
        (size_t)block_count * words *
            sizeof(*context->definition_bits));
    memset(
        context->out_bits,
        0,
        (size_t)block_count * words * sizeof(*context->out_bits));

    for (local = 0; local < block_count; ++local) {
        uint32_t targets[2];
        uint32_t successor_count =
            ribos_verifier_successors(
                context,
                first_block + local,
                targets);
        uint32_t successor;
        for (successor = 0;
             successor < successor_count;
             ++successor) {
            uint32_t target_local = targets[successor] - first_block;

            if (target_local >= block_count ||
                context->predecessor_counts[target_local] ==
                    UINT32_MAX) {
                return ribos_verifier_fail(
                    context,
                    RIBOS_VERIFIER_INVALID_TARGET,
                    RIBOS_VERIFIER_SUBJECT_BLOCK,
                    first_block + local,
                    targets[successor]);
            }
            ++context->predecessor_counts[target_local];
            ++edge_count;
        }
    }
    if (edge_count > block_count * 2u) {
        return ribos_verifier_fail(
            context,
            RIBOS_VERIFIER_INVALID_BLOCK,
            RIBOS_VERIFIER_SUBJECT_FUNCTION,
            function_id,
            edge_count);
    }
    for (local = 0; local < block_count; ++local) {
        context->predecessor_starts[local] = local == 0 ? 0 :
            context->predecessor_starts[local - 1] +
                context->predecessor_counts[local - 1];
        context->predecessor_cursor[local] =
            context->predecessor_starts[local];
    }
    for (local = 0; local < block_count; ++local) {
        uint32_t targets[2];
        uint32_t successor_count =
            ribos_verifier_successors(
                context,
                first_block + local,
                targets);
        uint32_t successor;

        for (successor = 0;
             successor < successor_count;
             ++successor) {
            uint32_t target_local = targets[successor] - first_block;

            context->predecessors[
                context->predecessor_cursor[target_local]++] = local;
        }
    }

    context->predecessor_cursor[queue_tail++] = entry;
    context->block_reachable[entry] = 1;
    while (queue_head < queue_tail) {
        uint32_t source = context->predecessor_cursor[queue_head++];
        uint32_t targets[2];
        uint32_t successor_count =
            ribos_verifier_successors(
                context,
                first_block + source,
                targets);
        uint32_t successor;

        ++reachable_count;
        for (successor = 0;
             successor < successor_count;
             ++successor) {
            uint32_t target_local = targets[successor] - first_block;

            if (context->block_reachable[target_local] == 0) {
                context->block_reachable[target_local] = 1;
                context->predecessor_cursor[queue_tail++] =
                    target_local;
            }
        }
    }
    for (local = 0; local < block_count; ++local) {
        const uint8_t *block;
        const uint8_t *terminal;
        uint32_t instruction_id;
        uint32_t instruction_count;
        uint32_t ordinal;

        if (context->block_reachable[local] == 0) {
            continue;
        }
        block = ribos_verifier_row(
            context->blocks,
            first_block + local);
        terminal = ribos_verifier_row(
            context->instructions,
            ribos_verifier_u32(block + 12));
        if (terminal[0] == RIBOS_BC_RETURN) {
            terminal_mask |= RIBOS_BC_TERMINAL_RETURN;
        } else if (terminal[0] == RIBOS_BC_TRAP) {
            terminal_mask |= RIBOS_BC_TERMINAL_TRAP;
        }
        instruction_id = ribos_verifier_u32(block + 8);
        instruction_count = ribos_verifier_u32(block + 16);
        for (ordinal = 0; ordinal < instruction_count; ++ordinal) {
            const uint8_t *instruction = ribos_verifier_row(
                context->instructions,
                instruction_id);

            if (instruction[0] == RIBOS_BC_CALL_DIRECT) {
                uint32_t callee =
                    ribos_verifier_u32(instruction + 20);

                context->call_edges[
                    (size_t)function_id *
                        context->functions->count + callee] = 1;
            }
            instruction_id =
                ribos_verifier_u32(instruction + 32);
        }
    }

    /*
     * Remove declared loop latch edges and require the remaining CFG to be a
     * DAG. This rejects every reachable cycle that is not represented by a
     * validated bounded-loop row.
     */
    memset(
        context->predecessor_counts,
        0,
        (size_t)block_count * sizeof(*context->predecessor_counts));
    for (local = 0; local < block_count; ++local) {
        uint32_t targets[2];
        uint32_t successor_count =
            ribos_verifier_successors(
                context,
                first_block + local,
                targets);
        uint32_t successor;

        if (context->block_reachable[local] == 0) {
            continue;
        }
        for (successor = 0;
             successor < successor_count;
             ++successor) {
            if (!ribos_verifier_loop_back_edge(
                    context,
                    function_id,
                    first_block + local,
                    targets[successor])) {
                ++context->predecessor_counts[
                    targets[successor] - first_block];
            }
        }
    }
    queue_head = 0;
    queue_tail = 0;
    for (local = 0; local < block_count; ++local) {
        if (context->block_reachable[local] != 0 &&
            context->predecessor_counts[local] == 0) {
            context->predecessor_cursor[queue_tail++] = local;
        }
    }
    while (queue_head < queue_tail) {
        uint32_t source = context->predecessor_cursor[queue_head++];
        uint32_t targets[2];
        uint32_t successor_count =
            ribos_verifier_successors(
                context,
                first_block + source,
                targets);
        uint32_t successor;

        for (successor = 0;
             successor < successor_count;
             ++successor) {
            uint32_t target_local = targets[successor] - first_block;

            if (ribos_verifier_loop_back_edge(
                    context,
                    function_id,
                    first_block + source,
                    targets[successor])) {
                continue;
            }
            if (--context->predecessor_counts[target_local] == 0) {
                context->predecessor_cursor[queue_tail++] =
                    target_local;
            }
        }
    }
    if (queue_tail != reachable_count) {
        return ribos_verifier_fail(
            context,
            RIBOS_VERIFIER_INVALID_BLOCK,
            RIBOS_VERIFIER_SUBJECT_FUNCTION,
            function_id,
            queue_tail);
    }

    /* Rebuild complete predecessor ranges for definite assignment. */
    memset(
        context->predecessor_counts,
        0,
        (size_t)block_count * sizeof(*context->predecessor_counts));
    for (local = 0; local < block_count; ++local) {
        uint32_t targets[2];
        uint32_t successor_count =
            ribos_verifier_successors(
                context,
                first_block + local,
                targets);
        uint32_t successor;

        if (context->block_reachable[local] == 0) {
            continue;
        }
        for (successor = 0;
             successor < successor_count;
             ++successor) {
            ++context->predecessor_counts[
                targets[successor] - first_block];
        }
    }
    for (local = 0; local < block_count; ++local) {
        context->predecessor_starts[local] = local == 0 ? 0 :
            context->predecessor_starts[local - 1] +
                context->predecessor_counts[local - 1];
        context->predecessor_cursor[local] =
            context->predecessor_starts[local];
    }
    for (local = 0; local < block_count; ++local) {
        uint32_t targets[2];
        uint32_t successor_count =
            ribos_verifier_successors(
                context,
                first_block + local,
                targets);
        uint32_t successor;

        if (context->block_reachable[local] == 0) {
            continue;
        }
        for (successor = 0;
             successor < successor_count;
             ++successor) {
            uint32_t target_local = targets[successor] - first_block;

            context->predecessors[
                context->predecessor_cursor[target_local]++] = local;
        }
    }

    for (local = 0; local < block_count; ++local) {
        const uint8_t *block =
            ribos_verifier_row(context->blocks, first_block + local);
        uint32_t instruction_id = ribos_verifier_u32(block + 8);
        uint32_t instruction_count = ribos_verifier_u32(block + 16);
        uint32_t ordinal;
        uint64_t *definitions = ribos_verifier_block_bits(
            context->definition_bits,
            local,
            words);
        uint64_t *out = ribos_verifier_block_bits(
            context->out_bits,
            local,
            words);

        if (context->block_reachable[local] == 0) {
            continue;
        }
        for (ordinal = 0; ordinal < instruction_count; ++ordinal) {
            const uint8_t *instruction =
                ribos_verifier_row(
                    context->instructions,
                    instruction_id);
            uint32_t result = ribos_verifier_u32(instruction + 12);

            if (result != RIBOS_ARTIFACT_INVALID_ID) {
                ribos_verifier_bits_set(
                    definitions,
                    result - first_slot);
            }
            instruction_id = ribos_verifier_u32(instruction + 32);
        }
        if (local != entry) {
            memset(out, 0xff, words * sizeof(*out));
            if (slot_count % 64 != 0) {
                out[words - 1] &=
                    (UINT64_C(1) << (slot_count % 64)) - 1;
            }
        }
    }
    for (iteration = 0; iteration <= block_count; ++iteration) {
        changed = 0;
        for (local = 0; local < block_count; ++local) {
            uint64_t *out = ribos_verifier_block_bits(
                context->out_bits,
                local,
                words);
            uint64_t *definitions = ribos_verifier_block_bits(
                context->definition_bits,
                local,
                words);
            uint32_t predecessor;

            if (context->block_reachable[local] == 0) {
                continue;
            }
            if (local == entry) {
                memset(
                    context->temporary_bits,
                    0,
                    words * sizeof(*context->temporary_bits));
            } else {
                memset(
                    context->temporary_bits,
                    0xff,
                    words * sizeof(*context->temporary_bits));
                for (predecessor = 0;
                     predecessor <
                        context->predecessor_counts[local];
                     ++predecessor) {
                    uint32_t source = context->predecessors[
                        context->predecessor_starts[local] +
                            predecessor];

                    ribos_verifier_bits_intersect(
                        context->temporary_bits,
                        ribos_verifier_block_bits(
                            context->out_bits,
                            source,
                            words),
                        words);
                }
            }
            ribos_verifier_bits_union(
                context->temporary_bits,
                definitions,
                words);
            if (!ribos_verifier_bits_equal(
                    out,
                    context->temporary_bits,
                    words)) {
                ribos_verifier_bits_copy(
                    out,
                    context->temporary_bits,
                    words);
                changed = 1;
            }
        }
        if (!changed) {
            break;
        }
    }
    if (changed) {
        return ribos_verifier_fail(
            context,
            RIBOS_VERIFIER_INVALID_BLOCK,
            RIBOS_VERIFIER_SUBJECT_FUNCTION,
            function_id,
            block_count);
    }

    for (local = 0; local < block_count; ++local) {
        const uint8_t *block =
            ribos_verifier_row(context->blocks, first_block + local);
        uint32_t instruction_id = ribos_verifier_u32(block + 8);
        uint32_t instruction_count = ribos_verifier_u32(block + 16);
        uint32_t predecessor;
        uint32_t ordinal;

        if (context->block_reachable[local] == 0) {
            continue;
        }
        if (local == entry) {
            memset(
                context->temporary_bits,
                0,
                words * sizeof(*context->temporary_bits));
        } else {
            memset(
                context->temporary_bits,
                0xff,
                words * sizeof(*context->temporary_bits));
            for (predecessor = 0;
                 predecessor < context->predecessor_counts[local];
                 ++predecessor) {
                uint32_t source = context->predecessors[
                    context->predecessor_starts[local] + predecessor];

                ribos_verifier_bits_intersect(
                    context->temporary_bits,
                    ribos_verifier_block_bits(
                        context->out_bits,
                        source,
                        words),
                    words);
            }
        }
        for (ordinal = 0; ordinal < instruction_count; ++ordinal) {
            const uint8_t *instruction =
                ribos_verifier_row(
                    context->instructions,
                    instruction_id);
            uint32_t operand_count =
                ribos_verifier_u16(instruction + 2);
            uint32_t operand;
            uint32_t result =
                ribos_verifier_u32(instruction + 12);

            for (operand = 0; operand < operand_count; ++operand) {
                uint32_t slot_id =
                    ribos_verifier_operand_slot(
                        context,
                        instruction_id,
                        operand);

                if (!ribos_verifier_bits_test(
                        context->temporary_bits,
                        slot_id - first_slot)) {
                    return ribos_verifier_fail(
                        context,
                        RIBOS_VERIFIER_UNINITIALIZED_SLOT,
                        RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
                        instruction_id,
                        slot_id);
                }
            }
            if (result != RIBOS_ARTIFACT_INVALID_ID) {
                ribos_verifier_bits_set(
                    context->temporary_bits,
                    result - first_slot);
            }
            instruction_id = ribos_verifier_u32(instruction + 32);
        }
    }
    if (ribos_verifier_u32(function + 96) != terminal_mask) {
        return ribos_verifier_fail(
            context,
            RIBOS_VERIFIER_RESOURCE_MISMATCH,
            RIBOS_VERIFIER_SUBJECT_FUNCTION,
            function_id,
            terminal_mask);
    }
    context->terminal_masks[function_id] = terminal_mask;
    return RIBOS_VERIFIER_OK;
}

static RibosVerifierStatus
ribos_verifier_validate_cfg_and_initialization(
    RibosVerifierContext *context)
{
    uint32_t function_id;
    RibosVerifierStatus status =
        ribos_verifier_validate_loops(context);

    if (status != RIBOS_VERIFIER_OK) {
        return status;
    }
    for (function_id = 0;
         function_id < context->functions->count;
         ++function_id) {
        status = ribos_verifier_validate_function_cfg(
            context,
            function_id);
        if (status != RIBOS_VERIFIER_OK) {
            return status;
        }
    }
    return RIBOS_VERIFIER_OK;
}

static RibosVerifierStatus
ribos_verifier_close_call_graph(
    RibosVerifierContext *context,
    uint32_t function_id)
{
    uint32_t callee;
    uint32_t depth = 1;
    uint64_t maximum_callee_stack = 0;

    if (context->call_states[function_id] == 2) {
        return RIBOS_VERIFIER_OK;
    }
    if (context->call_states[function_id] == 1) {
        return ribos_verifier_fail(
            context,
            RIBOS_VERIFIER_RECURSIVE_CALL,
            RIBOS_VERIFIER_SUBJECT_FUNCTION,
            function_id,
            function_id);
    }
    context->call_states[function_id] = 1;
    for (callee = 0; callee < context->functions->count; ++callee) {
        RibosVerifierStatus status;

        if (context->call_edges[
                (size_t)function_id *
                    context->functions->count + callee] == 0) {
            continue;
        }
        status = ribos_verifier_close_call_graph(context, callee);
        if (status != RIBOS_VERIFIER_OK) {
            return status;
        }
        if (context->call_depths[callee] == UINT32_MAX ||
            context->call_depths[callee] + 1 > depth) {
            if (context->call_depths[callee] == UINT32_MAX) {
                return ribos_verifier_fail(
                    context,
                    RIBOS_VERIFIER_RESOURCE_MISMATCH,
                    RIBOS_VERIFIER_SUBJECT_FUNCTION,
                    function_id,
                    callee);
            }
            depth = context->call_depths[callee] + 1;
        }
        if (context->stack_bytes[callee] > maximum_callee_stack) {
            maximum_callee_stack = context->stack_bytes[callee];
        }
    }
    if (maximum_callee_stack >
            UINT64_MAX - context->frame_bytes[function_id] ||
        maximum_callee_stack + context->frame_bytes[function_id] >
            RIBOS_VERIFIER_MAX_STACK_BYTES) {
        return ribos_verifier_fail(
            context,
            RIBOS_VERIFIER_RESOURCE_MISMATCH,
            RIBOS_VERIFIER_SUBJECT_FUNCTION,
            function_id,
            depth);
    }
    context->call_depths[function_id] = depth;
    context->stack_bytes[function_id] =
        maximum_callee_stack + context->frame_bytes[function_id];
    context->call_states[function_id] = 2;
    return RIBOS_VERIFIER_OK;
}

static RibosVerifierStatus
ribos_verifier_validate_call_resources(
    RibosVerifierContext *context)
{
    uint32_t function_id;
    uint32_t entry = context->artifact.entry_function;

    for (function_id = 0;
         function_id < context->functions->count;
         ++function_id) {
        RibosVerifierStatus status =
            ribos_verifier_close_call_graph(context, function_id);

        if (status != RIBOS_VERIFIER_OK) {
            return status;
        }
    }
    for (function_id = 0;
         function_id < context->functions->count;
         ++function_id) {
        const uint8_t *function =
            ribos_verifier_row(context->functions, function_id);

        if (ribos_verifier_u32(function + 92) !=
                context->call_depths[function_id] ||
            ribos_verifier_u64(function + 80) !=
                context->stack_bytes[function_id]) {
            return ribos_verifier_fail(
                context,
                RIBOS_VERIFIER_RESOURCE_MISMATCH,
                RIBOS_VERIFIER_SUBJECT_FUNCTION,
                function_id,
                context->call_depths[function_id]);
        }
    }
    if (context->artifact.maximum_call_depth !=
            context->call_depths[entry] ||
        context->artifact.maximum_stack_bytes !=
            context->stack_bytes[entry]) {
        return ribos_verifier_fail(
            context,
            RIBOS_VERIFIER_RESOURCE_MISMATCH,
            RIBOS_VERIFIER_SUBJECT_FUNCTION,
            entry,
            context->call_depths[entry]);
    }
    context->report->recomputed_frame_bytes =
        context->frame_bytes[entry];
    context->report->recomputed_call_depth =
        context->call_depths[entry];
    context->report->recomputed_stack_bytes =
        context->stack_bytes[entry];
    return RIBOS_VERIFIER_OK;
}

static uint64_t
ribos_verifier_path_max(const RibosVerifierPathBound *bound)
{
    uint64_t result = 0;

    if (bound->has_return && bound->returned > result) {
        result = bound->returned;
    }
    if (bound->has_trap && bound->trapped > result) {
        result = bound->trapped;
    }
    return result;
}

static void
ribos_verifier_path_merge(
    RibosVerifierPathBound *target,
    const RibosVerifierPathBound *source)
{
    if (source->has_stop &&
        (!target->has_stop || source->stop > target->stop)) {
        target->stop = source->stop;
        target->has_stop = 1;
    }
    if (source->has_return &&
        (!target->has_return || source->returned > target->returned)) {
        target->returned = source->returned;
        target->has_return = 1;
    }
    if (source->has_trap &&
        (!target->has_trap || source->trapped > target->trapped)) {
        target->trapped = source->trapped;
        target->has_trap = 1;
    }
}

static int
ribos_verifier_path_prepend(
    RibosVerifierPathBound *bound,
    uint64_t prefix)
{
    return
        (!bound->has_stop ||
         ribos_verifier_add_u64(prefix, bound->stop, &bound->stop)) &&
        (!bound->has_return ||
         ribos_verifier_add_u64(
             prefix,
             bound->returned,
             &bound->returned)) &&
        (!bound->has_trap ||
         ribos_verifier_add_u64(
             prefix,
             bound->trapped,
             &bound->trapped));
}

static const uint8_t *
ribos_verifier_loop_for_header(
    RibosVerifierContext *context,
    uint32_t block_id)
{
    uint32_t index;

    for (index = 0; index < context->loops->count; ++index) {
        const uint8_t *loop =
            ribos_verifier_row(context->loops, index);

        if (ribos_verifier_u32(loop + 8) == block_id) {
            return loop;
        }
    }
    return NULL;
}

static uint32_t
ribos_verifier_helper_import_index(
    RibosVerifierContext *context,
    uint32_t stable_id)
{
    uint32_t index;

    for (index = 0; index < context->helper_imports->count; ++index) {
        if (ribos_verifier_u32(
                ribos_verifier_row(
                    context->helper_imports,
                    index)) == stable_id) {
            return index;
        }
    }
    return RIBOS_ARTIFACT_INVALID_ID;
}

static uint64_t
ribos_verifier_block_weight(
    RibosVerifierPathContext *path,
    uint32_t block_id)
{
    RibosVerifierContext *context = path->verifier;
    const uint8_t *block =
        ribos_verifier_row(context->blocks, block_id);
    uint32_t instruction_id = ribos_verifier_u32(block + 8);
    uint32_t instruction_count = ribos_verifier_u32(block + 16);
    uint64_t weight = 0;
    uint32_t ordinal;

    for (ordinal = 0; ordinal < instruction_count; ++ordinal) {
        const uint8_t *instruction =
            ribos_verifier_row(context->instructions, instruction_id);
        uint64_t addition = 0;

        if (path->metric == RIBOS_VERIFIER_METRIC_INSTRUCTION) {
            addition = 1;
            if (instruction[0] == RIBOS_BC_CALL_DIRECT &&
                !ribos_verifier_add_u64(
                    addition,
                    context->instruction_bounds[
                        ribos_verifier_u32(instruction + 20)
                    ],
                    &addition)) {
                path->failed = 1;
                return 0;
            }
        } else if (path->metric ==
                RIBOS_VERIFIER_METRIC_HELPER_TOTAL) {
            if (instruction[0] == RIBOS_BC_CALL_HELPER) {
                addition = 1;
            } else if (instruction[0] == RIBOS_BC_CALL_DIRECT) {
                addition = context->helper_total_bounds[
                    ribos_verifier_u32(instruction + 20)];
            }
        } else if (instruction[0] == RIBOS_BC_CALL_HELPER) {
            addition = ribos_verifier_helper_import_index(
                context,
                ribos_verifier_u32(instruction + 20)) ==
                    path->helper_import;
        } else if (instruction[0] == RIBOS_BC_CALL_DIRECT) {
            addition = context->helper_matrix[
                (size_t)ribos_verifier_u32(instruction + 20) *
                    context->helper_imports->count +
                path->helper_import
            ];
        }
        if (!ribos_verifier_add_u64(weight, addition, &weight)) {
            path->failed = 1;
            return 0;
        }
        instruction_id = ribos_verifier_u32(instruction + 32);
    }
    return weight;
}

static RibosVerifierPathBound
ribos_verifier_analyze_node(
    RibosVerifierPathContext *path,
    uint32_t block_id);

static RibosVerifierPathBound
ribos_verifier_analyze_region(
    RibosVerifierContext *context,
    uint32_t function_id,
    uint32_t start_block,
    uint32_t stop_block,
    RibosVerifierMetric metric,
    uint32_t helper_import,
    uint32_t depth,
    int *failed)
{
    RibosVerifierPathContext path = {
        .verifier = context,
        .function_id = function_id,
        .stop_block = stop_block,
        .metric = metric,
        .helper_import = helper_import,
        .depth = depth,
    };
    RibosVerifierPathBound result = {0};
    size_t offset = (size_t)depth * context->blocks->count;

    if (depth > context->loops->count ||
        offset > context->path_matrix_entries ||
        context->blocks->count >
            context->path_matrix_entries - offset) {
        path.failed = 1;
    } else {
        memset(
            context->path_states + offset,
            0,
            context->blocks->count);
        memset(
            context->path_memo + offset,
            0,
            (size_t)context->blocks->count *
                sizeof(*context->path_memo));
        result = ribos_verifier_analyze_node(&path, start_block);
    }
    if (failed != NULL && path.failed) {
        *failed = 1;
    }
    return result;
}

static int
ribos_verifier_mark_reachable(
    RibosVerifierContext *context,
    uint32_t function_id)
{
    const uint8_t *function =
        ribos_verifier_row(context->functions, function_id);
    uint32_t first_block = ribos_verifier_u32(function + 16);
    uint32_t block_count = ribos_verifier_u32(function + 20);
    uint32_t entry = ribos_verifier_u32(function + 12);
    uint32_t head = 0;
    uint32_t tail = 0;

    memset(context->action_masks, 0, context->blocks->count);
    context->predecessor_cursor[tail++] = entry;
    context->action_masks[entry] = 1;
    while (head < tail) {
        uint32_t block_id = context->predecessor_cursor[head++];
        uint32_t targets[2];
        uint32_t count =
            ribos_verifier_successors(context, block_id, targets);
        uint32_t index;

        for (index = 0; index < count; ++index) {
            if (targets[index] < first_block ||
                targets[index] - first_block >= block_count) {
                return 0;
            }
            if (context->action_masks[targets[index]] == 0) {
                context->action_masks[targets[index]] = 1;
                context->predecessor_cursor[tail++] = targets[index];
            }
        }
    }
    return 1;
}

static RibosVerifierStatus
ribos_verifier_close_stage2_resources(
    RibosVerifierContext *context,
    uint32_t function_id)
{
    const uint8_t *function =
        ribos_verifier_row(context->functions, function_id);
    uint32_t first_block = ribos_verifier_u32(function + 16);
    uint32_t block_count = ribos_verifier_u32(function + 20);
    uint32_t callee;
    uint32_t capability = 0;
    uint32_t local;
    uint32_t helper;
    int failed = 0;
    RibosVerifierPathBound instructions;
    RibosVerifierPathBound helpers;

    if (context->resource_states[function_id] == 2) {
        return RIBOS_VERIFIER_OK;
    }
    if (context->resource_states[function_id] == 1) {
        return ribos_verifier_fail(
            context,
            RIBOS_VERIFIER_RECURSIVE_CALL,
            RIBOS_VERIFIER_SUBJECT_FUNCTION,
            function_id,
            function_id);
    }
    context->resource_states[function_id] = 1;
    for (callee = 0; callee < context->functions->count; ++callee) {
        RibosVerifierStatus status;

        if (context->call_edges[
                (size_t)function_id *
                    context->functions->count + callee] == 0) {
            continue;
        }
        status = ribos_verifier_close_stage2_resources(
            context,
            callee);
        if (status != RIBOS_VERIFIER_OK) {
            return status;
        }
        capability |= context->reachable_capabilities[callee];
    }
    if (!ribos_verifier_mark_reachable(context, function_id)) {
        return ribos_verifier_fail(
            context,
            RIBOS_VERIFIER_INVALID_BLOCK,
            RIBOS_VERIFIER_SUBJECT_FUNCTION,
            function_id,
            0);
    }
    for (local = 0; local < block_count; ++local) {
        uint32_t block_id = first_block + local;
        const uint8_t *block;
        uint32_t instruction_id;
        uint32_t count;
        uint32_t ordinal;

        if (context->action_masks[block_id] == 0) {
            continue;
        }
        block = ribos_verifier_row(context->blocks, block_id);
        instruction_id = ribos_verifier_u32(block + 8);
        count = ribos_verifier_u32(block + 16);
        for (ordinal = 0; ordinal < count; ++ordinal) {
            const uint8_t *instruction =
                ribos_verifier_row(
                    context->instructions,
                    instruction_id);

            if (instruction[0] == RIBOS_BC_CALL_HELPER) {
                const RibosSchemaHelper *schema_helper =
                    ribos_verifier_schema_helper(
                        context->schema,
                        ribos_verifier_u32(instruction + 20));

                if (schema_helper == NULL) {
                    return ribos_verifier_fail(
                        context,
                        RIBOS_VERIFIER_INVALID_TARGET,
                        RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
                        instruction_id,
                        ribos_verifier_u32(instruction + 20));
                }
                capability |= schema_helper->capabilities;
            }
            instruction_id = ribos_verifier_u32(instruction + 32);
        }
    }
    instructions = ribos_verifier_analyze_region(
        context,
        function_id,
        ribos_verifier_u32(function + 12),
        RIBOS_ARTIFACT_INVALID_ID,
        RIBOS_VERIFIER_METRIC_INSTRUCTION,
        0,
        0,
        &failed);
    helpers = ribos_verifier_analyze_region(
        context,
        function_id,
        ribos_verifier_u32(function + 12),
        RIBOS_ARTIFACT_INVALID_ID,
        RIBOS_VERIFIER_METRIC_HELPER_TOTAL,
        0,
        0,
        &failed);
    if (failed || instructions.has_stop || helpers.has_stop ||
        (!instructions.has_return && !instructions.has_trap)) {
        return ribos_verifier_fail(
            context,
            RIBOS_VERIFIER_RESOURCE_MISMATCH,
            RIBOS_VERIFIER_SUBJECT_FUNCTION,
            function_id,
            0);
    }
    context->instruction_bounds[function_id] =
        ribos_verifier_path_max(&instructions);
    context->helper_total_bounds[function_id] =
        ribos_verifier_path_max(&helpers);
    context->reachable_capabilities[function_id] = capability;
    for (helper = 0;
         helper < context->helper_imports->count;
         ++helper) {
        RibosVerifierPathBound one = ribos_verifier_analyze_region(
            context,
            function_id,
            ribos_verifier_u32(function + 12),
            RIBOS_ARTIFACT_INVALID_ID,
            RIBOS_VERIFIER_METRIC_HELPER_ID,
            helper,
            0,
            &failed);

        if (failed || one.has_stop) {
            return ribos_verifier_fail(
                context,
                RIBOS_VERIFIER_RESOURCE_MISMATCH,
                RIBOS_VERIFIER_SUBJECT_FUNCTION,
                function_id,
                helper);
        }
        context->helper_matrix[
            (size_t)function_id *
                context->helper_imports->count + helper] =
            ribos_verifier_path_max(&one);
    }
    if (ribos_verifier_u32(function + 44) != capability ||
        (capability & ~ribos_verifier_u32(function + 40)) != 0) {
        return ribos_verifier_fail(
            context,
            RIBOS_VERIFIER_CAPABILITY_MISMATCH,
            RIBOS_VERIFIER_SUBJECT_FUNCTION,
            function_id,
            capability);
    }
    if (ribos_verifier_u64(function + 56) !=
            context->instruction_bounds[function_id] ||
        ribos_verifier_u64(function + 72) !=
            context->helper_total_bounds[function_id]) {
        return ribos_verifier_fail(
            context,
            RIBOS_VERIFIER_RESOURCE_MISMATCH,
            RIBOS_VERIFIER_SUBJECT_FUNCTION,
            function_id,
            0);
    }
    if (context->instruction_bounds[function_id] >
            ribos_verifier_u64(function + 48) ||
        context->helper_total_bounds[function_id] >
            ribos_verifier_u64(function + 64)) {
        return ribos_verifier_fail(
            context,
            RIBOS_VERIFIER_BUDGET_EXCEEDED,
            RIBOS_VERIFIER_SUBJECT_FUNCTION,
            function_id,
            0);
    }
    context->resource_states[function_id] = 2;
    return RIBOS_VERIFIER_OK;
}

static RibosVerifierStatus
ribos_verifier_validate_helper_bounds(
    RibosVerifierContext *context)
{
    uint32_t row_id = 0;
    uint32_t function_id;

    for (function_id = 0;
         function_id < context->functions->count;
         ++function_id) {
        uint32_t helper;

        for (helper = 0;
             helper < context->helper_imports->count;
             ++helper) {
            uint64_t bound = context->helper_matrix[
                (size_t)function_id *
                    context->helper_imports->count + helper];

            if (bound != 0) {
                const uint8_t *row =
                    ribos_verifier_row(context->helper_bounds, row_id);
                uint32_t stable_id = ribos_verifier_u32(
                    ribos_verifier_row(
                        context->helper_imports,
                        helper));

                if (row == NULL ||
                    ribos_verifier_u32(row) != function_id ||
                    ribos_verifier_u32(row + 4) != stable_id ||
                    ribos_verifier_u64(row + 8) != bound) {
                    return ribos_verifier_fail(
                        context,
                        RIBOS_VERIFIER_RESOURCE_MISMATCH,
                        RIBOS_VERIFIER_SUBJECT_FUNCTION,
                        function_id,
                        stable_id);
                }
                ++row_id;
            }
        }
    }
    if (row_id != context->helper_bounds->count) {
        return ribos_verifier_fail(
            context,
            RIBOS_VERIFIER_RESOURCE_MISMATCH,
            RIBOS_VERIFIER_SUBJECT_ARTIFACT,
            RIBOS_ARTIFACT_SECTION_HELPER_BOUNDS,
            row_id);
    }
    return RIBOS_VERIFIER_OK;
}

static int
ribos_verifier_operand_is_consumed(
    RibosVerifierContext *context,
    uint32_t instruction_id,
    uint32_t ordinal)
{
    const uint8_t *instruction =
        ribos_verifier_row(context->instructions, instruction_id);
    uint32_t opcode = instruction[0];
    uint32_t result = ribos_verifier_u32(instruction + 12);
    int result_ownership = result == RIBOS_ARTIFACT_INVALID_ID ?
        RIBOS_SCHEMA_OWNERSHIP_COPY :
        ribos_verifier_type_ownership(
            context,
            ribos_verifier_slot_type(context, result));

    if (opcode == RIBOS_BC_CALL_HELPER) {
        const RibosSchemaHelper *helper =
            ribos_verifier_schema_helper(
                context->schema,
                ribos_verifier_u32(instruction + 20));

        return helper != NULL &&
            ordinal < helper->parameter_count &&
            helper->parameters[ordinal].mode ==
                RIBOS_SCHEMA_PARAMETER_CONSUME;
    }
    if (opcode == RIBOS_BC_MOVE ||
        opcode == RIBOS_BC_BUILD_LIST ||
        opcode == RIBOS_BC_BUILD_MAP ||
        opcode == RIBOS_BC_BUILD_STRUCT ||
        opcode == RIBOS_BC_BUILD_VARIANT ||
        opcode == RIBOS_BC_CALL_DIRECT ||
        opcode == RIBOS_BC_RETURN) {
        return 1;
    }
    if (ordinal == 0 &&
        (opcode == RIBOS_BC_MEMBER ||
         opcode == RIBOS_BC_INDEX ||
         opcode == RIBOS_BC_VARIANT_PAYLOAD) &&
        result_ownership > RIBOS_SCHEMA_OWNERSHIP_COPY) {
        return 1;
    }
    return 0;
}

static void
ribos_verifier_bits_clear(uint64_t *bits, uint32_t index)
{
    bits[index / 64] &=
        ~(UINT64_C(1) << (index % 64));
}

static RibosVerifierStatus
ribos_verifier_transfer_ownership_block(
    RibosVerifierContext *context,
    uint32_t block_id,
    uint32_t first_slot,
    uint64_t *available,
    int validate)
{
    const uint8_t *block =
        ribos_verifier_row(context->blocks, block_id);
    uint32_t instruction_id = ribos_verifier_u32(block + 8);
    uint32_t count = ribos_verifier_u32(block + 16);
    uint32_t ordinal;

    for (ordinal = 0; ordinal < count; ++ordinal) {
        const uint8_t *instruction =
            ribos_verifier_row(context->instructions, instruction_id);
        uint32_t operand_count =
            ribos_verifier_u16(instruction + 2);
        uint32_t operand;
        uint32_t result =
            ribos_verifier_u32(instruction + 12);

        for (operand = 0; operand < operand_count; ++operand) {
            uint32_t slot = ribos_verifier_operand_slot(
                context,
                instruction_id,
                operand);
            int ownership = ribos_verifier_type_ownership(
                context,
                ribos_verifier_slot_type(context, slot));

            if (ownership < 0) {
                return ribos_verifier_fail(
                    context,
                    RIBOS_VERIFIER_TYPESTATE_VIOLATION,
                    RIBOS_VERIFIER_SUBJECT_TYPE,
                    ribos_verifier_slot_type(context, slot),
                    slot);
            }
            if (ownership == RIBOS_SCHEMA_OWNERSHIP_COPY) {
                continue;
            }
            if (validate &&
                !ribos_verifier_bits_test(
                    available,
                    slot - first_slot)) {
                return ribos_verifier_fail(
                    context,
                    RIBOS_VERIFIER_OWNERSHIP_VIOLATION,
                    RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
                    instruction_id,
                    slot);
            }
        }
        for (operand = 0; operand < operand_count; ++operand) {
            uint32_t slot = ribos_verifier_operand_slot(
                context,
                instruction_id,
                operand);

            if (ribos_verifier_type_ownership(
                    context,
                    ribos_verifier_slot_type(context, slot)) >
                    RIBOS_SCHEMA_OWNERSHIP_COPY &&
                ribos_verifier_operand_is_consumed(
                    context,
                    instruction_id,
                    operand)) {
                ribos_verifier_bits_clear(
                    available,
                    slot - first_slot);
            }
        }
        if (result != RIBOS_ARTIFACT_INVALID_ID &&
            ribos_verifier_type_ownership(
                context,
                ribos_verifier_slot_type(context, result)) >
                RIBOS_SCHEMA_OWNERSHIP_COPY) {
            ribos_verifier_bits_set(
                available,
                result - first_slot);
        }
        instruction_id = ribos_verifier_u32(instruction + 32);
    }
    return RIBOS_VERIFIER_OK;
}

static RibosVerifierStatus
ribos_verifier_validate_function_ownership(
    RibosVerifierContext *context,
    uint32_t function_id)
{
    const uint8_t *function =
        ribos_verifier_row(context->functions, function_id);
    uint32_t first_block = ribos_verifier_u32(function + 16);
    uint32_t block_count = ribos_verifier_u32(function + 20);
    uint32_t first_slot = ribos_verifier_u32(function + 24);
    uint32_t slot_count = ribos_verifier_u32(function + 28);
    uint32_t entry =
        ribos_verifier_u32(function + 12) - first_block;
    size_t words = (slot_count + 63u) / 64u;
    uint32_t iteration;
    uint32_t local;
    int changed = 0;
    RibosVerifierStatus status =
        ribos_verifier_validate_function_cfg(context, function_id);

    if (status != RIBOS_VERIFIER_OK) {
        return status;
    }
    memset(
        context->out_bits,
        0,
        (size_t)block_count * words * sizeof(*context->out_bits));
    for (local = 0; local < block_count; ++local) {
        uint64_t *out = ribos_verifier_block_bits(
            context->out_bits,
            local,
            words);

        if (local != entry) {
            memset(out, 0xff, words * sizeof(*out));
            if (slot_count % 64 != 0) {
                out[words - 1] &=
                    (UINT64_C(1) << (slot_count % 64)) - 1;
            }
        }
    }
    for (iteration = 0; iteration <= block_count; ++iteration) {
        changed = 0;
        for (local = 0; local < block_count; ++local) {
            uint64_t *out = ribos_verifier_block_bits(
                context->out_bits,
                local,
                words);
            uint32_t predecessor;

            if (context->block_reachable[local] == 0) {
                continue;
            }
            if (local == entry) {
                memset(
                    context->temporary_bits,
                    0,
                    words * sizeof(*context->temporary_bits));
            } else {
                int has_predecessor = 0;

                memset(
                    context->temporary_bits,
                    0xff,
                    words * sizeof(*context->temporary_bits));
                for (predecessor = 0;
                     predecessor <
                        context->predecessor_counts[local];
                     ++predecessor) {
                    uint32_t source = context->predecessors[
                        context->predecessor_starts[local] +
                            predecessor];

                    if (context->block_reachable[source] == 0) {
                        continue;
                    }
                    ribos_verifier_bits_intersect(
                        context->temporary_bits,
                        ribos_verifier_block_bits(
                            context->out_bits,
                            source,
                            words),
                        words);
                    has_predecessor = 1;
                }
                if (!has_predecessor) {
                    memset(
                        context->temporary_bits,
                        0,
                        words * sizeof(*context->temporary_bits));
                }
            }
            status = ribos_verifier_transfer_ownership_block(
                context,
                first_block + local,
                first_slot,
                context->temporary_bits,
                0);
            if (status != RIBOS_VERIFIER_OK) {
                return status;
            }
            if (!ribos_verifier_bits_equal(
                    out,
                    context->temporary_bits,
                    words)) {
                ribos_verifier_bits_copy(
                    out,
                    context->temporary_bits,
                    words);
                changed = 1;
            }
        }
        if (!changed) {
            break;
        }
    }
    if (changed) {
        return ribos_verifier_fail(
            context,
            RIBOS_VERIFIER_OWNERSHIP_VIOLATION,
            RIBOS_VERIFIER_SUBJECT_FUNCTION,
            function_id,
            block_count);
    }
    for (local = 0; local < block_count; ++local) {
        uint32_t predecessor;

        if (context->block_reachable[local] == 0) {
            continue;
        }
        if (local == entry) {
            memset(
                context->temporary_bits,
                0,
                words * sizeof(*context->temporary_bits));
        } else {
            int has_predecessor = 0;

            memset(
                context->temporary_bits,
                0xff,
                words * sizeof(*context->temporary_bits));
            for (predecessor = 0;
                 predecessor < context->predecessor_counts[local];
                 ++predecessor) {
                uint32_t source = context->predecessors[
                    context->predecessor_starts[local] + predecessor];

                if (context->block_reachable[source] == 0) {
                    continue;
                }
                ribos_verifier_bits_intersect(
                    context->temporary_bits,
                    ribos_verifier_block_bits(
                        context->out_bits,
                        source,
                        words),
                    words);
                has_predecessor = 1;
            }
            if (!has_predecessor) {
                memset(
                    context->temporary_bits,
                    0,
                    words * sizeof(*context->temporary_bits));
            }
        }
        status = ribos_verifier_transfer_ownership_block(
            context,
            first_block + local,
            first_slot,
            context->temporary_bits,
            1);
        if (status != RIBOS_VERIFIER_OK) {
            return status;
        }
    }
    return RIBOS_VERIFIER_OK;
}

static RibosVerifierStatus
ribos_verifier_validate_schema_semantics(
    RibosVerifierContext *context)
{
    const uint8_t *entry = ribos_verifier_row(
        context->functions,
        context->artifact.entry_function);
    uint32_t parameter_start = ribos_verifier_u32(entry + 32);
    uint32_t return_type = ribos_verifier_u32(entry + 8);
    const uint8_t *return_row =
        ribos_verifier_row(context->types, return_type);
    uint32_t type_id;
    uint32_t instruction_id;

    if (ribos_verifier_u32(entry + 36) != 1 ||
        !ribos_verifier_type_matches_spelling(
            context,
            ribos_verifier_slot_type(context, parameter_start),
            context->schema->policy_context_type) ||
        return_row == NULL ||
        ribos_verifier_u16(return_row + 4) !=
            RIBOS_BC_TYPE_RESULT ||
        !ribos_verifier_type_matches_spelling(
            context,
            ribos_verifier_u32(return_row + 8),
            context->schema->policy_action_type) ||
        !ribos_verifier_type_matches_spelling(
            context,
            ribos_verifier_u32(return_row + 12),
            context->schema->policy_error_type)) {
        return ribos_verifier_fail(
            context,
            RIBOS_VERIFIER_TYPESTATE_VIOLATION,
            RIBOS_VERIFIER_SUBJECT_FUNCTION,
            context->artifact.entry_function,
            return_type);
    }
    for (type_id = 0; type_id < context->types->count; ++type_id) {
        if (ribos_verifier_type_ownership(context, type_id) < 0) {
            return ribos_verifier_fail(
                context,
                RIBOS_VERIFIER_TYPESTATE_VIOLATION,
                RIBOS_VERIFIER_SUBJECT_TYPE,
                type_id,
                0);
        }
    }
    for (instruction_id = 0;
         instruction_id < context->instructions->count;
         ++instruction_id) {
        const uint8_t *instruction =
            ribos_verifier_row(context->instructions, instruction_id);
        uint32_t result = ribos_verifier_u32(instruction + 12);
        const RibosSchemaType *result_schema = result ==
                RIBOS_ARTIFACT_INVALID_ID ?
            NULL :
            ribos_verifier_schema_type(
                context,
                ribos_verifier_slot_type(context, result));

        if (instruction[0] == RIBOS_BC_CONST_SYMBOL &&
            result_schema != NULL &&
            (result_schema->type_class ==
                RIBOS_SCHEMA_TYPE_OPAQUE_HANDLE ||
             result_schema->type_class == RIBOS_SCHEMA_TYPE_FACT ||
             result_schema->ownership !=
                RIBOS_SCHEMA_OWNERSHIP_COPY)) {
            return ribos_verifier_fail(
                context,
                RIBOS_VERIFIER_OPAQUE_FORGERY,
                RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
                instruction_id,
                result_schema->stable_id);
        }
        if (result_schema != NULL &&
            result_schema->type_class ==
                RIBOS_SCHEMA_TYPE_OPAQUE_HANDLE &&
            instruction[0] != RIBOS_BC_PARAMETER &&
            instruction[0] != RIBOS_BC_CALL_HELPER &&
            instruction[0] != RIBOS_BC_CALL_DIRECT &&
            instruction[0] != RIBOS_BC_MOVE &&
            instruction[0] != RIBOS_BC_MEMBER &&
            instruction[0] != RIBOS_BC_INDEX &&
            instruction[0] != RIBOS_BC_VARIANT_PAYLOAD) {
            return ribos_verifier_fail(
                context,
                RIBOS_VERIFIER_OPAQUE_FORGERY,
                RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
                instruction_id,
                result_schema->stable_id);
        }
        if (instruction[0] == RIBOS_BC_CALL_HELPER) {
            const RibosSchemaHelper *helper =
                ribos_verifier_schema_helper(
                    context->schema,
                    ribos_verifier_u32(instruction + 20));

            if (helper == NULL) {
                return ribos_verifier_fail(
                    context,
                    RIBOS_VERIFIER_INVALID_TARGET,
                    RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
                    instruction_id,
                    ribos_verifier_u32(instruction + 20));
            }
            if ((helper->flags &
                 RIBOS_SCHEMA_HELPER_TYPESTATE_TRANSITION) != 0) {
                uint32_t operand = ribos_verifier_operand_slot(
                    context,
                    instruction_id,
                    helper->transition_parameter);
                uint32_t input_type =
                    ribos_verifier_slot_type(context, operand);
                uint32_t output_type =
                    ribos_verifier_slot_type(context, result);
                const uint8_t *output_row =
                    ribos_verifier_row(context->types, output_type);
                int input_ownership = ribos_verifier_type_ownership(
                    context,
                    input_type);
                int output_ownership;

                if (helper->error_type != NULL &&
                    output_row != NULL &&
                    ribos_verifier_u16(output_row + 4) ==
                        RIBOS_BC_TYPE_RESULT) {
                    output_type = ribos_verifier_u32(output_row + 8);
                }
                output_ownership = ribos_verifier_type_ownership(
                    context,
                    output_type);
                if (input_ownership <=
                        RIBOS_SCHEMA_OWNERSHIP_COPY ||
                    helper->parameters[
                        helper->transition_parameter
                    ].mode != RIBOS_SCHEMA_PARAMETER_CONSUME ||
                    (output_ownership <=
                        RIBOS_SCHEMA_OWNERSHIP_COPY &&
                     !ribos_verifier_type_matches_spelling(
                         context,
                         output_type,
                         "Unit")) ||
                    output_type == input_type) {
                    return ribos_verifier_fail(
                        context,
                        RIBOS_VERIFIER_TYPESTATE_VIOLATION,
                        RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
                        instruction_id,
                        helper->transition_parameter);
                }
            }
            if ((helper->flags &
                 RIBOS_SCHEMA_HELPER_TERMINAL_BOOT_ACTION) != 0 &&
                ((helper->capabilities &
                  RIBOS_CAPABILITY_BOOT) == 0 ||
                 helper->error_type != NULL ||
                 !ribos_verifier_type_matches_spelling(
                     context,
                     ribos_verifier_slot_type(context, result),
                     context->schema->policy_action_type))) {
                return ribos_verifier_fail(
                    context,
                    RIBOS_VERIFIER_TYPESTATE_VIOLATION,
                    RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
                    instruction_id,
                    helper->stable_id);
            }
        }
    }
    return RIBOS_VERIFIER_OK;
}

static int
ribos_verifier_return_variant(
    RibosVerifierContext *context,
    uint32_t block_id,
    uint32_t slot_id,
    uint32_t *tag)
{
    const uint8_t *block =
        ribos_verifier_row(context->blocks, block_id);
    uint32_t count = ribos_verifier_u32(block + 16);
    uint32_t hop;

    for (hop = 0; hop < count; ++hop) {
        uint32_t instruction_id = ribos_verifier_u32(block + 8);
        uint32_t definition = RIBOS_ARTIFACT_INVALID_ID;
        uint32_t ordinal;
        const uint8_t *instruction;

        for (ordinal = 0; ordinal < count; ++ordinal) {
            const uint8_t *candidate =
                ribos_verifier_row(
                    context->instructions,
                    instruction_id);

            if (ribos_verifier_u32(candidate + 12) == slot_id) {
                definition = instruction_id;
            }
            instruction_id =
                ribos_verifier_u32(candidate + 32);
        }
        if (definition == RIBOS_ARTIFACT_INVALID_ID) {
            return 0;
        }
        instruction = ribos_verifier_row(
            context->instructions,
            definition);

        if (instruction[0] == RIBOS_BC_BUILD_VARIANT) {
            *tag = ribos_verifier_u32(instruction + 20);
            return 1;
        }
        if (instruction[0] != RIBOS_BC_MOVE) {
            return 0;
        }
        slot_id = ribos_verifier_operand_slot(
            context,
            definition,
            0);
    }
    return 0;
}

static RibosVerifierPathBound
ribos_verifier_analyze_loop(
    RibosVerifierPathContext *path,
    const uint8_t *loop)
{
    RibosVerifierContext *context = path->verifier;
    RibosVerifierPathBound result = {0};
    RibosVerifierPathBound body;
    RibosVerifierPathBound exit;
    uint32_t header = ribos_verifier_u32(loop + 8);
    uint32_t trip_count = ribos_verifier_u32(loop + 24);
    uint64_t header_weight =
        ribos_verifier_block_weight(path, header);
    uint64_t cycle = 0;
    uint64_t repeat_prefix = 0;
    int failed = 0;

    body = ribos_verifier_analyze_region(
        context,
        path->function_id,
        ribos_verifier_u32(loop + 12),
        header,
        path->metric,
        path->helper_import,
        path->depth + 1,
        &failed);
    exit = ribos_verifier_analyze_node(
        path,
        ribos_verifier_u32(loop + 16));
    if (failed || path->failed) {
        path->failed = 1;
        return result;
    }
    if (body.has_stop) {
        RibosVerifierPathBound normal = exit;
        uint64_t repeated;

        if (!ribos_verifier_add_u64(
                header_weight,
                body.stop,
                &cycle) ||
            !ribos_verifier_multiply_u64(
                cycle,
                trip_count,
                &repeated) ||
            !ribos_verifier_add_u64(
                repeated,
                header_weight,
                &repeated) ||
            !ribos_verifier_path_prepend(&normal, repeated)) {
            path->failed = 1;
            return result;
        }
        ribos_verifier_path_merge(&result, &normal);
        if (trip_count > 0 &&
            !ribos_verifier_multiply_u64(
                cycle,
                trip_count - 1,
                &repeat_prefix)) {
            path->failed = 1;
            return result;
        }
    } else {
        RibosVerifierPathBound zero_iteration = exit;

        if (!ribos_verifier_path_prepend(
                &zero_iteration,
                header_weight)) {
            path->failed = 1;
            return result;
        }
        ribos_verifier_path_merge(&result, &zero_iteration);
    }
    if (body.has_return) {
        RibosVerifierPathBound terminal = {
            .returned = body.returned,
            .has_return = 1,
        };
        uint64_t prefix;

        if (!ribos_verifier_add_u64(
                repeat_prefix,
                header_weight,
                &prefix) ||
            !ribos_verifier_path_prepend(&terminal, prefix)) {
            path->failed = 1;
            return result;
        }
        ribos_verifier_path_merge(&result, &terminal);
    }
    if (body.has_trap) {
        RibosVerifierPathBound terminal = {
            .trapped = body.trapped,
            .has_trap = 1,
        };
        uint64_t prefix;

        if (!ribos_verifier_add_u64(
                repeat_prefix,
                header_weight,
                &prefix) ||
            !ribos_verifier_path_prepend(&terminal, prefix)) {
            path->failed = 1;
            return result;
        }
        ribos_verifier_path_merge(&result, &terminal);
    }
    return result;
}

static RibosVerifierPathBound
ribos_verifier_analyze_node(
    RibosVerifierPathContext *path,
    uint32_t block_id)
{
    RibosVerifierContext *context = path->verifier;
    RibosVerifierPathBound result = {0};
    const uint8_t *block;
    const uint8_t *terminal;
    const uint8_t *loop;
    uint64_t weight;
    size_t index;

    if (block_id == path->stop_block) {
        result.has_stop = 1;
        return result;
    }
    block = ribos_verifier_row(context->blocks, block_id);
    if (block == NULL ||
        ribos_verifier_u32(block + 4) != path->function_id) {
        path->failed = 1;
        return result;
    }
    index = (size_t)path->depth * context->blocks->count + block_id;
    if (context->path_states[index] == 2) {
        return context->path_memo[index];
    }
    if (context->path_states[index] == 1) {
        path->failed = 1;
        return result;
    }
    context->path_states[index] = 1;
    loop = ribos_verifier_loop_for_header(context, block_id);
    if (loop != NULL) {
        result = ribos_verifier_analyze_loop(path, loop);
    } else {
        terminal = ribos_verifier_row(
            context->instructions,
            ribos_verifier_u32(block + 12));
        weight = ribos_verifier_block_weight(path, block_id);
        if (terminal[0] == RIBOS_BC_RETURN) {
            result.has_return = 1;
            result.returned = weight;
        } else if (terminal[0] == RIBOS_BC_TRAP) {
            result.has_trap = 1;
            result.trapped = weight;
        } else if (terminal[0] == RIBOS_BC_JUMP) {
            result = ribos_verifier_analyze_node(
                path,
                ribos_verifier_u32(terminal + 20));
            if (!ribos_verifier_path_prepend(&result, weight)) {
                path->failed = 1;
            }
        } else if (terminal[0] == RIBOS_BC_BRANCH) {
            RibosVerifierPathBound left =
                ribos_verifier_analyze_node(
                    path,
                    ribos_verifier_u32(terminal + 20));
            RibosVerifierPathBound right =
                ribos_verifier_analyze_node(
                    path,
                    ribos_verifier_u32(terminal + 24));

            ribos_verifier_path_merge(&result, &left);
            ribos_verifier_path_merge(&result, &right);
            if (!ribos_verifier_path_prepend(&result, weight)) {
                path->failed = 1;
            }
        } else {
            path->failed = 1;
        }
    }
    context->path_states[index] = path->failed ? 0 : 2;
    if (!path->failed) {
        context->path_memo[index] = result;
    }
    return result;
}

static uint8_t
ribos_verifier_action_shift(uint8_t mask)
{
    return (uint8_t)(((mask & 1u) << 1) |
        ((mask & 2u) << 1) |
        (mask & 4u));
}

static uint8_t
ribos_verifier_block_action_mask(
    RibosVerifierContext *context,
    uint32_t block_id,
    uint8_t incoming,
    RibosVerifierStatus *status)
{
    const uint8_t *block =
        ribos_verifier_row(context->blocks, block_id);
    uint32_t instruction_id = ribos_verifier_u32(block + 8);
    uint32_t count = ribos_verifier_u32(block + 16);
    uint32_t ordinal;
    int after_terminal = 0;

    for (ordinal = 0; ordinal < count; ++ordinal) {
        const uint8_t *instruction =
            ribos_verifier_row(context->instructions, instruction_id);
        int terminal_helper = 0;

        if (instruction[0] == RIBOS_BC_CALL_HELPER) {
            const RibosSchemaHelper *helper =
                ribos_verifier_schema_helper(
                    context->schema,
                    ribos_verifier_u32(instruction + 20));

            terminal_helper = helper != NULL &&
                (helper->flags &
                 RIBOS_SCHEMA_HELPER_TERMINAL_BOOT_ACTION) != 0;
        }
        if (terminal_helper) {
            incoming = ribos_verifier_action_shift(incoming);
            after_terminal = 1;
        } else if (after_terminal &&
            (instruction[0] == RIBOS_BC_CALL_DIRECT ||
             instruction[0] == RIBOS_BC_JUMP ||
             instruction[0] == RIBOS_BC_BRANCH ||
             instruction[0] == RIBOS_BC_TRAP)) {
            *status = ribos_verifier_fail(
                context,
                RIBOS_VERIFIER_TERMINAL_ACTION_VIOLATION,
                RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
                instruction_id,
                instruction[0]);
            return 0;
        }
        instruction_id = ribos_verifier_u32(instruction + 32);
    }
    return incoming;
}

static RibosVerifierStatus
ribos_verifier_validate_terminal_actions(
    RibosVerifierContext *context)
{
    uint32_t entry_id = context->artifact.entry_function;
    const uint8_t *function =
        ribos_verifier_row(context->functions, entry_id);
    uint32_t first_block = ribos_verifier_u32(function + 16);
    uint32_t block_count = ribos_verifier_u32(function + 20);
    uint32_t entry = ribos_verifier_u32(function + 12);
    uint32_t function_id;
    uint32_t iteration;
    int changed = 0;

    for (function_id = 0;
         function_id < context->functions->count;
         ++function_id) {
        const uint8_t *candidate =
            ribos_verifier_row(context->functions, function_id);
        uint32_t local;

        for (local = 0;
             local < ribos_verifier_u32(candidate + 20);
             ++local) {
            const uint8_t *block = ribos_verifier_row(
                context->blocks,
                ribos_verifier_u32(candidate + 16) + local);
            uint32_t instruction_id =
                ribos_verifier_u32(block + 8);
            uint32_t count = ribos_verifier_u32(block + 16);
            uint32_t ordinal;

            for (ordinal = 0; ordinal < count; ++ordinal) {
                const uint8_t *instruction =
                    ribos_verifier_row(
                        context->instructions,
                        instruction_id);

                if (instruction[0] == RIBOS_BC_CALL_HELPER) {
                    const RibosSchemaHelper *helper =
                        ribos_verifier_schema_helper(
                            context->schema,
                            ribos_verifier_u32(instruction + 20));

                    if (helper != NULL &&
                        (helper->flags &
                         RIBOS_SCHEMA_HELPER_TERMINAL_BOOT_ACTION) !=
                            0 &&
                        function_id != entry_id) {
                        return ribos_verifier_fail(
                            context,
                            RIBOS_VERIFIER_TERMINAL_ACTION_VIOLATION,
                            RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
                            instruction_id,
                            function_id);
                    }
                }
                instruction_id =
                    ribos_verifier_u32(instruction + 32);
            }
        }
    }
    memset(context->action_masks, 0, context->blocks->count);
    context->action_masks[entry] = 1;
    for (iteration = 0; iteration <= block_count * 3u; ++iteration) {
        uint32_t local;

        changed = 0;
        for (local = 0; local < block_count; ++local) {
            uint32_t block_id = first_block + local;
            uint32_t targets[2];
            uint32_t target_count;
            uint32_t target;
            RibosVerifierStatus status = RIBOS_VERIFIER_OK;
            uint8_t outgoing;

            if (context->action_masks[block_id] == 0) {
                continue;
            }
            outgoing = ribos_verifier_block_action_mask(
                context,
                block_id,
                context->action_masks[block_id],
                &status);
            if (status != RIBOS_VERIFIER_OK) {
                return status;
            }
            target_count = ribos_verifier_successors(
                context,
                block_id,
                targets);
            for (target = 0; target < target_count; ++target) {
                uint8_t merged =
                    context->action_masks[targets[target]] | outgoing;

                if (merged != context->action_masks[targets[target]]) {
                    context->action_masks[targets[target]] = merged;
                    changed = 1;
                }
            }
        }
        if (!changed) {
            break;
        }
    }
    if (changed) {
        return ribos_verifier_fail(
            context,
            RIBOS_VERIFIER_TERMINAL_ACTION_VIOLATION,
            RIBOS_VERIFIER_SUBJECT_FUNCTION,
            entry_id,
            block_count);
    }
    for (iteration = 0; iteration < block_count; ++iteration) {
        uint32_t block_id = first_block + iteration;
        const uint8_t *block;
        const uint8_t *terminal;
        RibosVerifierStatus status = RIBOS_VERIFIER_OK;
        uint8_t outgoing;

        if (context->action_masks[block_id] == 0) {
            continue;
        }
        block = ribos_verifier_row(context->blocks, block_id);
        terminal = ribos_verifier_row(
            context->instructions,
            ribos_verifier_u32(block + 12));
        outgoing = ribos_verifier_block_action_mask(
            context,
            block_id,
            context->action_masks[block_id],
            &status);
        if (status != RIBOS_VERIFIER_OK) {
            return status;
        }
        if (terminal[0] == RIBOS_BC_TRAP) {
            if (outgoing != 1u) {
                return ribos_verifier_fail(
                    context,
                    RIBOS_VERIFIER_FAIL_CLOSED_VIOLATION,
                    RIBOS_VERIFIER_SUBJECT_BLOCK,
                    block_id,
                    outgoing);
            }
        } else if (terminal[0] == RIBOS_BC_RETURN) {
            uint32_t tag;
            uint32_t result = ribos_verifier_operand_slot(
                context,
                ribos_verifier_u32(terminal + 4),
                0);

            if (!ribos_verifier_return_variant(
                    context,
                    block_id,
                    result,
                    &tag) ||
                tag > 1) {
                return ribos_verifier_fail(
                    context,
                    RIBOS_VERIFIER_FAIL_CLOSED_VIOLATION,
                    RIBOS_VERIFIER_SUBJECT_INSTRUCTION,
                    ribos_verifier_u32(terminal + 4),
                    result);
            }
            if (tag == 0) {
                if (outgoing != 2u) {
                    return ribos_verifier_fail(
                        context,
                        RIBOS_VERIFIER_TERMINAL_ACTION_VIOLATION,
                        RIBOS_VERIFIER_SUBJECT_BLOCK,
                        block_id,
                        outgoing);
                }
            } else if (outgoing != 1u) {
                return ribos_verifier_fail(
                    context,
                    RIBOS_VERIFIER_FAIL_CLOSED_VIOLATION,
                    RIBOS_VERIFIER_SUBJECT_BLOCK,
                    block_id,
                    outgoing);
            }
        }
    }
    return RIBOS_VERIFIER_OK;
}

static RibosVerifierStatus
ribos_verifier_validate_stage2(RibosVerifierContext *context)
{
    uint32_t function_id;
    uint32_t entry = context->artifact.entry_function;
    RibosVerifierStatus status =
        ribos_verifier_validate_schema_semantics(context);

    if (status != RIBOS_VERIFIER_OK) {
        return status;
    }
    for (function_id = 0;
         function_id < context->functions->count;
         ++function_id) {
        status = ribos_verifier_validate_function_ownership(
            context,
            function_id);
        if (status != RIBOS_VERIFIER_OK) {
            return status;
        }
    }
    memset(
        context->resource_states,
        0,
        context->functions->count);
    for (function_id = 0;
         function_id < context->functions->count;
         ++function_id) {
        status = ribos_verifier_close_stage2_resources(
            context,
            function_id);
        if (status != RIBOS_VERIFIER_OK) {
            return status;
        }
    }
    status = ribos_verifier_validate_helper_bounds(context);
    if (status != RIBOS_VERIFIER_OK) {
        return status;
    }
    if (context->artifact.required_capabilities !=
            context->reachable_capabilities[entry] ||
        context->artifact.instruction_upper_bound !=
            context->instruction_bounds[entry] ||
        context->artifact.helper_upper_bound !=
            context->helper_total_bounds[entry]) {
        return ribos_verifier_fail(
            context,
            RIBOS_VERIFIER_RESOURCE_MISMATCH,
            RIBOS_VERIFIER_SUBJECT_FUNCTION,
            entry,
            0);
    }
    status = ribos_verifier_validate_terminal_actions(context);
    if (status != RIBOS_VERIFIER_OK) {
        return status;
    }
    context->report->recomputed_reachable_capabilities =
        context->reachable_capabilities[entry];
    context->report->recomputed_instruction_upper_bound =
        context->instruction_bounds[entry];
    context->report->recomputed_helper_upper_bound =
        context->helper_total_bounds[entry];
    return RIBOS_VERIFIER_OK;
}

static RibosVerifierStatus
ribos_verifier_open(
    const uint8_t *artifact,
    size_t artifact_size,
    RibosArtifactView *view,
    RibosVerifierReport *report)
{
    RibosArtifactStatus status;

    if (artifact == NULL || view == NULL) {
        if (report != NULL) {
            report->status = RIBOS_VERIFIER_INVALID_ARGUMENT;
        }
        return RIBOS_VERIFIER_INVALID_ARGUMENT;
    }
    status = ribos_artifact_open_v1(artifact, artifact_size, view);
    if (status != RIBOS_ARTIFACT_OK) {
        if (report != NULL) {
            report->status = RIBOS_VERIFIER_STRUCTURAL_ERROR;
            report->subject = RIBOS_VERIFIER_SUBJECT_ARTIFACT;
            report->subject_id = RIBOS_ARTIFACT_INVALID_ID;
            report->detail = (uint32_t)status;
        }
        return RIBOS_VERIFIER_STRUCTURAL_ERROR;
    }
    return RIBOS_VERIFIER_OK;
}

static int
ribos_verifier_measure_workspace(
    const RibosArtifactView *view,
    size_t *required_size,
    size_t *maximum_bit_words)
{
    const RibosArtifactSectionView *types =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_TYPES);
    const RibosArtifactSectionView *functions =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_FUNCTIONS);
    const RibosArtifactSectionView *blocks =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_BLOCKS);
    const RibosArtifactSectionView *loops =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_LOOPS);
    const RibosArtifactSectionView *slots =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_SLOTS);
    const RibosArtifactSectionView *instructions =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_INSTRUCTIONS);
    const RibosArtifactSectionView *helper_imports =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_HELPER_IMPORTS);
    size_t maximum_matrix = 0;
    size_t function_square;
    size_t function_helpers;
    size_t path_matrix;
    size_t size = 7;
    uint32_t index;

    if (types == NULL || functions == NULL || blocks == NULL ||
        loops == NULL ||
        slots == NULL || instructions == NULL || helper_imports == NULL ||
        functions->count == 0) {
        return 0;
    }
    for (index = 0; index < functions->count; ++index) {
        const uint8_t *function =
            ribos_verifier_row(functions, index);
        uint32_t block_count = ribos_verifier_u32(function + 20);
        uint32_t slot_count = ribos_verifier_u32(function + 28);
        size_t words = (slot_count + 63u) / 64u;
        size_t matrix;

        if (block_count > blocks->count ||
            slot_count > slots->count ||
            !ribos_verifier_size_multiply(
                block_count,
                words,
                &matrix)) {
            return 0;
        }
        if (matrix > maximum_matrix) {
            maximum_matrix = matrix;
        }
    }
    if (!ribos_verifier_size_multiply(
            functions->count,
            functions->count,
            &function_square) ||
        !ribos_verifier_size_multiply(
            functions->count,
            helper_imports->count,
            &function_helpers) ||
        !ribos_verifier_size_multiply(
            blocks->count,
            (size_t)loops->count + 1u,
            &path_matrix) ||
        !ribos_verifier_workspace_add(
            &size,
            _Alignof(RibosVerifierTypeLayout),
            types->count,
            sizeof(RibosVerifierTypeLayout)) ||
        !ribos_verifier_workspace_add(
            &size, 1, types->count, sizeof(uint8_t)) ||
        !ribos_verifier_workspace_add(
            &size, 1, instructions->count, sizeof(uint8_t)) ||
        !ribos_verifier_workspace_add(
            &size, 1, blocks->count, sizeof(uint8_t)) ||
        !ribos_verifier_workspace_add(
            &size,
            _Alignof(uint32_t),
            blocks->count,
            sizeof(uint32_t)) ||
        !ribos_verifier_workspace_add(
            &size,
            _Alignof(uint32_t),
            blocks->count,
            sizeof(uint32_t)) ||
        !ribos_verifier_workspace_add(
            &size,
            _Alignof(uint32_t),
            blocks->count,
            sizeof(uint32_t)) ||
        !ribos_verifier_workspace_add(
            &size,
            _Alignof(uint32_t),
            (size_t)blocks->count * 2u,
            sizeof(uint32_t)) ||
        !ribos_verifier_workspace_add(
            &size,
            _Alignof(uint64_t),
            maximum_matrix,
            sizeof(uint64_t)) ||
        !ribos_verifier_workspace_add(
            &size,
            _Alignof(uint64_t),
            maximum_matrix,
            sizeof(uint64_t)) ||
        !ribos_verifier_workspace_add(
            &size,
            _Alignof(uint64_t),
            (slots->count + 63u) / 64u,
            sizeof(uint64_t)) ||
        !ribos_verifier_workspace_add(
            &size, 1, function_square, sizeof(uint8_t)) ||
        !ribos_verifier_workspace_add(
            &size, 1, functions->count, sizeof(uint8_t)) ||
        !ribos_verifier_workspace_add(
            &size,
            _Alignof(uint32_t),
            functions->count,
            sizeof(uint32_t)) ||
        !ribos_verifier_workspace_add(
            &size,
            _Alignof(uint32_t),
            functions->count,
            sizeof(uint32_t)) ||
        !ribos_verifier_workspace_add(
            &size,
            _Alignof(uint32_t),
            functions->count,
            sizeof(uint32_t)) ||
        !ribos_verifier_workspace_add(
            &size,
            _Alignof(uint64_t),
            functions->count,
            sizeof(uint64_t)) ||
        !ribos_verifier_workspace_add(
            &size, 1, types->count, sizeof(uint8_t)) ||
        !ribos_verifier_workspace_add(
            &size, 1, functions->count, sizeof(uint8_t)) ||
        !ribos_verifier_workspace_add(
            &size,
            _Alignof(uint32_t),
            functions->count,
            sizeof(uint32_t)) ||
        !ribos_verifier_workspace_add(
            &size,
            _Alignof(uint64_t),
            functions->count,
            sizeof(uint64_t)) ||
        !ribos_verifier_workspace_add(
            &size,
            _Alignof(uint64_t),
            functions->count,
            sizeof(uint64_t)) ||
        !ribos_verifier_workspace_add(
            &size,
            _Alignof(uint64_t),
            function_helpers,
            sizeof(uint64_t)) ||
        !ribos_verifier_workspace_add(
            &size, 1, path_matrix, sizeof(uint8_t)) ||
        !ribos_verifier_workspace_add(
            &size,
            _Alignof(RibosVerifierPathBound),
            path_matrix,
            sizeof(RibosVerifierPathBound)) ||
        !ribos_verifier_workspace_add(
            &size, 1, blocks->count, sizeof(uint8_t))) {
        return 0;
    }
    *required_size = size;
    *maximum_bit_words = maximum_matrix;
    return 1;
}

static int
ribos_verifier_bind_sections(RibosVerifierContext *context)
{
#define RIBOS_BIND(member, kind) \
    do { \
        context->member = ribos_artifact_find_section( \
            &context->artifact, (kind)); \
        if (context->member == NULL) { \
            return 0; \
        } \
    } while (0)

    RIBOS_BIND(types, RIBOS_ARTIFACT_SECTION_TYPES);
    RIBOS_BIND(shapes, RIBOS_ARTIFACT_SECTION_SHAPES);
    RIBOS_BIND(constants, RIBOS_ARTIFACT_SECTION_CONSTANTS);
    RIBOS_BIND(
        constant_bytes,
        RIBOS_ARTIFACT_SECTION_CONSTANT_BYTES);
    RIBOS_BIND(functions, RIBOS_ARTIFACT_SECTION_FUNCTIONS);
    RIBOS_BIND(blocks, RIBOS_ARTIFACT_SECTION_BLOCKS);
    RIBOS_BIND(loops, RIBOS_ARTIFACT_SECTION_LOOPS);
    RIBOS_BIND(slots, RIBOS_ARTIFACT_SECTION_SLOTS);
    RIBOS_BIND(
        instructions,
        RIBOS_ARTIFACT_SECTION_INSTRUCTIONS);
    RIBOS_BIND(operands, RIBOS_ARTIFACT_SECTION_OPERANDS);
    RIBOS_BIND(
        helper_imports,
        RIBOS_ARTIFACT_SECTION_HELPER_IMPORTS);
    RIBOS_BIND(
        helper_bounds,
        RIBOS_ARTIFACT_SECTION_HELPER_BOUNDS);
    RIBOS_BIND(
        source_maps,
        RIBOS_ARTIFACT_SECTION_SOURCE_MAPS);
#undef RIBOS_BIND
    return 1;
}

static int
ribos_verifier_allocate_workspace(
    RibosVerifierContext *context,
    RibosVerifierArena *arena,
    size_t maximum_bit_words)
{
    size_t function_square =
        (size_t)context->functions->count *
        context->functions->count;
    size_t path_matrix =
        (size_t)context->blocks->count *
        ((size_t)context->loops->count + 1u);

    context->type_layouts = ribos_verifier_arena_take(
        arena,
        _Alignof(RibosVerifierTypeLayout),
        context->types->count,
        sizeof(*context->type_layouts));
    context->type_states = ribos_verifier_arena_take(
        arena, 1, context->types->count, sizeof(*context->type_states));
    context->instruction_seen = ribos_verifier_arena_take(
        arena,
        1,
        context->instructions->count,
        sizeof(*context->instruction_seen));
    context->block_reachable = ribos_verifier_arena_take(
        arena,
        1,
        context->blocks->count,
        sizeof(*context->block_reachable));
    context->predecessor_counts = ribos_verifier_arena_take(
        arena,
        _Alignof(uint32_t),
        context->blocks->count,
        sizeof(*context->predecessor_counts));
    context->predecessor_starts = ribos_verifier_arena_take(
        arena,
        _Alignof(uint32_t),
        context->blocks->count,
        sizeof(*context->predecessor_starts));
    context->predecessor_cursor = ribos_verifier_arena_take(
        arena,
        _Alignof(uint32_t),
        context->blocks->count,
        sizeof(*context->predecessor_cursor));
    context->predecessors = ribos_verifier_arena_take(
        arena,
        _Alignof(uint32_t),
        (size_t)context->blocks->count * 2u,
        sizeof(*context->predecessors));
    context->definition_bits = ribos_verifier_arena_take(
        arena,
        _Alignof(uint64_t),
        maximum_bit_words,
        sizeof(*context->definition_bits));
    context->out_bits = ribos_verifier_arena_take(
        arena,
        _Alignof(uint64_t),
        maximum_bit_words,
        sizeof(*context->out_bits));
    context->temporary_bits = ribos_verifier_arena_take(
        arena,
        _Alignof(uint64_t),
        (context->slots->count + 63u) / 64u,
        sizeof(*context->temporary_bits));
    context->call_edges = ribos_verifier_arena_take(
        arena, 1, function_square, sizeof(*context->call_edges));
    context->call_states = ribos_verifier_arena_take(
        arena,
        1,
        context->functions->count,
        sizeof(*context->call_states));
    context->call_depths = ribos_verifier_arena_take(
        arena,
        _Alignof(uint32_t),
        context->functions->count,
        sizeof(*context->call_depths));
    context->frame_bytes = ribos_verifier_arena_take(
        arena,
        _Alignof(uint32_t),
        context->functions->count,
        sizeof(*context->frame_bytes));
    context->terminal_masks = ribos_verifier_arena_take(
        arena,
        _Alignof(uint32_t),
        context->functions->count,
        sizeof(*context->terminal_masks));
    context->stack_bytes = ribos_verifier_arena_take(
        arena,
        _Alignof(uint64_t),
        context->functions->count,
        sizeof(*context->stack_bytes));
    context->type_ownership = ribos_verifier_arena_take(
        arena,
        1,
        context->types->count,
        sizeof(*context->type_ownership));
    context->resource_states = ribos_verifier_arena_take(
        arena,
        1,
        context->functions->count,
        sizeof(*context->resource_states));
    context->reachable_capabilities = ribos_verifier_arena_take(
        arena,
        _Alignof(uint32_t),
        context->functions->count,
        sizeof(*context->reachable_capabilities));
    context->instruction_bounds = ribos_verifier_arena_take(
        arena,
        _Alignof(uint64_t),
        context->functions->count,
        sizeof(*context->instruction_bounds));
    context->helper_total_bounds = ribos_verifier_arena_take(
        arena,
        _Alignof(uint64_t),
        context->functions->count,
        sizeof(*context->helper_total_bounds));
    context->helper_matrix = ribos_verifier_arena_take(
        arena,
        _Alignof(uint64_t),
        (size_t)context->functions->count *
            context->helper_imports->count,
        sizeof(*context->helper_matrix));
    context->path_states = ribos_verifier_arena_take(
        arena,
        1,
        path_matrix,
        sizeof(*context->path_states));
    context->path_memo = ribos_verifier_arena_take(
        arena,
        _Alignof(RibosVerifierPathBound),
        path_matrix,
        sizeof(*context->path_memo));
    context->action_masks = ribos_verifier_arena_take(
        arena,
        1,
        context->blocks->count,
        sizeof(*context->action_masks));
    context->path_matrix_entries = path_matrix;
    context->bit_matrix_words = maximum_bit_words;
    return !arena->failed;
}

RibosVerifierStatus
ribos_verifier_workspace_size_v1(
    const uint8_t *artifact,
    size_t artifact_size,
    size_t *required_size,
    RibosVerifierReport *report)
{
    RibosArtifactView view;
    size_t maximum_bit_words;
    RibosVerifierStatus status;

    ribos_verifier_initialize_report(report);
    if (required_size == NULL) {
        return RIBOS_VERIFIER_INVALID_ARGUMENT;
    }
    *required_size = 0;
    status = ribos_verifier_open(
        artifact,
        artifact_size,
        &view,
        report);
    if (status != RIBOS_VERIFIER_OK) {
        return status;
    }
    if (!ribos_verifier_measure_workspace(
            &view,
            required_size,
            &maximum_bit_words)) {
        if (report != NULL) {
            report->status = RIBOS_VERIFIER_STRUCTURAL_ERROR;
            report->detail = RIBOS_ARTIFACT_SECTION_FUNCTIONS;
        }
        return RIBOS_VERIFIER_STRUCTURAL_ERROR;
    }
    if (report != NULL) {
        report->status = RIBOS_VERIFIER_OK;
    }
    return RIBOS_VERIFIER_OK;
}

static RibosVerifierStatus
ribos_verify_artifact_v1(
    const uint8_t *artifact,
    size_t artifact_size,
    const RibosProductSchema *schema,
    void *workspace,
    size_t workspace_size,
    RibosVerifierReport *report,
    int stage2)
{
    RibosVerifierContext context;
    RibosVerifierArena arena;
    RibosVerifierReport local_report;
    uint8_t schema_digest[RIBOS_SCHEMA_DIGEST_BYTES];
    size_t required_size;
    size_t maximum_bit_words;
    RibosVerifierStatus status;

    if (report == NULL) {
        report = &local_report;
    }
    ribos_verifier_initialize_report(report);
    memset(&context, 0, sizeof(context));
    context.report = report;
    context.schema = schema;
    if (schema == NULL || workspace == NULL) {
        return ribos_verifier_fail(
            &context,
            RIBOS_VERIFIER_INVALID_ARGUMENT,
            RIBOS_VERIFIER_SUBJECT_ARTIFACT,
            RIBOS_ARTIFACT_INVALID_ID,
            0);
    }
    status = ribos_verifier_open(
        artifact,
        artifact_size,
        &context.artifact,
        report);
    if (status != RIBOS_VERIFIER_OK) {
        return status;
    }
    if (!ribos_verifier_measure_workspace(
            &context.artifact,
            &required_size,
            &maximum_bit_words)) {
        return ribos_verifier_fail(
            &context,
            RIBOS_VERIFIER_STRUCTURAL_ERROR,
            RIBOS_VERIFIER_SUBJECT_ARTIFACT,
            RIBOS_ARTIFACT_INVALID_ID,
            RIBOS_ARTIFACT_SECTION_FUNCTIONS);
    }
    if (workspace_size < required_size) {
        return ribos_verifier_fail(
            &context,
            RIBOS_VERIFIER_WORKSPACE_TOO_SMALL,
            RIBOS_VERIFIER_SUBJECT_ARTIFACT,
            RIBOS_ARTIFACT_INVALID_ID,
            required_size > UINT32_MAX ?
                UINT32_MAX : (uint32_t)required_size);
    }
    if (ribos_schema_validate(schema) != RIBOS_SCHEMA_OK ||
        ribos_schema_compute_identity(schema, schema_digest) !=
            RIBOS_SCHEMA_OK ||
        memcmp(
            schema_digest,
            context.artifact.schema_digest,
            sizeof(schema_digest)) != 0) {
        return ribos_verifier_fail(
            &context,
            RIBOS_VERIFIER_SCHEMA_MISMATCH,
            RIBOS_VERIFIER_SUBJECT_SCHEMA,
            RIBOS_ARTIFACT_INVALID_ID,
            0);
    }
    if (!ribos_verifier_bind_sections(&context)) {
        return ribos_verifier_fail(
            &context,
            RIBOS_VERIFIER_STRUCTURAL_ERROR,
            RIBOS_VERIFIER_SUBJECT_ARTIFACT,
            RIBOS_ARTIFACT_INVALID_ID,
            0);
    }
    arena = (RibosVerifierArena){
        .bytes = workspace,
        .capacity = workspace_size,
    };
    if (!ribos_verifier_allocate_workspace(
            &context,
            &arena,
            maximum_bit_words)) {
        return ribos_verifier_fail(
            &context,
            RIBOS_VERIFIER_WORKSPACE_TOO_SMALL,
            RIBOS_VERIFIER_SUBJECT_ARTIFACT,
            RIBOS_ARTIFACT_INVALID_ID,
            (uint32_t)required_size);
    }
    status = ribos_verifier_validate_source_maps(&context);
    if (status == RIBOS_VERIFIER_OK) {
        status = ribos_verifier_validate_types(&context);
    }
    if (status == RIBOS_VERIFIER_OK) {
        status = ribos_verifier_validate_constants(&context);
    }
    if (status == RIBOS_VERIFIER_OK) {
        status = ribos_verifier_validate_functions_and_frames(&context);
    }
    if (status == RIBOS_VERIFIER_OK) {
        status =
            ribos_verifier_validate_blocks_and_instruction_shape(
                &context);
    }
    if (status == RIBOS_VERIFIER_OK) {
        status =
            ribos_verifier_validate_imports_and_instruction_types(
                &context);
    }
    if (status == RIBOS_VERIFIER_OK) {
        status =
            ribos_verifier_validate_cfg_and_initialization(&context);
    }
    if (status == RIBOS_VERIFIER_OK) {
        status = ribos_verifier_validate_call_resources(&context);
    }
    if (status == RIBOS_VERIFIER_OK) {
        const uint8_t *entry = ribos_verifier_row(
            context.functions,
            context.artifact.entry_function);

        if (context.artifact.declared_capabilities !=
                ribos_verifier_u32(entry + 40) ||
            context.artifact.required_capabilities !=
                ribos_verifier_u32(entry + 44) ||
            context.artifact.instruction_budget !=
                ribos_verifier_u64(entry + 48) ||
            context.artifact.helper_budget !=
                ribos_verifier_u64(entry + 64)) {
            status = ribos_verifier_fail(
                &context,
                RIBOS_VERIFIER_RESOURCE_MISMATCH,
                RIBOS_VERIFIER_SUBJECT_FUNCTION,
                context.artifact.entry_function,
                0);
        }
    }
    if (status == RIBOS_VERIFIER_OK && stage2) {
        status = ribos_verifier_validate_stage2(&context);
    }
    report->status = status;
    return status;
}

RibosVerifierStatus
ribos_verify_artifact_stage1_v1(
    const uint8_t *artifact,
    size_t artifact_size,
    const RibosProductSchema *schema,
    void *workspace,
    size_t workspace_size,
    RibosVerifierReport *report)
{
    return ribos_verify_artifact_v1(
        artifact,
        artifact_size,
        schema,
        workspace,
        workspace_size,
        report,
        0);
}

RibosVerifierStatus
ribos_verify_artifact_stage2_v1(
    const uint8_t *artifact,
    size_t artifact_size,
    const RibosProductSchema *schema,
    void *workspace,
    size_t workspace_size,
    RibosVerifierReport *report)
{
    return ribos_verify_artifact_v1(
        artifact,
        artifact_size,
        schema,
        workspace,
        workspace_size,
        report,
        1);
}

const char *
ribos_verifier_status_name(RibosVerifierStatus status)
{
    switch (status) {
    case RIBOS_VERIFIER_OK:
        return "ok";
    case RIBOS_VERIFIER_INVALID_ARGUMENT:
        return "invalid-argument";
    case RIBOS_VERIFIER_WORKSPACE_TOO_SMALL:
        return "workspace-too-small";
    case RIBOS_VERIFIER_STRUCTURAL_ERROR:
        return "structural-error";
    case RIBOS_VERIFIER_SCHEMA_MISMATCH:
        return "schema-mismatch";
    case RIBOS_VERIFIER_INVALID_TYPE:
        return "invalid-type";
    case RIBOS_VERIFIER_INVALID_CONSTANT:
        return "invalid-constant";
    case RIBOS_VERIFIER_INVALID_FUNCTION:
        return "invalid-function";
    case RIBOS_VERIFIER_INVALID_BLOCK:
        return "invalid-block";
    case RIBOS_VERIFIER_INVALID_SLOT:
        return "invalid-slot";
    case RIBOS_VERIFIER_INVALID_INSTRUCTION:
        return "invalid-instruction";
    case RIBOS_VERIFIER_INVALID_TARGET:
        return "invalid-target";
    case RIBOS_VERIFIER_UNINITIALIZED_SLOT:
        return "uninitialized-slot";
    case RIBOS_VERIFIER_TYPE_MISMATCH:
        return "type-mismatch";
    case RIBOS_VERIFIER_FRAME_MISMATCH:
        return "frame-mismatch";
    case RIBOS_VERIFIER_RECURSIVE_CALL:
        return "recursive-call";
    case RIBOS_VERIFIER_RESOURCE_MISMATCH:
        return "resource-mismatch";
    case RIBOS_VERIFIER_CAPABILITY_MISMATCH:
        return "capability-mismatch";
    case RIBOS_VERIFIER_OPAQUE_FORGERY:
        return "opaque-forgery";
    case RIBOS_VERIFIER_TYPESTATE_VIOLATION:
        return "typestate-violation";
    case RIBOS_VERIFIER_OWNERSHIP_VIOLATION:
        return "ownership-violation";
    case RIBOS_VERIFIER_BUDGET_EXCEEDED:
        return "budget-exceeded";
    case RIBOS_VERIFIER_TERMINAL_ACTION_VIOLATION:
        return "terminal-action-violation";
    case RIBOS_VERIFIER_FAIL_CLOSED_VIOLATION:
        return "fail-closed-violation";
    default:
        return "unknown";
    }
}
