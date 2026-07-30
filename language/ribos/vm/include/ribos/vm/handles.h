#ifndef RIBOS_VM_HANDLES_H
#define RIBOS_VM_HANDLES_H

#include <stddef.h>
#include <stdint.h>

#include "ribos/vm/storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file handles.h
 * @brief Opaque Ribos value용 fixed-capacity generation handle 계약.
 *
 * Ribos slot에는 little-endian `(index, generation)` token만 저장한다. Trusted
 * object pointer와 drop callback은 embedder-owned host entry에만 존재한다.
 */

#define RIBOS_VM_HANDLES_V1_MAJOR 1u
#define RIBOS_VM_HANDLES_V1_MINOR 0u
#define RIBOS_VM_HANDLE_TOKEN_BYTES_V1 8u

/** Arena handle record의 stable lifecycle다. */
typedef enum RibosVmHandleLifecycle {
    RIBOS_VM_HANDLE_EMPTY = 0,
    RIBOS_VM_HANDLE_AVAILABLE = 1,
    RIBOS_VM_HANDLE_BORROWED = 2,
    RIBOS_VM_HANDLE_IN_FLIGHT = 3,
    RIBOS_VM_HANDLE_REVOKED = 4
} RibosVmHandleLifecycle;

/** Bounded drop callback의 stable 결과다. */
typedef enum RibosVmHandleDropStatus {
    RIBOS_VM_HANDLE_DROP_COMPLETE = 0,
    RIBOS_VM_HANDLE_DROP_FAILED = 1
} RibosVmHandleDropStatus;

/** Consume 완료 시 trusted object를 처리하는 방식이다. */
typedef enum RibosVmHandleConsumeDisposition {
    RIBOS_VM_HANDLE_CONSUME_TRANSFERRED = 0,
    RIBOS_VM_HANDLE_CONSUME_DROP = 1
} RibosVmHandleConsumeDisposition;

/**
 * Runtime fault/revoke에서 trusted object를 최대 한 번 정리하는 callback이다.
 *
 * Callback은 VM에 재진입하거나 pointer를 보존할 수 없다. 실행 시간과 외부 effect
 * 상한은 embedder helper contract가 별도로 보장해야 한다.
 */
typedef uint32_t (*RibosVmHandleDropFn)(
    void *drop_context,
    void *trusted_object);

/**
 * Embedder가 소유하는 process-local object binding 하나다.
 *
 * 이 structure는 wire/arena image가 아니며 native pointer를 직렬화하지 않는다.
 */
typedef struct RibosVmHandleHostEntry {
    void *trusted_object;
    void *drop_context;
    RibosVmHandleDropFn drop;
    uint32_t generation;
    uint32_t flags;
    uint64_t reserved[2];
} RibosVmHandleHostEntry;

/** Caller가 고정 용량 entry array와 결합하는 process-local host table이다. */
typedef struct RibosVmHandleHostTable {
    uint32_t size;
    uint16_t handles_major;
    uint16_t handles_minor;
    uint32_t flags;
    uint32_t capacity;
    RibosVmHandleHostEntry *entries;
    uint64_t reserved[4];
} RibosVmHandleHostTable;

/** Pointer를 노출하지 않는 한 handle record snapshot이다. */
typedef struct RibosVmHandleSnapshot {
    uint32_t size;
    uint16_t handles_major;
    uint16_t handles_minor;
    uint32_t index;
    uint32_t generation;
    uint32_t lifecycle;
    uint32_t type_id;
    uint32_t ownership;
    uint32_t move_count;
    uint64_t reserved[2];
} RibosVmHandleSnapshot;

/** Helper-call 기간에만 trusted object를 노출하는 borrow lease다. */
typedef struct RibosVmHandleBorrow {
    uint32_t size;
    uint16_t handles_major;
    uint16_t handles_minor;
    uint32_t index;
    uint32_t generation;
    uint32_t type_id;
    uint32_t ownership;
    void *trusted_object;
    uint64_t reserved[2];
} RibosVmHandleBorrow;

/**
 * Consume 시작 뒤 old token을 부활시키지 않는 in-flight lease다.
 *
 * `generation`은 consume 시작에서 회전한 새 generation이다.
 */
typedef struct RibosVmHandleConsumeLease {
    uint32_t size;
    uint16_t handles_major;
    uint16_t handles_minor;
    uint32_t index;
    uint32_t generation;
    uint32_t source_type_id;
    uint32_t ownership;
    void *trusted_object;
    uint64_t reserved[2];
} RibosVmHandleConsumeLease;

/** Bounded cleanup 결과의 pointer-free count다. */
typedef struct RibosVmHandleCleanupReport {
    uint32_t size;
    uint16_t handles_major;
    uint16_t handles_minor;
    uint32_t scanned;
    uint32_t revoked;
    uint32_t drop_calls;
    uint32_t drop_failures;
    uint64_t reserved[2];
} RibosVmHandleCleanupReport;

/** Caller-owned host entry array를 zero-initialize하고 table ABI를 봉인한다. */
RibosVmStatus ribos_vm_handle_host_table_initialize_v1(
    RibosVmHandleHostTable *table,
    RibosVmHandleHostEntry *entries,
    uint32_t capacity);

/** Token을 canonical 8-byte little-endian value로 기록한다. */
RibosVmStatus ribos_vm_handle_token_encode_v1(
    uint32_t index,
    uint32_t generation,
    uint8_t bytes[RIBOS_VM_HANDLE_TOKEN_BYTES_V1]);

/** Canonical token에서 index와 nonzero generation을 읽는다. */
RibosVmStatus ribos_vm_handle_token_decode_v1(
    const uint8_t bytes[RIBOS_VM_HANDLE_TOKEN_BYTES_V1],
    uint32_t *index,
    uint32_t *generation);

/**
 * Verified opaque type과 trusted object를 새 generation record에 결합한다.
 *
 * Linear type은 non-null drop callback을 요구한다. 반환 token 외에는 trusted
 * pointer가 Ribos value storage에 기록되지 않는다.
 */
RibosVmStatus ribos_vm_handle_create_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHandleHostTable *host_table,
    uint32_t type_id,
    void *trusted_object,
    RibosVmHandleDropFn drop,
    void *drop_context,
    uint8_t token[RIBOS_VM_HANDLE_TOKEN_BYTES_V1]);

/** Token의 generation, lifecycle, type와 ownership을 pointer 없이 검사한다. */
RibosVmStatus ribos_vm_handle_lookup_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmHandleHostTable *host_table,
    const uint8_t token[RIBOS_VM_HANDLE_TOKEN_BYTES_V1],
    uint32_t expected_type_id,
    RibosVmHandleSnapshot *snapshot);

/** Available token을 callback 기간의 single borrow로 전환한다. */
RibosVmStatus ribos_vm_handle_borrow_begin_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHandleHostTable *host_table,
    const uint8_t token[RIBOS_VM_HANDLE_TOKEN_BYTES_V1],
    uint32_t expected_type_id,
    RibosVmHandleBorrow *borrow);

/** 정확히 일치하는 borrow lease를 available 상태로 닫는다. */
RibosVmStatus ribos_vm_handle_borrow_end_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHandleHostTable *host_table,
    const RibosVmHandleBorrow *borrow);

/**
 * Affine/linear token의 generation을 회전해 old token을 stale로 만든다.
 *
 * Copy type은 같은 token을 반환하며 source slot duplication discipline은 verifier가
 * 소유한다.
 */
RibosVmStatus ribos_vm_handle_move_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHandleHostTable *host_table,
    const uint8_t source_token[RIBOS_VM_HANDLE_TOKEN_BYTES_V1],
    uint32_t expected_type_id,
    uint8_t destination_token[RIBOS_VM_HANDLE_TOKEN_BYTES_V1]);

/** Callback 전에 token을 stale로 만들고 record를 in-flight로 전환한다. */
RibosVmStatus ribos_vm_handle_consume_begin_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHandleHostTable *host_table,
    const uint8_t token[RIBOS_VM_HANDLE_TOKEN_BYTES_V1],
    uint32_t expected_type_id,
    RibosVmHandleConsumeLease *lease);

/**
 * In-flight consume을 irrevocably 종료한다.
 *
 * Callback 성공/실패와 관계없이 old token은 복원되지 않는다. `DROP`은 callback을
 * 최대 한 번 호출하고 실패해도 record와 host binding을 revoke한다.
 */
RibosVmStatus ribos_vm_handle_consume_finish_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHandleHostTable *host_table,
    const RibosVmHandleConsumeLease *lease,
    uint32_t disposition);

/**
 * In-flight object를 다른 verified opaque type으로 typestate-transition한다.
 *
 * Source token은 계속 stale이고 새 generation token만 available하다.
 */
RibosVmStatus ribos_vm_handle_consume_replace_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHandleHostTable *host_table,
    const RibosVmHandleConsumeLease *lease,
    uint32_t target_type_id,
    void *trusted_object,
    RibosVmHandleDropFn drop,
    void *drop_context,
    uint8_t target_token[RIBOS_VM_HANDLE_TOKEN_BYTES_V1]);

/** Available token을 선택적 drop 뒤 revoke한다. */
RibosVmStatus ribos_vm_handle_revoke_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHandleHostTable *host_table,
    const uint8_t token[RIBOS_VM_HANDLE_TOKEN_BYTES_V1]);

/**
 * 모든 non-empty record를 capacity 이하 순회로 revoke한다.
 *
 * Drop 실패가 있어도 authority는 남기지 않으며 반환 상태는
 * `EMBEDDER_REJECTED`다.
 */
RibosVmStatus ribos_vm_handle_fault_cleanup_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHandleHostTable *host_table,
    RibosVmHandleCleanupReport *report);

#ifdef __cplusplus
}
#endif

#endif
