#include "ribos/vm/interpreter.h"

#include "internal.h"
#include "storage_internal.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

typedef struct RibosVmInterpreterTables {
    const RibosArtifactView *view;
    const RibosArtifactSectionView *types;
    const RibosArtifactSectionView *shapes;
    const RibosArtifactSectionView *constants;
    const RibosArtifactSectionView *functions;
    const RibosArtifactSectionView *blocks;
    const RibosArtifactSectionView *loops;
    const RibosArtifactSectionView *slots;
    const RibosArtifactSectionView *instructions;
    const RibosArtifactSectionView *operands;
} RibosVmInterpreterTables;

typedef struct RibosVmInterpreterSlot {
    uint32_t id;
    uint32_t function_id;
    uint32_t type_id;
    uint16_t type_kind;
    uint16_t bit_width;
    uint32_t storage_kind;
    uint32_t byte_size;
    uint32_t alignment;
} RibosVmInterpreterSlot;

typedef struct RibosVmInterpreterType {
    uint32_t id;
    uint16_t kind;
    uint16_t bit_width;
    uint32_t first_type;
    uint32_t second_type;
    uint32_t bound;
    uint32_t shape_start;
    uint32_t shape_count;
    uint32_t storage_kind;
    uint32_t byte_size;
    uint32_t element_stride;
    uint32_t payload_offset;
    uint32_t capacity;
} RibosVmInterpreterType;

static uint16_t
ribos_vm_interpreter_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] |
        ((uint16_t)bytes[1] << 8);
}

static uint32_t
ribos_vm_interpreter_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

static uint64_t
ribos_vm_interpreter_u64(const uint8_t *bytes)
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

static void
ribos_vm_interpreter_write_u32(uint8_t bytes[4], uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void
ribos_vm_interpreter_write_u64(uint8_t bytes[8], uint64_t value)
{
    uint32_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
}

static const uint8_t *
ribos_vm_interpreter_row(
    const RibosArtifactSectionView *section,
    uint32_t id)
{
    if (section == NULL || id >= section->count ||
        section->row_size == 0 ||
        (size_t)id > SIZE_MAX / section->row_size) {
        return NULL;
    }
    return section->bytes + (size_t)id * section->row_size;
}

static RibosVmStatus
ribos_vm_interpreter_tables(
    const RibosPreparedProgram *prepared_program,
    RibosVmInterpreterTables *tables)
{
    const RibosArtifactView *view;

    if (tables == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    memset(tables, 0, sizeof(*tables));
    if (ribos_prepared_program_validate_v1(prepared_program) !=
            RIBOS_VM_STATUS_OK) {
        return RIBOS_VM_STATUS_NOT_PREPARED;
    }
    view = ribos_prepared_program_artifact_view_v1(prepared_program);
    if (view == NULL) {
        return RIBOS_VM_STATUS_NOT_PREPARED;
    }
    tables->view = view;
    tables->types = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_TYPES);
    tables->shapes = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_SHAPES);
    tables->constants = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_CONSTANTS);
    tables->functions = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_FUNCTIONS);
    tables->blocks = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_BLOCKS);
    tables->loops = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_LOOPS);
    tables->slots = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_SLOTS);
    tables->instructions = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_INSTRUCTIONS);
    tables->operands = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_OPERANDS);
    if (tables->types == NULL || tables->shapes == NULL ||
        tables->constants == NULL ||
        tables->functions == NULL || tables->blocks == NULL ||
        tables->loops == NULL ||
        tables->slots == NULL || tables->instructions == NULL ||
        tables->operands == NULL) {
        memset(tables, 0, sizeof(*tables));
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    return RIBOS_VM_STATUS_OK;
}

static int
ribos_vm_interpreter_context_digest_matches(
    const RibosVmContext *context)
{
    uint8_t digest[RIBOS_VM_DIGEST_BYTES];
    size_t byte_size;

    if (context == NULL || context->byte_size > SIZE_MAX) {
        return 0;
    }
    byte_size = (size_t)context->byte_size;
    ribos_artifact_sha256(context->bytes, byte_size, digest);
    return memcmp(
        digest,
        context->digest,
        RIBOS_VM_DIGEST_BYTES) == 0;
}

static RibosVmStatus
ribos_vm_interpreter_validate_context(
    const RibosPreparedProgram *prepared_program,
    const RibosVmContext *context)
{
    const RibosVmLimits *limits =
        ribos_prepared_program_limits_v1(prepared_program);
    RibosVmStatus status;

    if (limits == NULL) {
        return RIBOS_VM_STATUS_NOT_PREPARED;
    }
    status = ribos_vm_context_validate_v1(context, limits);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (!ribos_vm_interpreter_context_digest_matches(context)) {
        return RIBOS_VM_STATUS_DIGEST_MISMATCH;
    }
    return RIBOS_VM_STATUS_OK;
}

static int
ribos_vm_interpreter_slot(
    const RibosVmInterpreterTables *tables,
    uint32_t slot_id,
    RibosVmInterpreterSlot *slot)
{
    const uint8_t *slot_row;
    const uint8_t *type_row;
    uint32_t type_id;

    if (tables == NULL || slot == NULL) {
        return 0;
    }
    memset(slot, 0, sizeof(*slot));
    slot_row = ribos_vm_interpreter_row(tables->slots, slot_id);
    if (slot_row == NULL ||
        ribos_vm_interpreter_u32(slot_row) != slot_id) {
        return 0;
    }
    type_id = ribos_vm_interpreter_u32(slot_row + 8);
    type_row = ribos_vm_interpreter_row(tables->types, type_id);
    if (type_row == NULL ||
        ribos_vm_interpreter_u32(type_row) != type_id) {
        return 0;
    }
    *slot = (RibosVmInterpreterSlot){
        .id = slot_id,
        .function_id = ribos_vm_interpreter_u32(slot_row + 4),
        .type_id = type_id,
        .type_kind = ribos_vm_interpreter_u16(type_row + 4),
        .bit_width = ribos_vm_interpreter_u16(type_row + 6),
        .storage_kind = ribos_vm_interpreter_u32(type_row + 36),
        .byte_size = ribos_vm_interpreter_u32(slot_row + 16),
        .alignment = ribos_vm_interpreter_u32(slot_row + 20),
    };
    return 1;
}

static int
ribos_vm_interpreter_type(
    const RibosVmInterpreterTables *tables,
    uint32_t type_id,
    RibosVmInterpreterType *type)
{
    const uint8_t *row;

    if (tables == NULL || type == NULL) {
        return 0;
    }
    row = ribos_vm_interpreter_row(tables->types, type_id);
    if (row == NULL ||
        ribos_vm_interpreter_u32(row) != type_id) {
        return 0;
    }
    *type = (RibosVmInterpreterType){
        .id = type_id,
        .kind = ribos_vm_interpreter_u16(row + 4),
        .bit_width = ribos_vm_interpreter_u16(row + 6),
        .first_type = ribos_vm_interpreter_u32(row + 8),
        .second_type = ribos_vm_interpreter_u32(row + 12),
        .bound = ribos_vm_interpreter_u32(row + 16),
        .shape_start = ribos_vm_interpreter_u32(row + 20),
        .shape_count = ribos_vm_interpreter_u32(row + 24),
        .storage_kind = ribos_vm_interpreter_u32(row + 36),
        .byte_size = ribos_vm_interpreter_u32(row + 40),
        .element_stride = ribos_vm_interpreter_u32(row + 44),
        .payload_offset = ribos_vm_interpreter_u32(row + 48),
        .capacity = ribos_vm_interpreter_u32(row + 52),
    };
    return 1;
}

static int
ribos_vm_interpreter_type_alignment(
    const RibosVmInterpreterTables *tables,
    uint32_t type_id,
    uint32_t *alignment)
{
    uint32_t slot_id;

    if (tables == NULL || alignment == NULL) {
        return 0;
    }
    for (slot_id = 0; slot_id < tables->slots->count; ++slot_id) {
        const uint8_t *row =
            ribos_vm_interpreter_row(tables->slots, slot_id);

        if (row != NULL &&
            ribos_vm_interpreter_u32(row + 8) == type_id) {
            uint32_t value =
                ribos_vm_interpreter_u32(row + 20);

            if (value == 0 || value > 8 ||
                (value & (value - 1u)) != 0) {
                return 0;
            }
            *alignment = value;
            return 1;
        }
    }
    return 0;
}

static int
ribos_vm_interpreter_align_u32(
    uint32_t value,
    uint32_t alignment,
    uint32_t *result)
{
    uint32_t mask;

    if (result == NULL || alignment == 0 ||
        (alignment & (alignment - 1u)) != 0) {
        return 0;
    }
    mask = alignment - 1u;
    if (value > UINT32_MAX - mask) {
        return 0;
    }
    *result = (value + mask) & ~mask;
    return 1;
}

static int
ribos_vm_interpreter_range_u32(
    uint32_t offset,
    uint32_t byte_size,
    uint32_t limit)
{
    return offset <= limit && byte_size <= limit - offset;
}

static uint32_t
ribos_vm_interpreter_operand(
    const RibosVmInterpreterTables *tables,
    const uint8_t *instruction,
    uint32_t ordinal)
{
    uint32_t start;
    uint32_t count;
    const uint8_t *row;

    if (tables == NULL || instruction == NULL) {
        return RIBOS_VM_INVALID_ID;
    }
    start = ribos_vm_interpreter_u32(instruction + 16);
    count = ribos_vm_interpreter_u16(instruction + 2);
    if (ordinal >= count || start > tables->operands->count ||
        ordinal >= tables->operands->count - start) {
        return RIBOS_VM_INVALID_ID;
    }
    row = ribos_vm_interpreter_row(
        tables->operands,
        start + ordinal);
    return row == NULL ?
        RIBOS_VM_INVALID_ID :
        ribos_vm_interpreter_u32(row);
}

static uint64_t
ribos_vm_interpreter_mask(uint16_t bit_width)
{
    return bit_width == 64 ?
        UINT64_MAX :
        (UINT64_C(1) << bit_width) - 1;
}

static int
ribos_vm_interpreter_integer_width(uint16_t bit_width)
{
    return bit_width == 8 || bit_width == 16 ||
        bit_width == 32 || bit_width == 64;
}

static int64_t
ribos_vm_interpreter_signed_minimum(uint16_t bit_width)
{
    return bit_width == 64 ?
        INT64_MIN :
        -(INT64_C(1) << (bit_width - 1u));
}

static int64_t
ribos_vm_interpreter_signed_maximum(uint16_t bit_width)
{
    return bit_width == 64 ?
        INT64_MAX :
        (INT64_C(1) << (bit_width - 1u)) - 1;
}

static uint64_t
ribos_vm_interpreter_signed_magnitude(int64_t value)
{
    return value < 0 ?
        (uint64_t)(-(value + 1)) + 1 :
        (uint64_t)value;
}

static int
ribos_vm_interpreter_signed_from_magnitude(
    uint64_t magnitude,
    int negative,
    int64_t *value)
{
    if (value == NULL) {
        return 0;
    }
    if (!negative) {
        if (magnitude > (uint64_t)INT64_MAX) {
            return 0;
        }
        *value = (int64_t)magnitude;
        return 1;
    }
    if (magnitude > (UINT64_C(1) << 63)) {
        return 0;
    }
    if (magnitude == (UINT64_C(1) << 63)) {
        *value = INT64_MIN;
    } else {
        *value = -(int64_t)magnitude;
    }
    return 1;
}

static int
ribos_vm_interpreter_signed_add(
    int64_t left,
    int64_t right,
    int64_t minimum,
    int64_t maximum,
    int64_t *result)
{
    if ((right > 0 && left > maximum - right) ||
        (right < 0 && left < minimum - right)) {
        return 0;
    }
    *result = left + right;
    return 1;
}

static int
ribos_vm_interpreter_signed_subtract(
    int64_t left,
    int64_t right,
    int64_t minimum,
    int64_t maximum,
    int64_t *result)
{
    if ((right > 0 && left < minimum + right) ||
        (right < 0 && left > maximum + right)) {
        return 0;
    }
    *result = left - right;
    return 1;
}

static int
ribos_vm_interpreter_signed_multiply(
    int64_t left,
    int64_t right,
    int64_t minimum,
    int64_t maximum,
    int64_t *result)
{
    uint64_t left_magnitude =
        ribos_vm_interpreter_signed_magnitude(left);
    uint64_t right_magnitude =
        ribos_vm_interpreter_signed_magnitude(right);
    int negative = (left < 0) != (right < 0);
    uint64_t limit = negative ?
        ribos_vm_interpreter_signed_magnitude(minimum) :
        (uint64_t)maximum;
    uint64_t product;

    if (right_magnitude != 0 &&
        left_magnitude > limit / right_magnitude) {
        return 0;
    }
    product = left_magnitude * right_magnitude;
    return ribos_vm_interpreter_signed_from_magnitude(
        product,
        negative,
        result);
}

static int
ribos_vm_interpreter_signed_shift_right(
    int64_t value,
    uint32_t shift,
    int64_t *result)
{
    uint64_t magnitude;
    uint64_t quotient;
    uint64_t remainder_mask;

    if (shift == 0) {
        *result = value;
        return 1;
    }
    if (value >= 0) {
        *result = (int64_t)((uint64_t)value >> shift);
        return 1;
    }
    magnitude = ribos_vm_interpreter_signed_magnitude(value);
    remainder_mask = (UINT64_C(1) << shift) - 1;
    quotient = magnitude >> shift;
    if ((magnitude & remainder_mask) != 0) {
        ++quotient;
    }
    return ribos_vm_interpreter_signed_from_magnitude(
        quotient,
        1,
        result);
}

static int
ribos_vm_interpreter_slot_read(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *control,
    const RibosVmInterpreterSlot *slot,
    uint8_t bytes[8])
{
    if (slot->function_id != control->function_id ||
        slot->byte_size > 8) {
        return 0;
    }
    memset(bytes, 0, 8);
    return ribos_vm_storage_slot_read_v1(
        prepared_program,
        storage,
        arena_size,
        control->function_id,
        control->frame_base,
        slot->id,
        bytes,
        slot->byte_size) == RIBOS_VM_STATUS_OK;
}

static int
ribos_vm_interpreter_slot_write(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *control,
    const RibosVmInterpreterSlot *slot,
    const uint8_t bytes[8])
{
    return slot->function_id == control->function_id &&
        slot->byte_size <= 8 &&
        ribos_vm_storage_slot_write_v1(
            prepared_program,
            storage,
            arena_size,
            control->function_id,
            control->frame_base,
            slot->id,
            bytes,
            slot->byte_size) == RIBOS_VM_STATUS_OK;
}

static int
ribos_vm_interpreter_load_raw_integer(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *control,
    const RibosVmInterpreterSlot *slot,
    uint64_t *value)
{
    uint8_t bytes[8];

    if (value == NULL ||
        !ribos_vm_interpreter_integer_width(slot->bit_width) ||
        slot->byte_size != slot->bit_width / 8u ||
        !ribos_vm_interpreter_slot_read(
            prepared_program,
            storage,
            arena_size,
            control,
            slot,
            bytes)) {
        return 0;
    }
    *value = ribos_vm_interpreter_u64(bytes) &
        ribos_vm_interpreter_mask(slot->bit_width);
    return 1;
}

static int
ribos_vm_interpreter_store_raw_integer(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *control,
    const RibosVmInterpreterSlot *slot,
    uint64_t value)
{
    uint8_t bytes[8] = {0};

    if (!ribos_vm_interpreter_integer_width(slot->bit_width) ||
        slot->byte_size != slot->bit_width / 8u) {
        return 0;
    }
    ribos_vm_interpreter_write_u64(
        bytes,
        value & ribos_vm_interpreter_mask(slot->bit_width));
    return ribos_vm_interpreter_slot_write(
        prepared_program,
        storage,
        arena_size,
        control,
        slot,
        bytes);
}

static int
ribos_vm_interpreter_load_signed(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *control,
    const RibosVmInterpreterSlot *slot,
    int64_t *value)
{
    return slot->type_kind == RIBOS_BC_TYPE_SIGNED &&
        ribos_vm_storage_slot_load_signed_v1(
            prepared_program,
            storage,
            arena_size,
            control->function_id,
            control->frame_base,
            slot->id,
            slot->bit_width,
            value) == RIBOS_VM_STATUS_OK;
}

static int
ribos_vm_interpreter_store_signed(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *control,
    const RibosVmInterpreterSlot *slot,
    int64_t value)
{
    return slot->type_kind == RIBOS_BC_TYPE_SIGNED &&
        ribos_vm_storage_slot_store_signed_v1(
            prepared_program,
            storage,
            arena_size,
            control->function_id,
            control->frame_base,
            slot->id,
            slot->bit_width,
            value) == RIBOS_VM_STATUS_OK;
}

static int
ribos_vm_interpreter_load_unsigned(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *control,
    const RibosVmInterpreterSlot *slot,
    uint64_t *value)
{
    return slot->type_kind == RIBOS_BC_TYPE_UNSIGNED &&
        ribos_vm_storage_slot_load_unsigned_v1(
            prepared_program,
            storage,
            arena_size,
            control->function_id,
            control->frame_base,
            slot->id,
            slot->bit_width,
            value) == RIBOS_VM_STATUS_OK;
}

static int
ribos_vm_interpreter_store_unsigned(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *control,
    const RibosVmInterpreterSlot *slot,
    uint64_t value)
{
    return slot->type_kind == RIBOS_BC_TYPE_UNSIGNED &&
        ribos_vm_storage_slot_store_unsigned_v1(
            prepared_program,
            storage,
            arena_size,
            control->function_id,
            control->frame_base,
            slot->id,
            slot->bit_width,
        value) == RIBOS_VM_STATUS_OK;
}

static int
ribos_vm_interpreter_zero_slot(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *control,
    const RibosVmInterpreterSlot *slot)
{
    return slot != NULL &&
        slot->function_id == control->function_id &&
        ribos_vm_storage_slot_zero_internal_v1(
            prepared_program,
            storage,
            arena_size,
            control->function_id,
            control->frame_base,
            slot->id) == RIBOS_VM_STATUS_OK;
}

static int
ribos_vm_interpreter_copy_slot(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *source_control,
    const RibosVmInterpreterSlot *source,
    const RibosVmStorageExecutionControl *destination_control,
    const RibosVmInterpreterSlot *destination)
{
    return source != NULL && destination != NULL &&
        source->function_id == source_control->function_id &&
        destination->function_id ==
            destination_control->function_id &&
        source->type_id == destination->type_id &&
        source->byte_size == destination->byte_size &&
        ribos_vm_storage_slot_copy_internal_v1(
            prepared_program,
            storage,
            arena_size,
            source_control->function_id,
            source_control->frame_base,
            source->id,
            destination_control->function_id,
            destination_control->frame_base,
            destination->id) == RIBOS_VM_STATUS_OK;
}

static int
ribos_vm_interpreter_copy_slice(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *source_control,
    const RibosVmInterpreterSlot *source,
    uint32_t source_offset,
    const RibosVmStorageExecutionControl *destination_control,
    const RibosVmInterpreterSlot *destination,
    uint32_t destination_offset,
    uint32_t byte_size)
{
    return source != NULL && destination != NULL &&
        source->function_id == source_control->function_id &&
        destination->function_id ==
            destination_control->function_id &&
        ribos_vm_storage_slot_slice_copy_internal_v1(
            prepared_program,
            storage,
            arena_size,
            source_control->function_id,
            source_control->frame_base,
            source->id,
            source_offset,
            destination_control->function_id,
            destination_control->frame_base,
            destination->id,
            destination_offset,
            byte_size) == RIBOS_VM_STATUS_OK;
}

static int
ribos_vm_interpreter_read_slice(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *control,
    const RibosVmInterpreterSlot *slot,
    uint32_t offset,
    uint8_t *bytes,
    uint32_t byte_size)
{
    return slot != NULL &&
        slot->function_id == control->function_id &&
        ribos_vm_storage_slot_slice_read_internal_v1(
            prepared_program,
            storage,
            arena_size,
            control->function_id,
            control->frame_base,
            slot->id,
            offset,
            bytes,
            byte_size) == RIBOS_VM_STATUS_OK;
}

static int
ribos_vm_interpreter_write_slice(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *control,
    const RibosVmInterpreterSlot *slot,
    uint32_t offset,
    const uint8_t *bytes,
    uint32_t byte_size)
{
    return slot != NULL &&
        slot->function_id == control->function_id &&
        ribos_vm_storage_slot_slice_write_internal_v1(
            prepared_program,
            storage,
            arena_size,
            control->function_id,
            control->frame_base,
            slot->id,
            offset,
            bytes,
            byte_size) == RIBOS_VM_STATUS_OK;
}

static int
ribos_vm_interpreter_struct_field(
    const RibosVmInterpreterTables *tables,
    const RibosVmInterpreterType *type,
    uint32_t requested_ordinal,
    uint32_t *field_type,
    uint32_t *field_offset)
{
    uint32_t offset = 0;
    uint32_t index;

    if (tables == NULL || type == NULL ||
        field_type == NULL || field_offset == NULL ||
        type->kind != RIBOS_BC_TYPE_STRUCT ||
        requested_ordinal >= type->shape_count ||
        type->shape_start > tables->shapes->count ||
        type->shape_count >
            tables->shapes->count - type->shape_start) {
        return 0;
    }
    for (index = 0; index < type->shape_count; ++index) {
        const uint8_t *shape = ribos_vm_interpreter_row(
            tables->shapes,
            type->shape_start + index);
        RibosVmInterpreterType value_type;
        uint32_t value_type_id;
        uint32_t alignment;

        if (shape == NULL ||
            ribos_vm_interpreter_u32(shape + 4) !=
                RIBOS_BC_SHAPE_STRUCT_FIELD ||
            ribos_vm_interpreter_u32(shape + 8) != type->id ||
            ribos_vm_interpreter_u32(shape + 16) != index) {
            return 0;
        }
        value_type_id = ribos_vm_interpreter_u32(shape + 20);
        if (!ribos_vm_interpreter_type(
                tables,
                value_type_id,
                &value_type) ||
            !ribos_vm_interpreter_type_alignment(
                tables,
                value_type_id,
                &alignment) ||
            !ribos_vm_interpreter_align_u32(
                offset,
                alignment,
                &offset) ||
            !ribos_vm_interpreter_range_u32(
                offset,
                value_type.byte_size,
                type->byte_size)) {
            return 0;
        }
        if (index == requested_ordinal) {
            *field_type = value_type_id;
            *field_offset = offset;
            return 1;
        }
        offset += value_type.byte_size;
    }
    return 0;
}

static int
ribos_vm_interpreter_enum_variant_exists(
    const RibosVmInterpreterTables *tables,
    const RibosVmInterpreterType *type,
    uint32_t tag)
{
    uint32_t index;

    if (tables == NULL || type == NULL ||
        type->kind != RIBOS_BC_TYPE_ENUM ||
        tag > UINT8_MAX ||
        type->shape_start > tables->shapes->count ||
        type->shape_count >
            tables->shapes->count - type->shape_start) {
        return 0;
    }
    for (index = 0; index < type->shape_count; ++index) {
        const uint8_t *shape = ribos_vm_interpreter_row(
            tables->shapes,
            type->shape_start + index);

        if (shape != NULL &&
            ribos_vm_interpreter_u32(shape + 4) ==
                RIBOS_BC_SHAPE_ENUM_VARIANT &&
            ribos_vm_interpreter_u32(shape + 8) == type->id &&
            ribos_vm_interpreter_u32(shape + 12) == tag) {
            return 1;
        }
    }
    return 0;
}

static int
ribos_vm_interpreter_enum_payload(
    const RibosVmInterpreterTables *tables,
    const RibosVmInterpreterType *type,
    uint32_t tag,
    uint32_t requested_ordinal,
    uint32_t *payload_type,
    uint32_t *payload_offset,
    uint32_t *payload_count)
{
    uint32_t offset = type == NULL ? 0 : type->payload_offset;
    uint32_t count = 0;
    uint32_t index;

    if (tables == NULL || type == NULL ||
        type->kind != RIBOS_BC_TYPE_ENUM ||
        !ribos_vm_interpreter_enum_variant_exists(
            tables,
            type,
            tag)) {
        return 0;
    }
    for (index = 0; index < type->shape_count; ++index) {
        const uint8_t *shape = ribos_vm_interpreter_row(
            tables->shapes,
            type->shape_start + index);
        uint32_t value_type_id;
        RibosVmInterpreterType value_type;
        uint32_t alignment;

        if (shape == NULL ||
            ribos_vm_interpreter_u32(shape + 4) !=
                RIBOS_BC_SHAPE_ENUM_PAYLOAD ||
            ribos_vm_interpreter_u32(shape + 12) != tag) {
            continue;
        }
        if (ribos_vm_interpreter_u32(shape + 8) != type->id ||
            ribos_vm_interpreter_u32(shape + 16) != count) {
            return 0;
        }
        value_type_id = ribos_vm_interpreter_u32(shape + 20);
        if (!ribos_vm_interpreter_type(
                tables,
                value_type_id,
                &value_type) ||
            !ribos_vm_interpreter_type_alignment(
                tables,
                value_type_id,
                &alignment) ||
            !ribos_vm_interpreter_align_u32(
                offset,
                alignment,
                &offset) ||
            !ribos_vm_interpreter_range_u32(
                offset,
                value_type.byte_size,
                type->byte_size)) {
            return 0;
        }
        if (requested_ordinal == count &&
            payload_type != NULL && payload_offset != NULL) {
            *payload_type = value_type_id;
            *payload_offset = offset;
        }
        offset += value_type.byte_size;
        ++count;
    }
    if (payload_count != NULL) {
        *payload_count = count;
    }
    return requested_ordinal < count ||
        requested_ordinal == RIBOS_VM_INVALID_ID;
}

static int
ribos_vm_interpreter_variant_payload(
    const RibosVmInterpreterTables *tables,
    const RibosVmInterpreterType *type,
    uint32_t tag,
    uint32_t ordinal,
    uint32_t *payload_type,
    uint32_t *payload_offset,
    uint32_t *payload_count)
{
    if (type == NULL || tag > UINT8_MAX) {
        return 0;
    }
    if (type->kind == RIBOS_BC_TYPE_OPTION) {
        uint32_t count = tag == 0 ? 1u : 0u;

        if (tag > 1 || (ordinal != RIBOS_VM_INVALID_ID &&
                       ordinal >= count)) {
            return 0;
        }
        if (payload_count != NULL) {
            *payload_count = count;
        }
        if (count != 0 && ordinal != RIBOS_VM_INVALID_ID) {
            *payload_type = type->first_type;
            *payload_offset = type->payload_offset;
        }
        return 1;
    }
    if (type->kind == RIBOS_BC_TYPE_RESULT) {
        if (tag > 1 ||
            (ordinal != RIBOS_VM_INVALID_ID && ordinal != 0)) {
            return 0;
        }
        if (payload_count != NULL) {
            *payload_count = 1;
        }
        if (ordinal != RIBOS_VM_INVALID_ID) {
            *payload_type =
                tag == 0 ? type->first_type : type->second_type;
            *payload_offset = type->payload_offset;
        }
        return 1;
    }
    return ribos_vm_interpreter_enum_payload(
        tables,
        type,
        tag,
        ordinal,
        payload_type,
        payload_offset,
        payload_count);
}

static int
ribos_vm_interpreter_variant_tag_is_valid(
    const RibosVmInterpreterTables *tables,
    const RibosVmInterpreterType *type,
    uint32_t tag)
{
    uint32_t count;

    return ribos_vm_interpreter_variant_payload(
        tables,
        type,
        tag,
        RIBOS_VM_INVALID_ID,
        NULL,
        NULL,
        &count);
}

static uint32_t
ribos_vm_interpreter_execute_build_list(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmInterpreterTables *tables,
    const RibosVmStorageExecutionControl *control,
    const uint8_t *instruction,
    const RibosVmInterpreterSlot *result)
{
    RibosVmInterpreterType aggregate;
    RibosVmInterpreterType element;
    uint32_t count;
    uint32_t index;
    uint8_t length_bytes[4];

    if (!ribos_vm_interpreter_type(
            tables,
            result->type_id,
            &aggregate) ||
        (aggregate.kind != RIBOS_BC_TYPE_ARRAY &&
         aggregate.kind != RIBOS_BC_TYPE_LIST) ||
        !ribos_vm_interpreter_type(
            tables,
            aggregate.first_type,
            &element) ||
        result->byte_size != aggregate.byte_size ||
        aggregate.capacity != aggregate.bound ||
        aggregate.element_stride < element.byte_size ||
        aggregate.payload_offset > aggregate.byte_size ||
        aggregate.element_stride == 0 ||
        (aggregate.element_stride != 0 &&
            aggregate.capacity >
                (aggregate.byte_size - aggregate.payload_offset) /
                    aggregate.element_stride) ||
        ribos_vm_interpreter_u32(instruction + 20) !=
            aggregate.capacity) {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    count = ribos_vm_interpreter_u16(instruction + 2);
    if (count > aggregate.capacity ||
        (aggregate.kind == RIBOS_BC_TYPE_ARRAY &&
         count != aggregate.capacity) ||
        !ribos_vm_interpreter_zero_slot(
            prepared_program,
            storage,
            arena_size,
            control,
            result)) {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    if (aggregate.kind == RIBOS_BC_TYPE_LIST) {
        ribos_vm_interpreter_write_u32(length_bytes, count);
        if (!ribos_vm_interpreter_write_slice(
                prepared_program,
                storage,
                arena_size,
                control,
                result,
                0,
                length_bytes,
                sizeof(length_bytes))) {
            return RIBOS_VM_FAULT_INVALID_VALUE;
        }
    }
    for (index = 0; index < count; ++index) {
        uint32_t operand_id = ribos_vm_interpreter_operand(
            tables,
            instruction,
            index);
        RibosVmInterpreterSlot operand;
        uint32_t destination =
            aggregate.payload_offset +
            index * aggregate.element_stride;

        if (!ribos_vm_interpreter_slot(
                tables,
                operand_id,
                &operand) ||
            operand.function_id != control->function_id ||
            operand.type_id != aggregate.first_type ||
            operand.byte_size != element.byte_size ||
            !ribos_vm_interpreter_range_u32(
                destination,
                element.byte_size,
                aggregate.byte_size) ||
            !ribos_vm_interpreter_copy_slice(
                prepared_program,
                storage,
                arena_size,
                control,
                &operand,
                0,
                control,
                result,
                destination,
                element.byte_size)) {
            return RIBOS_VM_FAULT_INVALID_VALUE;
        }
    }
    return RIBOS_VM_FAULT_NONE;
}

static uint32_t
ribos_vm_interpreter_execute_build_struct(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmInterpreterTables *tables,
    const RibosVmStorageExecutionControl *control,
    const uint8_t *instruction,
    const RibosVmInterpreterSlot *result)
{
    RibosVmInterpreterType aggregate;
    uint32_t count = ribos_vm_interpreter_u16(instruction + 2);
    uint32_t index;

    if (!ribos_vm_interpreter_type(
            tables,
            result->type_id,
            &aggregate) ||
        aggregate.kind != RIBOS_BC_TYPE_STRUCT ||
        result->byte_size != aggregate.byte_size ||
        ribos_vm_interpreter_u32(instruction + 20) !=
            result->type_id ||
        count != aggregate.shape_count ||
        !ribos_vm_interpreter_zero_slot(
            prepared_program,
            storage,
            arena_size,
            control,
            result)) {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    for (index = 0; index < count; ++index) {
        uint32_t operand_id = ribos_vm_interpreter_operand(
            tables,
            instruction,
            index);
        RibosVmInterpreterSlot operand;
        RibosVmInterpreterType field;
        uint32_t field_type;
        uint32_t field_offset;

        if (!ribos_vm_interpreter_struct_field(
                tables,
                &aggregate,
                index,
                &field_type,
                &field_offset) ||
            !ribos_vm_interpreter_type(
                tables,
                field_type,
                &field) ||
            !ribos_vm_interpreter_slot(
                tables,
                operand_id,
                &operand) ||
            operand.function_id != control->function_id ||
            operand.type_id != field_type ||
            operand.byte_size != field.byte_size ||
            !ribos_vm_interpreter_copy_slice(
                prepared_program,
                storage,
                arena_size,
                control,
                &operand,
                0,
                control,
                result,
                field_offset,
                field.byte_size)) {
            return RIBOS_VM_FAULT_INVALID_VALUE;
        }
    }
    return RIBOS_VM_FAULT_NONE;
}

static uint32_t
ribos_vm_interpreter_execute_build_variant(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmInterpreterTables *tables,
    const RibosVmStorageExecutionControl *control,
    const uint8_t *instruction,
    const RibosVmInterpreterSlot *result)
{
    RibosVmInterpreterType aggregate;
    uint32_t tag = ribos_vm_interpreter_u32(instruction + 20);
    uint32_t count = ribos_vm_interpreter_u16(instruction + 2);
    uint32_t expected_count = 0;
    uint32_t index;
    uint8_t tag_byte;

    if (!ribos_vm_interpreter_type(
            tables,
            result->type_id,
            &aggregate) ||
        (aggregate.kind != RIBOS_BC_TYPE_OPTION &&
         aggregate.kind != RIBOS_BC_TYPE_RESULT &&
         aggregate.kind != RIBOS_BC_TYPE_ENUM) ||
        result->byte_size != aggregate.byte_size ||
        tag > UINT8_MAX ||
        !ribos_vm_interpreter_variant_payload(
            tables,
            &aggregate,
            tag,
            RIBOS_VM_INVALID_ID,
            NULL,
            NULL,
            &expected_count) ||
        count != expected_count ||
        !ribos_vm_interpreter_zero_slot(
            prepared_program,
            storage,
            arena_size,
            control,
            result)) {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    tag_byte = (uint8_t)tag;
    if (!ribos_vm_interpreter_write_slice(
            prepared_program,
            storage,
            arena_size,
            control,
            result,
            0,
            &tag_byte,
            1)) {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    for (index = 0; index < count; ++index) {
        uint32_t operand_id = ribos_vm_interpreter_operand(
            tables,
            instruction,
            index);
        RibosVmInterpreterSlot operand;
        RibosVmInterpreterType payload;
        uint32_t payload_type;
        uint32_t payload_offset;

        if (!ribos_vm_interpreter_variant_payload(
                tables,
                &aggregate,
                tag,
                index,
                &payload_type,
                &payload_offset,
                NULL) ||
            !ribos_vm_interpreter_type(
                tables,
                payload_type,
                &payload) ||
            !ribos_vm_interpreter_slot(
                tables,
                operand_id,
                &operand) ||
            operand.function_id != control->function_id ||
            operand.type_id != payload_type ||
            operand.byte_size != payload.byte_size ||
            !ribos_vm_interpreter_range_u32(
                payload_offset,
                payload.byte_size,
                aggregate.byte_size) ||
            !ribos_vm_interpreter_copy_slice(
                prepared_program,
                storage,
                arena_size,
                control,
                &operand,
                0,
                control,
                result,
                payload_offset,
                payload.byte_size)) {
            return RIBOS_VM_FAULT_INVALID_VALUE;
        }
    }
    return RIBOS_VM_FAULT_NONE;
}

static int64_t
ribos_vm_interpreter_sign_extend(
    uint64_t value,
    uint16_t bit_width)
{
    if (bit_width == 64) {
        return value <= (uint64_t)INT64_MAX ?
            (int64_t)value :
            -1 - (int64_t)(UINT64_MAX - value);
    }
    if ((value & (UINT64_C(1) << (bit_width - 1u))) != 0) {
        return (int64_t)value -
            (int64_t)(UINT64_C(1) << bit_width);
    }
    return (int64_t)value;
}

static int
ribos_vm_interpreter_compare_keys(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *left_control,
    const RibosVmInterpreterSlot *left,
    uint32_t left_offset,
    const RibosVmStorageExecutionControl *right_control,
    const RibosVmInterpreterSlot *right,
    uint32_t right_offset,
    const RibosVmInterpreterType *key_type,
    int *comparison)
{
    uint8_t left_bytes[8] = {0};
    uint8_t right_bytes[8] = {0};
    uint64_t left_value;
    uint64_t right_value;

    if (key_type == NULL || comparison == NULL ||
        key_type->byte_size == 0 ||
        key_type->byte_size > sizeof(left_bytes) ||
        !ribos_vm_interpreter_read_slice(
            prepared_program,
            storage,
            arena_size,
            left_control,
            left,
            left_offset,
            left_bytes,
            key_type->byte_size) ||
        !ribos_vm_interpreter_read_slice(
            prepared_program,
            storage,
            arena_size,
            right_control,
            right,
            right_offset,
            right_bytes,
            key_type->byte_size)) {
        return 0;
    }
    left_value = ribos_vm_interpreter_u64(left_bytes);
    right_value = ribos_vm_interpreter_u64(right_bytes);
    if (key_type->kind == RIBOS_BC_TYPE_SIGNED) {
        int64_t left_signed = ribos_vm_interpreter_sign_extend(
            left_value,
            key_type->bit_width);
        int64_t right_signed = ribos_vm_interpreter_sign_extend(
            right_value,
            key_type->bit_width);

        *comparison =
            left_signed < right_signed ? -1 :
            left_signed > right_signed ? 1 : 0;
        return 1;
    }
    if (key_type->kind != RIBOS_BC_TYPE_UNSIGNED &&
        !(key_type->kind == RIBOS_BC_TYPE_NAMED &&
          key_type->byte_size == 4)) {
        return 0;
    }
    *comparison =
        left_value < right_value ? -1 :
        left_value > right_value ? 1 : 0;
    return 1;
}

static int
ribos_vm_interpreter_map_layout(
    const RibosVmInterpreterTables *tables,
    const RibosVmInterpreterType *map,
    RibosVmInterpreterType *key,
    RibosVmInterpreterType *value,
    uint32_t *value_offset)
{
    uint32_t value_alignment;

    return tables != NULL && map != NULL &&
        key != NULL && value != NULL &&
        value_offset != NULL &&
        (map->kind == RIBOS_BC_TYPE_FROZEN_MAP ||
         map->kind == RIBOS_BC_TYPE_DICT) &&
        map->storage_kind == RIBOS_BC_STORAGE_SORTED_MAP &&
        ribos_vm_interpreter_type(
            tables,
            map->first_type,
            key) &&
        ribos_vm_interpreter_type(
            tables,
            map->second_type,
            value) &&
        ribos_vm_interpreter_type_alignment(
            tables,
            map->second_type,
            &value_alignment) &&
        ribos_vm_interpreter_align_u32(
            key->byte_size,
            value_alignment,
            value_offset) &&
        map->element_stride != 0 &&
        *value_offset <= map->element_stride &&
        value->byte_size <= map->element_stride - *value_offset &&
        map->payload_offset <= map->byte_size &&
        map->capacity <=
            (map->byte_size - map->payload_offset) /
                map->element_stride;
}

static uint32_t
ribos_vm_interpreter_execute_build_map(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmInterpreterTables *tables,
    const RibosVmStorageExecutionControl *control,
    const uint8_t *instruction,
    const RibosVmInterpreterSlot *result)
{
    RibosVmInterpreterType map;
    RibosVmInterpreterType key_type;
    RibosVmInterpreterType value_type;
    uint32_t value_offset;
    uint32_t operand_count =
        ribos_vm_interpreter_u16(instruction + 2);
    uint32_t cardinality = operand_count / 2u;
    uint32_t inserted = 0;
    uint32_t pair;
    uint8_t cardinality_bytes[4];

    if ((operand_count & 1u) != 0 ||
        !ribos_vm_interpreter_type(
            tables,
            result->type_id,
            &map) ||
        !ribos_vm_interpreter_map_layout(
            tables,
            &map,
            &key_type,
            &value_type,
            &value_offset) ||
        result->byte_size != map.byte_size ||
        ribos_vm_interpreter_u32(instruction + 20) !=
            map.capacity ||
        cardinality > map.capacity ||
        (map.kind == RIBOS_BC_TYPE_FROZEN_MAP &&
         cardinality != map.capacity) ||
        !ribos_vm_interpreter_zero_slot(
            prepared_program,
            storage,
            arena_size,
            control,
            result)) {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    for (pair = 0; pair < cardinality; ++pair) {
        uint32_t key_slot_id = ribos_vm_interpreter_operand(
            tables,
            instruction,
            pair * 2u);
        uint32_t value_slot_id = ribos_vm_interpreter_operand(
            tables,
            instruction,
            pair * 2u + 1u);
        RibosVmInterpreterSlot key;
        RibosVmInterpreterSlot value;
        uint32_t insertion = 0;
        uint32_t entry_offset;
        uint32_t shift_bytes;

        if (!ribos_vm_interpreter_slot(
                tables,
                key_slot_id,
                &key) ||
            !ribos_vm_interpreter_slot(
                tables,
                value_slot_id,
                &value) ||
            key.function_id != control->function_id ||
            value.function_id != control->function_id ||
            key.type_id != map.first_type ||
            value.type_id != map.second_type ||
            key.byte_size != key_type.byte_size ||
            value.byte_size != value_type.byte_size) {
            return RIBOS_VM_FAULT_INVALID_VALUE;
        }
        while (insertion < inserted) {
            int comparison;
            uint32_t existing_offset =
                map.payload_offset +
                insertion * map.element_stride;

            if (!ribos_vm_interpreter_compare_keys(
                    prepared_program,
                    storage,
                    arena_size,
                    control,
                    &key,
                    0,
                    control,
                    result,
                    existing_offset,
                    &key_type,
                    &comparison)) {
                return RIBOS_VM_FAULT_INVALID_VALUE;
            }
            if (comparison == 0) {
                return RIBOS_VM_FAULT_INVALID_VALUE;
            }
            if (comparison < 0) {
                break;
            }
            ++insertion;
        }
        entry_offset =
            map.payload_offset +
            insertion * map.element_stride;
        shift_bytes = (inserted - insertion) *
            map.element_stride;
        if (!ribos_vm_interpreter_range_u32(
                entry_offset,
                map.element_stride,
                map.byte_size) ||
            (shift_bytes != 0 &&
             ribos_vm_storage_slot_slice_move_internal_v1(
                 prepared_program,
                 storage,
                 arena_size,
                 control->function_id,
                 control->frame_base,
                 result->id,
                 entry_offset,
                 entry_offset + map.element_stride,
                 shift_bytes) != RIBOS_VM_STATUS_OK) ||
            ribos_vm_storage_slot_slice_zero_internal_v1(
                prepared_program,
                storage,
                arena_size,
                control->function_id,
                control->frame_base,
                result->id,
                entry_offset,
                map.element_stride) != RIBOS_VM_STATUS_OK ||
            !ribos_vm_interpreter_copy_slice(
                prepared_program,
                storage,
                arena_size,
                control,
                &key,
                0,
                control,
                result,
                entry_offset,
                key_type.byte_size) ||
            !ribos_vm_interpreter_copy_slice(
                prepared_program,
                storage,
                arena_size,
                control,
                &value,
                0,
                control,
                result,
                entry_offset + value_offset,
                value_type.byte_size)) {
            return RIBOS_VM_FAULT_INVALID_VALUE;
        }
        ++inserted;
    }
    ribos_vm_interpreter_write_u32(
        cardinality_bytes,
        cardinality);
    if (!ribos_vm_interpreter_write_slice(
            prepared_program,
            storage,
            arena_size,
            control,
            result,
            0,
            cardinality_bytes,
            sizeof(cardinality_bytes))) {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    return RIBOS_VM_FAULT_NONE;
}

static uint32_t
ribos_vm_interpreter_execute_member(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmInterpreterTables *tables,
    const RibosVmStorageExecutionControl *control,
    const uint8_t *instruction,
    const RibosVmInterpreterSlot *result)
{
    uint32_t owner_id = ribos_vm_interpreter_operand(
        tables,
        instruction,
        0);
    RibosVmInterpreterSlot owner;
    RibosVmInterpreterType owner_type;
    RibosVmInterpreterType field_type;
    uint32_t field_type_id;
    uint32_t field_offset;
    uint32_t ordinal =
        ribos_vm_interpreter_u32(instruction + 24);

    if (!ribos_vm_interpreter_slot(
            tables,
            owner_id,
            &owner) ||
        owner.function_id != control->function_id ||
        !ribos_vm_interpreter_type(
            tables,
            owner.type_id,
            &owner_type) ||
        owner_type.kind != RIBOS_BC_TYPE_STRUCT ||
        !ribos_vm_interpreter_struct_field(
            tables,
            &owner_type,
            ordinal,
            &field_type_id,
            &field_offset) ||
        field_type_id != result->type_id ||
        !ribos_vm_interpreter_type(
            tables,
            field_type_id,
            &field_type) ||
        result->byte_size != field_type.byte_size ||
        !ribos_vm_interpreter_zero_slot(
            prepared_program,
            storage,
            arena_size,
            control,
            result) ||
        !ribos_vm_interpreter_copy_slice(
            prepared_program,
            storage,
            arena_size,
            control,
            &owner,
            field_offset,
            control,
            result,
            0,
            field_type.byte_size)) {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    return RIBOS_VM_FAULT_NONE;
}

static int
ribos_vm_interpreter_load_index(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *control,
    const RibosVmInterpreterSlot *slot,
    uint32_t *index)
{
    uint64_t value;

    if (index == NULL ||
        slot->type_kind != RIBOS_BC_TYPE_UNSIGNED ||
        !ribos_vm_interpreter_load_unsigned(
            prepared_program,
            storage,
            arena_size,
            control,
            slot,
            &value) ||
        value > UINT32_MAX) {
        return 0;
    }
    *index = (uint32_t)value;
    return 1;
}

static uint32_t
ribos_vm_interpreter_collection_length(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *control,
    const RibosVmInterpreterSlot *collection,
    const RibosVmInterpreterType *type,
    uint32_t *length)
{
    uint8_t bytes[4];

    if (length == NULL || type == NULL) {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    if (type->kind == RIBOS_BC_TYPE_ARRAY) {
        *length = type->capacity;
        return RIBOS_VM_FAULT_NONE;
    }
    if (type->kind != RIBOS_BC_TYPE_LIST ||
        !ribos_vm_interpreter_read_slice(
            prepared_program,
            storage,
            arena_size,
            control,
            collection,
            0,
            bytes,
            sizeof(bytes))) {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    *length = ribos_vm_interpreter_u32(bytes);
    return *length <= type->capacity ?
        RIBOS_VM_FAULT_NONE :
        RIBOS_VM_FAULT_INVALID_VALUE;
}

static uint32_t
ribos_vm_interpreter_execute_index(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmInterpreterTables *tables,
    const RibosVmStorageExecutionControl *control,
    const uint8_t *instruction,
    const RibosVmInterpreterSlot *result)
{
    uint32_t collection_id = ribos_vm_interpreter_operand(
        tables,
        instruction,
        0);
    uint32_t key_id = ribos_vm_interpreter_operand(
        tables,
        instruction,
        1);
    RibosVmInterpreterSlot collection;
    RibosVmInterpreterSlot key;
    RibosVmInterpreterType collection_type;
    uint32_t mode = ribos_vm_interpreter_u32(instruction + 20);

    if (!ribos_vm_interpreter_slot(
            tables,
            collection_id,
            &collection) ||
        !ribos_vm_interpreter_slot(
            tables,
            key_id,
            &key) ||
        collection.function_id != control->function_id ||
        key.function_id != control->function_id ||
        !ribos_vm_interpreter_type(
            tables,
            collection.type_id,
            &collection_type)) {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    if (collection_type.kind == RIBOS_BC_TYPE_ARRAY ||
        collection_type.kind == RIBOS_BC_TYPE_LIST) {
        RibosVmInterpreterType element;
        uint32_t index;
        uint32_t length;
        uint32_t offset;
        uint32_t fault;

        fault = ribos_vm_interpreter_collection_length(
            prepared_program,
            storage,
            arena_size,
            control,
            &collection,
            &collection_type,
            &length);
        if (mode != 0 ||
            fault != RIBOS_VM_FAULT_NONE ||
            !ribos_vm_interpreter_load_index(
                prepared_program,
                storage,
                arena_size,
                control,
                &key,
                &index) ||
            index >= length ||
            !ribos_vm_interpreter_type(
                tables,
                collection_type.first_type,
                &element) ||
            result->type_id != collection_type.first_type ||
            result->byte_size != element.byte_size ||
            collection_type.element_stride == 0 ||
            index >
                (UINT32_MAX -
                    collection_type.payload_offset) /
                    collection_type.element_stride) {
            return RIBOS_VM_FAULT_INVALID_VALUE;
        }
        offset = collection_type.payload_offset +
            index * collection_type.element_stride;
        if (!ribos_vm_interpreter_range_u32(
                offset,
                element.byte_size,
                collection_type.byte_size) ||
            !ribos_vm_interpreter_zero_slot(
                prepared_program,
                storage,
                arena_size,
                control,
                result) ||
            !ribos_vm_interpreter_copy_slice(
                prepared_program,
                storage,
                arena_size,
                control,
                &collection,
                offset,
                control,
                result,
                0,
                element.byte_size)) {
            return RIBOS_VM_FAULT_INVALID_VALUE;
        }
        return RIBOS_VM_FAULT_NONE;
    }
    if (collection_type.kind == RIBOS_BC_TYPE_FROZEN_MAP ||
        collection_type.kind == RIBOS_BC_TYPE_DICT) {
        RibosVmInterpreterType key_type;
        RibosVmInterpreterType value_type;
        uint32_t value_offset;
        uint8_t cardinality_bytes[4];
        uint32_t cardinality;
        uint32_t index;

        if ((mode != 0 && mode != 1) ||
            key.type_id != collection_type.first_type ||
            result->type_id != collection_type.second_type ||
            !ribos_vm_interpreter_map_layout(
                tables,
                &collection_type,
                &key_type,
                &value_type,
                &value_offset) ||
            !ribos_vm_interpreter_read_slice(
                prepared_program,
                storage,
                arena_size,
                control,
                &collection,
                0,
                cardinality_bytes,
                sizeof(cardinality_bytes))) {
            return RIBOS_VM_FAULT_INVALID_VALUE;
        }
        cardinality =
            ribos_vm_interpreter_u32(cardinality_bytes);
        if (cardinality > collection_type.capacity) {
            return RIBOS_VM_FAULT_INVALID_VALUE;
        }
        for (index = 0; index < cardinality; ++index) {
            int comparison;
            uint32_t entry_offset =
                collection_type.payload_offset +
                index * collection_type.element_stride;

            if (!ribos_vm_interpreter_compare_keys(
                    prepared_program,
                    storage,
                    arena_size,
                    control,
                    &collection,
                    entry_offset,
                    control,
                    &key,
                    0,
                    &key_type,
                    &comparison)) {
                return RIBOS_VM_FAULT_INVALID_VALUE;
            }
            if (comparison == 0) {
                if (!ribos_vm_interpreter_zero_slot(
                        prepared_program,
                        storage,
                        arena_size,
                        control,
                        result) ||
                    !ribos_vm_interpreter_copy_slice(
                        prepared_program,
                        storage,
                        arena_size,
                        control,
                        &collection,
                        entry_offset + value_offset,
                        control,
                        result,
                        0,
                        value_type.byte_size)) {
                    return RIBOS_VM_FAULT_INVALID_VALUE;
                }
                return RIBOS_VM_FAULT_NONE;
            }
            if (comparison > 0) {
                break;
            }
        }
        if (mode == 1) {
            uint32_t default_id = ribos_vm_interpreter_operand(
                tables,
                instruction,
                2);
            RibosVmInterpreterSlot fallback;

            if (!ribos_vm_interpreter_slot(
                    tables,
                    default_id,
                    &fallback) ||
                fallback.function_id != control->function_id ||
                fallback.type_id != result->type_id ||
                !ribos_vm_interpreter_copy_slot(
                    prepared_program,
                    storage,
                    arena_size,
                    control,
                    &fallback,
                    control,
                    result)) {
                return RIBOS_VM_FAULT_INVALID_VALUE;
            }
            return RIBOS_VM_FAULT_NONE;
        }
    }
    return RIBOS_VM_FAULT_INVALID_VALUE;
}

static uint32_t
ribos_vm_interpreter_execute_collection_length(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmInterpreterTables *tables,
    const RibosVmStorageExecutionControl *control,
    const uint8_t *instruction,
    const RibosVmInterpreterSlot *result)
{
    uint32_t collection_id = ribos_vm_interpreter_operand(
        tables,
        instruction,
        0);
    RibosVmInterpreterSlot collection;
    RibosVmInterpreterType type;
    uint32_t length;
    uint32_t fault;

    if (!ribos_vm_interpreter_slot(
            tables,
            collection_id,
            &collection) ||
        collection.function_id != control->function_id ||
        !ribos_vm_interpreter_type(
            tables,
            collection.type_id,
            &type) ||
        ribos_vm_interpreter_u32(instruction + 20) !=
            type.capacity) {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    fault = ribos_vm_interpreter_collection_length(
        prepared_program,
        storage,
        arena_size,
        control,
        &collection,
        &type,
        &length);
    if (fault != RIBOS_VM_FAULT_NONE ||
        result->type_kind != RIBOS_BC_TYPE_UNSIGNED ||
        result->bit_width != 32 ||
        !ribos_vm_interpreter_store_unsigned(
            prepared_program,
            storage,
            arena_size,
            control,
            result,
            length)) {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    return RIBOS_VM_FAULT_NONE;
}

static uint32_t
ribos_vm_interpreter_execute_variant_tag(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmInterpreterTables *tables,
    const RibosVmStorageExecutionControl *control,
    const uint8_t *instruction,
    const RibosVmInterpreterSlot *result)
{
    uint32_t aggregate_id = ribos_vm_interpreter_operand(
        tables,
        instruction,
        0);
    RibosVmInterpreterSlot aggregate;
    RibosVmInterpreterType type;
    uint8_t tag;

    if (!ribos_vm_interpreter_slot(
            tables,
            aggregate_id,
            &aggregate) ||
        aggregate.function_id != control->function_id ||
        !ribos_vm_interpreter_type(
            tables,
            aggregate.type_id,
            &type) ||
        !ribos_vm_interpreter_read_slice(
            prepared_program,
            storage,
            arena_size,
            control,
            &aggregate,
            0,
            &tag,
            1) ||
        !ribos_vm_interpreter_variant_tag_is_valid(
            tables,
            &type,
            tag) ||
        result->type_kind != RIBOS_BC_TYPE_UNSIGNED ||
        result->bit_width != 32 ||
        !ribos_vm_interpreter_store_unsigned(
            prepared_program,
            storage,
            arena_size,
            control,
            result,
            tag)) {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    return RIBOS_VM_FAULT_NONE;
}

static uint32_t
ribos_vm_interpreter_execute_variant_payload(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmInterpreterTables *tables,
    const RibosVmStorageExecutionControl *control,
    const uint8_t *instruction,
    const RibosVmInterpreterSlot *result)
{
    uint32_t aggregate_id = ribos_vm_interpreter_operand(
        tables,
        instruction,
        0);
    RibosVmInterpreterSlot aggregate;
    RibosVmInterpreterType type;
    RibosVmInterpreterType payload;
    uint32_t expected_tag =
        ribos_vm_interpreter_u32(instruction + 20);
    uint32_t ordinal =
        ribos_vm_interpreter_u32(instruction + 24);
    uint32_t payload_type;
    uint32_t payload_offset;
    uint8_t stored_tag;

    if (ordinal == RIBOS_VM_INVALID_ID) {
        ordinal = 0;
    }
    if (!ribos_vm_interpreter_slot(
            tables,
            aggregate_id,
            &aggregate) ||
        aggregate.function_id != control->function_id ||
        !ribos_vm_interpreter_type(
            tables,
            aggregate.type_id,
            &type) ||
        expected_tag > UINT8_MAX ||
        !ribos_vm_interpreter_read_slice(
            prepared_program,
            storage,
            arena_size,
            control,
            &aggregate,
            0,
            &stored_tag,
            1) ||
        stored_tag != expected_tag ||
        !ribos_vm_interpreter_variant_payload(
            tables,
            &type,
            expected_tag,
            ordinal,
            &payload_type,
            &payload_offset,
            NULL) ||
        payload_type != result->type_id ||
        !ribos_vm_interpreter_type(
            tables,
            payload_type,
            &payload) ||
        result->byte_size != payload.byte_size ||
        !ribos_vm_interpreter_zero_slot(
            prepared_program,
            storage,
            arena_size,
            control,
            result) ||
        !ribos_vm_interpreter_copy_slice(
            prepared_program,
            storage,
            arena_size,
            control,
            &aggregate,
            payload_offset,
            control,
            result,
            0,
            payload.byte_size)) {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    return RIBOS_VM_FAULT_NONE;
}

static uint32_t
ribos_vm_interpreter_execute_unary(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *control,
    const RibosVmInterpreterSlot *operand,
    const RibosVmInterpreterSlot *result,
    uint32_t operation)
{
    uint32_t boolean_value;
    uint64_t raw;
    int64_t signed_value;
    int64_t signed_result;

    if (operand->type_id != result->type_id) {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    if (operation == RIBOS_BC_CHECK_NOT) {
        if (operand->type_kind != RIBOS_BC_TYPE_BOOL ||
            ribos_vm_storage_slot_load_bool_v1(
                prepared_program,
                storage,
                arena_size,
                control->function_id,
                control->frame_base,
                operand->id,
                &boolean_value) != RIBOS_VM_STATUS_OK ||
            ribos_vm_storage_slot_store_bool_v1(
                prepared_program,
                storage,
                arena_size,
                control->function_id,
                control->frame_base,
                result->id,
                !boolean_value) != RIBOS_VM_STATUS_OK) {
            return RIBOS_VM_FAULT_INVALID_VALUE;
        }
        return RIBOS_VM_FAULT_NONE;
    }
    if (operand->type_kind != RIBOS_BC_TYPE_UNSIGNED &&
        operand->type_kind != RIBOS_BC_TYPE_SIGNED) {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    if (operation == RIBOS_BC_CHECK_POSITIVE) {
        uint8_t bytes[8];

        return ribos_vm_interpreter_slot_read(
                   prepared_program,
                   storage,
                   arena_size,
                   control,
                   operand,
                   bytes) &&
               ribos_vm_interpreter_slot_write(
                   prepared_program,
                   storage,
                   arena_size,
                   control,
                   result,
                   bytes) ?
            RIBOS_VM_FAULT_NONE :
            RIBOS_VM_FAULT_INVALID_VALUE;
    }
    if (operation == RIBOS_BC_CHECK_BIT_NOT) {
        if (!ribos_vm_interpreter_load_raw_integer(
                prepared_program,
                storage,
                arena_size,
                control,
                operand,
                &raw) ||
            !ribos_vm_interpreter_store_raw_integer(
                prepared_program,
                storage,
                arena_size,
                control,
                result,
                (~raw) &
                    ribos_vm_interpreter_mask(operand->bit_width))) {
            return RIBOS_VM_FAULT_INVALID_VALUE;
        }
        return RIBOS_VM_FAULT_NONE;
    }
    if (operation != RIBOS_BC_CHECK_NEGATIVE ||
        !ribos_vm_interpreter_load_signed(
            prepared_program,
            storage,
            arena_size,
            control,
            operand,
            &signed_value)) {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    if (signed_value ==
        ribos_vm_interpreter_signed_minimum(operand->bit_width)) {
        return RIBOS_VM_FAULT_ARITHMETIC;
    }
    signed_result = -signed_value;
    if (!ribos_vm_interpreter_store_signed(
            prepared_program,
            storage,
            arena_size,
            control,
            result,
            signed_result)) {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    return RIBOS_VM_FAULT_NONE;
}

static uint32_t
ribos_vm_interpreter_execute_compare(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *control,
    const RibosVmInterpreterSlot *left,
    const RibosVmInterpreterSlot *right,
    const RibosVmInterpreterSlot *result,
    uint32_t operation)
{
    uint8_t left_bytes[8] = {0};
    uint8_t right_bytes[8] = {0};
    uint32_t comparison = 0;

    if (left->type_id != right->type_id ||
        left->byte_size != right->byte_size ||
        result->type_kind != RIBOS_BC_TYPE_BOOL) {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    if (operation == RIBOS_BC_CHECK_EQUAL ||
        operation == RIBOS_BC_CHECK_NOT_EQUAL) {
        int exact_comparison;

        if (ribos_vm_storage_slot_slice_compare_internal_v1(
                prepared_program,
                storage,
                arena_size,
                control->function_id,
                control->frame_base,
                left->id,
                0,
                control->function_id,
                control->frame_base,
                right->id,
                0,
                left->byte_size,
                &exact_comparison) != RIBOS_VM_STATUS_OK) {
            return RIBOS_VM_FAULT_INVALID_VALUE;
        }
        comparison = exact_comparison == 0;
        if (operation == RIBOS_BC_CHECK_NOT_EQUAL) {
            comparison = !comparison;
        }
    } else if (left->byte_size > sizeof(left_bytes) ||
        !ribos_vm_interpreter_slot_read(
            prepared_program,
            storage,
            arena_size,
            control,
            left,
            left_bytes) ||
        !ribos_vm_interpreter_slot_read(
            prepared_program,
            storage,
            arena_size,
            control,
            right,
            right_bytes)) {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    } else if (left->type_kind == RIBOS_BC_TYPE_UNSIGNED) {
        uint64_t left_value;
        uint64_t right_value;

        if (!ribos_vm_interpreter_load_unsigned(
                prepared_program,
                storage,
                arena_size,
                control,
                left,
                &left_value) ||
            !ribos_vm_interpreter_load_unsigned(
                prepared_program,
                storage,
                arena_size,
                control,
                right,
                &right_value)) {
            return RIBOS_VM_FAULT_INVALID_VALUE;
        }
        if (operation == RIBOS_BC_CHECK_LESS) {
            comparison = left_value < right_value;
        } else if (operation == RIBOS_BC_CHECK_LESS_EQUAL) {
            comparison = left_value <= right_value;
        } else if (operation == RIBOS_BC_CHECK_GREATER) {
            comparison = left_value > right_value;
        } else if (operation == RIBOS_BC_CHECK_GREATER_EQUAL) {
            comparison = left_value >= right_value;
        } else {
            return RIBOS_VM_FAULT_INVALID_VALUE;
        }
    } else if (left->type_kind == RIBOS_BC_TYPE_SIGNED) {
        int64_t left_value;
        int64_t right_value;

        if (!ribos_vm_interpreter_load_signed(
                prepared_program,
                storage,
                arena_size,
                control,
                left,
                &left_value) ||
            !ribos_vm_interpreter_load_signed(
                prepared_program,
                storage,
                arena_size,
                control,
                right,
                &right_value)) {
            return RIBOS_VM_FAULT_INVALID_VALUE;
        }
        if (operation == RIBOS_BC_CHECK_LESS) {
            comparison = left_value < right_value;
        } else if (operation == RIBOS_BC_CHECK_LESS_EQUAL) {
            comparison = left_value <= right_value;
        } else if (operation == RIBOS_BC_CHECK_GREATER) {
            comparison = left_value > right_value;
        } else if (operation == RIBOS_BC_CHECK_GREATER_EQUAL) {
            comparison = left_value >= right_value;
        } else {
            return RIBOS_VM_FAULT_INVALID_VALUE;
        }
    } else if (left->type_kind == RIBOS_BC_TYPE_NAMED &&
               (left->byte_size == 4 || left->byte_size == 8)) {
        uint64_t left_value = left->byte_size == 4 ?
            ribos_vm_interpreter_u32(left_bytes) :
            ribos_vm_interpreter_u64(left_bytes);
        uint64_t right_value = right->byte_size == 4 ?
            ribos_vm_interpreter_u32(right_bytes) :
            ribos_vm_interpreter_u64(right_bytes);

        if (operation == RIBOS_BC_CHECK_LESS) {
            comparison = left_value < right_value;
        } else if (operation == RIBOS_BC_CHECK_LESS_EQUAL) {
            comparison = left_value <= right_value;
        } else if (operation == RIBOS_BC_CHECK_GREATER) {
            comparison = left_value > right_value;
        } else if (operation == RIBOS_BC_CHECK_GREATER_EQUAL) {
            comparison = left_value >= right_value;
        } else {
            return RIBOS_VM_FAULT_INVALID_VALUE;
        }
    } else {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    return ribos_vm_storage_slot_store_bool_v1(
               prepared_program,
               storage,
               arena_size,
               control->function_id,
               control->frame_base,
               result->id,
               comparison) == RIBOS_VM_STATUS_OK ?
        RIBOS_VM_FAULT_NONE :
        RIBOS_VM_FAULT_INVALID_VALUE;
}

static uint32_t
ribos_vm_interpreter_execute_unsigned_binary(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *control,
    const RibosVmInterpreterSlot *left,
    const RibosVmInterpreterSlot *right,
    const RibosVmInterpreterSlot *result,
    uint32_t operation)
{
    uint64_t left_value;
    uint64_t right_value;
    uint64_t output = 0;
    uint64_t maximum =
        ribos_vm_interpreter_mask(left->bit_width);

    if (!ribos_vm_interpreter_load_unsigned(
            prepared_program,
            storage,
            arena_size,
            control,
            left,
            &left_value) ||
        !ribos_vm_interpreter_load_unsigned(
            prepared_program,
            storage,
            arena_size,
            control,
            right,
            &right_value)) {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    switch (operation) {
    case RIBOS_BC_CHECK_BIT_OR:
        output = left_value | right_value;
        break;
    case RIBOS_BC_CHECK_BIT_XOR:
        output = left_value ^ right_value;
        break;
    case RIBOS_BC_CHECK_BIT_AND:
        output = left_value & right_value;
        break;
    case RIBOS_BC_CHECK_SHIFT_LEFT:
        if (right_value >= left->bit_width ||
            left_value > (maximum >> right_value)) {
            return RIBOS_VM_FAULT_ARITHMETIC;
        }
        output = left_value << right_value;
        break;
    case RIBOS_BC_CHECK_SHIFT_RIGHT:
        if (right_value >= left->bit_width) {
            return RIBOS_VM_FAULT_ARITHMETIC;
        }
        output = left_value >> right_value;
        break;
    case RIBOS_BC_CHECK_ADD:
        if (left_value > maximum - right_value) {
            return RIBOS_VM_FAULT_ARITHMETIC;
        }
        output = left_value + right_value;
        break;
    case RIBOS_BC_CHECK_SUBTRACT:
        if (left_value < right_value) {
            return RIBOS_VM_FAULT_ARITHMETIC;
        }
        output = left_value - right_value;
        break;
    case RIBOS_BC_CHECK_MULTIPLY:
        if (right_value != 0 &&
            left_value > maximum / right_value) {
            return RIBOS_VM_FAULT_ARITHMETIC;
        }
        output = left_value * right_value;
        break;
    case RIBOS_BC_CHECK_DIVIDE:
        if (right_value == 0) {
            return RIBOS_VM_FAULT_ARITHMETIC;
        }
        output = left_value / right_value;
        break;
    case RIBOS_BC_CHECK_REMAINDER:
        if (right_value == 0) {
            return RIBOS_VM_FAULT_ARITHMETIC;
        }
        output = left_value % right_value;
        break;
    default:
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    return ribos_vm_interpreter_store_unsigned(
               prepared_program,
               storage,
               arena_size,
               control,
               result,
               output) ?
        RIBOS_VM_FAULT_NONE :
        RIBOS_VM_FAULT_INVALID_VALUE;
}

static uint32_t
ribos_vm_interpreter_execute_signed_binary(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *control,
    const RibosVmInterpreterSlot *left,
    const RibosVmInterpreterSlot *right,
    const RibosVmInterpreterSlot *result,
    uint32_t operation)
{
    int64_t left_value;
    int64_t right_value;
    int64_t output = 0;
    int64_t minimum =
        ribos_vm_interpreter_signed_minimum(left->bit_width);
    int64_t maximum =
        ribos_vm_interpreter_signed_maximum(left->bit_width);

    if (!ribos_vm_interpreter_load_signed(
            prepared_program,
            storage,
            arena_size,
            control,
            left,
            &left_value) ||
        !ribos_vm_interpreter_load_signed(
            prepared_program,
            storage,
            arena_size,
            control,
            right,
            &right_value)) {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    switch (operation) {
    case RIBOS_BC_CHECK_BIT_OR:
    case RIBOS_BC_CHECK_BIT_XOR:
    case RIBOS_BC_CHECK_BIT_AND: {
        uint64_t left_raw;
        uint64_t right_raw;
        uint64_t output_raw;

        if (!ribos_vm_interpreter_load_raw_integer(
                prepared_program,
                storage,
                arena_size,
                control,
                left,
                &left_raw) ||
            !ribos_vm_interpreter_load_raw_integer(
                prepared_program,
                storage,
                arena_size,
                control,
                right,
                &right_raw)) {
            return RIBOS_VM_FAULT_INVALID_VALUE;
        }
        output_raw = operation == RIBOS_BC_CHECK_BIT_OR ?
            left_raw | right_raw :
            operation == RIBOS_BC_CHECK_BIT_XOR ?
                left_raw ^ right_raw :
                left_raw & right_raw;
        return ribos_vm_interpreter_store_raw_integer(
                   prepared_program,
                   storage,
                   arena_size,
                   control,
                   result,
                   output_raw) ?
            RIBOS_VM_FAULT_NONE :
            RIBOS_VM_FAULT_INVALID_VALUE;
    }
    case RIBOS_BC_CHECK_SHIFT_LEFT: {
        uint64_t magnitude;
        uint64_t limit;
        uint32_t shift;
        int negative;

        if (right_value < 0 ||
            (uint64_t)right_value >= left->bit_width) {
            return RIBOS_VM_FAULT_ARITHMETIC;
        }
        shift = (uint32_t)right_value;
        negative = left_value < 0;
        magnitude =
            ribos_vm_interpreter_signed_magnitude(left_value);
        limit = negative ?
            ribos_vm_interpreter_signed_magnitude(minimum) :
            (uint64_t)maximum;
        if (magnitude > (limit >> shift) ||
            !ribos_vm_interpreter_signed_from_magnitude(
                magnitude << shift,
                negative,
                &output)) {
            return RIBOS_VM_FAULT_ARITHMETIC;
        }
        break;
    }
    case RIBOS_BC_CHECK_SHIFT_RIGHT:
        if (right_value < 0 ||
            (uint64_t)right_value >= left->bit_width ||
            !ribos_vm_interpreter_signed_shift_right(
                left_value,
                (uint32_t)right_value,
                &output)) {
            return RIBOS_VM_FAULT_ARITHMETIC;
        }
        break;
    case RIBOS_BC_CHECK_ADD:
        if (!ribos_vm_interpreter_signed_add(
                left_value,
                right_value,
                minimum,
                maximum,
                &output)) {
            return RIBOS_VM_FAULT_ARITHMETIC;
        }
        break;
    case RIBOS_BC_CHECK_SUBTRACT:
        if (!ribos_vm_interpreter_signed_subtract(
                left_value,
                right_value,
                minimum,
                maximum,
                &output)) {
            return RIBOS_VM_FAULT_ARITHMETIC;
        }
        break;
    case RIBOS_BC_CHECK_MULTIPLY:
        if (!ribos_vm_interpreter_signed_multiply(
                left_value,
                right_value,
                minimum,
                maximum,
                &output)) {
            return RIBOS_VM_FAULT_ARITHMETIC;
        }
        break;
    case RIBOS_BC_CHECK_DIVIDE:
        if (right_value == 0 ||
            (left_value == minimum && right_value == -1)) {
            return RIBOS_VM_FAULT_ARITHMETIC;
        }
        output = left_value / right_value;
        break;
    case RIBOS_BC_CHECK_REMAINDER:
        if (right_value == 0 ||
            (left_value == minimum && right_value == -1)) {
            return RIBOS_VM_FAULT_ARITHMETIC;
        }
        output = left_value % right_value;
        break;
    default:
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    return ribos_vm_interpreter_store_signed(
               prepared_program,
               storage,
               arena_size,
               control,
               result,
               output) ?
        RIBOS_VM_FAULT_NONE :
        RIBOS_VM_FAULT_INVALID_VALUE;
}

static uint32_t
ribos_vm_interpreter_execute_binary(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *control,
    const RibosVmInterpreterSlot *left,
    const RibosVmInterpreterSlot *right,
    const RibosVmInterpreterSlot *result,
    uint32_t operation)
{
    if (operation >= RIBOS_BC_CHECK_EQUAL &&
        operation <= RIBOS_BC_CHECK_GREATER_EQUAL) {
        return ribos_vm_interpreter_execute_compare(
            prepared_program,
            storage,
            arena_size,
            control,
            left,
            right,
            result,
            operation);
    }
    if (operation == RIBOS_BC_CHECK_IN ||
        operation == RIBOS_BC_CHECK_NOT_IN ||
        left->type_id != right->type_id ||
        left->type_id != result->type_id) {
        return RIBOS_VM_FAULT_INVALID_VALUE;
    }
    if (left->type_kind == RIBOS_BC_TYPE_UNSIGNED) {
        return ribos_vm_interpreter_execute_unsigned_binary(
            prepared_program,
            storage,
            arena_size,
            control,
            left,
            right,
            result,
            operation);
    }
    if (left->type_kind == RIBOS_BC_TYPE_SIGNED) {
        return ribos_vm_interpreter_execute_signed_binary(
            prepared_program,
            storage,
            arena_size,
            control,
            left,
            right,
            result,
            operation);
    }
    return RIBOS_VM_FAULT_INVALID_VALUE;
}

static RibosVmStatus
ribos_vm_interpreter_execute_call(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmInterpreterTables *tables,
    const uint8_t *instruction,
    const RibosVmInterpreterSlot *result,
    RibosVmStorageExecutionControl *control,
    uint32_t *fault_code,
    uint32_t *fault_detail)
{
    RibosVmStorageExecutionControl callee_control;
    RibosVmStorageExecutionControl callee_value_control;
    RibosVmStorageCallTarget target;
    const uint8_t *function;
    uint32_t callee_function_id;
    uint32_t parameter_start;
    uint32_t parameter_count;
    uint32_t continuation_instruction_id;
    uint32_t query_fault;
    uint32_t ordinal;
    RibosVmStatus status;

    if (tables == NULL || instruction == NULL || result == NULL ||
        control == NULL || fault_code == NULL ||
        fault_detail == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *fault_code = RIBOS_VM_FAULT_NONE;
    *fault_detail = RIBOS_VM_INVALID_ID;
    callee_function_id =
        ribos_vm_interpreter_u32(instruction + 20);
    continuation_instruction_id =
        ribos_vm_interpreter_u32(instruction + 32);
    function = ribos_vm_interpreter_row(
        tables->functions,
        callee_function_id);
    if (function == NULL ||
        ribos_vm_interpreter_u32(function) != callee_function_id ||
        ribos_vm_interpreter_u32(function + 8) != result->type_id) {
        *fault_code = RIBOS_VM_FAULT_INVALID_VALUE;
        *fault_detail = callee_function_id;
        return RIBOS_VM_STATUS_OK;
    }
    parameter_start = ribos_vm_interpreter_u32(function + 32);
    parameter_count = ribos_vm_interpreter_u32(function + 36);
    if (parameter_count !=
        ribos_vm_interpreter_u16(instruction + 2)) {
        *fault_code = RIBOS_VM_FAULT_INVALID_VALUE;
        *fault_detail = callee_function_id;
        return RIBOS_VM_STATUS_OK;
    }
    status = ribos_vm_storage_call_target_internal_v1(
        prepared_program,
        storage,
        arena_size,
        control,
        callee_function_id,
        &target,
        &query_fault);
    if (status == RIBOS_VM_STATUS_LIMIT_EXCEEDED &&
        (query_fault == RIBOS_VM_FAULT_CALL_DEPTH ||
         query_fault == RIBOS_VM_FAULT_STACK_BOUNDS)) {
        *fault_code = query_fault;
        *fault_detail = callee_function_id;
        return RIBOS_VM_STATUS_OK;
    }
    if (status != RIBOS_VM_STATUS_OK) {
        *fault_code = RIBOS_VM_FAULT_INTERNAL;
        *fault_detail = callee_function_id;
        return RIBOS_VM_STATUS_OK;
    }
    status = ribos_vm_storage_reset_frame_v1(
        prepared_program,
        storage,
        arena_size,
        callee_function_id,
        target.frame_base);
    if (status != RIBOS_VM_STATUS_OK) {
        *fault_code = RIBOS_VM_FAULT_INTERNAL;
        *fault_detail = callee_function_id;
        return RIBOS_VM_STATUS_OK;
    }
    callee_value_control = *control;
    callee_value_control.function_id = callee_function_id;
    callee_value_control.frame_base = target.frame_base;
    for (ordinal = 0; ordinal < parameter_count; ++ordinal) {
        uint32_t operand_id = ribos_vm_interpreter_operand(
            tables,
            instruction,
            ordinal);
        RibosVmInterpreterSlot operand;
        RibosVmInterpreterSlot parameter;

        if (!ribos_vm_interpreter_slot(
                tables,
                operand_id,
                &operand) ||
            !ribos_vm_interpreter_slot(
                tables,
                parameter_start + ordinal,
                &parameter) ||
            operand.function_id != control->function_id ||
            parameter.function_id != callee_function_id ||
            operand.type_id != parameter.type_id ||
            operand.byte_size != parameter.byte_size ||
            !ribos_vm_interpreter_copy_slot(
                prepared_program,
                storage,
                arena_size,
                control,
                &operand,
                &callee_value_control,
                &parameter)) {
            *fault_code = RIBOS_VM_FAULT_INVALID_VALUE;
            *fault_detail = ordinal;
            return RIBOS_VM_STATUS_OK;
        }
    }
    status = ribos_vm_storage_frame_push_internal_v1(
        prepared_program,
        storage,
        arena_size,
        control,
        &target,
        continuation_instruction_id,
        result->id,
        &callee_control);
    if (status != RIBOS_VM_STATUS_OK) {
        *fault_code = RIBOS_VM_FAULT_INTERNAL;
        *fault_detail = callee_function_id;
        return RIBOS_VM_STATUS_OK;
    }
    *control = callee_control;
    return RIBOS_VM_STATUS_OK;
}

static RibosVmStatus
ribos_vm_interpreter_execute_return(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmInterpreterTables *tables,
    const RibosVmInterpreterSlot *operand,
    RibosVmStorageExecutionControl *control,
    uint32_t *fault_code,
    uint32_t *fault_detail)
{
    RibosVmStorageExecutionControl caller_control;
    RibosVmStorageExecutionControl caller_value_control;
    RibosVmStorageReturnTarget target;
    RibosVmInterpreterSlot caller_result;
    RibosVmStatus status;

    if (tables == NULL || operand == NULL || control == NULL ||
        fault_code == NULL || fault_detail == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *fault_code = RIBOS_VM_FAULT_NONE;
    *fault_detail = RIBOS_VM_INVALID_ID;
    status = ribos_vm_storage_return_target_internal_v1(
        prepared_program,
        storage,
        arena_size,
        control,
        &target);
    if (status != RIBOS_VM_STATUS_OK ||
        !ribos_vm_interpreter_slot(
            tables,
            target.return_slot_id,
            &caller_result) ||
        caller_result.function_id != target.function_id ||
        caller_result.type_id != operand->type_id ||
        caller_result.byte_size != operand->byte_size ||
        operand->function_id != control->function_id) {
        *fault_code = status == RIBOS_VM_STATUS_OK ?
            RIBOS_VM_FAULT_INVALID_VALUE :
            RIBOS_VM_FAULT_INTERNAL;
        *fault_detail = operand->id;
        return RIBOS_VM_STATUS_OK;
    }
    caller_value_control = *control;
    caller_value_control.function_id = target.function_id;
    caller_value_control.frame_base = target.frame_base;
    if (!ribos_vm_interpreter_copy_slot(
            prepared_program,
            storage,
            arena_size,
            control,
            operand,
            &caller_value_control,
            &caller_result)) {
        *fault_code = RIBOS_VM_FAULT_INVALID_VALUE;
        *fault_detail = caller_result.id;
        return RIBOS_VM_STATUS_OK;
    }
    status = ribos_vm_storage_frame_pop_internal_v1(
        prepared_program,
        storage,
        arena_size,
        control,
        &target,
        &caller_control);
    if (status != RIBOS_VM_STATUS_OK) {
        *fault_code = RIBOS_VM_FAULT_INTERNAL;
        *fault_detail = target.function_id;
        return RIBOS_VM_STATUS_OK;
    }
    *control = caller_control;
    return RIBOS_VM_STATUS_OK;
}

static RibosVmStatus
ribos_vm_interpreter_fault(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmStorageExecutionControl *control,
    uint32_t fault_code,
    uint32_t subject,
    uint32_t detail)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(prepared_program);
    RibosVmFaultReceipt receipt;
    RibosVmStatus status;

    if (view == NULL || control == NULL) {
        return RIBOS_VM_STATUS_NOT_PREPARED;
    }
    memset(&receipt, 0, sizeof(receipt));
    receipt.fault_code = fault_code;
    receipt.subject = subject;
    receipt.function_id = control->function_id;
    receipt.instruction_id = control->instruction_id;
    receipt.helper_id = RIBOS_VM_INVALID_ID;
    receipt.detail = detail;
    receipt.consumed_instructions = control->consumed_instructions;
    memcpy(
        receipt.artifact_hash,
        view->artifact_hash,
        RIBOS_VM_DIGEST_BYTES);
    status = ribos_vm_storage_seal_fault_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &receipt);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    control->state = RIBOS_VM_INTERPRETER_FAULTED;
    control->return_slot_id = RIBOS_VM_INVALID_ID;
    return ribos_vm_storage_execution_store_internal_v1(
        prepared_program,
        storage,
        arena_size,
        control);
}

static RibosVmStatus
ribos_vm_interpreter_advance(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmStorageExecutionControl *control,
    const RibosVmInterpreterTables *tables,
    uint32_t next_instruction)
{
    const uint8_t *next =
        ribos_vm_interpreter_row(tables->instructions, next_instruction);

    if (next == NULL ||
        ribos_vm_interpreter_u32(next + 4) != next_instruction ||
        ribos_vm_interpreter_u32(next + 8) != control->block_id) {
        return ribos_vm_interpreter_fault(
            prepared_program,
            storage,
            arena_size,
            control,
            RIBOS_VM_FAULT_INTERNAL,
            RIBOS_VM_FAULT_SUBJECT_INSTRUCTION,
            RIBOS_VM_INVALID_ID);
    }
    control->state = RIBOS_VM_INTERPRETER_RUNNING;
    control->instruction_id = next_instruction;
    control->return_slot_id = RIBOS_VM_INVALID_ID;
    return ribos_vm_storage_execution_store_internal_v1(
        prepared_program,
        storage,
        arena_size,
        control);
}

static RibosVmStatus
ribos_vm_interpreter_enter_block(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmStorageExecutionControl *control,
    const RibosVmInterpreterTables *tables,
    uint32_t block_id)
{
    const uint8_t *block =
        ribos_vm_interpreter_row(tables->blocks, block_id);
    uint32_t first_instruction;
    uint32_t violation_loop_id;
    RibosVmStatus status;

    if (block == NULL ||
        ribos_vm_interpreter_u32(block) != block_id ||
        ribos_vm_interpreter_u32(block + 4) != control->function_id) {
        return ribos_vm_interpreter_fault(
            prepared_program,
            storage,
            arena_size,
            control,
            RIBOS_VM_FAULT_INTERNAL,
            RIBOS_VM_FAULT_SUBJECT_FUNCTION,
            RIBOS_VM_INVALID_ID);
    }
    status = ribos_vm_storage_loop_transition_internal_v1(
        prepared_program,
        storage,
        arena_size,
        control->function_id,
        control->block_id,
        block_id,
        &violation_loop_id);
    if (status == RIBOS_VM_STATUS_LIMIT_EXCEEDED) {
        return ribos_vm_interpreter_fault(
            prepared_program,
            storage,
            arena_size,
            control,
            RIBOS_VM_FAULT_LOOP_BOUND,
            RIBOS_VM_FAULT_SUBJECT_INSTRUCTION,
            violation_loop_id);
    }
    if (status != RIBOS_VM_STATUS_OK) {
        return ribos_vm_interpreter_fault(
            prepared_program,
            storage,
            arena_size,
            control,
            RIBOS_VM_FAULT_INTERNAL,
            RIBOS_VM_FAULT_SUBJECT_RUNTIME,
            violation_loop_id);
    }
    first_instruction = ribos_vm_interpreter_u32(block + 8);
    control->block_id = block_id;
    return ribos_vm_interpreter_advance(
        prepared_program,
        storage,
        arena_size,
        control,
        tables,
        first_instruction);
}

RibosVmStatus
ribos_vm_interpreter_initialize_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmContext *context,
    RibosVmStorage *storage,
    size_t arena_size)
{
    RibosVmInterpreterTables tables;
    RibosVmStorageExecutionControl control;
    const uint8_t *function;
    const uint8_t *block;
    RibosVmInterpreterSlot parameter;
    uint32_t entry_function;
    uint32_t entry_block;
    uint32_t parameter_start;
    uint32_t parameter_count;
    RibosVmStatus status;

    status = ribos_vm_interpreter_tables(
        prepared_program,
        &tables);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_interpreter_validate_context(
        prepared_program,
        context);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_storage_validate_v1(
        prepared_program,
        storage,
        arena_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    entry_function = tables.view->entry_function;
    function = ribos_vm_interpreter_row(
        tables.functions,
        entry_function);
    if (function == NULL ||
        ribos_vm_interpreter_u32(function) != entry_function) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    entry_block = ribos_vm_interpreter_u32(function + 12);
    parameter_start = ribos_vm_interpreter_u32(function + 32);
    parameter_count = ribos_vm_interpreter_u32(function + 36);
    block = ribos_vm_interpreter_row(tables.blocks, entry_block);
    if (parameter_count != 1 ||
        !ribos_vm_interpreter_slot(
            &tables,
            parameter_start,
            &parameter) ||
        parameter.function_id != entry_function ||
        parameter.type_id != context->context_type_id ||
        parameter.byte_size != context->byte_size ||
        parameter.byte_size > 8 ||
        block == NULL ||
        ribos_vm_interpreter_u32(block) != entry_block ||
        ribos_vm_interpreter_u32(block + 4) != entry_function) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    status = ribos_vm_storage_reset_frame_v1(
        prepared_program,
        storage,
        arena_size,
        entry_function,
        0);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    memset(&control, 0, sizeof(control));
    control.state = RIBOS_VM_INTERPRETER_READY;
    control.function_id = entry_function;
    control.block_id = entry_block;
    control.instruction_id =
        ribos_vm_interpreter_u32(block + 8);
    control.return_slot_id = RIBOS_VM_INVALID_ID;
    control.context_generation = context->generation;
    control.context_type_id = context->context_type_id;
    memcpy(
        control.context_digest,
        context->digest,
        RIBOS_VM_DIGEST_BYTES);
    return ribos_vm_storage_execution_begin_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &control);
}

static RibosVmStatus
ribos_vm_interpreter_snapshot_from_control(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *control,
    uint64_t remaining,
    RibosVmInterpreterSnapshot *snapshot)
{
    RibosVmInterpreterTables tables;
    const uint8_t *instruction;
    RibosVmStatus status;

    if (snapshot == NULL || control == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    status = ribos_vm_interpreter_tables(
        prepared_program,
        &tables);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    instruction = ribos_vm_interpreter_row(
        tables.instructions,
        control->instruction_id);
    snapshot->size = sizeof(*snapshot);
    snapshot->interpreter_major = RIBOS_VM_INTERPRETER_V1_MAJOR;
    snapshot->interpreter_minor = RIBOS_VM_INTERPRETER_V1_MINOR;
    snapshot->state = control->state;
    snapshot->function_id = control->function_id;
    snapshot->block_id = control->block_id;
    snapshot->instruction_id = control->instruction_id;
    snapshot->source_map_id = instruction == NULL ?
        RIBOS_VM_INVALID_ID :
        ribos_vm_interpreter_u32(instruction + 28);
    snapshot->return_slot_id = control->return_slot_id;
    snapshot->remaining_instructions = remaining;
    snapshot->consumed_instructions =
        control->consumed_instructions;
    snapshot->frame_depth = control->frame_depth;
    snapshot->stack_bytes = control->stack_cursor;
    if (control->state == RIBOS_VM_INTERPRETER_FAULTED) {
        RibosVmFaultReceipt receipt;

        status = ribos_vm_storage_read_fault_internal_v1(
            prepared_program,
            storage,
            arena_size,
            &receipt);
        if (status != RIBOS_VM_STATUS_OK) {
            memset(snapshot, 0, sizeof(*snapshot));
            return status;
        }
        snapshot->fault_code = receipt.fault_code;
    }
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_interpreter_snapshot_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    RibosVmInterpreterSnapshot *snapshot)
{
    RibosVmStorageExecutionControl control;
    uint64_t remaining;
    RibosVmStatus status;

    status = ribos_vm_storage_execution_load_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &control,
        &remaining);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (control.state == RIBOS_VM_INTERPRETER_EMPTY) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    return ribos_vm_interpreter_snapshot_from_control(
        prepared_program,
        storage,
        arena_size,
        &control,
        remaining,
        snapshot);
}

RibosVmStatus
ribos_vm_interpreter_fault_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    RibosVmFaultReceipt *receipt)
{
    RibosVmStorageExecutionControl control;
    uint64_t remaining;
    RibosVmStatus status;

    status = ribos_vm_storage_execution_load_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &control,
        &remaining);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    (void)remaining;
    if (control.state != RIBOS_VM_INTERPRETER_FAULTED) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    return ribos_vm_storage_read_fault_internal_v1(
        prepared_program,
        storage,
        arena_size,
        receipt);
}

RibosVmStatus
ribos_vm_interpreter_step_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmContext *context,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmInterpreterSnapshot *snapshot)
{
    RibosVmInterpreterTables tables;
    RibosVmStorageExecutionControl control;
    const uint8_t *function;
    const uint8_t *block;
    const uint8_t *instruction;
    uint32_t opcode;
    uint32_t result_id;
    uint32_t source_map_id;
    uint32_t fault_detail;
    uint32_t next_instruction;
    uint32_t fault_code = RIBOS_VM_FAULT_NONE;
    uint64_t remaining;
    uint64_t consumed;
    RibosVmInterpreterSlot result;
    RibosVmStatus status;

    if (snapshot == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    status = ribos_vm_interpreter_tables(
        prepared_program,
        &tables);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_interpreter_validate_context(
        prepared_program,
        context);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_storage_execution_load_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &control,
        &remaining);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (control.state != RIBOS_VM_INTERPRETER_READY &&
        control.state != RIBOS_VM_INTERPRETER_RUNNING) {
        return RIBOS_VM_STATUS_ALREADY_CONSUMED;
    }
    if (control.context_generation != context->generation ||
        control.context_type_id != context->context_type_id ||
        memcmp(
            control.context_digest,
            context->digest,
            RIBOS_VM_DIGEST_BYTES) != 0) {
        return RIBOS_VM_STATUS_DIGEST_MISMATCH;
    }
    if (remaining == 0) {
        status = ribos_vm_interpreter_fault(
            prepared_program,
            storage,
            arena_size,
            &control,
            RIBOS_VM_FAULT_INSTRUCTION_BUDGET,
            RIBOS_VM_FAULT_SUBJECT_INSTRUCTION,
            RIBOS_VM_INVALID_ID);
        if (status != RIBOS_VM_STATUS_OK) {
            return status;
        }
        return ribos_vm_interpreter_snapshot_v1(
            prepared_program,
            storage,
            arena_size,
            snapshot);
    }
    status = ribos_vm_storage_consume_instruction_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &remaining,
        &consumed);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    control.consumed_instructions = consumed;

    function = ribos_vm_interpreter_row(
        tables.functions,
        control.function_id);
    block = ribos_vm_interpreter_row(
        tables.blocks,
        control.block_id);
    instruction = ribos_vm_interpreter_row(
        tables.instructions,
        control.instruction_id);
    if (function == NULL || block == NULL || instruction == NULL ||
        ribos_vm_interpreter_u32(function) != control.function_id ||
        ribos_vm_interpreter_u32(block) != control.block_id ||
        ribos_vm_interpreter_u32(block + 4) != control.function_id ||
        ribos_vm_interpreter_u32(instruction + 4) !=
            control.instruction_id ||
        ribos_vm_interpreter_u32(instruction + 8) !=
            control.block_id ||
        instruction[1] != 0 ||
        ribos_vm_interpreter_u32(instruction + 36) != 0) {
        status = ribos_vm_interpreter_fault(
            prepared_program,
            storage,
            arena_size,
            &control,
            RIBOS_VM_FAULT_INTERNAL,
            RIBOS_VM_FAULT_SUBJECT_INSTRUCTION,
            RIBOS_VM_INVALID_ID);
        if (status != RIBOS_VM_STATUS_OK) {
            return status;
        }
        return ribos_vm_interpreter_snapshot_v1(
            prepared_program,
            storage,
            arena_size,
            snapshot);
    }
    opcode = instruction[0];
    result_id = ribos_vm_interpreter_u32(instruction + 12);
    source_map_id = ribos_vm_interpreter_u32(instruction + 28);
    fault_detail = source_map_id;
    next_instruction = ribos_vm_interpreter_u32(instruction + 32);
    memset(&result, 0, sizeof(result));
    if (result_id != RIBOS_VM_INVALID_ID &&
        (!ribos_vm_interpreter_slot(&tables, result_id, &result) ||
         result.function_id != control.function_id)) {
        fault_code = RIBOS_VM_FAULT_INVALID_VALUE;
    }

    if (fault_code == RIBOS_VM_FAULT_NONE &&
        opcode == RIBOS_BC_PARAMETER) {
        uint32_t parameter_index =
            ribos_vm_interpreter_u32(instruction + 20);
        uint32_t parameter_start =
            ribos_vm_interpreter_u32(function + 32);
        uint32_t parameter_count =
            ribos_vm_interpreter_u32(function + 36);
        uint8_t bytes[8] = {0};

        if (parameter_index >= parameter_count ||
            result.id != parameter_start + parameter_index) {
            fault_code = RIBOS_VM_FAULT_INVALID_VALUE;
        } else if (control.frame_depth == 1) {
            if (control.function_id != tables.view->entry_function ||
                parameter_index != 0 ||
                result.type_id != context->context_type_id ||
                result.byte_size != context->byte_size ||
                result.byte_size > sizeof(bytes)) {
                fault_code = RIBOS_VM_FAULT_INVALID_VALUE;
            } else {
                memcpy(bytes, context->bytes, result.byte_size);
                if (!ribos_vm_interpreter_slot_write(
                        prepared_program,
                        storage,
                        arena_size,
                        &control,
                        &result,
                        bytes)) {
                    fault_code = RIBOS_VM_FAULT_INVALID_VALUE;
                }
            }
        } else {
            uint32_t slot_state;

            if (ribos_vm_storage_slot_state_v1(
                    prepared_program,
                    storage,
                    arena_size,
                    result.id,
                    &slot_state) != RIBOS_VM_STATUS_OK ||
                slot_state != RIBOS_VM_SLOT_STORAGE_INITIALIZED) {
                fault_code = RIBOS_VM_FAULT_INVALID_VALUE;
            }
        }
    } else if (fault_code == RIBOS_VM_FAULT_NONE &&
               opcode == RIBOS_BC_CONST_UNIT) {
        uint8_t bytes[8] = {0};

        if (result.type_kind != RIBOS_BC_TYPE_UNIT ||
            result.byte_size != 0 ||
            !ribos_vm_interpreter_slot_write(
                prepared_program,
                storage,
                arena_size,
                &control,
                &result,
                bytes)) {
            fault_code = RIBOS_VM_FAULT_INVALID_VALUE;
        }
    } else if (fault_code == RIBOS_VM_FAULT_NONE &&
               opcode == RIBOS_BC_CONST_BOOL) {
        uint64_t immediate =
            ribos_vm_interpreter_u64(instruction + 40);

        if (immediate > 1 ||
            ribos_vm_storage_slot_store_bool_v1(
                prepared_program,
                storage,
                arena_size,
                control.function_id,
                control.frame_base,
                result.id,
                (uint32_t)immediate) != RIBOS_VM_STATUS_OK) {
            fault_code = RIBOS_VM_FAULT_INVALID_VALUE;
        }
    } else if (fault_code == RIBOS_VM_FAULT_NONE &&
               opcode == RIBOS_BC_CONST_INTEGER) {
        uint64_t immediate =
            ribos_vm_interpreter_u64(instruction + 40);
        int stored = 0;

        if (result.type_kind == RIBOS_BC_TYPE_UNSIGNED) {
            stored = ribos_vm_interpreter_store_unsigned(
                prepared_program,
                storage,
                arena_size,
                &control,
                &result,
                immediate);
        } else if (result.type_kind == RIBOS_BC_TYPE_SIGNED &&
                   immediate <= (uint64_t)INT64_MAX) {
            stored = ribos_vm_interpreter_store_signed(
                prepared_program,
                storage,
                arena_size,
                &control,
                &result,
                (int64_t)immediate);
        }
        if (!stored) {
            fault_code = RIBOS_VM_FAULT_INVALID_VALUE;
        }
    } else if (fault_code == RIBOS_VM_FAULT_NONE &&
               (opcode == RIBOS_BC_CONST_STRING ||
                opcode == RIBOS_BC_CONST_SYMBOL)) {
        uint32_t constant_id =
            ribos_vm_interpreter_u32(instruction + 20);
        const uint8_t *constant =
            ribos_vm_interpreter_row(
                tables.constants,
                constant_id);
        uint8_t bytes[8] = {0};
        uint32_t expected_kind =
            opcode == RIBOS_BC_CONST_STRING ?
                RIBOS_BC_CONSTANT_STRING :
                RIBOS_BC_CONSTANT_SYMBOL;

        if (constant == NULL ||
            ribos_vm_interpreter_u16(constant + 4) !=
                expected_kind ||
            (result.byte_size != 4 && result.byte_size != 8) ||
            (opcode == RIBOS_BC_CONST_STRING &&
             result.byte_size != 8)) {
            fault_code = RIBOS_VM_FAULT_INVALID_VALUE;
        } else {
            ribos_vm_interpreter_write_u32(bytes, constant_id);
            if (result.byte_size == 8) {
                ribos_vm_interpreter_write_u32(
                    bytes + 4,
                    ribos_vm_interpreter_u32(constant + 12));
            }
            if (!ribos_vm_interpreter_slot_write(
                    prepared_program,
                    storage,
                    arena_size,
                    &control,
                    &result,
                    bytes)) {
                fault_code = RIBOS_VM_FAULT_INVALID_VALUE;
            }
        }
    } else if (fault_code == RIBOS_VM_FAULT_NONE &&
               opcode == RIBOS_BC_MOVE) {
        uint32_t operand_id =
            ribos_vm_interpreter_operand(&tables, instruction, 0);
        RibosVmInterpreterSlot operand;

        if (!ribos_vm_interpreter_slot(
                &tables,
                operand_id,
                &operand) ||
            operand.type_id != result.type_id ||
            !ribos_vm_interpreter_copy_slot(
                prepared_program,
                storage,
                arena_size,
                &control,
                &operand,
                &control,
                &result)) {
            fault_code = RIBOS_VM_FAULT_INVALID_VALUE;
        }
    } else if (fault_code == RIBOS_VM_FAULT_NONE &&
               opcode == RIBOS_BC_BUILD_LIST) {
        fault_code = ribos_vm_interpreter_execute_build_list(
            prepared_program,
            storage,
            arena_size,
            &tables,
            &control,
            instruction,
            &result);
    } else if (fault_code == RIBOS_VM_FAULT_NONE &&
               opcode == RIBOS_BC_BUILD_MAP) {
        fault_code = ribos_vm_interpreter_execute_build_map(
            prepared_program,
            storage,
            arena_size,
            &tables,
            &control,
            instruction,
            &result);
    } else if (fault_code == RIBOS_VM_FAULT_NONE &&
               opcode == RIBOS_BC_BUILD_STRUCT) {
        fault_code = ribos_vm_interpreter_execute_build_struct(
            prepared_program,
            storage,
            arena_size,
            &tables,
            &control,
            instruction,
            &result);
    } else if (fault_code == RIBOS_VM_FAULT_NONE &&
               opcode == RIBOS_BC_BUILD_VARIANT) {
        fault_code = ribos_vm_interpreter_execute_build_variant(
            prepared_program,
            storage,
            arena_size,
            &tables,
            &control,
            instruction,
            &result);
    } else if (fault_code == RIBOS_VM_FAULT_NONE &&
               opcode == RIBOS_BC_MEMBER) {
        fault_code = ribos_vm_interpreter_execute_member(
            prepared_program,
            storage,
            arena_size,
            &tables,
            &control,
            instruction,
            &result);
    } else if (fault_code == RIBOS_VM_FAULT_NONE &&
               opcode == RIBOS_BC_INDEX) {
        fault_code = ribos_vm_interpreter_execute_index(
            prepared_program,
            storage,
            arena_size,
            &tables,
            &control,
            instruction,
            &result);
    } else if (fault_code == RIBOS_VM_FAULT_NONE &&
               opcode == RIBOS_BC_COLLECTION_LENGTH) {
        fault_code =
            ribos_vm_interpreter_execute_collection_length(
                prepared_program,
                storage,
                arena_size,
                &tables,
                &control,
                instruction,
                &result);
    } else if (fault_code == RIBOS_VM_FAULT_NONE &&
               opcode == RIBOS_BC_VARIANT_TAG) {
        fault_code = ribos_vm_interpreter_execute_variant_tag(
            prepared_program,
            storage,
            arena_size,
            &tables,
            &control,
            instruction,
            &result);
    } else if (fault_code == RIBOS_VM_FAULT_NONE &&
               opcode == RIBOS_BC_VARIANT_PAYLOAD) {
        fault_code =
            ribos_vm_interpreter_execute_variant_payload(
                prepared_program,
                storage,
                arena_size,
                &tables,
                &control,
                instruction,
                &result);
    } else if (fault_code == RIBOS_VM_FAULT_NONE &&
               opcode == RIBOS_BC_CHECKED_UNARY) {
        uint32_t operand_id =
            ribos_vm_interpreter_operand(&tables, instruction, 0);
        RibosVmInterpreterSlot operand;

        if (!ribos_vm_interpreter_slot(
                &tables,
                operand_id,
                &operand)) {
            fault_code = RIBOS_VM_FAULT_INVALID_VALUE;
        } else {
            fault_code = ribos_vm_interpreter_execute_unary(
                prepared_program,
                storage,
                arena_size,
                &control,
                &operand,
                &result,
                ribos_vm_interpreter_u32(instruction + 20));
        }
    } else if (fault_code == RIBOS_VM_FAULT_NONE &&
               opcode == RIBOS_BC_CHECKED_BINARY) {
        uint32_t left_id =
            ribos_vm_interpreter_operand(&tables, instruction, 0);
        uint32_t right_id =
            ribos_vm_interpreter_operand(&tables, instruction, 1);
        RibosVmInterpreterSlot left;
        RibosVmInterpreterSlot right;

        if (!ribos_vm_interpreter_slot(
                &tables,
                left_id,
                &left) ||
            !ribos_vm_interpreter_slot(
                &tables,
                right_id,
                &right)) {
            fault_code = RIBOS_VM_FAULT_INVALID_VALUE;
        } else {
            fault_code = ribos_vm_interpreter_execute_binary(
                prepared_program,
                storage,
                arena_size,
                &control,
                &left,
                &right,
                &result,
                ribos_vm_interpreter_u32(instruction + 20));
        }
    } else if (fault_code == RIBOS_VM_FAULT_NONE &&
               opcode == RIBOS_BC_CALL_DIRECT) {
        status = ribos_vm_interpreter_execute_call(
            prepared_program,
            storage,
            arena_size,
            &tables,
            instruction,
            &result,
            &control,
            &fault_code,
            &fault_detail);
        if (status != RIBOS_VM_STATUS_OK) {
            return status;
        }
        if (fault_code == RIBOS_VM_FAULT_NONE) {
            return ribos_vm_interpreter_snapshot_v1(
                prepared_program,
                storage,
                arena_size,
                snapshot);
        }
    } else if (fault_code == RIBOS_VM_FAULT_NONE &&
               opcode == RIBOS_BC_JUMP) {
        status = ribos_vm_interpreter_enter_block(
            prepared_program,
            storage,
            arena_size,
            &control,
            &tables,
            ribos_vm_interpreter_u32(instruction + 20));
        if (status != RIBOS_VM_STATUS_OK) {
            return status;
        }
        return ribos_vm_interpreter_snapshot_v1(
            prepared_program,
            storage,
            arena_size,
            snapshot);
    } else if (fault_code == RIBOS_VM_FAULT_NONE &&
               opcode == RIBOS_BC_BRANCH) {
        uint32_t condition_id =
            ribos_vm_interpreter_operand(&tables, instruction, 0);
        RibosVmInterpreterSlot condition;
        uint32_t value;
        uint32_t target;

        if (!ribos_vm_interpreter_slot(
                &tables,
                condition_id,
                &condition) ||
            condition.type_kind != RIBOS_BC_TYPE_BOOL ||
            ribos_vm_storage_slot_load_bool_v1(
                prepared_program,
                storage,
                arena_size,
                control.function_id,
                control.frame_base,
                condition.id,
                &value) != RIBOS_VM_STATUS_OK) {
            fault_code = RIBOS_VM_FAULT_INVALID_VALUE;
        } else {
            target = value != 0 ?
                ribos_vm_interpreter_u32(instruction + 20) :
                ribos_vm_interpreter_u32(instruction + 24);
            status = ribos_vm_interpreter_enter_block(
                prepared_program,
                storage,
                arena_size,
                &control,
                &tables,
                target);
            if (status != RIBOS_VM_STATUS_OK) {
                return status;
            }
            return ribos_vm_interpreter_snapshot_v1(
                prepared_program,
                storage,
                arena_size,
                snapshot);
        }
    } else if (fault_code == RIBOS_VM_FAULT_NONE &&
               opcode == RIBOS_BC_RETURN) {
        uint32_t operand_id =
            ribos_vm_interpreter_operand(&tables, instruction, 0);
        RibosVmInterpreterSlot operand;
        uint32_t slot_state;

        if (!ribos_vm_interpreter_slot(
                &tables,
                operand_id,
                &operand) ||
            operand.function_id != control.function_id ||
            ribos_vm_storage_slot_state_v1(
                prepared_program,
                storage,
                arena_size,
                operand.id,
                &slot_state) != RIBOS_VM_STATUS_OK ||
            slot_state != RIBOS_VM_SLOT_STORAGE_INITIALIZED) {
            fault_code = RIBOS_VM_FAULT_INVALID_VALUE;
        } else if (control.frame_depth > 1) {
            status = ribos_vm_interpreter_execute_return(
                prepared_program,
                storage,
                arena_size,
                &tables,
                &operand,
                &control,
                &fault_code,
                &fault_detail);
            if (status != RIBOS_VM_STATUS_OK) {
                return status;
            }
            if (fault_code == RIBOS_VM_FAULT_NONE) {
                return ribos_vm_interpreter_snapshot_v1(
                    prepared_program,
                    storage,
                    arena_size,
                    snapshot);
            }
        } else {
            control.state = RIBOS_VM_INTERPRETER_RETURNED;
            control.return_slot_id = operand.id;
            status = ribos_vm_storage_execution_store_internal_v1(
                prepared_program,
                storage,
                arena_size,
                &control);
            if (status != RIBOS_VM_STATUS_OK) {
                return status;
            }
            return ribos_vm_interpreter_snapshot_v1(
                prepared_program,
                storage,
                arena_size,
                snapshot);
        }
    } else if (fault_code == RIBOS_VM_FAULT_NONE &&
               opcode == RIBOS_BC_TRAP) {
        fault_code = RIBOS_VM_FAULT_INVALID_STATE;
    } else if (fault_code == RIBOS_VM_FAULT_NONE) {
        fault_code = RIBOS_VM_FAULT_INVALID_STATE;
    }

    if (fault_code != RIBOS_VM_FAULT_NONE) {
        uint32_t subject =
            fault_code == RIBOS_VM_FAULT_ARITHMETIC ?
                RIBOS_VM_FAULT_SUBJECT_VALUE :
            (fault_code == RIBOS_VM_FAULT_CALL_DEPTH ||
             fault_code == RIBOS_VM_FAULT_STACK_BOUNDS) ?
                RIBOS_VM_FAULT_SUBJECT_FUNCTION :
            fault_code == RIBOS_VM_FAULT_INTERNAL ?
                RIBOS_VM_FAULT_SUBJECT_RUNTIME :
                RIBOS_VM_FAULT_SUBJECT_INSTRUCTION;

        status = ribos_vm_interpreter_fault(
            prepared_program,
            storage,
            arena_size,
            &control,
            fault_code,
            subject,
            fault_detail);
        if (status != RIBOS_VM_STATUS_OK) {
            return status;
        }
    } else {
        status = ribos_vm_interpreter_advance(
            prepared_program,
            storage,
            arena_size,
            &control,
            &tables,
            next_instruction);
        if (status != RIBOS_VM_STATUS_OK) {
            return status;
        }
    }
    return ribos_vm_interpreter_snapshot_v1(
        prepared_program,
        storage,
        arena_size,
        snapshot);
}

RibosVmStatus
ribos_vm_interpreter_run_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmContext *context,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmInterpreterSnapshot *snapshot)
{
    RibosVmInterpreterSnapshot local;
    uint64_t iterations;
    RibosVmStatus status;

    if (snapshot == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    status = ribos_vm_interpreter_snapshot_v1(
        prepared_program,
        storage,
        arena_size,
        &local);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (local.remaining_instructions == UINT64_MAX) {
        return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
    }
    iterations = local.remaining_instructions + 1;
    while (iterations != 0 &&
           (local.state == RIBOS_VM_INTERPRETER_READY ||
            local.state == RIBOS_VM_INTERPRETER_RUNNING)) {
        --iterations;
        status = ribos_vm_interpreter_step_v1(
            prepared_program,
            context,
            storage,
            arena_size,
            &local);
        if (status != RIBOS_VM_STATUS_OK) {
            return status;
        }
    }
    if (local.state != RIBOS_VM_INTERPRETER_RETURNED &&
        local.state != RIBOS_VM_INTERPRETER_FAULTED) {
        return RIBOS_VM_STATUS_INTERNAL_ERROR;
    }
    *snapshot = local;
    return RIBOS_VM_STATUS_OK;
}
