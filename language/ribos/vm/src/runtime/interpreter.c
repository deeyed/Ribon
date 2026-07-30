#include "ribos/vm/interpreter.h"

#include "internal.h"
#include "storage_internal.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

typedef struct RibosVmInterpreterTables {
    const RibosArtifactView *view;
    const RibosArtifactSectionView *types;
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
} RibosVmInterpreterSlot;

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
    if (tables->types == NULL || tables->constants == NULL ||
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
    };
    return 1;
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
    uint8_t left_bytes[8];
    uint8_t right_bytes[8];
    uint32_t comparison = 0;

    if (left->type_id != right->type_id ||
        result->type_kind != RIBOS_BC_TYPE_BOOL ||
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
    }
    if (operation == RIBOS_BC_CHECK_EQUAL ||
        operation == RIBOS_BC_CHECK_NOT_EQUAL) {
        comparison = memcmp(
            left_bytes,
            right_bytes,
            left->byte_size) == 0;
        if (operation == RIBOS_BC_CHECK_NOT_EQUAL) {
            comparison = !comparison;
        }
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

static int
ribos_vm_interpreter_copy_scalar_slot(
    const RibosVmInterpreterSlot *slot)
{
    return slot != NULL &&
        slot->storage_kind == RIBOS_BC_STORAGE_SCALAR &&
        slot->byte_size <= 8;
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
        ribos_vm_interpreter_u32(function + 8) != result->type_id ||
        !ribos_vm_interpreter_copy_scalar_slot(result)) {
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
        uint8_t bytes[8] = {0};

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
            !ribos_vm_interpreter_copy_scalar_slot(&operand) ||
            !ribos_vm_interpreter_copy_scalar_slot(&parameter) ||
            operand.byte_size != parameter.byte_size ||
            !ribos_vm_interpreter_slot_read(
                prepared_program,
                storage,
                arena_size,
                control,
                &operand,
                bytes) ||
            !ribos_vm_interpreter_slot_write(
                prepared_program,
                storage,
                arena_size,
                &callee_value_control,
                &parameter,
                bytes)) {
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
    uint8_t bytes[8] = {0};
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
        !ribos_vm_interpreter_copy_scalar_slot(operand) ||
        !ribos_vm_interpreter_copy_scalar_slot(&caller_result) ||
        !ribos_vm_interpreter_slot_read(
            prepared_program,
            storage,
            arena_size,
            control,
            operand,
            bytes)) {
        *fault_code = status == RIBOS_VM_STATUS_OK ?
            RIBOS_VM_FAULT_INVALID_VALUE :
            RIBOS_VM_FAULT_INTERNAL;
        *fault_detail = operand->id;
        return RIBOS_VM_STATUS_OK;
    }
    caller_value_control = *control;
    caller_value_control.function_id = target.function_id;
    caller_value_control.frame_base = target.frame_base;
    if (!ribos_vm_interpreter_slot_write(
            prepared_program,
            storage,
            arena_size,
            &caller_value_control,
            &caller_result,
            bytes)) {
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

            if (!ribos_vm_interpreter_copy_scalar_slot(&result) ||
                ribos_vm_storage_slot_state_v1(
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
        uint8_t bytes[8];

        if (!ribos_vm_interpreter_slot(
                &tables,
                operand_id,
                &operand) ||
            operand.type_id != result.type_id ||
            !ribos_vm_interpreter_slot_read(
                prepared_program,
                storage,
                arena_size,
                &control,
                &operand,
                bytes) ||
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
