#ifndef RIBOS_VM_STORAGE_H
#define RIBOS_VM_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#include "ribos/vm/prepared.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file storage.h
 * @brief PreparedProgram용 bounded runtime arena와 typed value storage 계약.
 *
 * Plan은 process-local C ABI지만 모든 layout 수치는 fixed-width byte offset이다.
 * Arena 내부에는 native pointer, packed wire structure와 host-width value가 없다.
 */

#define RIBOS_VM_STORAGE_V1_MAJOR 1u
#define RIBOS_VM_STORAGE_V1_MINOR 0u
#define RIBOS_VM_STORAGE_ALIGNMENT_V1 8u
#define RIBOS_VM_STORAGE_REGION_COUNT_V1 12u
#define RIBOS_VM_RUNTIME_MAX_ARENA_BYTES_V1 \
    (UINT64_C(128) * UINT64_C(1024) * UINT64_C(1024))

/** Runtime arena의 고정된 region ordering이다. */
typedef enum RibosVmStorageRegionKind {
    RIBOS_VM_STORAGE_REGION_CONTROL = 0,
    RIBOS_VM_STORAGE_REGION_FRAMES = 1,
    RIBOS_VM_STORAGE_REGION_FRAME_VALUES = 2,
    RIBOS_VM_STORAGE_REGION_SLOT_STATES = 3,
    RIBOS_VM_STORAGE_REGION_LOOP_COUNTERS = 4,
    RIBOS_VM_STORAGE_REGION_HELPER_COUNTERS = 5,
    RIBOS_VM_STORAGE_REGION_HANDLES = 6,
    RIBOS_VM_STORAGE_REGION_AGGREGATE_SCRATCH = 7,
    RIBOS_VM_STORAGE_REGION_OUTCOME = 8,
    RIBOS_VM_STORAGE_REGION_OUTPUT = 9,
    RIBOS_VM_STORAGE_REGION_FAULT = 10,
    RIBOS_VM_STORAGE_REGION_TRACE = 11
} RibosVmStorageRegionKind;

/** Initialization 시 선택할 수 있는 diagnostic-only 동작이다. */
typedef enum RibosVmStorageInitializeFlags {
    RIBOS_VM_STORAGE_INITIALIZE_DIAGNOSTIC_POISON = 1u << 0
} RibosVmStorageInitializeFlags;

/** Runtime slot의 stable initialization/ownership 상태다. */
typedef enum RibosVmSlotStorageState {
    RIBOS_VM_SLOT_STORAGE_UNINITIALIZED = 0,
    RIBOS_VM_SLOT_STORAGE_INITIALIZED = 1,
    RIBOS_VM_SLOT_STORAGE_MOVED = 2
} RibosVmSlotStorageState;

/** Arena 안 한 region의 architecture-neutral layout이다. */
typedef struct RibosVmStorageRegion {
    uint64_t offset;
    uint64_t byte_size;
    uint32_t count;
    uint32_t stride;
} RibosVmStorageRegion;

/**
 * PreparedProgram과 effective product limit에서 재현 가능한 exact arena plan이다.
 *
 * `effective_arena_limit`는 generic absolute cap과 product/mode cap의 작은 값이다.
 * `required_bytes`는 그 limit 안에서 caller가 반드시 제공해야 하는 최소 크기다.
 */
typedef struct RibosVmStoragePlan {
    uint32_t size;
    uint16_t storage_major;
    uint16_t storage_minor;
    uint32_t flags;
    uint32_t region_count;
    uint64_t required_bytes;
    uint64_t effective_arena_limit;
    uint64_t maximum_value_bytes;
    uint32_t entry_function;
    uint32_t function_count;
    uint32_t slot_count;
    uint32_t loop_count;
    uint32_t helper_count;
    uint32_t call_depth;
    uint32_t handle_count;
    uint32_t trace_count;
    uint32_t reserved0;
    RibosVmStorageRegion regions[RIBOS_VM_STORAGE_REGION_COUNT_V1];
    uint64_t reserved[4];
} RibosVmStoragePlan;

/** Caller-owned arena 시작에 생성되는 opaque runtime storage다. */
typedef struct RibosVmStorage RibosVmStorage;

/** Runtime arena가 요구하는 최소 pointer alignment를 반환한다. */
size_t ribos_vm_runtime_alignment_v1(void);

/**
 * PreparedProgram의 exact runtime layout과 caller allocation byte 수를 계산한다.
 *
 * Artifact의 verified exact closure와 product/mode limit을 교차 적용한다. Query는
 * runtime state를 만들거나 arena를 변경하지 않는다.
 */
RibosVmStatus ribos_vm_runtime_size_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStoragePlan *plan,
    size_t *required_size);

/**
 * Caller-owned arena를 plan대로 초기화하고 opaque storage를 만든다.
 *
 * `plan`은 같은 PreparedProgram에서 계산한 exact plan이어야 하며 arena는 최소
 * `ribos_vm_runtime_alignment_v1()`로 정렬되어야 한다.
 */
RibosVmStatus ribos_vm_storage_initialize_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStoragePlan *plan,
    void *arena,
    size_t arena_size,
    uint32_t flags,
    RibosVmStorage **storage);

/** Storage header, plan, binding digest와 caller capacity를 다시 검증한다. */
RibosVmStatus ribos_vm_storage_validate_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size);

/**
 * 한 direct-call frame의 value bytes와 global slot state를 초기 상태로 되돌린다.
 *
 * `frame_base`는 frame-value region 내부 offset이며 artifact function frame alignment를
 * 만족해야 한다.
 */
RibosVmStatus ribos_vm_storage_reset_frame_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base);

/** Slot의 exact artifact type size만큼 bytes를 저장하고 initialized로 표시한다. */
RibosVmStatus ribos_vm_storage_slot_write_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    const uint8_t *bytes,
    size_t byte_size);

/** Initialized slot의 exact artifact type bytes를 caller buffer로 복사한다. */
RibosVmStatus ribos_vm_storage_slot_read_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    uint8_t *bytes,
    size_t byte_size);

/** Initialized affine/linear slot을 moved state로 바꾸고 선택적으로 poison한다. */
RibosVmStatus ribos_vm_storage_slot_mark_moved_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id);

/** Slot의 runtime state를 pointer-free stable registry 값으로 반환한다. */
RibosVmStatus ribos_vm_storage_slot_state_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    uint32_t slot_id,
    uint32_t *state);

/** Artifact unsigned scalar slot에 canonical little-endian 값을 저장한다. */
RibosVmStatus ribos_vm_storage_slot_store_unsigned_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    uint16_t bit_width,
    uint64_t value);

/** Artifact unsigned scalar slot에서 canonical little-endian 값을 읽는다. */
RibosVmStatus ribos_vm_storage_slot_load_unsigned_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    uint16_t bit_width,
    uint64_t *value);

/** Artifact signed scalar slot에 범위 검사한 canonical little-endian 값을 저장한다. */
RibosVmStatus ribos_vm_storage_slot_store_signed_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    uint16_t bit_width,
    int64_t value);

/** Artifact signed scalar slot에서 sign-extended 값을 읽는다. */
RibosVmStatus ribos_vm_storage_slot_load_signed_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    uint16_t bit_width,
    int64_t *value);

/** Artifact bool slot에 canonical 0 또는 1을 저장한다. */
RibosVmStatus ribos_vm_storage_slot_store_bool_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    uint32_t value);

/** Artifact bool slot에서 canonical 0 또는 1을 읽는다. */
RibosVmStatus ribos_vm_storage_slot_load_bool_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    uint32_t function_id,
    uint64_t frame_base,
    uint32_t slot_id,
    uint32_t *value);

#ifdef __cplusplus
}
#endif

#endif
