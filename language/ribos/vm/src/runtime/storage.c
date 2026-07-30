#include "ribos/vm/storage.h"

#include "ribos/base/checked.h"
#include "storage_internal.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

_Static_assert(
    RIBOS_VM_STORAGE_ALIGNMENT_V1 ==
        RIBOS_BYTECODE_FRAME_ALIGNMENT_V1,
    "runtime arena alignment must match bytecode frame alignment");
_Static_assert(
    sizeof(RibosVmHelperExecutionSnapshot) == 256,
    "helper execution snapshot must fit the first outcome record");
_Static_assert(
    sizeof(RibosVmTerminalSnapshot) == 256,
    "terminal snapshot must fit the second outcome record");

#define RIBOS_VM_STORAGE_MAGIC UINT64_C(0x524253564d535431)
#define RIBOS_VM_CONTROL_BYTES UINT64_C(512)
#define RIBOS_VM_FRAME_RECORD_BYTES UINT32_C(32)
#define RIBOS_VM_SLOT_STATE_BYTES UINT32_C(8)
#define RIBOS_VM_LOOP_COUNTER_BYTES UINT32_C(8)
#define RIBOS_VM_HELPER_COUNTER_BYTES UINT32_C(16)
#define RIBOS_VM_HANDLE_RECORD_BYTES UINT32_C(32)
#define RIBOS_VM_HELPER_OUTCOME_RECORD_BYTES UINT32_C(256)
#define RIBOS_VM_TERMINAL_OUTCOME_RECORD_BYTES UINT32_C(256)
#define RIBOS_VM_OUTCOME_RECORD_BYTES UINT32_C(512)
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
#define RIBOS_VM_CONTROL_EXECUTION_STATE_OFFSET 428u
#define RIBOS_VM_CONTROL_CURRENT_FUNCTION_OFFSET 432u
#define RIBOS_VM_CONTROL_CURRENT_BLOCK_OFFSET 436u
#define RIBOS_VM_CONTROL_CURRENT_INSTRUCTION_OFFSET 440u
#define RIBOS_VM_CONTROL_RETURN_SLOT_OFFSET 444u
#define RIBOS_VM_CONTROL_CURRENT_FRAME_BASE_OFFSET 448u
#define RIBOS_VM_CONTROL_CONSUMED_INSTRUCTIONS_OFFSET 456u
#define RIBOS_VM_CONTROL_CONTEXT_GENERATION_OFFSET 464u
#define RIBOS_VM_CONTROL_CONTEXT_TYPE_OFFSET 472u
#define RIBOS_VM_CONTROL_EXECUTION_RESERVED_OFFSET 476u
#define RIBOS_VM_CONTROL_CONTEXT_DIGEST_OFFSET 480u

#define RIBOS_VM_FRAME_FUNCTION_OFFSET 0u
#define RIBOS_VM_FRAME_CONTINUATION_OFFSET 4u
#define RIBOS_VM_FRAME_RETURN_SLOT_OFFSET 8u
#define RIBOS_VM_FRAME_RESERVED0_OFFSET 12u
#define RIBOS_VM_FRAME_BASE_OFFSET 16u
#define RIBOS_VM_FRAME_SIZE_OFFSET 24u
#define RIBOS_VM_FRAME_RESERVED1_OFFSET 28u

#define RIBOS_VM_FAULT_CODE_OFFSET 0u
#define RIBOS_VM_FAULT_SUBJECT_OFFSET 4u
#define RIBOS_VM_FAULT_FUNCTION_OFFSET 8u
#define RIBOS_VM_FAULT_INSTRUCTION_OFFSET 12u
#define RIBOS_VM_FAULT_HELPER_OFFSET 16u
#define RIBOS_VM_FAULT_DETAIL_OFFSET 20u
#define RIBOS_VM_FAULT_LAST_EFFECT_OFFSET 24u
#define RIBOS_VM_FAULT_LAST_DURABILITY_OFFSET 28u
#define RIBOS_VM_FAULT_CONSUMED_INSTRUCTIONS_OFFSET 32u
#define RIBOS_VM_FAULT_CONSUMED_HELPERS_OFFSET 40u
#define RIBOS_VM_FAULT_CONSUMED_INPUT_OFFSET 48u
#define RIBOS_VM_FAULT_CONSUMED_OUTPUT_OFFSET 56u
#define RIBOS_VM_FAULT_CONSUMED_OPERATIONS_OFFSET 64u
#define RIBOS_VM_FAULT_CONSUMED_POLLS_OFFSET 72u
#define RIBOS_VM_FAULT_ELAPSED_OFFSET 80u
#define RIBOS_VM_FAULT_ARTIFACT_HASH_OFFSET 88u
#define RIBOS_VM_FAULT_TRACE_DIGEST_OFFSET 120u
#define RIBOS_VM_FAULT_SEALED_OFFSET 152u
#define RIBOS_VM_FAULT_RECOVERY_NOTIFIED_OFFSET 156u

#define RIBOS_VM_HELPER_STATE_SIZE_OFFSET 0u
#define RIBOS_VM_HELPER_STATE_MAJOR_OFFSET 4u
#define RIBOS_VM_HELPER_STATE_MINOR_OFFSET 6u
#define RIBOS_VM_HELPER_STATE_LIFECYCLE_OFFSET 8u
#define RIBOS_VM_HELPER_STATE_CALLBACK_ACTIVE_OFFSET 12u
#define RIBOS_VM_HELPER_STATE_MODE_OFFSET 16u
#define RIBOS_VM_HELPER_STATE_PHASE_OFFSET 20u
#define RIBOS_VM_HELPER_STATE_CAPABILITIES_OFFSET 24u
#define RIBOS_VM_HELPER_STATE_ACTIVE_ID_OFFSET 28u
#define RIBOS_VM_HELPER_STATE_START_OFFSET 32u
#define RIBOS_VM_HELPER_STATE_DEADLINE_OFFSET 40u
#define RIBOS_VM_HELPER_STATE_LAST_NOW_OFFSET 48u
#define RIBOS_VM_HELPER_STATE_CONSUMED_CALLS_OFFSET 56u
#define RIBOS_VM_HELPER_STATE_CONSUMED_INPUT_OFFSET 64u
#define RIBOS_VM_HELPER_STATE_CONSUMED_OUTPUT_OFFSET 72u
#define RIBOS_VM_HELPER_STATE_CONSUMED_OPERATIONS_OFFSET 80u
#define RIBOS_VM_HELPER_STATE_CONSUMED_POLLS_OFFSET 88u
#define RIBOS_VM_HELPER_STATE_RECEIPT_SEQUENCE_OFFSET 96u
#define RIBOS_VM_HELPER_STATE_LAST_ID_OFFSET 104u
#define RIBOS_VM_HELPER_STATE_LAST_STATUS_OFFSET 108u
#define RIBOS_VM_HELPER_STATE_LAST_EFFECT_OFFSET 112u
#define RIBOS_VM_HELPER_STATE_LAST_DURABILITY_OFFSET 116u
#define RIBOS_VM_HELPER_STATE_LAST_TRANSITION_OFFSET 120u
#define RIBOS_VM_HELPER_STATE_LAST_RESULT_OFFSET 124u
#define RIBOS_VM_HELPER_STATE_LAST_INPUT_OFFSET 128u
#define RIBOS_VM_HELPER_STATE_LAST_OUTPUT_OFFSET 136u
#define RIBOS_VM_HELPER_STATE_LAST_OPERATIONS_OFFSET 144u
#define RIBOS_VM_HELPER_STATE_LAST_POLLS_OFFSET 152u
#define RIBOS_VM_HELPER_STATE_LAST_DURATION_OFFSET 160u
#define RIBOS_VM_HELPER_STATE_CONTEXT_GENERATION_OFFSET 168u
#define RIBOS_VM_HELPER_STATE_CONTEXT_DIGEST_OFFSET 176u
#define RIBOS_VM_HELPER_STATE_EXECUTION_DIGEST_OFFSET 208u
#define RIBOS_VM_HELPER_STATE_RESERVED_OFFSET 240u

#define RIBOS_VM_TERMINAL_RECORD_OFFSET 256u
#define RIBOS_VM_TERMINAL_SIZE_OFFSET 0u
#define RIBOS_VM_TERMINAL_MAJOR_OFFSET 4u
#define RIBOS_VM_TERMINAL_MINOR_OFFSET 6u
#define RIBOS_VM_TERMINAL_STATE_OFFSET 8u
#define RIBOS_VM_TERMINAL_OUTCOME_KIND_OFFSET 12u
#define RIBOS_VM_TERMINAL_HELPER_ID_OFFSET 16u
#define RIBOS_VM_TERMINAL_ACTION_TYPE_OFFSET 20u
#define RIBOS_VM_TERMINAL_ERROR_TYPE_OFFSET 24u
#define RIBOS_VM_TERMINAL_ERROR_CODE_OFFSET 28u
#define RIBOS_VM_TERMINAL_SOURCE_MAP_OFFSET 32u
#define RIBOS_VM_TERMINAL_FLAGS_OFFSET 36u
#define RIBOS_VM_TERMINAL_ACTION_CONSUMED_OFFSET 40u
#define RIBOS_VM_TERMINAL_RECOVERY_NOTIFIED_OFFSET 44u
#define RIBOS_VM_TERMINAL_AUTHORITY_REVOKED_OFFSET 48u
#define RIBOS_VM_TERMINAL_JOURNAL_STATE_OFFSET 52u
#define RIBOS_VM_TERMINAL_PAYLOAD_SIZE_OFFSET 56u
#define RIBOS_VM_TERMINAL_CONTEXT_GENERATION_OFFSET 64u
#define RIBOS_VM_TERMINAL_JOURNAL_SEQUENCE_OFFSET 72u
#define RIBOS_VM_TERMINAL_JOURNAL_COUNT_OFFSET 80u
#define RIBOS_VM_TERMINAL_LAST_JOURNAL_HELPER_OFFSET 88u
#define RIBOS_VM_TERMINAL_LAST_JOURNAL_STATUS_OFFSET 92u
#define RIBOS_VM_TERMINAL_BINDING_DIGEST_OFFSET 96u
#define RIBOS_VM_TERMINAL_CONTEXT_DIGEST_OFFSET 128u
#define RIBOS_VM_TERMINAL_ACTION_DIGEST_OFFSET 160u
#define RIBOS_VM_TERMINAL_JOURNAL_DIGEST_OFFSET 192u
#define RIBOS_VM_TERMINAL_TRACE_DIGEST_OFFSET 224u

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

typedef struct RibosVmFrameRecord {
    uint32_t function_id;
    uint32_t continuation_instruction_id;
    uint32_t return_slot_id;
    uint64_t frame_base;
    uint32_t frame_size;
} RibosVmFrameRecord;

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

static int
ribos_vm_storage_slice_is_valid(
    const RibosVmSlotLocation *location,
    uint32_t offset,
    uint32_t byte_size)
{
    return location != NULL &&
        offset <= location->byte_size &&
        byte_size <= location->byte_size - offset;
}

static int
ribos_vm_storage_location_is_initialized(
    const RibosVmStorage *storage,
    const RibosVmSlotLocation *location)
{
    const uint8_t *state;

    if (storage == NULL || location == NULL) {
        return 0;
    }
    state = ribos_vm_storage_location_const_state(storage, location);
    return state[0] == RIBOS_VM_SLOT_STORAGE_INITIALIZED &&
        state[1] == 0 && state[2] == 0 && state[3] == 0 &&
        state[4] == 0 && state[5] == 0 && state[6] == 0 &&
        state[7] == 0;
}

static void
ribos_vm_storage_location_mark_initialized(
    RibosVmStorage *storage,
    const RibosVmSlotLocation *location)
{
    uint8_t *state =
        ribos_vm_storage_location_state(storage, location);

    memset(state, 0, RIBOS_VM_SLOT_STATE_BYTES);
    state[0] = RIBOS_VM_SLOT_STORAGE_INITIALIZED;
}

RibosVmStatus
ribos_vm_storage_slot_zero_internal_v1(
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

    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (location.byte_size != 0) {
        memset(
            ribos_vm_storage_location_value(storage, &location),
            0,
            location.byte_size);
    }
    ribos_vm_storage_location_mark_initialized(storage, &location);
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_slot_copy_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t source_function_id,
    uint64_t source_frame_base,
    uint32_t source_slot_id,
    uint32_t destination_function_id,
    uint64_t destination_frame_base,
    uint32_t destination_slot_id)
{
    RibosVmSlotLocation source;
    RibosVmSlotLocation destination;
    RibosVmStatus status;

    status = ribos_vm_storage_locate_slot(
        prepared_program,
        storage,
        arena_size,
        source_function_id,
        source_frame_base,
        source_slot_id,
        &source);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_storage_locate_slot(
        prepared_program,
        storage,
        arena_size,
        destination_function_id,
        destination_frame_base,
        destination_slot_id,
        &destination);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (source.type_id != destination.type_id ||
        source.byte_size != destination.byte_size) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    if (!ribos_vm_storage_location_is_initialized(
            storage,
            &source)) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    if (source.byte_size != 0) {
        memmove(
            ribos_vm_storage_location_value(
                storage,
                &destination),
            ribos_vm_storage_location_const_value(
                storage,
                &source),
            source.byte_size);
    }
    ribos_vm_storage_location_mark_initialized(
        storage,
        &destination);
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_slot_slice_read_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    uint32_t offset,
    uint8_t *bytes,
    uint32_t byte_size)
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

    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (!ribos_vm_storage_slice_is_valid(
            &location,
            offset,
            byte_size) ||
        (byte_size != 0 && bytes == NULL)) {
        return RIBOS_VM_STATUS_INVALID_SIZE;
    }
    if (!ribos_vm_storage_location_is_initialized(
            storage,
            &location)) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    if (byte_size != 0) {
        memcpy(
            bytes,
            ribos_vm_storage_location_const_value(
                storage,
                &location) + offset,
            byte_size);
    }
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_slot_slice_write_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    uint32_t offset,
    const uint8_t *bytes,
    uint32_t byte_size)
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

    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (!ribos_vm_storage_slice_is_valid(
            &location,
            offset,
            byte_size) ||
        (byte_size != 0 && bytes == NULL)) {
        return RIBOS_VM_STATUS_INVALID_SIZE;
    }
    if (!ribos_vm_storage_location_is_initialized(
            storage,
            &location)) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    if (byte_size != 0) {
        memcpy(
            ribos_vm_storage_location_value(
                storage,
                &location) + offset,
            bytes,
            byte_size);
    }
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_slot_slice_zero_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    uint32_t offset,
    uint32_t byte_size)
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

    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (!ribos_vm_storage_slice_is_valid(
            &location,
            offset,
            byte_size)) {
        return RIBOS_VM_STATUS_INVALID_SIZE;
    }
    if (!ribos_vm_storage_location_is_initialized(
            storage,
            &location)) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    if (byte_size != 0) {
        memset(
            ribos_vm_storage_location_value(
                storage,
                &location) + offset,
            0,
            byte_size);
    }
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_slot_slice_copy_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t source_function_id,
    uint64_t source_frame_base,
    uint32_t source_slot_id,
    uint32_t source_offset,
    uint32_t destination_function_id,
    uint64_t destination_frame_base,
    uint32_t destination_slot_id,
    uint32_t destination_offset,
    uint32_t byte_size)
{
    RibosVmSlotLocation source;
    RibosVmSlotLocation destination;
    RibosVmStatus status;

    status = ribos_vm_storage_locate_slot(
        prepared_program,
        storage,
        arena_size,
        source_function_id,
        source_frame_base,
        source_slot_id,
        &source);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_storage_locate_slot(
        prepared_program,
        storage,
        arena_size,
        destination_function_id,
        destination_frame_base,
        destination_slot_id,
        &destination);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (!ribos_vm_storage_slice_is_valid(
            &source,
            source_offset,
            byte_size) ||
        !ribos_vm_storage_slice_is_valid(
            &destination,
            destination_offset,
            byte_size)) {
        return RIBOS_VM_STATUS_INVALID_SIZE;
    }
    if (!ribos_vm_storage_location_is_initialized(
            storage,
            &source) ||
        !ribos_vm_storage_location_is_initialized(
            storage,
            &destination)) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    if (byte_size != 0) {
        memmove(
            ribos_vm_storage_location_value(
                storage,
                &destination) + destination_offset,
            ribos_vm_storage_location_const_value(
                storage,
                &source) + source_offset,
            byte_size);
    }
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_slot_slice_move_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    uint32_t source_offset,
    uint32_t destination_offset,
    uint32_t byte_size)
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

    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (!ribos_vm_storage_slice_is_valid(
            &location,
            source_offset,
            byte_size) ||
        !ribos_vm_storage_slice_is_valid(
            &location,
            destination_offset,
            byte_size)) {
        return RIBOS_VM_STATUS_INVALID_SIZE;
    }
    if (!ribos_vm_storage_location_is_initialized(
            storage,
            &location)) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    if (byte_size != 0) {
        memmove(
            ribos_vm_storage_location_value(
                storage,
                &location) + destination_offset,
            ribos_vm_storage_location_value(
                storage,
                &location) + source_offset,
            byte_size);
    }
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_slot_slice_compare_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    uint32_t left_function_id,
    uint64_t left_frame_base,
    uint32_t left_slot_id,
    uint32_t left_offset,
    uint32_t right_function_id,
    uint64_t right_frame_base,
    uint32_t right_slot_id,
    uint32_t right_offset,
    uint32_t byte_size,
    int *comparison)
{
    RibosVmSlotLocation left;
    RibosVmSlotLocation right;
    RibosVmStatus status;

    if (comparison == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *comparison = 0;
    status = ribos_vm_storage_locate_slot(
        prepared_program,
        storage,
        arena_size,
        left_function_id,
        left_frame_base,
        left_slot_id,
        &left);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_storage_locate_slot(
        prepared_program,
        storage,
        arena_size,
        right_function_id,
        right_frame_base,
        right_slot_id,
        &right);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (!ribos_vm_storage_slice_is_valid(
            &left,
            left_offset,
            byte_size) ||
        !ribos_vm_storage_slice_is_valid(
            &right,
            right_offset,
            byte_size)) {
        return RIBOS_VM_STATUS_INVALID_SIZE;
    }
    if (!ribos_vm_storage_location_is_initialized(
            storage,
            &left) ||
        !ribos_vm_storage_location_is_initialized(
            storage,
            &right)) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    if (byte_size != 0) {
        *comparison = memcmp(
            ribos_vm_storage_location_const_value(
                storage,
                &left) + left_offset,
            ribos_vm_storage_location_const_value(
                storage,
                &right) + right_offset,
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

static RibosVmStatus
ribos_vm_storage_function_info(
    const RibosPreparedProgram *prepared_program,
    uint32_t function_id,
    uint32_t *entry_block_id,
    uint32_t *entry_instruction_id,
    uint32_t *frame_size)
{
    const RibosArtifactView *view =
        ribos_prepared_program_artifact_view_v1(prepared_program);
    const RibosArtifactSectionView *functions;
    const RibosArtifactSectionView *blocks;
    const uint8_t *function;
    const uint8_t *block;
    uint32_t entry;

    if (view == NULL || frame_size == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    functions = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_FUNCTIONS);
    blocks = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_BLOCKS);
    function = ribos_vm_storage_row(functions, function_id);
    if (function == NULL ||
        ribos_vm_value_read_u32(function) != function_id) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    entry = ribos_vm_value_read_u32(function + 12);
    block = ribos_vm_storage_row(blocks, entry);
    if (block == NULL ||
        ribos_vm_value_read_u32(block) != entry ||
        ribos_vm_value_read_u32(block + 4) != function_id) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    if (entry_block_id != NULL) {
        *entry_block_id = entry;
    }
    if (entry_instruction_id != NULL) {
        *entry_instruction_id = ribos_vm_value_read_u32(block + 8);
    }
    *frame_size = ribos_vm_value_read_u32(function + 88);
    return RIBOS_VM_STATUS_OK;
}

static uint8_t *
ribos_vm_storage_frame_bytes(
    RibosVmStorage *storage,
    const RibosVmStoragePlan *plan,
    uint32_t depth_index)
{
    const RibosVmStorageRegion *frames =
        &plan->regions[RIBOS_VM_STORAGE_REGION_FRAMES];

    if (depth_index >= frames->count ||
        frames->stride != RIBOS_VM_FRAME_RECORD_BYTES) {
        return NULL;
    }
    return ribos_vm_storage_mutable_bytes(storage) +
        (size_t)frames->offset +
        (size_t)depth_index * RIBOS_VM_FRAME_RECORD_BYTES;
}

static const uint8_t *
ribos_vm_storage_const_frame_bytes(
    const RibosVmStorage *storage,
    const RibosVmStoragePlan *plan,
    uint32_t depth_index)
{
    const RibosVmStorageRegion *frames =
        &plan->regions[RIBOS_VM_STORAGE_REGION_FRAMES];

    if (depth_index >= frames->count ||
        frames->stride != RIBOS_VM_FRAME_RECORD_BYTES) {
        return NULL;
    }
    return ribos_vm_storage_const_bytes(storage) +
        (size_t)frames->offset +
        (size_t)depth_index * RIBOS_VM_FRAME_RECORD_BYTES;
}

static void
ribos_vm_storage_encode_frame(
    uint8_t *bytes,
    const RibosVmFrameRecord *record)
{
    memset(bytes, 0, RIBOS_VM_FRAME_RECORD_BYTES);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_FRAME_FUNCTION_OFFSET,
        record->function_id);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_FRAME_CONTINUATION_OFFSET,
        record->continuation_instruction_id);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_FRAME_RETURN_SLOT_OFFSET,
        record->return_slot_id);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_FRAME_BASE_OFFSET,
        record->frame_base);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_FRAME_SIZE_OFFSET,
        record->frame_size);
}

static int
ribos_vm_storage_decode_frame(
    const uint8_t *bytes,
    RibosVmFrameRecord *record)
{
    if (bytes == NULL || record == NULL ||
        ribos_vm_value_read_u32(
            bytes + RIBOS_VM_FRAME_RESERVED0_OFFSET) != 0 ||
        ribos_vm_value_read_u32(
            bytes + RIBOS_VM_FRAME_RESERVED1_OFFSET) != 0) {
        return 0;
    }
    *record = (RibosVmFrameRecord){
        .function_id = ribos_vm_value_read_u32(
            bytes + RIBOS_VM_FRAME_FUNCTION_OFFSET),
        .continuation_instruction_id = ribos_vm_value_read_u32(
            bytes + RIBOS_VM_FRAME_CONTINUATION_OFFSET),
        .return_slot_id = ribos_vm_value_read_u32(
            bytes + RIBOS_VM_FRAME_RETURN_SLOT_OFFSET),
        .frame_base = ribos_vm_value_read_u64(
            bytes + RIBOS_VM_FRAME_BASE_OFFSET),
        .frame_size = ribos_vm_value_read_u32(
            bytes + RIBOS_VM_FRAME_SIZE_OFFSET),
    };
    return record->function_id != RIBOS_VM_INVALID_ID;
}

static int
ribos_vm_storage_execution_control_matches(
    const RibosVmStorageExecutionControl *left,
    const RibosVmStorageExecutionControl *right)
{
    return left != NULL && right != NULL &&
        left->state == right->state &&
        left->function_id == right->function_id &&
        left->block_id == right->block_id &&
        left->instruction_id == right->instruction_id &&
        left->return_slot_id == right->return_slot_id &&
        left->frame_base == right->frame_base &&
        left->stack_cursor == right->stack_cursor &&
        left->frame_depth == right->frame_depth &&
        left->consumed_instructions == right->consumed_instructions &&
        left->context_generation == right->context_generation &&
        left->context_type_id == right->context_type_id &&
        memcmp(
            left->context_digest,
            right->context_digest,
            RIBOS_VM_DIGEST_BYTES) == 0;
}

static int
ribos_vm_storage_frames_are_valid(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    const RibosVmStoragePlan *plan,
    const RibosVmStorageExecutionControl *control)
{
    uint64_t cursor = 0;
    uint32_t index;

    if (control->state == 0) {
        return control->frame_depth == 0 &&
            control->stack_cursor == 0 &&
            control->frame_base == 0;
    }
    if (control->frame_depth == 0 ||
        control->frame_depth > plan->call_depth ||
        control->frame_depth >
            plan->regions[RIBOS_VM_STORAGE_REGION_FRAMES].count) {
        return 0;
    }
    for (index = 0; index < control->frame_depth; ++index) {
        RibosVmFrameRecord record;
        uint32_t expected_size;
        uint32_t previous;

        if (!ribos_vm_storage_decode_frame(
                ribos_vm_storage_const_frame_bytes(
                    storage,
                    plan,
                    index),
                &record) ||
            ribos_vm_storage_function_info(
                prepared_program,
                record.function_id,
                NULL,
                NULL,
                &expected_size) != RIBOS_VM_STATUS_OK ||
            record.frame_base != cursor ||
            record.frame_size != expected_size ||
            !ribos_checked_u64_add(
                cursor,
                record.frame_size,
                &cursor)) {
            return 0;
        }
        if ((index == 0 &&
             (record.continuation_instruction_id !=
                  RIBOS_VM_INVALID_ID ||
              record.return_slot_id != RIBOS_VM_INVALID_ID)) ||
            (index != 0 &&
             (record.continuation_instruction_id ==
                  RIBOS_VM_INVALID_ID ||
              record.return_slot_id == RIBOS_VM_INVALID_ID))) {
            return 0;
        }
        for (previous = 0; previous < index; ++previous) {
            RibosVmFrameRecord active;

            if (!ribos_vm_storage_decode_frame(
                    ribos_vm_storage_const_frame_bytes(
                        storage,
                        plan,
                        previous),
                    &active) ||
                active.function_id == record.function_id) {
                return 0;
            }
        }
        if (index + 1 == control->frame_depth &&
            (record.function_id != control->function_id ||
             record.frame_base != control->frame_base)) {
            return 0;
        }
    }
    return cursor == control->stack_cursor &&
        cursor <= plan->regions[
            RIBOS_VM_STORAGE_REGION_FRAME_VALUES].byte_size;
}

static void
ribos_vm_storage_write_execution_control(
    uint8_t *bytes,
    const RibosVmStorageExecutionControl *control)
{
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_CONTROL_STACK_CURSOR_OFFSET,
        control->stack_cursor);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_CONTROL_FRAME_DEPTH_OFFSET,
        control->frame_depth);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_CONTROL_EXECUTION_STATE_OFFSET,
        control->state);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_CONTROL_CURRENT_FUNCTION_OFFSET,
        control->function_id);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_CONTROL_CURRENT_BLOCK_OFFSET,
        control->block_id);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_CONTROL_CURRENT_INSTRUCTION_OFFSET,
        control->instruction_id);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_CONTROL_RETURN_SLOT_OFFSET,
        control->return_slot_id);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_CONTROL_CURRENT_FRAME_BASE_OFFSET,
        control->frame_base);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_CONTROL_CONSUMED_INSTRUCTIONS_OFFSET,
        control->consumed_instructions);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_CONTROL_CONTEXT_GENERATION_OFFSET,
        control->context_generation);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_CONTROL_CONTEXT_TYPE_OFFSET,
        control->context_type_id);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_CONTROL_EXECUTION_RESERVED_OFFSET,
        0);
    memcpy(
        bytes + RIBOS_VM_CONTROL_CONTEXT_DIGEST_OFFSET,
        control->context_digest,
        RIBOS_VM_DIGEST_BYTES);
}

static RibosVmStatus
ribos_vm_storage_reset_function_loops(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id)
{
    const RibosArtifactView *view;
    const RibosArtifactSectionView *loops;
    RibosVmStoragePlan plan;
    size_t required_size;
    uint8_t *counters;
    uint32_t index;
    RibosVmStatus status;

    status = ribos_vm_runtime_size_v1(
        prepared_program,
        &plan,
        &required_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    (void)required_size;
    status = ribos_vm_storage_validate_v1(
        prepared_program,
        storage,
        arena_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    view = ribos_prepared_program_artifact_view_v1(prepared_program);
    loops = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_LOOPS);
    if (loops == NULL || loops->count != plan.loop_count) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    counters = ribos_vm_storage_mutable_bytes(storage) +
        (size_t)plan.regions[
            RIBOS_VM_STORAGE_REGION_LOOP_COUNTERS].offset;
    for (index = 0; index < loops->count; ++index) {
        const uint8_t *row = ribos_vm_storage_row(loops, index);

        if (row == NULL || ribos_vm_value_read_u32(row) != index) {
            return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
        }
        if (ribos_vm_value_read_u32(row + 4) == function_id) {
            ribos_vm_value_write_u64(
                counters +
                    (size_t)index * RIBOS_VM_LOOP_COUNTER_BYTES,
                ribos_vm_value_read_u32(row + 24));
        }
    }
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_execution_begin_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *control)
{
    RibosVmStorageExecutionControl initial;
    RibosVmStoragePlan plan;
    RibosVmFrameRecord frame;
    uint8_t *frame_bytes;
    uint8_t *bytes;
    size_t required_size;
    uint32_t entry_block;
    uint32_t entry_instruction;
    uint32_t frame_size;
    RibosVmStatus status;

    if (control == NULL || control->state != 1 ||
        control->function_id == RIBOS_VM_INVALID_ID ||
        control->block_id == RIBOS_VM_INVALID_ID ||
        control->instruction_id == RIBOS_VM_INVALID_ID ||
        control->return_slot_id != RIBOS_VM_INVALID_ID ||
        control->frame_base != 0 ||
        control->stack_cursor != 0 ||
        control->frame_depth != 0 ||
        control->consumed_instructions != 0 ||
        control->context_generation == 0 ||
        control->context_type_id == RIBOS_VM_INVALID_ID ||
        !ribos_vm_digest_is_nonzero(control->context_digest)) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
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
    status = ribos_vm_storage_function_info(
        prepared_program,
        control->function_id,
        &entry_block,
        &entry_instruction,
        &frame_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (plan.call_depth == 0 ||
        control->block_id != entry_block ||
        control->instruction_id != entry_instruction ||
        frame_size > plan.regions[
            RIBOS_VM_STORAGE_REGION_FRAME_VALUES].byte_size) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    bytes = ribos_vm_storage_mutable_bytes(storage);
    if (ribos_vm_value_read_u32(
            bytes + RIBOS_VM_CONTROL_EXECUTION_STATE_OFFSET) != 0) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    frame = (RibosVmFrameRecord){
        .function_id = control->function_id,
        .continuation_instruction_id = RIBOS_VM_INVALID_ID,
        .return_slot_id = RIBOS_VM_INVALID_ID,
        .frame_base = 0,
        .frame_size = frame_size,
    };
    frame_bytes = ribos_vm_storage_frame_bytes(storage, &plan, 0);
    if (frame_bytes == NULL) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    status = ribos_vm_storage_reset_function_loops(
        prepared_program,
        storage,
        arena_size,
        control->function_id);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    ribos_vm_storage_encode_frame(frame_bytes, &frame);
    initial = *control;
    initial.stack_cursor = frame_size;
    initial.frame_depth = 1;
    ribos_vm_storage_write_execution_control(bytes, &initial);
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_execution_load_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    RibosVmStorageExecutionControl *control,
    uint64_t *remaining_instructions)
{
    const uint8_t *bytes;
    RibosVmStoragePlan plan;
    size_t required_size;
    RibosVmStatus status;

    if (control == NULL || remaining_instructions == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    memset(control, 0, sizeof(*control));
    *remaining_instructions = 0;
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
    bytes = ribos_vm_storage_const_bytes(storage);
    control->state = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_CONTROL_EXECUTION_STATE_OFFSET);
    control->function_id = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_CONTROL_CURRENT_FUNCTION_OFFSET);
    control->block_id = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_CONTROL_CURRENT_BLOCK_OFFSET);
    control->instruction_id = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_CONTROL_CURRENT_INSTRUCTION_OFFSET);
    control->return_slot_id = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_CONTROL_RETURN_SLOT_OFFSET);
    control->frame_base = ribos_vm_value_read_u64(
        bytes + RIBOS_VM_CONTROL_CURRENT_FRAME_BASE_OFFSET);
    control->stack_cursor = ribos_vm_value_read_u64(
        bytes + RIBOS_VM_CONTROL_STACK_CURSOR_OFFSET);
    control->frame_depth = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_CONTROL_FRAME_DEPTH_OFFSET);
    control->consumed_instructions = ribos_vm_value_read_u64(
        bytes + RIBOS_VM_CONTROL_CONSUMED_INSTRUCTIONS_OFFSET);
    control->context_generation = ribos_vm_value_read_u64(
        bytes + RIBOS_VM_CONTROL_CONTEXT_GENERATION_OFFSET);
    control->context_type_id = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_CONTROL_CONTEXT_TYPE_OFFSET);
    memcpy(
        control->context_digest,
        bytes + RIBOS_VM_CONTROL_CONTEXT_DIGEST_OFFSET,
        RIBOS_VM_DIGEST_BYTES);
    *remaining_instructions = ribos_vm_value_read_u64(
        bytes +
        RIBOS_VM_STORAGE_CONTROL_REMAINING_INSTRUCTIONS_OFFSET_V1);
    if (control->state > 4 ||
        ribos_vm_value_read_u32(
            bytes + RIBOS_VM_CONTROL_EXECUTION_RESERVED_OFFSET) != 0 ||
        (control->state != 0 &&
	         (control->function_id == RIBOS_VM_INVALID_ID ||
	          control->block_id == RIBOS_VM_INVALID_ID ||
	          control->instruction_id == RIBOS_VM_INVALID_ID ||
	          control->context_generation == 0 ||
          control->context_type_id == RIBOS_VM_INVALID_ID ||
          !ribos_vm_digest_is_nonzero(control->context_digest) ||
          ((control->state == 3) !=
           (control->return_slot_id != RIBOS_VM_INVALID_ID))))) {
        memset(control, 0, sizeof(*control));
        *remaining_instructions = 0;
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    if (!ribos_vm_storage_frames_are_valid(
            prepared_program,
            storage,
            &plan,
            control)) {
        memset(control, 0, sizeof(*control));
        *remaining_instructions = 0;
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_execution_store_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *control)
{
    RibosVmStorageExecutionControl existing;
    uint64_t remaining;
    uint8_t *bytes;
    RibosVmStatus status;

    if (control == NULL || control->state < 1 || control->state > 4 ||
        control->function_id == RIBOS_VM_INVALID_ID ||
        control->block_id == RIBOS_VM_INVALID_ID ||
        control->instruction_id == RIBOS_VM_INVALID_ID ||
        control->context_generation == 0 ||
        control->context_type_id == RIBOS_VM_INVALID_ID ||
        !ribos_vm_digest_is_nonzero(control->context_digest) ||
        ((control->state == 3) !=
         (control->return_slot_id != RIBOS_VM_INVALID_ID))) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    status = ribos_vm_storage_execution_load_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &existing,
        &remaining);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    (void)remaining;
    if (existing.state == 0 || existing.state == 3 ||
        existing.state == 4 || control->state == 1 ||
        existing.function_id != control->function_id ||
        existing.frame_base != control->frame_base ||
        existing.stack_cursor != control->stack_cursor ||
        existing.frame_depth != control->frame_depth ||
        existing.context_generation != control->context_generation ||
        existing.context_type_id != control->context_type_id ||
        memcmp(
            existing.context_digest,
            control->context_digest,
            RIBOS_VM_DIGEST_BYTES) != 0 ||
        control->consumed_instructions !=
            existing.consumed_instructions) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    bytes = ribos_vm_storage_mutable_bytes(storage);
    ribos_vm_storage_write_execution_control(bytes, control);
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_call_target_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *caller,
    uint32_t callee_function_id,
    RibosVmStorageCallTarget *target,
    uint32_t *fault_code)
{
    RibosVmStorageExecutionControl existing;
    RibosVmStoragePlan plan;
    uint64_t remaining;
    uint64_t frame_end;
    size_t required_size;
    uint32_t frame_size;
    uint32_t entry_block_id;
    uint32_t entry_instruction_id;
    uint32_t index;
    RibosVmStatus status;

    if (caller == NULL || target == NULL || fault_code == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    memset(target, 0, sizeof(*target));
    *fault_code = RIBOS_VM_FAULT_NONE;
    status = ribos_vm_storage_execution_load_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &existing,
        &remaining);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    (void)remaining;
    if ((caller->state != 1 && caller->state != 2) ||
        !ribos_vm_storage_execution_control_matches(
            &existing,
            caller)) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    status = ribos_vm_runtime_size_v1(
        prepared_program,
        &plan,
        &required_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    (void)required_size;
    if (caller->frame_depth >= plan.call_depth) {
        *fault_code = RIBOS_VM_FAULT_CALL_DEPTH;
        return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
    }
    for (index = 0; index < caller->frame_depth; ++index) {
        RibosVmFrameRecord active;

        if (!ribos_vm_storage_decode_frame(
                ribos_vm_storage_const_frame_bytes(
                    storage,
                    &plan,
                    index),
                &active)) {
            return RIBOS_VM_STATUS_INVALID_STATE;
        }
        if (active.function_id == callee_function_id) {
            *fault_code = RIBOS_VM_FAULT_CALL_DEPTH;
            return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
        }
    }
    status = ribos_vm_storage_function_info(
        prepared_program,
        callee_function_id,
        &entry_block_id,
        &entry_instruction_id,
        &frame_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (!ribos_checked_u64_add(
            caller->stack_cursor,
            frame_size,
            &frame_end) ||
        frame_end > plan.regions[
            RIBOS_VM_STORAGE_REGION_FRAME_VALUES].byte_size) {
        *fault_code = RIBOS_VM_FAULT_STACK_BOUNDS;
        return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
    }
    *target = (RibosVmStorageCallTarget){
        .function_id = callee_function_id,
        .entry_block_id = entry_block_id,
        .entry_instruction_id = entry_instruction_id,
        .frame_size = frame_size,
        .frame_base = caller->stack_cursor,
    };
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_frame_push_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *caller,
    const RibosVmStorageCallTarget *target,
    uint32_t continuation_instruction_id,
    uint32_t return_slot_id,
    RibosVmStorageExecutionControl *callee)
{
    const RibosArtifactView *view;
    const RibosArtifactSectionView *instructions;
    const RibosArtifactSectionView *blocks;
    const RibosArtifactSectionView *slots;
    const uint8_t *continuation;
    const uint8_t *block;
    const uint8_t *slot;
    RibosVmStorageCallTarget expected;
    RibosVmStoragePlan plan;
    RibosVmFrameRecord record;
    uint8_t *frame_bytes;
    uint8_t *bytes;
    uint64_t frame_end;
    size_t required_size;
    uint32_t query_fault;
    RibosVmStatus status;

    if (caller == NULL || target == NULL || callee == NULL ||
        continuation_instruction_id == RIBOS_VM_INVALID_ID ||
        return_slot_id == RIBOS_VM_INVALID_ID) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    memset(callee, 0, sizeof(*callee));
    status = ribos_vm_storage_call_target_internal_v1(
        prepared_program,
        storage,
        arena_size,
        caller,
        target->function_id,
        &expected,
        &query_fault);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (query_fault != RIBOS_VM_FAULT_NONE ||
        expected.function_id != target->function_id ||
        expected.entry_block_id != target->entry_block_id ||
        expected.entry_instruction_id != target->entry_instruction_id ||
        expected.frame_size != target->frame_size ||
        expected.frame_base != target->frame_base) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
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
    instructions = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_INSTRUCTIONS);
    blocks = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_BLOCKS);
    slots = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_SLOTS);
    continuation = ribos_vm_storage_row(
        instructions,
        continuation_instruction_id);
    slot = ribos_vm_storage_row(slots, return_slot_id);
    if (continuation == NULL ||
        ribos_vm_value_read_u32(continuation + 4) !=
            continuation_instruction_id ||
        (block = ribos_vm_storage_row(
            blocks,
            ribos_vm_value_read_u32(continuation + 8))) == NULL ||
        ribos_vm_value_read_u32(block + 4) != caller->function_id ||
        slot == NULL ||
        ribos_vm_value_read_u32(slot) != return_slot_id ||
        ribos_vm_value_read_u32(slot + 4) != caller->function_id ||
        !ribos_checked_u64_add(
            target->frame_base,
            target->frame_size,
            &frame_end)) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    frame_bytes = ribos_vm_storage_frame_bytes(
        storage,
        &plan,
        caller->frame_depth);
    if (frame_bytes == NULL) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    status = ribos_vm_storage_reset_function_loops(
        prepared_program,
        storage,
        arena_size,
        target->function_id);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    record = (RibosVmFrameRecord){
        .function_id = target->function_id,
        .continuation_instruction_id = continuation_instruction_id,
        .return_slot_id = return_slot_id,
        .frame_base = target->frame_base,
        .frame_size = target->frame_size,
    };
    ribos_vm_storage_encode_frame(frame_bytes, &record);
    *callee = *caller;
    callee->state = 2;
    callee->function_id = target->function_id;
    callee->block_id = target->entry_block_id;
    callee->instruction_id = target->entry_instruction_id;
    callee->return_slot_id = RIBOS_VM_INVALID_ID;
    callee->frame_base = target->frame_base;
    callee->stack_cursor = frame_end;
    callee->frame_depth = caller->frame_depth + 1;
    bytes = ribos_vm_storage_mutable_bytes(storage);
    ribos_vm_storage_write_execution_control(bytes, callee);
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_return_target_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *callee,
    RibosVmStorageReturnTarget *target)
{
    const RibosArtifactView *view;
    const RibosArtifactSectionView *instructions;
    const RibosArtifactSectionView *blocks;
    const RibosArtifactSectionView *slots;
    const uint8_t *continuation;
    const uint8_t *block;
    const uint8_t *slot;
    RibosVmStorageExecutionControl existing;
    RibosVmStoragePlan plan;
    RibosVmFrameRecord current;
    RibosVmFrameRecord previous;
    uint64_t remaining;
    size_t required_size;
    RibosVmStatus status;

    if (callee == NULL || target == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    memset(target, 0, sizeof(*target));
    status = ribos_vm_storage_execution_load_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &existing,
        &remaining);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    (void)remaining;
    if (callee->state != 2 ||
        callee->frame_depth <= 1 ||
        !ribos_vm_storage_execution_control_matches(
            &existing,
            callee)) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    status = ribos_vm_runtime_size_v1(
        prepared_program,
        &plan,
        &required_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    (void)required_size;
    if (!ribos_vm_storage_decode_frame(
            ribos_vm_storage_const_frame_bytes(
                storage,
                &plan,
                callee->frame_depth - 1),
            &current) ||
        !ribos_vm_storage_decode_frame(
            ribos_vm_storage_const_frame_bytes(
                storage,
                &plan,
                callee->frame_depth - 2),
            &previous) ||
        current.function_id != callee->function_id ||
        current.continuation_instruction_id == RIBOS_VM_INVALID_ID ||
        current.return_slot_id == RIBOS_VM_INVALID_ID) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    view = ribos_prepared_program_artifact_view_v1(prepared_program);
    instructions = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_INSTRUCTIONS);
    blocks = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_BLOCKS);
    slots = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_SLOTS);
    continuation = ribos_vm_storage_row(
        instructions,
        current.continuation_instruction_id);
    slot = ribos_vm_storage_row(slots, current.return_slot_id);
    if (continuation == NULL ||
        ribos_vm_value_read_u32(continuation + 4) !=
            current.continuation_instruction_id ||
        (block = ribos_vm_storage_row(
            blocks,
            ribos_vm_value_read_u32(continuation + 8))) == NULL ||
        ribos_vm_value_read_u32(block + 4) != previous.function_id ||
        slot == NULL ||
        ribos_vm_value_read_u32(slot) != current.return_slot_id ||
        ribos_vm_value_read_u32(slot + 4) != previous.function_id) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    *target = (RibosVmStorageReturnTarget){
        .function_id = previous.function_id,
        .block_id = ribos_vm_value_read_u32(continuation + 8),
        .instruction_id = current.continuation_instruction_id,
        .return_slot_id = current.return_slot_id,
        .frame_base = previous.frame_base,
    };
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_frame_pop_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmStorageExecutionControl *callee,
    const RibosVmStorageReturnTarget *target,
    RibosVmStorageExecutionControl *caller)
{
    RibosVmStorageReturnTarget expected;
    RibosVmStoragePlan plan;
    RibosVmFrameRecord current;
    uint8_t *frame_bytes;
    uint8_t *bytes;
    size_t required_size;
    RibosVmStatus status;

    if (callee == NULL || target == NULL || caller == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    memset(caller, 0, sizeof(*caller));
    status = ribos_vm_storage_return_target_internal_v1(
        prepared_program,
        storage,
        arena_size,
        callee,
        &expected);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (expected.function_id != target->function_id ||
        expected.block_id != target->block_id ||
        expected.instruction_id != target->instruction_id ||
        expected.return_slot_id != target->return_slot_id ||
        expected.frame_base != target->frame_base) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    status = ribos_vm_runtime_size_v1(
        prepared_program,
        &plan,
        &required_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    (void)required_size;
    frame_bytes = ribos_vm_storage_frame_bytes(
        storage,
        &plan,
        callee->frame_depth - 1);
    if (!ribos_vm_storage_decode_frame(frame_bytes, &current)) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    status = ribos_vm_storage_reset_frame_v1(
        prepared_program,
        storage,
        arena_size,
        callee->function_id,
        callee->frame_base);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    memset(frame_bytes, 0, RIBOS_VM_FRAME_RECORD_BYTES);
    *caller = *callee;
    caller->state = 2;
    caller->function_id = target->function_id;
    caller->block_id = target->block_id;
    caller->instruction_id = target->instruction_id;
    caller->return_slot_id = RIBOS_VM_INVALID_ID;
    caller->frame_base = target->frame_base;
    caller->stack_cursor = current.frame_base;
    caller->frame_depth = callee->frame_depth - 1;
    bytes = ribos_vm_storage_mutable_bytes(storage);
    ribos_vm_storage_write_execution_control(bytes, caller);
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_loop_transition_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint32_t source_block_id,
    uint32_t target_block_id,
    uint32_t *violation_loop_id)
{
    const RibosArtifactView *view;
    const RibosArtifactSectionView *loops;
    RibosVmStoragePlan plan;
    uint8_t *counters;
    size_t required_size;
    uint32_t latch_matches = 0;
    uint32_t header_matches = 0;
    uint32_t index;
    RibosVmStatus status;

    if (violation_loop_id == NULL ||
        function_id == RIBOS_VM_INVALID_ID ||
        source_block_id == RIBOS_VM_INVALID_ID ||
        target_block_id == RIBOS_VM_INVALID_ID) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *violation_loop_id = RIBOS_VM_INVALID_ID;
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
    loops = ribos_artifact_find_section(
        view,
        RIBOS_ARTIFACT_SECTION_LOOPS);
    if (loops == NULL || loops->count != plan.loop_count) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    counters = ribos_vm_storage_mutable_bytes(storage) +
        (size_t)plan.regions[
            RIBOS_VM_STORAGE_REGION_LOOP_COUNTERS].offset;
    for (index = 0; index < loops->count; ++index) {
        const uint8_t *row = ribos_vm_storage_row(loops, index);
        uint32_t owner;
        uint32_t header;
        uint32_t body;
        uint32_t latch;
        uint32_t trip_count;
        uint8_t *counter;
        uint64_t remaining;

        if (row == NULL || ribos_vm_value_read_u32(row) != index) {
            return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
        }
        owner = ribos_vm_value_read_u32(row + 4);
        header = ribos_vm_value_read_u32(row + 8);
        body = ribos_vm_value_read_u32(row + 12);
        latch = ribos_vm_value_read_u32(row + 20);
        trip_count = ribos_vm_value_read_u32(row + 24);
        if (owner != function_id) {
            continue;
        }
        counter = counters +
            (size_t)index * RIBOS_VM_LOOP_COUNTER_BYTES;
        remaining = ribos_vm_value_read_u64(counter);
        if (trip_count == 0 || remaining > trip_count) {
            return RIBOS_VM_STATUS_INVALID_STATE;
        }
        if (source_block_id == latch &&
            target_block_id == header) {
            ++latch_matches;
            if (remaining == 0) {
                *violation_loop_id = index;
                return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
            }
            ribos_vm_value_write_u64(counter, remaining - 1);
            continue;
        }
        if (source_block_id == header &&
            target_block_id == body) {
            ++header_matches;
            if (remaining == 0) {
                *violation_loop_id = index;
                return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
            }
        }
        if (target_block_id == header &&
            source_block_id != latch) {
            ribos_vm_value_write_u64(counter, trip_count);
        }
    }
    if (latch_matches > 1 || header_matches > 1) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_consume_instruction_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint64_t *remaining_instructions,
    uint64_t *consumed_instructions)
{
    RibosVmStorageExecutionControl control;
    uint64_t remaining;
    uint8_t *bytes;
    RibosVmStatus status;

    if (remaining_instructions == NULL ||
        consumed_instructions == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *remaining_instructions = 0;
    *consumed_instructions = 0;
    status = ribos_vm_storage_execution_load_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &control,
        &remaining);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (remaining == 0) {
        return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
    }
    if (control.consumed_instructions == UINT64_MAX) {
        return RIBOS_VM_STATUS_INTERNAL_ERROR;
    }
    --remaining;
    ++control.consumed_instructions;
    bytes = ribos_vm_storage_mutable_bytes(storage);
    ribos_vm_value_write_u64(
        bytes +
        RIBOS_VM_STORAGE_CONTROL_REMAINING_INSTRUCTIONS_OFFSET_V1,
        remaining);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_CONTROL_CONSUMED_INSTRUCTIONS_OFFSET,
        control.consumed_instructions);
    *remaining_instructions = remaining;
    *consumed_instructions = control.consumed_instructions;
    return RIBOS_VM_STATUS_OK;
}

static RibosVmStatus
ribos_vm_storage_helper_region(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    const uint8_t **helper_bytes)
{
    RibosVmStoragePlan plan;
    size_t required_size;
    RibosVmStatus status;

    if (helper_bytes == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *helper_bytes = NULL;
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
    if (plan.regions[RIBOS_VM_STORAGE_REGION_OUTCOME].byte_size !=
            RIBOS_VM_OUTCOME_RECORD_BYTES ||
        plan.regions[RIBOS_VM_STORAGE_REGION_OUTCOME].offset >
            SIZE_MAX) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    *helper_bytes = ribos_vm_storage_const_bytes(storage) +
        (size_t)plan.regions[
            RIBOS_VM_STORAGE_REGION_OUTCOME].offset;
    return RIBOS_VM_STATUS_OK;
}

static int
ribos_vm_storage_helper_snapshot_is_valid(
    const RibosVmHelperExecutionSnapshot *snapshot)
{
    if (snapshot == NULL ||
        snapshot->size != (uint32_t)sizeof(*snapshot) ||
        snapshot->helpers_major != RIBOS_VM_HELPERS_V1_MAJOR ||
        snapshot->helpers_minor != RIBOS_VM_HELPERS_V1_MINOR ||
        snapshot->state < RIBOS_VM_HELPER_EXECUTION_READY ||
        snapshot->state > RIBOS_VM_HELPER_EXECUTION_FAULTED ||
        snapshot->selected_mode >= 64 ||
        snapshot->selected_phase >= 64 ||
        snapshot->execution_start_ns == 0 ||
        snapshot->execution_deadline_ns <
            snapshot->execution_start_ns ||
        snapshot->last_now_ns < snapshot->execution_start_ns ||
        snapshot->context_generation == 0 ||
        !ribos_vm_digest_is_nonzero(snapshot->context_digest) ||
        !ribos_vm_digest_is_nonzero(
            snapshot->helper_execution_digest) ||
        !ribos_vm_reserved_words_are_zero(snapshot->reserved, 2)) {
        return 0;
    }
    if ((snapshot->state ==
             RIBOS_VM_HELPER_EXECUTION_CALLBACK_ACTIVE) !=
            (snapshot->callback_active != 0) ||
        (snapshot->callback_active != 0) !=
            (snapshot->active_helper_id != RIBOS_VM_INVALID_ID)) {
        return 0;
    }
    if (snapshot->last_effect > RIBOS_VM_HELPER_EFFECT_TERMINAL ||
        snapshot->last_durability >
            RIBOS_VM_HELPER_DURABILITY_SEALED_INTENT ||
        snapshot->last_handle_transition >
            RIBOS_VM_HANDLE_TRANSITION_TERMINAL_CONSUME ||
        snapshot->last_result_kind >
            RIBOS_VM_HELPER_RESULT_POLICY_ERROR) {
        return 0;
    }
    if (snapshot->receipt_sequence == 0) {
        return snapshot->last_helper_id == RIBOS_VM_INVALID_ID &&
            snapshot->last_effect == RIBOS_VM_HELPER_EFFECT_NONE &&
            snapshot->last_durability ==
                RIBOS_VM_HELPER_DURABILITY_NONE &&
            snapshot->last_handle_transition ==
                RIBOS_VM_HANDLE_TRANSITION_NONE &&
            snapshot->last_result_kind ==
                RIBOS_VM_HELPER_RESULT_NONE;
    }
    return snapshot->last_helper_id != RIBOS_VM_INVALID_ID &&
        snapshot->last_callback_status <=
            RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT &&
        snapshot->last_effect >= RIBOS_VM_HELPER_EFFECT_PURE;
}

static void
ribos_vm_storage_encode_helper_snapshot(
    uint8_t *bytes,
    const RibosVmHelperExecutionSnapshot *snapshot)
{
    memset(bytes, 0, RIBOS_VM_HELPER_OUTCOME_RECORD_BYTES);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_HELPER_STATE_SIZE_OFFSET,
        snapshot->size);
    ribos_vm_value_write_u16(
        bytes + RIBOS_VM_HELPER_STATE_MAJOR_OFFSET,
        snapshot->helpers_major);
    ribos_vm_value_write_u16(
        bytes + RIBOS_VM_HELPER_STATE_MINOR_OFFSET,
        snapshot->helpers_minor);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_HELPER_STATE_LIFECYCLE_OFFSET,
        snapshot->state);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_HELPER_STATE_CALLBACK_ACTIVE_OFFSET,
        snapshot->callback_active);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_HELPER_STATE_MODE_OFFSET,
        snapshot->selected_mode);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_HELPER_STATE_PHASE_OFFSET,
        snapshot->selected_phase);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_HELPER_STATE_CAPABILITIES_OFFSET,
        snapshot->granted_capabilities);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_HELPER_STATE_ACTIVE_ID_OFFSET,
        snapshot->active_helper_id);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_HELPER_STATE_START_OFFSET,
        snapshot->execution_start_ns);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_HELPER_STATE_DEADLINE_OFFSET,
        snapshot->execution_deadline_ns);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_HELPER_STATE_LAST_NOW_OFFSET,
        snapshot->last_now_ns);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_HELPER_STATE_CONSUMED_CALLS_OFFSET,
        snapshot->consumed_helper_calls);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_HELPER_STATE_CONSUMED_INPUT_OFFSET,
        snapshot->consumed_input_bytes);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_HELPER_STATE_CONSUMED_OUTPUT_OFFSET,
        snapshot->consumed_output_bytes);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_HELPER_STATE_CONSUMED_OPERATIONS_OFFSET,
        snapshot->consumed_operations);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_HELPER_STATE_CONSUMED_POLLS_OFFSET,
        snapshot->consumed_polls);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_HELPER_STATE_RECEIPT_SEQUENCE_OFFSET,
        snapshot->receipt_sequence);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_HELPER_STATE_LAST_ID_OFFSET,
        snapshot->last_helper_id);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_HELPER_STATE_LAST_STATUS_OFFSET,
        snapshot->last_callback_status);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_HELPER_STATE_LAST_EFFECT_OFFSET,
        snapshot->last_effect);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_HELPER_STATE_LAST_DURABILITY_OFFSET,
        snapshot->last_durability);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_HELPER_STATE_LAST_TRANSITION_OFFSET,
        snapshot->last_handle_transition);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_HELPER_STATE_LAST_RESULT_OFFSET,
        snapshot->last_result_kind);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_HELPER_STATE_LAST_INPUT_OFFSET,
        snapshot->last_input_bytes);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_HELPER_STATE_LAST_OUTPUT_OFFSET,
        snapshot->last_output_bytes);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_HELPER_STATE_LAST_OPERATIONS_OFFSET,
        snapshot->last_operations);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_HELPER_STATE_LAST_POLLS_OFFSET,
        snapshot->last_polls);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_HELPER_STATE_LAST_DURATION_OFFSET,
        snapshot->last_duration_ns);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_HELPER_STATE_CONTEXT_GENERATION_OFFSET,
        snapshot->context_generation);
    memcpy(
        bytes + RIBOS_VM_HELPER_STATE_CONTEXT_DIGEST_OFFSET,
        snapshot->context_digest,
        RIBOS_VM_DIGEST_BYTES);
    memcpy(
        bytes + RIBOS_VM_HELPER_STATE_EXECUTION_DIGEST_OFFSET,
        snapshot->helper_execution_digest,
        RIBOS_VM_DIGEST_BYTES);
}

static void
ribos_vm_storage_decode_helper_snapshot(
    const uint8_t *bytes,
    RibosVmHelperExecutionSnapshot *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->size = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_HELPER_STATE_SIZE_OFFSET);
    snapshot->helpers_major = ribos_vm_value_read_u16(
        bytes + RIBOS_VM_HELPER_STATE_MAJOR_OFFSET);
    snapshot->helpers_minor = ribos_vm_value_read_u16(
        bytes + RIBOS_VM_HELPER_STATE_MINOR_OFFSET);
    snapshot->state = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_HELPER_STATE_LIFECYCLE_OFFSET);
    snapshot->callback_active = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_HELPER_STATE_CALLBACK_ACTIVE_OFFSET);
    snapshot->selected_mode = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_HELPER_STATE_MODE_OFFSET);
    snapshot->selected_phase = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_HELPER_STATE_PHASE_OFFSET);
    snapshot->granted_capabilities = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_HELPER_STATE_CAPABILITIES_OFFSET);
    snapshot->active_helper_id = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_HELPER_STATE_ACTIVE_ID_OFFSET);
    snapshot->execution_start_ns = ribos_vm_value_read_u64(
        bytes + RIBOS_VM_HELPER_STATE_START_OFFSET);
    snapshot->execution_deadline_ns = ribos_vm_value_read_u64(
        bytes + RIBOS_VM_HELPER_STATE_DEADLINE_OFFSET);
    snapshot->last_now_ns = ribos_vm_value_read_u64(
        bytes + RIBOS_VM_HELPER_STATE_LAST_NOW_OFFSET);
    snapshot->consumed_helper_calls = ribos_vm_value_read_u64(
        bytes + RIBOS_VM_HELPER_STATE_CONSUMED_CALLS_OFFSET);
    snapshot->consumed_input_bytes = ribos_vm_value_read_u64(
        bytes + RIBOS_VM_HELPER_STATE_CONSUMED_INPUT_OFFSET);
    snapshot->consumed_output_bytes = ribos_vm_value_read_u64(
        bytes + RIBOS_VM_HELPER_STATE_CONSUMED_OUTPUT_OFFSET);
    snapshot->consumed_operations = ribos_vm_value_read_u64(
        bytes + RIBOS_VM_HELPER_STATE_CONSUMED_OPERATIONS_OFFSET);
    snapshot->consumed_polls = ribos_vm_value_read_u64(
        bytes + RIBOS_VM_HELPER_STATE_CONSUMED_POLLS_OFFSET);
    snapshot->receipt_sequence = ribos_vm_value_read_u64(
        bytes + RIBOS_VM_HELPER_STATE_RECEIPT_SEQUENCE_OFFSET);
    snapshot->last_helper_id = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_HELPER_STATE_LAST_ID_OFFSET);
    snapshot->last_callback_status = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_HELPER_STATE_LAST_STATUS_OFFSET);
    snapshot->last_effect = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_HELPER_STATE_LAST_EFFECT_OFFSET);
    snapshot->last_durability = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_HELPER_STATE_LAST_DURABILITY_OFFSET);
    snapshot->last_handle_transition = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_HELPER_STATE_LAST_TRANSITION_OFFSET);
    snapshot->last_result_kind = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_HELPER_STATE_LAST_RESULT_OFFSET);
    snapshot->last_input_bytes = ribos_vm_value_read_u64(
        bytes + RIBOS_VM_HELPER_STATE_LAST_INPUT_OFFSET);
    snapshot->last_output_bytes = ribos_vm_value_read_u64(
        bytes + RIBOS_VM_HELPER_STATE_LAST_OUTPUT_OFFSET);
    snapshot->last_operations = ribos_vm_value_read_u64(
        bytes + RIBOS_VM_HELPER_STATE_LAST_OPERATIONS_OFFSET);
    snapshot->last_polls = ribos_vm_value_read_u64(
        bytes + RIBOS_VM_HELPER_STATE_LAST_POLLS_OFFSET);
    snapshot->last_duration_ns = ribos_vm_value_read_u64(
        bytes + RIBOS_VM_HELPER_STATE_LAST_DURATION_OFFSET);
    snapshot->context_generation = ribos_vm_value_read_u64(
        bytes + RIBOS_VM_HELPER_STATE_CONTEXT_GENERATION_OFFSET);
    memcpy(
        snapshot->context_digest,
        bytes + RIBOS_VM_HELPER_STATE_CONTEXT_DIGEST_OFFSET,
        RIBOS_VM_DIGEST_BYTES);
    memcpy(
        snapshot->helper_execution_digest,
        bytes + RIBOS_VM_HELPER_STATE_EXECUTION_DIGEST_OFFSET,
        RIBOS_VM_DIGEST_BYTES);
}

RibosVmStatus
ribos_vm_storage_helper_execution_initialize_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmHelperExecutionSnapshot *snapshot)
{
    const uint8_t *const_bytes;
    RibosVmStatus status;

    if (!ribos_vm_storage_helper_snapshot_is_valid(snapshot) ||
        snapshot->state != RIBOS_VM_HELPER_EXECUTION_READY ||
        snapshot->callback_active != 0 ||
        snapshot->active_helper_id != RIBOS_VM_INVALID_ID ||
        snapshot->receipt_sequence != 0) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    status = ribos_vm_storage_helper_region(
        prepared_program,
        storage,
        arena_size,
        &const_bytes);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (ribos_vm_value_read_u32(
            const_bytes + RIBOS_VM_HELPER_STATE_LIFECYCLE_OFFSET) !=
            RIBOS_VM_HELPER_EXECUTION_EMPTY) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    ribos_vm_storage_encode_helper_snapshot(
        (uint8_t *)(void *)const_bytes,
        snapshot);
    return RIBOS_VM_STATUS_OK;
}

static RibosVmStatus
ribos_vm_storage_terminal_region(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    const uint8_t **terminal_bytes)
{
    const uint8_t *outcome_bytes;
    RibosVmStatus status;

    if (terminal_bytes == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *terminal_bytes = NULL;
    status = ribos_vm_storage_helper_region(
        prepared_program,
        storage,
        arena_size,
        &outcome_bytes);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    *terminal_bytes =
        outcome_bytes + RIBOS_VM_TERMINAL_RECORD_OFFSET;
    return RIBOS_VM_STATUS_OK;
}

static int
ribos_vm_storage_terminal_snapshot_is_valid(
    const RibosVmTerminalSnapshot *snapshot)
{
    if (snapshot == NULL ||
        snapshot->size != (uint32_t)sizeof(*snapshot) ||
        snapshot->terminal_major != RIBOS_VM_TERMINAL_V1_MAJOR ||
        snapshot->terminal_minor != RIBOS_VM_TERMINAL_V1_MINOR ||
        snapshot->state < RIBOS_VM_TERMINAL_EXECUTING ||
        snapshot->state > RIBOS_VM_TERMINAL_ACTION_CONSUMED ||
        snapshot->flags != 0 ||
        snapshot->context_generation == 0 ||
        !ribos_vm_digest_is_nonzero(snapshot->binding_digest) ||
        !ribos_vm_digest_is_nonzero(snapshot->context_digest) ||
        snapshot->journal_state >
            RIBOS_VM_JOURNAL_RECEIPT_UNCERTAIN ||
        snapshot->last_journal_callback_status >
            RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT ||
        snapshot->action_consumed > 1 ||
        snapshot->recovery_notified > 1 ||
        snapshot->authority_revoked > 1) {
        return 0;
    }
    if (snapshot->journal_count == 0) {
        if (snapshot->journal_state !=
                RIBOS_VM_JOURNAL_RECEIPT_NONE ||
            snapshot->last_journal_helper_id !=
                RIBOS_VM_INVALID_ID ||
            ribos_vm_digest_is_nonzero(
                snapshot->journal_chain_digest)) {
            return 0;
        }
    } else if (snapshot->journal_state ==
                   RIBOS_VM_JOURNAL_RECEIPT_NONE ||
               snapshot->last_journal_helper_id ==
                   RIBOS_VM_INVALID_ID ||
               !ribos_vm_digest_is_nonzero(
                   snapshot->journal_chain_digest)) {
        return 0;
    }
    if (snapshot->state == RIBOS_VM_TERMINAL_ACTION_PENDING ||
        snapshot->state == RIBOS_VM_TERMINAL_ACTION_SEALED ||
        snapshot->state == RIBOS_VM_TERMINAL_ACTION_CONSUMED) {
        if (snapshot->terminal_helper_id == RIBOS_VM_INVALID_ID ||
            snapshot->action_type_id == RIBOS_VM_INVALID_ID ||
            snapshot->payload_size == 0) {
            return 0;
        }
    }
    if (snapshot->state == RIBOS_VM_TERMINAL_ACTION_SEALED ||
        snapshot->state == RIBOS_VM_TERMINAL_ACTION_CONSUMED) {
        if (snapshot->outcome_kind !=
                RIBOS_VM_OUTCOME_BOOT_ACTION ||
            !ribos_vm_digest_is_nonzero(
                snapshot->action_receipt_digest)) {
            return 0;
        }
    }
    if ((snapshot->state ==
             RIBOS_VM_TERMINAL_ACTION_CONSUMED) !=
            (snapshot->action_consumed != 0)) {
        return 0;
    }
    if (snapshot->state == RIBOS_VM_TERMINAL_POLICY_ERROR &&
        (snapshot->outcome_kind !=
             RIBOS_VM_OUTCOME_POLICY_ERROR ||
         snapshot->error_type_id == RIBOS_VM_INVALID_ID ||
         snapshot->payload_size == 0)) {
        return 0;
    }
    if (snapshot->state == RIBOS_VM_TERMINAL_VM_FAULT &&
        (snapshot->outcome_kind != RIBOS_VM_OUTCOME_VM_FAULT ||
         snapshot->authority_revoked != 1 ||
         !ribos_vm_digest_is_nonzero(snapshot->trace_digest))) {
        return 0;
    }
    return 1;
}

static void
ribos_vm_storage_encode_terminal_snapshot(
    uint8_t *bytes,
    const RibosVmTerminalSnapshot *snapshot)
{
    memset(bytes, 0, RIBOS_VM_TERMINAL_OUTCOME_RECORD_BYTES);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_TERMINAL_SIZE_OFFSET,
        snapshot->size);
    ribos_vm_value_write_u16(
        bytes + RIBOS_VM_TERMINAL_MAJOR_OFFSET,
        snapshot->terminal_major);
    ribos_vm_value_write_u16(
        bytes + RIBOS_VM_TERMINAL_MINOR_OFFSET,
        snapshot->terminal_minor);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_TERMINAL_STATE_OFFSET,
        snapshot->state);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_TERMINAL_OUTCOME_KIND_OFFSET,
        snapshot->outcome_kind);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_TERMINAL_HELPER_ID_OFFSET,
        snapshot->terminal_helper_id);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_TERMINAL_ACTION_TYPE_OFFSET,
        snapshot->action_type_id);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_TERMINAL_ERROR_TYPE_OFFSET,
        snapshot->error_type_id);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_TERMINAL_ERROR_CODE_OFFSET,
        snapshot->stable_error_code);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_TERMINAL_SOURCE_MAP_OFFSET,
        snapshot->source_map_id);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_TERMINAL_FLAGS_OFFSET,
        snapshot->flags);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_TERMINAL_ACTION_CONSUMED_OFFSET,
        snapshot->action_consumed);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_TERMINAL_RECOVERY_NOTIFIED_OFFSET,
        snapshot->recovery_notified);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_TERMINAL_AUTHORITY_REVOKED_OFFSET,
        snapshot->authority_revoked);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_TERMINAL_JOURNAL_STATE_OFFSET,
        snapshot->journal_state);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_TERMINAL_PAYLOAD_SIZE_OFFSET,
        snapshot->payload_size);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_TERMINAL_CONTEXT_GENERATION_OFFSET,
        snapshot->context_generation);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_TERMINAL_JOURNAL_SEQUENCE_OFFSET,
        snapshot->journal_sequence);
    ribos_vm_value_write_u64(
        bytes + RIBOS_VM_TERMINAL_JOURNAL_COUNT_OFFSET,
        snapshot->journal_count);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_TERMINAL_LAST_JOURNAL_HELPER_OFFSET,
        snapshot->last_journal_helper_id);
    ribos_vm_value_write_u32(
        bytes + RIBOS_VM_TERMINAL_LAST_JOURNAL_STATUS_OFFSET,
        snapshot->last_journal_callback_status);
    memcpy(
        bytes + RIBOS_VM_TERMINAL_BINDING_DIGEST_OFFSET,
        snapshot->binding_digest,
        RIBOS_VM_DIGEST_BYTES);
    memcpy(
        bytes + RIBOS_VM_TERMINAL_CONTEXT_DIGEST_OFFSET,
        snapshot->context_digest,
        RIBOS_VM_DIGEST_BYTES);
    memcpy(
        bytes + RIBOS_VM_TERMINAL_ACTION_DIGEST_OFFSET,
        snapshot->action_receipt_digest,
        RIBOS_VM_DIGEST_BYTES);
    memcpy(
        bytes + RIBOS_VM_TERMINAL_JOURNAL_DIGEST_OFFSET,
        snapshot->journal_chain_digest,
        RIBOS_VM_DIGEST_BYTES);
    memcpy(
        bytes + RIBOS_VM_TERMINAL_TRACE_DIGEST_OFFSET,
        snapshot->trace_digest,
        RIBOS_VM_DIGEST_BYTES);
}

static void
ribos_vm_storage_decode_terminal_snapshot(
    const uint8_t *bytes,
    RibosVmTerminalSnapshot *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->size = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_TERMINAL_SIZE_OFFSET);
    snapshot->terminal_major = ribos_vm_value_read_u16(
        bytes + RIBOS_VM_TERMINAL_MAJOR_OFFSET);
    snapshot->terminal_minor = ribos_vm_value_read_u16(
        bytes + RIBOS_VM_TERMINAL_MINOR_OFFSET);
    snapshot->state = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_TERMINAL_STATE_OFFSET);
    snapshot->outcome_kind = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_TERMINAL_OUTCOME_KIND_OFFSET);
    snapshot->terminal_helper_id = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_TERMINAL_HELPER_ID_OFFSET);
    snapshot->action_type_id = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_TERMINAL_ACTION_TYPE_OFFSET);
    snapshot->error_type_id = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_TERMINAL_ERROR_TYPE_OFFSET);
    snapshot->stable_error_code = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_TERMINAL_ERROR_CODE_OFFSET);
    snapshot->source_map_id = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_TERMINAL_SOURCE_MAP_OFFSET);
    snapshot->flags = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_TERMINAL_FLAGS_OFFSET);
    snapshot->action_consumed = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_TERMINAL_ACTION_CONSUMED_OFFSET);
    snapshot->recovery_notified = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_TERMINAL_RECOVERY_NOTIFIED_OFFSET);
    snapshot->authority_revoked = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_TERMINAL_AUTHORITY_REVOKED_OFFSET);
    snapshot->journal_state = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_TERMINAL_JOURNAL_STATE_OFFSET);
    snapshot->payload_size = ribos_vm_value_read_u64(
        bytes + RIBOS_VM_TERMINAL_PAYLOAD_SIZE_OFFSET);
    snapshot->context_generation = ribos_vm_value_read_u64(
        bytes + RIBOS_VM_TERMINAL_CONTEXT_GENERATION_OFFSET);
    snapshot->journal_sequence = ribos_vm_value_read_u64(
        bytes + RIBOS_VM_TERMINAL_JOURNAL_SEQUENCE_OFFSET);
    snapshot->journal_count = ribos_vm_value_read_u64(
        bytes + RIBOS_VM_TERMINAL_JOURNAL_COUNT_OFFSET);
    snapshot->last_journal_helper_id = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_TERMINAL_LAST_JOURNAL_HELPER_OFFSET);
    snapshot->last_journal_callback_status = ribos_vm_value_read_u32(
        bytes + RIBOS_VM_TERMINAL_LAST_JOURNAL_STATUS_OFFSET);
    memcpy(
        snapshot->binding_digest,
        bytes + RIBOS_VM_TERMINAL_BINDING_DIGEST_OFFSET,
        RIBOS_VM_DIGEST_BYTES);
    memcpy(
        snapshot->context_digest,
        bytes + RIBOS_VM_TERMINAL_CONTEXT_DIGEST_OFFSET,
        RIBOS_VM_DIGEST_BYTES);
    memcpy(
        snapshot->action_receipt_digest,
        bytes + RIBOS_VM_TERMINAL_ACTION_DIGEST_OFFSET,
        RIBOS_VM_DIGEST_BYTES);
    memcpy(
        snapshot->journal_chain_digest,
        bytes + RIBOS_VM_TERMINAL_JOURNAL_DIGEST_OFFSET,
        RIBOS_VM_DIGEST_BYTES);
    memcpy(
        snapshot->trace_digest,
        bytes + RIBOS_VM_TERMINAL_TRACE_DIGEST_OFFSET,
        RIBOS_VM_DIGEST_BYTES);
}

RibosVmStatus
ribos_vm_storage_terminal_initialize_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmTerminalSnapshot *snapshot)
{
    const uint8_t *bytes;
    RibosVmStatus status;

    if (!ribos_vm_storage_terminal_snapshot_is_valid(snapshot) ||
        snapshot->state != RIBOS_VM_TERMINAL_EXECUTING ||
        snapshot->outcome_kind != 0 ||
        snapshot->terminal_helper_id != RIBOS_VM_INVALID_ID ||
        snapshot->action_type_id != RIBOS_VM_INVALID_ID ||
        snapshot->error_type_id != RIBOS_VM_INVALID_ID ||
        snapshot->last_journal_helper_id != RIBOS_VM_INVALID_ID) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    status = ribos_vm_storage_terminal_region(
        prepared_program,
        storage,
        arena_size,
        &bytes);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (ribos_vm_value_read_u32(
            bytes + RIBOS_VM_TERMINAL_STATE_OFFSET) !=
            RIBOS_VM_TERMINAL_EMPTY) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    ribos_vm_storage_encode_terminal_snapshot(
        (uint8_t *)(void *)bytes,
        snapshot);
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_terminal_load_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    RibosVmTerminalSnapshot *snapshot)
{
    const uint8_t *bytes;
    RibosVmStatus status;

    if (snapshot == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    status = ribos_vm_storage_terminal_region(
        prepared_program,
        storage,
        arena_size,
        &bytes);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    ribos_vm_storage_decode_terminal_snapshot(bytes, snapshot);
    if (!ribos_vm_storage_terminal_snapshot_is_valid(snapshot)) {
        memset(snapshot, 0, sizeof(*snapshot));
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_terminal_store_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmTerminalSnapshot *snapshot)
{
    const uint8_t *bytes;
    RibosVmTerminalSnapshot existing;
    RibosVmStatus status;

    if (!ribos_vm_storage_terminal_snapshot_is_valid(snapshot)) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    status = ribos_vm_storage_terminal_load_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &existing);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (existing.context_generation != snapshot->context_generation ||
        memcmp(
            existing.binding_digest,
            snapshot->binding_digest,
            RIBOS_VM_DIGEST_BYTES) != 0 ||
        memcmp(
            existing.context_digest,
            snapshot->context_digest,
            RIBOS_VM_DIGEST_BYTES) != 0 ||
        snapshot->journal_sequence < existing.journal_sequence ||
        snapshot->journal_count < existing.journal_count ||
        snapshot->state < existing.state ||
        (existing.outcome_kind != 0 &&
         existing.outcome_kind != snapshot->outcome_kind) ||
        (existing.action_consumed != 0 &&
         snapshot->action_consumed == 0) ||
        (existing.recovery_notified != 0 &&
         snapshot->recovery_notified == 0) ||
        (existing.authority_revoked != 0 &&
         snapshot->authority_revoked == 0)) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    status = ribos_vm_storage_terminal_region(
        prepared_program,
        storage,
        arena_size,
        &bytes);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    ribos_vm_storage_encode_terminal_snapshot(
        (uint8_t *)(void *)bytes,
        snapshot);
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_helper_execution_load_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHelperExecutionSnapshot *snapshot)
{
    const uint8_t *bytes;
    RibosVmStatus status;

    if (snapshot == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    status = ribos_vm_storage_helper_region(
        prepared_program,
        storage,
        arena_size,
        &bytes);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    ribos_vm_storage_decode_helper_snapshot(bytes, snapshot);
    if (!ribos_vm_storage_helper_snapshot_is_valid(snapshot)) {
        memset(snapshot, 0, sizeof(*snapshot));
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_helper_execution_store_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmHelperExecutionSnapshot *snapshot)
{
    const uint8_t *const_bytes;
    RibosVmHelperExecutionSnapshot existing;
    RibosVmStatus status;

    if (!ribos_vm_storage_helper_snapshot_is_valid(snapshot)) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    status = ribos_vm_storage_helper_execution_load_internal_v1(
        prepared_program,
        storage,
        arena_size,
        &existing);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (existing.context_generation != snapshot->context_generation ||
        memcmp(
            existing.context_digest,
            snapshot->context_digest,
            RIBOS_VM_DIGEST_BYTES) != 0 ||
        memcmp(
            existing.helper_execution_digest,
            snapshot->helper_execution_digest,
            RIBOS_VM_DIGEST_BYTES) != 0 ||
        existing.selected_mode != snapshot->selected_mode ||
        existing.selected_phase != snapshot->selected_phase ||
        existing.granted_capabilities !=
            snapshot->granted_capabilities ||
        snapshot->consumed_helper_calls <
            existing.consumed_helper_calls ||
        snapshot->consumed_input_bytes <
            existing.consumed_input_bytes ||
        snapshot->consumed_output_bytes <
            existing.consumed_output_bytes ||
        snapshot->consumed_operations <
            existing.consumed_operations ||
        snapshot->consumed_polls < existing.consumed_polls ||
        snapshot->receipt_sequence < existing.receipt_sequence ||
        snapshot->last_now_ns < existing.last_now_ns) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    status = ribos_vm_storage_helper_region(
        prepared_program,
        storage,
        arena_size,
        &const_bytes);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    ribos_vm_storage_encode_helper_snapshot(
        (uint8_t *)(void *)const_bytes,
        snapshot);
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_consume_helper_call_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t stable_id)
{
    RibosVmStoragePlan plan;
    size_t required_size;
    uint8_t *bytes;
    uint8_t *counters;
    uint64_t total_remaining;
    uint32_t index;
    RibosVmStatus status;

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
    bytes = ribos_vm_storage_mutable_bytes(storage);
    total_remaining = ribos_vm_value_read_u64(
        bytes + RIBOS_VM_CONTROL_REMAINING_HELPERS_OFFSET);
    if (total_remaining == 0) {
        return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
    }
    counters = bytes +
        (size_t)plan.regions[
            RIBOS_VM_STORAGE_REGION_HELPER_COUNTERS].offset;
    for (index = 0; index < plan.helper_count; ++index) {
        uint8_t *counter =
            counters + (size_t)index * RIBOS_VM_HELPER_COUNTER_BYTES;
        uint64_t remaining;
        uint32_t consumed;

        if (ribos_vm_value_read_u32(counter) != stable_id) {
            continue;
        }
        remaining = ribos_vm_value_read_u64(counter + 8);
        consumed = ribos_vm_value_read_u32(counter + 4);
        if (remaining == 0) {
            return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
        }
        if (consumed == UINT32_MAX) {
            return RIBOS_VM_STATUS_INTERNAL_ERROR;
        }
        ribos_vm_value_write_u64(
            bytes + RIBOS_VM_CONTROL_REMAINING_HELPERS_OFFSET,
            total_remaining - 1);
        ribos_vm_value_write_u32(counter + 4, consumed + 1);
        ribos_vm_value_write_u64(counter + 8, remaining - 1);
        return RIBOS_VM_STATUS_OK;
    }
    return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
}

static RibosVmStatus
ribos_vm_storage_consume_external_budget(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t offset,
    uint64_t count)
{
    uint8_t *bytes;
    uint64_t remaining;
    RibosVmStatus status;

    if (count == 0) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    status = ribos_vm_storage_validate_v1(
        prepared_program,
        storage,
        arena_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    bytes = ribos_vm_storage_mutable_bytes(storage);
    remaining = ribos_vm_value_read_u64(bytes + offset);
    if (remaining < count) {
        return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
    }
    ribos_vm_value_write_u64(bytes + offset, remaining - count);
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_consume_operations_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint64_t count)
{
    return ribos_vm_storage_consume_external_budget(
        prepared_program,
        storage,
        arena_size,
        RIBOS_VM_CONTROL_REMAINING_OPERATIONS_OFFSET,
        count);
}

RibosVmStatus
ribos_vm_storage_consume_polls_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint64_t count)
{
    return ribos_vm_storage_consume_external_budget(
        prepared_program,
        storage,
        arena_size,
        RIBOS_VM_CONTROL_REMAINING_POLLS_OFFSET,
        count);
}

static RibosVmStatus
ribos_vm_storage_output_region(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    const uint8_t **output_bytes,
    size_t *output_capacity)
{
    RibosVmStoragePlan plan;
    size_t required_size;
    RibosVmStatus status;

    if (output_bytes == NULL || output_capacity == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *output_bytes = NULL;
    *output_capacity = 0;
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
    if (plan.regions[RIBOS_VM_STORAGE_REGION_OUTPUT].offset >
            SIZE_MAX ||
        plan.regions[RIBOS_VM_STORAGE_REGION_OUTPUT].byte_size >
            SIZE_MAX) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    *output_bytes = ribos_vm_storage_const_bytes(storage) +
        (size_t)plan.regions[
            RIBOS_VM_STORAGE_REGION_OUTPUT].offset;
    *output_capacity = (size_t)plan.regions[
        RIBOS_VM_STORAGE_REGION_OUTPUT].byte_size;
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_output_write_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const uint8_t *bytes,
    size_t byte_size)
{
    const uint8_t *output;
    size_t capacity;
    RibosVmStatus status;

    if (byte_size == 0 || bytes == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    status = ribos_vm_storage_output_region(
        prepared_program,
        storage,
        arena_size,
        &output,
        &capacity);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (byte_size > capacity) {
        return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
    }
    memset((uint8_t *)(void *)output, 0, capacity);
    memcpy((uint8_t *)(void *)output, bytes, byte_size);
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_output_view_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    size_t byte_size,
    const uint8_t **bytes)
{
    const uint8_t *output;
    size_t capacity;
    RibosVmStatus status;

    if (bytes == NULL || byte_size == 0) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *bytes = NULL;
    status = ribos_vm_storage_output_region(
        prepared_program,
        storage,
        arena_size,
        &output,
        &capacity);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (byte_size > capacity) {
        return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
    }
    *bytes = output;
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_output_zero_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size)
{
    const uint8_t *output;
    size_t capacity;
    RibosVmStatus status;

    status = ribos_vm_storage_output_region(
        prepared_program,
        storage,
        arena_size,
        &output,
        &capacity);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (capacity != 0) {
        memset((uint8_t *)(void *)output, 0, capacity);
    }
    return RIBOS_VM_STATUS_OK;
}

static RibosVmStatus
ribos_vm_storage_fault_region(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    const uint8_t **fault_bytes)
{
    RibosVmStoragePlan plan;
    size_t required_size;
    RibosVmStatus status;

    if (fault_bytes == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *fault_bytes = NULL;
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
    if (plan.regions[RIBOS_VM_STORAGE_REGION_FAULT].byte_size !=
            RIBOS_VM_FAULT_RECORD_BYTES ||
        plan.regions[RIBOS_VM_STORAGE_REGION_FAULT].offset >
            SIZE_MAX) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    *fault_bytes = ribos_vm_storage_const_bytes(storage) +
        (size_t)plan.regions[
            RIBOS_VM_STORAGE_REGION_FAULT].offset;
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_seal_fault_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmFaultReceipt *receipt)
{
    const uint8_t *const_fault_bytes;
    uint8_t *fault_bytes;
    RibosVmStatus status;

    if (receipt == NULL ||
        receipt->fault_code < RIBOS_VM_FAULT_INTERNAL ||
        receipt->fault_code > RIBOS_VM_FAULT_RECOVERY ||
        receipt->subject < RIBOS_VM_FAULT_SUBJECT_RUNTIME ||
        receipt->subject > RIBOS_VM_FAULT_SUBJECT_RECOVERY ||
        receipt->last_effect > RIBOS_VM_HELPER_EFFECT_TERMINAL ||
        receipt->last_durability >
            RIBOS_VM_HELPER_DURABILITY_SEALED_INTENT ||
        !ribos_vm_digest_is_nonzero(receipt->artifact_hash) ||
        !ribos_vm_reserved_words_are_zero(receipt->reserved, 2)) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    status = ribos_vm_storage_fault_region(
        prepared_program,
        storage,
        arena_size,
        &const_fault_bytes);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (ribos_vm_value_read_u32(
            const_fault_bytes + RIBOS_VM_FAULT_SEALED_OFFSET) != 0) {
        return RIBOS_VM_STATUS_ALREADY_CONSUMED;
    }
    fault_bytes = (uint8_t *)(void *)const_fault_bytes;
    memset(fault_bytes, 0, RIBOS_VM_FAULT_RECORD_BYTES);
    ribos_vm_value_write_u32(
        fault_bytes + RIBOS_VM_FAULT_CODE_OFFSET,
        receipt->fault_code);
    ribos_vm_value_write_u32(
        fault_bytes + RIBOS_VM_FAULT_SUBJECT_OFFSET,
        receipt->subject);
    ribos_vm_value_write_u32(
        fault_bytes + RIBOS_VM_FAULT_FUNCTION_OFFSET,
        receipt->function_id);
    ribos_vm_value_write_u32(
        fault_bytes + RIBOS_VM_FAULT_INSTRUCTION_OFFSET,
        receipt->instruction_id);
    ribos_vm_value_write_u32(
        fault_bytes + RIBOS_VM_FAULT_HELPER_OFFSET,
        receipt->helper_id);
    ribos_vm_value_write_u32(
        fault_bytes + RIBOS_VM_FAULT_DETAIL_OFFSET,
        receipt->detail);
    ribos_vm_value_write_u32(
        fault_bytes + RIBOS_VM_FAULT_LAST_EFFECT_OFFSET,
        receipt->last_effect);
    ribos_vm_value_write_u32(
        fault_bytes + RIBOS_VM_FAULT_LAST_DURABILITY_OFFSET,
        receipt->last_durability);
    ribos_vm_value_write_u64(
        fault_bytes + RIBOS_VM_FAULT_CONSUMED_INSTRUCTIONS_OFFSET,
        receipt->consumed_instructions);
    ribos_vm_value_write_u64(
        fault_bytes + RIBOS_VM_FAULT_CONSUMED_HELPERS_OFFSET,
        receipt->consumed_helper_calls);
    ribos_vm_value_write_u64(
        fault_bytes + RIBOS_VM_FAULT_CONSUMED_INPUT_OFFSET,
        receipt->consumed_input_bytes);
    ribos_vm_value_write_u64(
        fault_bytes + RIBOS_VM_FAULT_CONSUMED_OUTPUT_OFFSET,
        receipt->consumed_output_bytes);
    ribos_vm_value_write_u64(
        fault_bytes + RIBOS_VM_FAULT_CONSUMED_OPERATIONS_OFFSET,
        receipt->consumed_operations);
    ribos_vm_value_write_u64(
        fault_bytes + RIBOS_VM_FAULT_CONSUMED_POLLS_OFFSET,
        receipt->consumed_polls);
    ribos_vm_value_write_u64(
        fault_bytes + RIBOS_VM_FAULT_ELAPSED_OFFSET,
        receipt->elapsed_ns);
    memcpy(
        fault_bytes + RIBOS_VM_FAULT_ARTIFACT_HASH_OFFSET,
        receipt->artifact_hash,
        RIBOS_VM_DIGEST_BYTES);
    memcpy(
        fault_bytes + RIBOS_VM_FAULT_TRACE_DIGEST_OFFSET,
        receipt->trace_digest,
        RIBOS_VM_DIGEST_BYTES);
    ribos_vm_value_write_u32(
        fault_bytes + RIBOS_VM_FAULT_RECOVERY_NOTIFIED_OFFSET,
        0);
    ribos_vm_value_write_u32(
        fault_bytes + RIBOS_VM_FAULT_SEALED_OFFSET,
        1);
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_read_fault_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    RibosVmFaultReceipt *receipt)
{
    const uint8_t *fault_bytes;
    RibosVmStatus status;

    if (receipt == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    memset(receipt, 0, sizeof(*receipt));
    status = ribos_vm_storage_fault_region(
        prepared_program,
        storage,
        arena_size,
        &fault_bytes);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (ribos_vm_value_read_u32(
            fault_bytes + RIBOS_VM_FAULT_SEALED_OFFSET) != 1 ||
        ribos_vm_value_read_u32(
            fault_bytes +
            RIBOS_VM_FAULT_RECOVERY_NOTIFIED_OFFSET) > 1) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    receipt->fault_code = ribos_vm_value_read_u32(
        fault_bytes + RIBOS_VM_FAULT_CODE_OFFSET);
    receipt->subject = ribos_vm_value_read_u32(
        fault_bytes + RIBOS_VM_FAULT_SUBJECT_OFFSET);
    receipt->function_id = ribos_vm_value_read_u32(
        fault_bytes + RIBOS_VM_FAULT_FUNCTION_OFFSET);
    receipt->instruction_id = ribos_vm_value_read_u32(
        fault_bytes + RIBOS_VM_FAULT_INSTRUCTION_OFFSET);
    receipt->helper_id = ribos_vm_value_read_u32(
        fault_bytes + RIBOS_VM_FAULT_HELPER_OFFSET);
    receipt->detail = ribos_vm_value_read_u32(
        fault_bytes + RIBOS_VM_FAULT_DETAIL_OFFSET);
    receipt->last_effect = ribos_vm_value_read_u32(
        fault_bytes + RIBOS_VM_FAULT_LAST_EFFECT_OFFSET);
    receipt->last_durability = ribos_vm_value_read_u32(
        fault_bytes + RIBOS_VM_FAULT_LAST_DURABILITY_OFFSET);
    receipt->consumed_instructions = ribos_vm_value_read_u64(
        fault_bytes + RIBOS_VM_FAULT_CONSUMED_INSTRUCTIONS_OFFSET);
    receipt->consumed_helper_calls = ribos_vm_value_read_u64(
        fault_bytes + RIBOS_VM_FAULT_CONSUMED_HELPERS_OFFSET);
    receipt->consumed_input_bytes = ribos_vm_value_read_u64(
        fault_bytes + RIBOS_VM_FAULT_CONSUMED_INPUT_OFFSET);
    receipt->consumed_output_bytes = ribos_vm_value_read_u64(
        fault_bytes + RIBOS_VM_FAULT_CONSUMED_OUTPUT_OFFSET);
    receipt->consumed_operations = ribos_vm_value_read_u64(
        fault_bytes + RIBOS_VM_FAULT_CONSUMED_OPERATIONS_OFFSET);
    receipt->consumed_polls = ribos_vm_value_read_u64(
        fault_bytes + RIBOS_VM_FAULT_CONSUMED_POLLS_OFFSET);
    receipt->elapsed_ns = ribos_vm_value_read_u64(
        fault_bytes + RIBOS_VM_FAULT_ELAPSED_OFFSET);
    memcpy(
        receipt->artifact_hash,
        fault_bytes + RIBOS_VM_FAULT_ARTIFACT_HASH_OFFSET,
        RIBOS_VM_DIGEST_BYTES);
    memcpy(
        receipt->trace_digest,
        fault_bytes + RIBOS_VM_FAULT_TRACE_DIGEST_OFFSET,
        RIBOS_VM_DIGEST_BYTES);
    if (receipt->fault_code < RIBOS_VM_FAULT_INTERNAL ||
        receipt->fault_code > RIBOS_VM_FAULT_RECOVERY ||
        receipt->subject < RIBOS_VM_FAULT_SUBJECT_RUNTIME ||
        receipt->subject > RIBOS_VM_FAULT_SUBJECT_RECOVERY ||
        receipt->last_effect > RIBOS_VM_HELPER_EFFECT_TERMINAL ||
        receipt->last_durability >
            RIBOS_VM_HELPER_DURABILITY_SEALED_INTENT ||
        !ribos_vm_digest_is_nonzero(receipt->artifact_hash)) {
        memset(receipt, 0, sizeof(*receipt));
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_fault_recovery_mark_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size)
{
    const uint8_t *const_fault_bytes;
    uint8_t *fault_bytes;
    RibosVmStatus status;

    status = ribos_vm_storage_fault_region(
        prepared_program,
        storage,
        arena_size,
        &const_fault_bytes);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (ribos_vm_value_read_u32(
            const_fault_bytes + RIBOS_VM_FAULT_SEALED_OFFSET) != 1) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    if (ribos_vm_value_read_u32(
            const_fault_bytes +
            RIBOS_VM_FAULT_RECOVERY_NOTIFIED_OFFSET) != 0) {
        return RIBOS_VM_STATUS_ALREADY_CONSUMED;
    }
    fault_bytes = (uint8_t *)(void *)const_fault_bytes;
    ribos_vm_value_write_u32(
        fault_bytes + RIBOS_VM_FAULT_RECOVERY_NOTIFIED_OFFSET,
        1);
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_storage_fault_recovery_state_internal_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    uint32_t *notified)
{
    const uint8_t *fault_bytes;
    RibosVmStatus status;

    if (notified == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *notified = 0;
    status = ribos_vm_storage_fault_region(
        prepared_program,
        storage,
        arena_size,
        &fault_bytes);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (ribos_vm_value_read_u32(
            fault_bytes + RIBOS_VM_FAULT_SEALED_OFFSET) != 1) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    *notified = ribos_vm_value_read_u32(
        fault_bytes + RIBOS_VM_FAULT_RECOVERY_NOTIFIED_OFFSET);
    return *notified <= 1 ?
        RIBOS_VM_STATUS_OK :
        RIBOS_VM_STATUS_INVALID_STATE;
}

RibosVmStatus
ribos_vm_storage_fault_trace_digest_internal_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    const uint8_t trace_digest[RIBOS_VM_DIGEST_BYTES])
{
    const uint8_t *const_fault_bytes;
    uint8_t *fault_bytes;
    RibosVmStatus status;

    if (!ribos_vm_digest_is_nonzero(trace_digest)) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    status = ribos_vm_storage_fault_region(
        prepared_program,
        storage,
        arena_size,
        &const_fault_bytes);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (ribos_vm_value_read_u32(
            const_fault_bytes + RIBOS_VM_FAULT_SEALED_OFFSET) != 1 ||
        ribos_vm_value_read_u32(
            const_fault_bytes +
            RIBOS_VM_FAULT_RECOVERY_NOTIFIED_OFFSET) != 0 ||
        ribos_vm_digest_is_nonzero(
            const_fault_bytes +
            RIBOS_VM_FAULT_TRACE_DIGEST_OFFSET)) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    fault_bytes = (uint8_t *)(void *)const_fault_bytes;
    memcpy(
        fault_bytes + RIBOS_VM_FAULT_TRACE_DIGEST_OFFSET,
        trace_digest,
        RIBOS_VM_DIGEST_BYTES);
    return RIBOS_VM_STATUS_OK;
}
