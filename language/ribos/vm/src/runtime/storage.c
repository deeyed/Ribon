#include "ribos/vm/storage.h"

#include "ribos/base/checked.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#define RIBOS_VM_STORAGE_MAGIC UINT64_C(0x524253564d535431)
#define RIBOS_VM_CONTROL_BYTES UINT64_C(512)
#define RIBOS_VM_FRAME_RECORD_BYTES UINT32_C(32)
#define RIBOS_VM_SLOT_STATE_BYTES UINT32_C(8)
#define RIBOS_VM_LOOP_COUNTER_BYTES UINT32_C(8)
#define RIBOS_VM_HELPER_COUNTER_BYTES UINT32_C(16)
#define RIBOS_VM_HANDLE_RECORD_BYTES UINT32_C(32)
#define RIBOS_VM_OUTCOME_RECORD_BYTES UINT32_C(256)
#define RIBOS_VM_FAULT_RECORD_BYTES UINT32_C(160)
#define RIBOS_VM_TRACE_RECORD_BYTES UINT32_C(32)
#define RIBOS_VM_STORAGE_POISON_INITIAL UINT8_C(0xa5)
#define RIBOS_VM_STORAGE_POISON_MOVED UINT8_C(0xdd)

#define RIBOS_VM_CONTROL_MAGIC_OFFSET 0u
#define RIBOS_VM_CONTROL_MAJOR_OFFSET 8u
#define RIBOS_VM_CONTROL_MINOR_OFFSET 10u
#define RIBOS_VM_CONTROL_FLAGS_OFFSET 12u
#define RIBOS_VM_CONTROL_REQUIRED_OFFSET 16u
#define RIBOS_VM_CONTROL_LIMIT_OFFSET 24u
#define RIBOS_VM_CONTROL_REGION_COUNT_OFFSET 32u
#define RIBOS_VM_CONTROL_ENTRY_FUNCTION_OFFSET 36u
#define RIBOS_VM_CONTROL_BINDING_DIGEST_OFFSET 40u
#define RIBOS_VM_CONTROL_REGION_TABLE_OFFSET 96u
#define RIBOS_VM_CONTROL_REGION_ROW_BYTES 24u
#define RIBOS_VM_CONTROL_REMAINING_INSTRUCTIONS_OFFSET 384u
#define RIBOS_VM_CONTROL_REMAINING_HELPERS_OFFSET 392u
#define RIBOS_VM_CONTROL_REMAINING_OPERATIONS_OFFSET 400u
#define RIBOS_VM_CONTROL_REMAINING_POLLS_OFFSET 408u
#define RIBOS_VM_CONTROL_STACK_CURSOR_OFFSET 416u
#define RIBOS_VM_CONTROL_FRAME_DEPTH_OFFSET 424u

typedef struct RibosVmSlotLocation {
    uint32_t type_id;
    uint16_t type_kind;
    uint16_t bit_width;
    uint32_t slot_id;
    uint32_t first_slot;
    uint32_t slot_count;
    uint32_t frame_size;
    uint32_t byte_size;
    uint32_t alignment;
    uint64_t frame_base;
    uint64_t value_offset;
    uint64_t state_offset;
} RibosVmSlotLocation;

static uint16_t
ribos_vm_value_read_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] |
        ((uint16_t)bytes[1] << 8);
}

static uint32_t
ribos_vm_value_read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

static uint64_t
ribos_vm_value_read_u64(const uint8_t *bytes)
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
ribos_vm_value_write_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void
ribos_vm_value_write_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void
ribos_vm_value_write_u64(uint8_t *bytes, uint64_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
    bytes[4] = (uint8_t)(value >> 32);
    bytes[5] = (uint8_t)(value >> 40);
    bytes[6] = (uint8_t)(value >> 48);
    bytes[7] = (uint8_t)(value >> 56);
}

static int
ribos_vm_storage_pointer_is_aligned(const void *pointer)
{
    return pointer != NULL &&
        ((uintptr_t)pointer & (RIBOS_VM_STORAGE_ALIGNMENT_V1 - 1u)) == 0;
}

static const uint8_t *
ribos_vm_storage_row(
    const RibosArtifactSectionView *section,
    uint32_t index)
{
    size_t offset;

    if (section == NULL || index >= section->count ||
        !ribos_checked_size_multiply(
            (size_t)index,
            (size_t)section->row_size,
            &offset) ||
        !ribos_checked_size_range(
            section->byte_length,
            offset,
            section->row_size)) {
        return NULL;
    }
    return section->bytes + offset;
}

static int
ribos_vm_storage_plan_reserved_is_zero(const RibosVmStoragePlan *plan)
{
    uint32_t index;

    if (plan->reserved0 != 0) {
        return 0;
    }
    for (index = 0; index < 4; ++index) {
        if (plan->reserved[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static int
ribos_vm_storage_plan_header_is_valid(const RibosVmStoragePlan *plan)
{
    return plan != NULL &&
        plan->size == (uint32_t)sizeof(*plan) &&
        plan->storage_major == RIBOS_VM_STORAGE_V1_MAJOR &&
        plan->storage_minor == RIBOS_VM_STORAGE_V1_MINOR &&
        plan->flags == 0 &&
        plan->region_count == RIBOS_VM_STORAGE_REGION_COUNT_V1 &&
        plan->required_bytes != 0 &&
        plan->required_bytes <= plan->effective_arena_limit &&
        plan->effective_arena_limit <=
            RIBOS_VM_RUNTIME_MAX_ARENA_BYTES_V1 &&
        ribos_vm_storage_plan_reserved_is_zero(plan);
}

static int
ribos_vm_storage_plans_equal(
    const RibosVmStoragePlan *left,
    const RibosVmStoragePlan *right)
{
    uint32_t index;

    if (!ribos_vm_storage_plan_header_is_valid(left) ||
        !ribos_vm_storage_plan_header_is_valid(right) ||
        left->required_bytes != right->required_bytes ||
        left->effective_arena_limit != right->effective_arena_limit ||
        left->maximum_value_bytes != right->maximum_value_bytes ||
        left->entry_function != right->entry_function ||
        left->function_count != right->function_count ||
        left->slot_count != right->slot_count ||
        left->loop_count != right->loop_count ||
        left->helper_count != right->helper_count ||
        left->call_depth != right->call_depth ||
        left->handle_count != right->handle_count ||
        left->trace_count != right->trace_count) {
        return 0;
    }
    for (index = 0;
         index < RIBOS_VM_STORAGE_REGION_COUNT_V1;
         ++index) {
        const RibosVmStorageRegion *left_region =
            &left->regions[index];
        const RibosVmStorageRegion *right_region =
            &right->regions[index];

        if (left_region->offset != right_region->offset ||
            left_region->byte_size != right_region->byte_size ||
            left_region->count != right_region->count ||
            left_region->stride != right_region->stride) {
            return 0;
        }
    }
    return 1;
}

static int
ribos_vm_storage_place_region(
    RibosVmStoragePlan *plan,
    uint32_t region_kind,
    uint64_t byte_size,
    uint32_t count,
    uint32_t stride,
    uint64_t *cursor)
{
    uint64_t aligned;
    uint64_t end;

    if (plan == NULL || cursor == NULL ||
        region_kind >= RIBOS_VM_STORAGE_REGION_COUNT_V1 ||
        !ribos_checked_u64_align(
            *cursor,
            RIBOS_VM_STORAGE_ALIGNMENT_V1,
            &aligned) ||
        !ribos_checked_u64_add(aligned, byte_size, &end)) {
        return 0;
    }
    plan->regions[region_kind] = (RibosVmStorageRegion){
        .offset = aligned,
        .byte_size = byte_size,
        .count = count,
        .stride = stride,
    };
    *cursor = end;
    return 1;
}

static int
ribos_vm_storage_counted_bytes(
    uint32_t count,
    uint32_t stride,
    uint64_t *byte_size)
{
    return ribos_checked_u64_multiply(
        (uint64_t)count,
        (uint64_t)stride,
        byte_size);
}

static RibosVmStatus
ribos_vm_storage_build_plan(
    const RibosPreparedProgram *prepared_program,
    RibosVmStoragePlan *plan)
{
    const RibosArtifactView *view;
    const RibosVerifierReport *report;
    const RibosVmLimits *limits;
    const RibosArtifactSectionView *types;
    const RibosArtifactSectionView *functions;
    const RibosArtifactSectionView *slots;
    const RibosArtifactSectionView *loops;
    const RibosArtifactSectionView *imports;
    uint64_t effective_limit;
    uint64_t maximum_value_bytes = 0;
    uint64_t region_bytes;
    uint64_t cursor = 0;
    uint32_t index;

    if (plan == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    memset(plan, 0, sizeof(*plan));
    if (ribos_prepared_program_validate_v1(prepared_program) !=
        RIBOS_VM_STATUS_OK) {
        return RIBOS_VM_STATUS_NOT_PREPARED;
    }
    view = ribos_prepared_program_artifact_view_v1(prepared_program);
    report = ribos_prepared_program_report_v1(prepared_program);
    limits = ribos_prepared_program_limits_v1(prepared_program);
    if (view == NULL || report == NULL || limits == NULL ||
        report->status != RIBOS_VERIFIER_OK) {
        return RIBOS_VM_STATUS_NOT_PREPARED;
    }
    types = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_TYPES);
    functions = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_FUNCTIONS);
    slots = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_SLOTS);
    loops = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_LOOPS);
    imports = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_HELPER_IMPORTS);
    if (types == NULL || functions == NULL || slots == NULL ||
        loops == NULL || imports == NULL || types->count == 0 ||
        functions->count == 0) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    for (index = 0; index < slots->count; ++index) {
        const uint8_t *row = ribos_vm_storage_row(slots, index);
        const uint8_t *type;
        uint32_t type_id;
        uint32_t byte_size;

        if (row == NULL || ribos_vm_value_read_u32(row) != index) {
            return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
        }
        type_id = ribos_vm_value_read_u32(row + 8);
        type = ribos_vm_storage_row(types, type_id);
        byte_size = ribos_vm_value_read_u32(row + 16);
        if (type == NULL ||
            byte_size != ribos_vm_value_read_u32(type + 40)) {
            return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
        }
        if (byte_size > RIBOS_VERIFIER_MAX_VALUE_BYTES) {
            return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
        }
        if (byte_size > maximum_value_bytes) {
            maximum_value_bytes = byte_size;
        }
    }

    effective_limit = limits->maximum_arena_bytes;
    if (effective_limit > RIBOS_VM_RUNTIME_MAX_ARENA_BYTES_V1) {
        effective_limit = RIBOS_VM_RUNTIME_MAX_ARENA_BYTES_V1;
    }
    *plan = (RibosVmStoragePlan){
        .size = sizeof(*plan),
        .storage_major = RIBOS_VM_STORAGE_V1_MAJOR,
        .storage_minor = RIBOS_VM_STORAGE_V1_MINOR,
        .region_count = RIBOS_VM_STORAGE_REGION_COUNT_V1,
        .effective_arena_limit = effective_limit,
        .maximum_value_bytes = maximum_value_bytes,
        .entry_function = view->entry_function,
        .function_count = functions->count,
        .slot_count = view->slot_count,
        .loop_count = loops->count,
        .helper_count = imports->count,
        .call_depth = report->recomputed_call_depth,
        .handle_count = limits->maximum_handles,
        .trace_count = limits->maximum_trace_records,
    };

    if (!ribos_vm_storage_place_region(
            plan,
            RIBOS_VM_STORAGE_REGION_CONTROL,
            RIBOS_VM_CONTROL_BYTES,
            1,
            (uint32_t)RIBOS_VM_CONTROL_BYTES,
            &cursor) ||
        !ribos_vm_storage_counted_bytes(
            report->recomputed_call_depth,
            RIBOS_VM_FRAME_RECORD_BYTES,
            &region_bytes) ||
        !ribos_vm_storage_place_region(
            plan,
            RIBOS_VM_STORAGE_REGION_FRAMES,
            region_bytes,
            report->recomputed_call_depth,
            RIBOS_VM_FRAME_RECORD_BYTES,
            &cursor) ||
        !ribos_vm_storage_place_region(
            plan,
            RIBOS_VM_STORAGE_REGION_FRAME_VALUES,
            report->recomputed_stack_bytes,
            1,
            report->recomputed_stack_bytes <= UINT32_MAX ?
                (uint32_t)report->recomputed_stack_bytes : 0,
            &cursor) ||
        !ribos_vm_storage_counted_bytes(
            view->slot_count,
            RIBOS_VM_SLOT_STATE_BYTES,
            &region_bytes) ||
        !ribos_vm_storage_place_region(
            plan,
            RIBOS_VM_STORAGE_REGION_SLOT_STATES,
            region_bytes,
            view->slot_count,
            RIBOS_VM_SLOT_STATE_BYTES,
            &cursor) ||
        !ribos_vm_storage_counted_bytes(
            loops->count,
            RIBOS_VM_LOOP_COUNTER_BYTES,
            &region_bytes) ||
        !ribos_vm_storage_place_region(
            plan,
            RIBOS_VM_STORAGE_REGION_LOOP_COUNTERS,
            region_bytes,
            loops->count,
            RIBOS_VM_LOOP_COUNTER_BYTES,
            &cursor) ||
        !ribos_vm_storage_counted_bytes(
            imports->count,
            RIBOS_VM_HELPER_COUNTER_BYTES,
            &region_bytes) ||
        !ribos_vm_storage_place_region(
            plan,
            RIBOS_VM_STORAGE_REGION_HELPER_COUNTERS,
            region_bytes,
            imports->count,
            RIBOS_VM_HELPER_COUNTER_BYTES,
            &cursor) ||
        !ribos_vm_storage_counted_bytes(
            limits->maximum_handles,
            RIBOS_VM_HANDLE_RECORD_BYTES,
            &region_bytes) ||
        !ribos_vm_storage_place_region(
            plan,
            RIBOS_VM_STORAGE_REGION_HANDLES,
            region_bytes,
            limits->maximum_handles,
            RIBOS_VM_HANDLE_RECORD_BYTES,
            &cursor) ||
        !ribos_vm_storage_place_region(
            plan,
            RIBOS_VM_STORAGE_REGION_AGGREGATE_SCRATCH,
            maximum_value_bytes,
            maximum_value_bytes != 0 ? 1u : 0u,
            (uint32_t)maximum_value_bytes,
            &cursor) ||
        !ribos_vm_storage_place_region(
            plan,
            RIBOS_VM_STORAGE_REGION_OUTCOME,
            RIBOS_VM_OUTCOME_RECORD_BYTES,
            1,
            RIBOS_VM_OUTCOME_RECORD_BYTES,
            &cursor) ||
        !ribos_vm_storage_place_region(
            plan,
            RIBOS_VM_STORAGE_REGION_OUTPUT,
            limits->maximum_output_bytes,
            limits->maximum_output_bytes != 0 ? 1u : 0u,
            limits->maximum_output_bytes <= UINT32_MAX ?
                (uint32_t)limits->maximum_output_bytes : 0,
            &cursor) ||
        !ribos_vm_storage_place_region(
            plan,
            RIBOS_VM_STORAGE_REGION_FAULT,
            RIBOS_VM_FAULT_RECORD_BYTES,
            1,
            RIBOS_VM_FAULT_RECORD_BYTES,
            &cursor) ||
        !ribos_vm_storage_counted_bytes(
            limits->maximum_trace_records,
            RIBOS_VM_TRACE_RECORD_BYTES,
            &region_bytes) ||
        !ribos_vm_storage_place_region(
            plan,
            RIBOS_VM_STORAGE_REGION_TRACE,
            region_bytes,
            limits->maximum_trace_records,
            RIBOS_VM_TRACE_RECORD_BYTES,
            &cursor) ||
        !ribos_checked_u64_align(
            cursor,
            RIBOS_VM_STORAGE_ALIGNMENT_V1,
            &plan->required_bytes)) {
        memset(plan, 0, sizeof(*plan));
        return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
    }
    if (plan->required_bytes > effective_limit) {
        memset(plan, 0, sizeof(*plan));
        return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
    }
    return RIBOS_VM_STATUS_OK;
}

size_t
ribos_vm_runtime_alignment_v1(void)
{
    return RIBOS_VM_STORAGE_ALIGNMENT_V1;
}

RibosVmStatus
ribos_vm_runtime_size_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStoragePlan *plan,
    size_t *required_size)
{
    RibosVmStatus status;

    if (plan == NULL || required_size == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *required_size = 0;
    status = ribos_vm_storage_build_plan(prepared_program, plan);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (!ribos_checked_u64_to_size(
            plan->required_bytes,
            required_size)) {
        memset(plan, 0, sizeof(*plan));
        return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
    }
    return RIBOS_VM_STATUS_OK;
}

static uint8_t *
ribos_vm_storage_mutable_bytes(RibosVmStorage *storage)
{
    return (uint8_t *)(void *)storage;
}

static const uint8_t *
ribos_vm_storage_const_bytes(const RibosVmStorage *storage)
{
    return (const uint8_t *)(const void *)storage;
}

static void
ribos_vm_storage_encode_plan(
    uint8_t *bytes,
    const RibosVmStoragePlan *plan,
    const uint8_t binding_digest[RIBOS_VM_DIGEST_BYTES],
    const RibosVmLimits *limits,
    const RibosVerifierReport *report,
    uint32_t flags)
{
    uint32_t index;

    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_CONTROL_MAGIC_OFFSET,
        RIBOS_VM_STORAGE_MAGIC);
    ribos_vm_value_write_u16(
        bytes + RIBOS_VM_CONTROL_MAJOR_OFFSET,
        RIBOS_VM_STORAGE_V1_MAJOR);
    ribos_vm_value_write_u16(
        bytes + RIBOS_VM_CONTROL_MINOR_OFFSET,
        RIBOS_VM_STORAGE_V1_MINOR);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_CONTROL_FLAGS_OFFSET,
        flags);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_CONTROL_REQUIRED_OFFSET,
        plan->required_bytes);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_CONTROL_LIMIT_OFFSET,
        plan->effective_arena_limit);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_CONTROL_REGION_COUNT_OFFSET,
        plan->region_count);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_CONTROL_ENTRY_FUNCTION_OFFSET,
        plan->entry_function);
    memcpy(
        bytes + RIBOS_VM_CONTROL_BINDING_DIGEST_OFFSET,
        binding_digest,
        RIBOS_VM_DIGEST_BYTES);
    for (index = 0;
         index < RIBOS_VM_STORAGE_REGION_COUNT_V1;
         ++index) {
        uint8_t *row = bytes +
            RIBOS_VM_CONTROL_REGION_TABLE_OFFSET +
            (size_t)index * RIBOS_VM_CONTROL_REGION_ROW_BYTES;
        const RibosVmStorageRegion *region =
            &plan->regions[index];

        ribos_vm_value_write_u64(row, region->offset);
        ribos_vm_value_write_u64(row + 8, region->byte_size);
        ribos_vm_value_write_u32(row + 16, region->count);
        ribos_vm_value_write_u32(row + 20, region->stride);
    }
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_CONTROL_REMAINING_INSTRUCTIONS_OFFSET,
        report->recomputed_instruction_upper_bound);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_CONTROL_REMAINING_HELPERS_OFFSET,
        report->recomputed_helper_upper_bound);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_CONTROL_REMAINING_OPERATIONS_OFFSET,
        limits->maximum_operations);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_CONTROL_REMAINING_POLLS_OFFSET,
        limits->maximum_polls);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_CONTROL_STACK_CURSOR_OFFSET,
        0);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_CONTROL_FRAME_DEPTH_OFFSET,
        0);
}

static int
ribos_vm_storage_control_matches_plan(
    const uint8_t *bytes,
    const RibosVmStoragePlan *plan,
    const uint8_t binding_digest[RIBOS_VM_DIGEST_BYTES])
{
    uint32_t index;
    uint32_t flags;

    if (ribos_vm_value_read_u64(
            bytes + RIBOS_VM_CONTROL_MAGIC_OFFSET) !=
            RIBOS_VM_STORAGE_MAGIC ||
        ribos_vm_value_read_u16(
            bytes + RIBOS_VM_CONTROL_MAJOR_OFFSET) !=
            RIBOS_VM_STORAGE_V1_MAJOR ||
        ribos_vm_value_read_u16(
            bytes + RIBOS_VM_CONTROL_MINOR_OFFSET) !=
            RIBOS_VM_STORAGE_V1_MINOR) {
        return 0;
    }
    flags = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_CONTROL_FLAGS_OFFSET);
    if ((flags & ~RIBOS_VM_STORAGE_INITIALIZE_DIAGNOSTIC_POISON) != 0 ||
        ribos_vm_value_read_u64(
            bytes + RIBOS_VM_CONTROL_REQUIRED_OFFSET) !=
            plan->required_bytes ||
        ribos_vm_value_read_u64(
            bytes + RIBOS_VM_CONTROL_LIMIT_OFFSET) !=
            plan->effective_arena_limit ||
        ribos_vm_value_read_u32(
            bytes + RIBOS_VM_CONTROL_REGION_COUNT_OFFSET) !=
            plan->region_count ||
        ribos_vm_value_read_u32(
            bytes + RIBOS_VM_CONTROL_ENTRY_FUNCTION_OFFSET) !=
            plan->entry_function ||
        memcmp(
            bytes + RIBOS_VM_CONTROL_BINDING_DIGEST_OFFSET,
            binding_digest,
            RIBOS_VM_DIGEST_BYTES) != 0) {
        return 0;
    }
    for (index = 0;
         index < RIBOS_VM_STORAGE_REGION_COUNT_V1;
         ++index) {
        const uint8_t *row = bytes +
            RIBOS_VM_CONTROL_REGION_TABLE_OFFSET +
            (size_t)index * RIBOS_VM_CONTROL_REGION_ROW_BYTES;
        const RibosVmStorageRegion *region =
            &plan->regions[index];

        if (ribos_vm_value_read_u64(row) != region->offset ||
            ribos_vm_value_read_u64(row + 8) !=
                region->byte_size ||
            ribos_vm_value_read_u32(row + 16) != region->count ||
            ribos_vm_value_read_u32(row + 20) != region->stride) {
            return 0;
        }
    }
    return 1;
}

static RibosVmStatus
ribos_vm_storage_initialize_counters(
    const RibosArtifactView *view,
    const RibosVmStoragePlan *plan,
    uint8_t *arena)
{
    const RibosArtifactSectionView *loops =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_LOOPS);
    const RibosArtifactSectionView *imports =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_HELPER_IMPORTS);
    const RibosArtifactSectionView *bounds =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_HELPER_BOUNDS);
    uint8_t *loop_bytes = arena +
        (size_t)plan->regions[
            RIBOS_VM_STORAGE_REGION_LOOP_COUNTERS].offset;
    uint8_t *helper_bytes = arena +
        (size_t)plan->regions[
            RIBOS_VM_STORAGE_REGION_HELPER_COUNTERS].offset;
    uint32_t index;

    if (loops == NULL || imports == NULL || bounds == NULL) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    for (index = 0; index < loops->count; ++index) {
        const uint8_t *row = ribos_vm_storage_row(loops, index);

        if (row == NULL) {
            return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
        }
        ribos_vm_value_write_u64(
            loop_bytes + (size_t)index *
                RIBOS_VM_LOOP_COUNTER_BYTES,
            ribos_vm_value_read_u32(row + 24));
    }
    for (index = 0; index < imports->count; ++index) {
        const uint8_t *import_row =
            ribos_vm_storage_row(imports, index);
        uint8_t *counter = helper_bytes +
            (size_t)index * RIBOS_VM_HELPER_COUNTER_BYTES;
        uint32_t stable_id;
        uint64_t upper_bound = 0;
        uint32_t bound_index;

        if (import_row == NULL) {
            return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
        }
        stable_id = ribos_vm_value_read_u32(import_row);
        for (bound_index = 0;
             bound_index < bounds->count;
             ++bound_index) {
            const uint8_t *bound_row =
                ribos_vm_storage_row(bounds, bound_index);

            if (bound_row == NULL) {
                return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
            }
            if (ribos_vm_value_read_u32(bound_row) ==
                    plan->entry_function &&
                ribos_vm_value_read_u32(bound_row + 4) ==
                    stable_id) {
                upper_bound =
                    ribos_vm_value_read_u64(bound_row + 8);
                break;
            }
        }
        ribos_vm_value_write_u32(counter, stable_id);
        ribos_vm_value_write_u32(counter + 4, 0);
        ribos_vm_value_write_u64(counter + 8, upper_bound);
    }
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_initialize_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStoragePlan *plan,
    void *arena,
    size_t arena_size,
    uint32_t flags,
    RibosVmStorage **storage)
{
    const RibosArtifactView *view;
    const RibosVerifierReport *report;
    const RibosVmLimits *limits;
    const uint8_t *binding_digest;
    RibosVmStoragePlan expected;
    RibosVmStatus status;
    size_t required_size;
    uint8_t *bytes = arena;

    if (storage == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *storage = NULL;
    if (plan == NULL || arena == NULL ||
        (flags & ~RIBOS_VM_STORAGE_INITIALIZE_DIAGNOSTIC_POISON) != 0 ||
        !ribos_vm_storage_pointer_is_aligned(arena)) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    status = ribos_vm_runtime_size_v1(
        prepared_program,
        &expected,
        &required_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (!ribos_vm_storage_plans_equal(plan, &expected)) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    if (arena_size < required_size) {
        return RIBOS_VM_STATUS_ARENA_TOO_SMALL;
    }
    if ((uintptr_t)arena >
        UINTPTR_MAX - (uintptr_t)required_size) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    view = ribos_prepared_program_artifact_view_v1(prepared_program);
    report = ribos_prepared_program_report_v1(prepared_program);
    limits = ribos_prepared_program_limits_v1(prepared_program);
    binding_digest =
        ribos_prepared_program_binding_digest_v1(prepared_program);
    if (view == NULL || report == NULL || limits == NULL ||
        binding_digest == NULL) {
        return RIBOS_VM_STATUS_NOT_PREPARED;
    }
    memset(bytes, 0, required_size);
    if ((flags &
            RIBOS_VM_STORAGE_INITIALIZE_DIAGNOSTIC_POISON) != 0) {
        const uint32_t poison_regions[] = {
            RIBOS_VM_STORAGE_REGION_FRAME_VALUES,
            RIBOS_VM_STORAGE_REGION_AGGREGATE_SCRATCH,
            RIBOS_VM_STORAGE_REGION_OUTPUT,
        };
        uint32_t index;

        for (index = 0;
             index < sizeof(poison_regions) /
                 sizeof(poison_regions[0]);
             ++index) {
            const RibosVmStorageRegion *region =
                &plan->regions[poison_regions[index]];

            if (region->byte_size != 0) {
                memset(
                    bytes + (size_t)region->offset,
                    RIBOS_VM_STORAGE_POISON_INITIAL,
                    (size_t)region->byte_size);
            }
        }
    }
    ribos_vm_storage_encode_plan(
        bytes,
        plan,
        binding_digest,
        limits,
        report,
        flags);
    status = ribos_vm_storage_initialize_counters(
        view,
        plan,
        bytes);
    if (status != RIBOS_VM_STATUS_OK) {
        memset(bytes, 0, required_size);
        return status;
    }
    *storage = (RibosVmStorage *)(void *)bytes;
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_validate_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size)
{
    RibosVmStoragePlan plan;
    RibosVmStatus status;
    const uint8_t *binding_digest;
    const uint8_t *bytes;
    size_t required_size;

    if (storage == NULL ||
        !ribos_vm_storage_pointer_is_aligned(storage)) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    status = ribos_vm_runtime_size_v1(
        prepared_program,
        &plan,
        &required_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (arena_size < required_size ||
        (uintptr_t)storage >
            UINTPTR_MAX - (uintptr_t)required_size) {
        return RIBOS_VM_STATUS_ARENA_TOO_SMALL;
    }
    binding_digest =
        ribos_prepared_program_binding_digest_v1(prepared_program);
    bytes = ribos_vm_storage_const_bytes(storage);
    if (binding_digest == NULL ||
        !ribos_vm_storage_control_matches_plan(
            bytes,
            &plan,
            binding_digest)) {
        return RIBOS_VM_STATUS_DIGEST_MISMATCH;
    }
    return RIBOS_VM_STATUS_OK;
}

static uint32_t
ribos_vm_storage_flags(const RibosVmStorage *storage)
{
    return ribos_vm_value_read_u32(
        ribos_vm_storage_const_bytes(storage) +
        RIBOS_VM_CONTROL_FLAGS_OFFSET);
}

static RibosVmStatus
ribos_vm_storage_locate_slot(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    RibosVmSlotLocation *location)
{
    const RibosArtifactView *view;
    const RibosArtifactSectionView *functions;
    const RibosArtifactSectionView *slots;
    const RibosArtifactSectionView *types;
    const uint8_t *function;
    const uint8_t *slot;
    const uint8_t *type;
    uint32_t first_slot;
    uint32_t slot_count;
    uint32_t frame_size;
    uint32_t slot_offset;
    uint32_t byte_size;
    uint32_t alignment;
    uint32_t type_id;
    uint64_t frame_end;
    uint64_t value_offset;
    uint64_t value_end;
    uint64_t state_offset;
    uint64_t state_end;
    RibosVmStoragePlan plan;
    size_t required_size;
    RibosVmStatus status;

    if (location == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    memset(location, 0, sizeof(*location));
    status = ribos_vm_storage_validate_v1(
        prepared_program,
        storage,
        arena_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_runtime_size_v1(
        prepared_program,
        &plan,
        &required_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    (void)required_size;
    view = ribos_prepared_program_artifact_view_v1(prepared_program);
    functions = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_FUNCTIONS);
    slots = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_SLOTS);
    types = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_TYPES);
    function = ribos_vm_storage_row(functions, function_id);
    slot = ribos_vm_storage_row(slots, slot_id);
    if (function == NULL || slot == NULL ||
        ribos_vm_value_read_u32(function) != function_id ||
        ribos_vm_value_read_u32(slot) != slot_id ||
        ribos_vm_value_read_u32(slot + 4) != function_id) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    first_slot = ribos_vm_value_read_u32(function + 24);
    slot_count = ribos_vm_value_read_u32(function + 28);
    frame_size = ribos_vm_value_read_u32(function + 88);
    if (slot_id < first_slot ||
        slot_id - first_slot >= slot_count ||
        (frame_base & (RIBOS_VM_STORAGE_ALIGNMENT_V1 - 1u)) != 0 ||
        !ribos_checked_u64_add(
            frame_base,
            frame_size,
            &frame_end) ||
        frame_end > plan.regions[
            RIBOS_VM_STORAGE_REGION_FRAME_VALUES].byte_size) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    type_id = ribos_vm_value_read_u32(slot + 8);
    slot_offset = ribos_vm_value_read_u32(slot + 12);
    byte_size = ribos_vm_value_read_u32(slot + 16);
    alignment = ribos_vm_value_read_u32(slot + 20);
    type = ribos_vm_storage_row(types, type_id);
    if (type == NULL || alignment == 0 ||
        alignment > RIBOS_VM_STORAGE_ALIGNMENT_V1 ||
        (alignment & (alignment - 1u)) != 0 ||
        (slot_offset & (alignment - 1u)) != 0 ||
        byte_size != ribos_vm_value_read_u32(type + 40) ||
        !ribos_checked_u64_add(
            frame_base,
            slot_offset,
            &value_offset) ||
        !ribos_checked_u64_add(
            value_offset,
            byte_size,
            &value_end) ||
        value_end > frame_end ||
        !ribos_checked_u64_multiply(
            slot_id,
            RIBOS_VM_SLOT_STATE_BYTES,
            &state_offset) ||
        !ribos_checked_u64_add(
            state_offset,
            RIBOS_VM_SLOT_STATE_BYTES,
            &state_end) ||
        state_end > plan.regions[
            RIBOS_VM_STORAGE_REGION_SLOT_STATES].byte_size) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    *location = (RibosVmSlotLocation){
        .type_id = type_id,
        .type_kind = ribos_vm_value_read_u16(type + 4),
        .bit_width = ribos_vm_value_read_u16(type + 6),
        .slot_id = slot_id,
        .first_slot = first_slot,
        .slot_count = slot_count,
        .frame_size = frame_size,
        .byte_size = byte_size,
        .alignment = alignment,
        .frame_base = frame_base,
        .value_offset =
            plan.regions[
                RIBOS_VM_STORAGE_REGION_FRAME_VALUES].offset +
            value_offset,
        .state_offset =
            plan.regions[
                RIBOS_VM_STORAGE_REGION_SLOT_STATES].offset +
            state_offset,
    };
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_reset_frame_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base)
{
    const RibosArtifactView *view;
    const RibosArtifactSectionView *functions;
    const uint8_t *function;
    RibosVmStoragePlan plan;
    RibosVmStatus status;
    uint8_t *bytes;
    uint64_t frame_end;
    uint64_t state_offset;
    uint64_t state_size;
    uint64_t state_end;
    size_t required_size;
    uint32_t first_slot;
    uint32_t slot_count;
    uint32_t frame_size;
    uint8_t fill = 0;

    status = ribos_vm_storage_validate_v1(
        prepared_program,
        storage,
        arena_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_runtime_size_v1(
        prepared_program,
        &plan,
        &required_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    (void)required_size;
    view = ribos_prepared_program_artifact_view_v1(prepared_program);
    functions = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_FUNCTIONS);
    function = ribos_vm_storage_row(functions, function_id);
    if (function == NULL ||
        ribos_vm_value_read_u32(function) != function_id) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    first_slot = ribos_vm_value_read_u32(function + 24);
    slot_count = ribos_vm_value_read_u32(function + 28);
    frame_size = ribos_vm_value_read_u32(function + 88);
    if ((frame_base & (RIBOS_VM_STORAGE_ALIGNMENT_V1 - 1u)) != 0 ||
        !ribos_checked_u64_add(
            frame_base,
            frame_size,
            &frame_end) ||
        frame_end > plan.regions[
            RIBOS_VM_STORAGE_REGION_FRAME_VALUES].byte_size ||
        !ribos_checked_u64_multiply(
            first_slot,
            RIBOS_VM_SLOT_STATE_BYTES,
            &state_offset) ||
        !ribos_checked_u64_multiply(
            slot_count,
            RIBOS_VM_SLOT_STATE_BYTES,
            &state_size) ||
        !ribos_checked_u64_add(
            state_offset,
            state_size,
            &state_end) ||
        state_end > plan.regions[
            RIBOS_VM_STORAGE_REGION_SLOT_STATES].byte_size) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    bytes = ribos_vm_storage_mutable_bytes(storage);
    if ((ribos_vm_storage_flags(storage) &
            RIBOS_VM_STORAGE_INITIALIZE_DIAGNOSTIC_POISON) != 0) {
        fill = RIBOS_VM_STORAGE_POISON_INITIAL;
    }
    if (frame_size != 0) {
        memset(
            bytes +
                (size_t)plan.regions[
                    RIBOS_VM_STORAGE_REGION_FRAME_VALUES].offset +
                (size_t)frame_base,
            fill,
            frame_size);
    }
    if (state_size != 0) {
        memset(
            bytes +
                (size_t)plan.regions[
                    RIBOS_VM_STORAGE_REGION_SLOT_STATES].offset +
                (size_t)state_offset,
            0,
            (size_t)state_size);
    }
    return RIBOS_VM_STATUS_OK;
}

static uint8_t *
ribos_vm_storage_location_value(
    RibosVmStorage *storage,
    const RibosVmSlotLocation *location)
{
    return ribos_vm_storage_mutable_bytes(storage) +
        (size_t)location->value_offset;
}

static const uint8_t *
ribos_vm_storage_location_const_value(
    const RibosVmStorage *storage,
    const RibosVmSlotLocation *location)
{
    return ribos_vm_storage_const_bytes(storage) +
        (size_t)location->value_offset;
}

static uint8_t *
ribos_vm_storage_location_state(
    RibosVmStorage *storage,
    const RibosVmSlotLocation *location)
{
    return ribos_vm_storage_mutable_bytes(storage) +
        (size_t)location->state_offset;
}

static const uint8_t *
ribos_vm_storage_location_const_state(
    const RibosVmStorage *storage,
    const RibosVmSlotLocation *location)
{
    return ribos_vm_storage_const_bytes(storage) +
        (size_t)location->state_offset;
}

RibosVmStatus
ribos_vm_storage_slot_write_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    const uint8_t *bytes,
    size_t byte_size)
{
    RibosVmSlotLocation location;
    RibosVmStatus status = ribos_vm_storage_locate_slot(
        prepared_program,
        storage,
        arena_size,
        function_id,
        frame_base,
        slot_id,
        &location);
    uint8_t *state;

    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (byte_size != location.byte_size ||
        (byte_size != 0 && bytes == NULL)) {
        return RIBOS_VM_STATUS_INVALID_SIZE;
    }
    if (byte_size != 0) {
        memcpy(
            ribos_vm_storage_location_value(storage, &location),
            bytes,
            byte_size);
    }
    state = ribos_vm_storage_location_state(storage, &location);
    memset(state, 0, RIBOS_VM_SLOT_STATE_BYTES);
    state[0] = RIBOS_VM_SLOT_STORAGE_INITIALIZED;
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_slot_read_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    uint8_t *bytes,
    size_t byte_size)
{
    RibosVmSlotLocation location;
    RibosVmStatus status = ribos_vm_storage_locate_slot(
        prepared_program,
        storage,
        arena_size,
        function_id,
        frame_base,
        slot_id,
        &location);
    const uint8_t *state;

    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (byte_size != location.byte_size ||
        (byte_size != 0 && bytes == NULL)) {
        return RIBOS_VM_STATUS_INVALID_SIZE;
    }
    state = ribos_vm_storage_location_const_state(storage, &location);
    if (state[0] != RIBOS_VM_SLOT_STORAGE_INITIALIZED) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    if (byte_size != 0) {
        memcpy(
            bytes,
            ribos_vm_storage_location_const_value(
                storage,
                &location),
            byte_size);
    }
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_slot_mark_moved_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id)
{
    RibosVmSlotLocation location;
    RibosVmStatus status = ribos_vm_storage_locate_slot(
        prepared_program,
        storage,
        arena_size,
        function_id,
        frame_base,
        slot_id,
        &location);
    uint8_t *state;

    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    state = ribos_vm_storage_location_state(storage, &location);
    if (state[0] != RIBOS_VM_SLOT_STORAGE_INITIALIZED) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    state[0] = RIBOS_VM_SLOT_STORAGE_MOVED;
    if ((ribos_vm_storage_flags(storage) &
            RIBOS_VM_STORAGE_INITIALIZE_DIAGNOSTIC_POISON) != 0 &&
        location.byte_size != 0) {
        memset(
            ribos_vm_storage_location_value(storage, &location),
            RIBOS_VM_STORAGE_POISON_MOVED,
            location.byte_size);
    }
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_slot_state_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    uint32_t slot_id,
    uint32_t *state)
{
    RibosVmStoragePlan plan;
    RibosVmStatus status;
    const uint8_t *bytes;
    uint64_t state_offset;
    uint64_t state_end;
    size_t required_size;

    if (state == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *state = RIBOS_VM_SLOT_STORAGE_UNINITIALIZED;
    status = ribos_vm_storage_validate_v1(
        prepared_program,
        storage,
        arena_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_runtime_size_v1(
        prepared_program,
        &plan,
        &required_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    (void)required_size;
    if (slot_id >= plan.slot_count ||
        !ribos_checked_u64_multiply(
            slot_id,
            RIBOS_VM_SLOT_STATE_BYTES,
            &state_offset) ||
        !ribos_checked_u64_add(
            state_offset,
            RIBOS_VM_SLOT_STATE_BYTES,
            &state_end) ||
        state_end > plan.regions[
            RIBOS_VM_STORAGE_REGION_SLOT_STATES].byte_size) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    bytes = ribos_vm_storage_const_bytes(storage) +
        (size_t)plan.regions[
            RIBOS_VM_STORAGE_REGION_SLOT_STATES].offset +
        (size_t)state_offset;
    if (bytes[0] > RIBOS_VM_SLOT_STORAGE_MOVED ||
        bytes[1] != 0 || bytes[2] != 0 || bytes[3] != 0 ||
        bytes[4] != 0 || bytes[5] != 0 || bytes[6] != 0 ||
        bytes[7] != 0) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    *state = bytes[0];
    return RIBOS_VM_STATUS_OK;
}

static int
ribos_vm_storage_scalar_width_is_valid(uint16_t bit_width)
{
    return bit_width == 8 || bit_width == 16 ||
        bit_width == 32 || bit_width == 64;
}

static void
ribos_vm_storage_encode_scalar(
    uint8_t bytes[8],
    uint16_t bit_width,
    uint64_t value)
{
    uint32_t index;
    uint32_t byte_count = bit_width / 8u;

    for (index = 0; index < byte_count; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
}

static uint64_t
ribos_vm_storage_decode_scalar(
    const uint8_t bytes[8],
    uint16_t bit_width)
{
    uint64_t value = 0;
    uint32_t index;
    uint32_t byte_count = bit_width / 8u;

    for (index = 0; index < byte_count; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8u);
    }
    return value;
}

static RibosVmStatus
ribos_vm_storage_validate_scalar_slot(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    uint16_t expected_kind,
    uint16_t bit_width)
{
    RibosVmSlotLocation location;
    RibosVmStatus status;

    if (!ribos_vm_storage_scalar_width_is_valid(bit_width)) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    status = ribos_vm_storage_locate_slot(
        prepared_program,
        storage,
        arena_size,
        function_id,
        frame_base,
        slot_id,
        &location);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (location.type_kind != expected_kind ||
        location.bit_width != bit_width ||
        location.byte_size != bit_width / 8u) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_slot_store_unsigned_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    uint16_t bit_width,
    uint64_t value)
{
    uint8_t bytes[8] = {0};
    RibosVmStatus status =
        ribos_vm_storage_validate_scalar_slot(
            prepared_program,
            storage,
            arena_size,
            function_id,
            frame_base,
            slot_id,
            RIBOS_BC_TYPE_UNSIGNED,
            bit_width);

    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (bit_width < 64 &&
        value >= (UINT64_C(1) << bit_width)) {
        return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
    }
    ribos_vm_storage_encode_scalar(bytes, bit_width, value);
    return ribos_vm_storage_slot_write_v1(
        prepared_program,
        storage,
        arena_size,
        function_id,
        frame_base,
        slot_id,
        bytes,
        bit_width / 8u);
}

RibosVmStatus
ribos_vm_storage_slot_load_unsigned_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    uint16_t bit_width,
    uint64_t *value)
{
    uint8_t bytes[8] = {0};
    RibosVmStatus status;

    if (value == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *value = 0;
    status = ribos_vm_storage_validate_scalar_slot(
        prepared_program,
        storage,
        arena_size,
        function_id,
        frame_base,
        slot_id,
        RIBOS_BC_TYPE_UNSIGNED,
        bit_width);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_storage_slot_read_v1(
        prepared_program,
        storage,
        arena_size,
        function_id,
        frame_base,
        slot_id,
        bytes,
        bit_width / 8u);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    *value = ribos_vm_storage_decode_scalar(bytes, bit_width);
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_slot_store_signed_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    uint16_t bit_width,
    int64_t value)
{
    uint8_t bytes[8] = {0};
    uint64_t encoded;
    RibosVmStatus status =
        ribos_vm_storage_validate_scalar_slot(
            prepared_program,
            storage,
            arena_size,
            function_id,
            frame_base,
            slot_id,
            RIBOS_BC_TYPE_SIGNED,
            bit_width);

    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (bit_width < 64) {
        int64_t minimum =
            -(INT64_C(1) << (bit_width - 1u));
        int64_t maximum =
            (INT64_C(1) << (bit_width - 1u)) - 1;

        if (value < minimum || value > maximum) {
            return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
        }
    }
    encoded = (uint64_t)value;
    if (bit_width < 64) {
        encoded &= (UINT64_C(1) << bit_width) - 1u;
    }
    ribos_vm_storage_encode_scalar(bytes, bit_width, encoded);
    return ribos_vm_storage_slot_write_v1(
        prepared_program,
        storage,
        arena_size,
        function_id,
        frame_base,
        slot_id,
        bytes,
        bit_width / 8u);
}

RibosVmStatus
ribos_vm_storage_slot_load_signed_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    uint16_t bit_width,
    int64_t *value)
{
    uint8_t bytes[8] = {0};
    uint64_t encoded;
    RibosVmStatus status;

    if (value == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *value = 0;
    status = ribos_vm_storage_validate_scalar_slot(
        prepared_program,
        storage,
        arena_size,
        function_id,
        frame_base,
        slot_id,
        RIBOS_BC_TYPE_SIGNED,
        bit_width);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_storage_slot_read_v1(
        prepared_program,
        storage,
        arena_size,
        function_id,
        frame_base,
        slot_id,
        bytes,
        bit_width / 8u);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    encoded = ribos_vm_storage_decode_scalar(bytes, bit_width);
    if (bit_width == 64) {
        if (encoded <= (uint64_t)INT64_MAX) {
            *value = (int64_t)encoded;
        } else {
            *value = -1 -
                (int64_t)(UINT64_MAX - encoded);
        }
    } else if ((encoded &
            (UINT64_C(1) << (bit_width - 1u))) != 0) {
        *value = (int64_t)encoded -
            (int64_t)(UINT64_C(1) << bit_width);
    } else {
        *value = (int64_t)encoded;
    }
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_slot_store_bool_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    uint32_t value)
{
    RibosVmSlotLocation location;
    uint8_t byte;
    RibosVmStatus status = ribos_vm_storage_locate_slot(
        prepared_program,
        storage,
        arena_size,
        function_id,
        frame_base,
        slot_id,
        &location);

    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (location.type_kind != RIBOS_BC_TYPE_BOOL ||
        location.bit_width != 1 || location.byte_size != 1 ||
        value > 1) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    byte = (uint8_t)value;
    return ribos_vm_storage_slot_write_v1(
        prepared_program,
        storage,
        arena_size,
        function_id,
        frame_base,
        slot_id,
        &byte,
        1);
}

RibosVmStatus
ribos_vm_storage_slot_load_bool_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    uint32_t *value)
{
    RibosVmSlotLocation location;
    uint8_t byte = 0;
    RibosVmStatus status;

    if (value == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *value = 0;
    status = ribos_vm_storage_locate_slot(
        prepared_program,
        storage,
        arena_size,
        function_id,
        frame_base,
        slot_id,
        &location);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (location.type_kind != RIBOS_BC_TYPE_BOOL ||
        location.bit_width != 1 || location.byte_size != 1) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    status = ribos_vm_storage_slot_read_v1(
        prepared_program,
        storage,
        arena_size,
        function_id,
        frame_base,
        slot_id,
        &byte,
        1);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (byte > 1) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    *value = byte;
    return RIBOS_VM_STATUS_OK;
}
