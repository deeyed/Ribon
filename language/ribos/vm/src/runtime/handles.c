#include "ribos/vm/handles.h"

#include <stdint.h>
#include <string.h>

#define RIBOS_VM_HANDLE_RECORD_BYTES UINT32_C(32)
#define RIBOS_VM_HANDLE_GENERATION_OFFSET 0u
#define RIBOS_VM_HANDLE_LIFECYCLE_OFFSET 4u
#define RIBOS_VM_HANDLE_TYPE_OFFSET 8u
#define RIBOS_VM_HANDLE_OWNERSHIP_OFFSET 12u
#define RIBOS_VM_HANDLE_MOVE_COUNT_OFFSET 16u
#define RIBOS_VM_HANDLE_RESERVED_OFFSET 20u

typedef struct RibosVmHandleRecordView {
    uint32_t index;
    uint32_t generation;
    uint32_t lifecycle;
    uint32_t type_id;
    uint32_t ownership;
    uint32_t move_count;
    uint8_t *bytes;
    RibosVmHandleHostEntry *host;
} RibosVmHandleRecordView;

static uint32_t
ribos_vm_handle_read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

static void
ribos_vm_handle_write_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static int
ribos_vm_handle_reserved_is_zero(const uint64_t reserved[2])
{
    return reserved != NULL &&
        reserved[0] == 0 && reserved[1] == 0;
}

static int
ribos_vm_handle_table_reserved_is_zero(
    const RibosVmHandleHostTable *table)
{
    return table->reserved[0] == 0 &&
        table->reserved[1] == 0 &&
        table->reserved[2] == 0 &&
        table->reserved[3] == 0;
}

static RibosVmStatus
ribos_vm_handle_host_table_validate(
    const RibosVmHandleHostTable *table)
{
    if (table == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    if (table->size != sizeof(*table)) {
        return RIBOS_VM_STATUS_INVALID_SIZE;
    }
    if (table->handles_major != RIBOS_VM_HANDLES_V1_MAJOR ||
        table->handles_minor != RIBOS_VM_HANDLES_V1_MINOR) {
        return RIBOS_VM_STATUS_UNSUPPORTED_RUNTIME_ABI;
    }
    if (table->flags != 0 ||
        !ribos_vm_handle_table_reserved_is_zero(table)) {
        return RIBOS_VM_STATUS_RESERVED_NONZERO;
    }
    if ((table->capacity != 0 && table->entries == NULL) ||
        table->capacity > RIBOS_ARTIFACT_MAX_SLOTS) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    return RIBOS_VM_STATUS_OK;
}

static int
ribos_vm_handle_host_entry_is_clear(
    const RibosVmHandleHostEntry *entry)
{
    return entry != NULL &&
        entry->trusted_object == NULL &&
        entry->drop_context == NULL &&
        entry->drop == NULL &&
        entry->generation == 0 &&
        entry->flags == 0 &&
        entry->reserved[0] == 0 &&
        entry->reserved[1] == 0;
}

static int
ribos_vm_handle_host_entry_is_bound(
    const RibosVmHandleHostEntry *entry,
    uint32_t generation)
{
    return entry != NULL &&
        entry->trusted_object != NULL &&
        entry->generation == generation &&
        entry->flags == 0 &&
        entry->reserved[0] == 0 &&
        entry->reserved[1] == 0;
}

static void
ribos_vm_handle_host_entry_clear(RibosVmHandleHostEntry *entry)
{
    if (entry != NULL) {
        memset(entry, 0, sizeof(*entry));
    }
}

static int
ribos_vm_handle_next_generation(
    uint32_t generation,
    uint32_t *next)
{
    if (next == NULL || generation == UINT32_MAX) {
        return 0;
    }
    *next = generation + 1u;
    if (*next == 0) {
        return 0;
    }
    return 1;
}

static RibosVmStatus
ribos_vm_handle_region(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHandleHostTable *host_table,
    uint8_t **records,
    uint32_t *count)
{
    RibosVmStoragePlan plan;
    RibosVmStatus status;
    const RibosVmStorageRegion *region;
    size_t required_size;

    if (records == NULL || count == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *records = NULL;
    *count = 0;
    status = ribos_vm_handle_host_table_validate(host_table);
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
    status = ribos_vm_runtime_size_v1(
        prepared_program,
        &plan,
        &required_size);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    (void)required_size;
    region = &plan.regions[RIBOS_VM_STORAGE_REGION_HANDLES];
    if (region->count != plan.handle_count ||
        region->stride != RIBOS_VM_HANDLE_RECORD_BYTES ||
        region->byte_size !=
            (uint64_t)region->count * region->stride ||
        host_table->capacity != region->count ||
        region->offset > arena_size ||
        region->byte_size > arena_size - (size_t)region->offset) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    *records = (uint8_t *)(void *)storage +
        (size_t)region->offset;
    *count = region->count;
    return RIBOS_VM_STATUS_OK;
}

static RibosVmStatus
ribos_vm_handle_type_semantics(
    const RibosPreparedProgram *prepared_program,
    uint32_t type_id,
    uint32_t *ownership)
{
    uint32_t type_class;
    RibosVmStatus status =
        ribos_prepared_program_type_semantics_v1(
            prepared_program,
            type_id,
            ownership,
            &type_class);

    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (type_class != RIBOS_SCHEMA_TYPE_OPAQUE_HANDLE ||
        *ownership > RIBOS_SCHEMA_OWNERSHIP_LINEAR) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    return RIBOS_VM_STATUS_OK;
}

static int
ribos_vm_handle_record_reserved_is_zero(const uint8_t *record)
{
    uint32_t offset;

    for (offset = RIBOS_VM_HANDLE_RESERVED_OFFSET;
         offset < RIBOS_VM_HANDLE_RECORD_BYTES;
         ++offset) {
        if (record[offset] != 0) {
            return 0;
        }
    }
    return 1;
}

static void
ribos_vm_handle_record_write(
    uint8_t *record,
    uint32_t generation,
    uint32_t lifecycle,
    uint32_t type_id,
    uint32_t ownership,
    uint32_t move_count)
{
    memset(record, 0, RIBOS_VM_HANDLE_RECORD_BYTES);
    ribos_vm_handle_write_u32(
        record + RIBOS_VM_HANDLE_GENERATION_OFFSET,
        generation);
    ribos_vm_handle_write_u32(
        record + RIBOS_VM_HANDLE_LIFECYCLE_OFFSET,
        lifecycle);
    ribos_vm_handle_write_u32(
        record + RIBOS_VM_HANDLE_TYPE_OFFSET,
        type_id);
    ribos_vm_handle_write_u32(
        record + RIBOS_VM_HANDLE_OWNERSHIP_OFFSET,
        ownership);
    ribos_vm_handle_write_u32(
        record + RIBOS_VM_HANDLE_MOVE_COUNT_OFFSET,
        move_count);
}

static RibosVmStatus
ribos_vm_handle_record_at(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHandleHostTable *host_table,
    uint32_t index,
    RibosVmHandleRecordView *view)
{
    uint8_t *records;
    uint32_t count;
    uint8_t *record;
    RibosVmStatus status = ribos_vm_handle_region(
        prepared_program,
        storage,
        arena_size,
        host_table,
        &records,
        &count);

    if (view == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    memset(view, 0, sizeof(*view));
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (index >= count) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    record = records + (size_t)index *
        RIBOS_VM_HANDLE_RECORD_BYTES;
    if (!ribos_vm_handle_record_reserved_is_zero(record)) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    *view = (RibosVmHandleRecordView){
        .index = index,
        .generation = ribos_vm_handle_read_u32(
            record + RIBOS_VM_HANDLE_GENERATION_OFFSET),
        .lifecycle = ribos_vm_handle_read_u32(
            record + RIBOS_VM_HANDLE_LIFECYCLE_OFFSET),
        .type_id = ribos_vm_handle_read_u32(
            record + RIBOS_VM_HANDLE_TYPE_OFFSET),
        .ownership = ribos_vm_handle_read_u32(
            record + RIBOS_VM_HANDLE_OWNERSHIP_OFFSET),
        .move_count = ribos_vm_handle_read_u32(
            record + RIBOS_VM_HANDLE_MOVE_COUNT_OFFSET),
        .bytes = record,
        .host = &host_table->entries[index],
    };
    if (view->lifecycle > RIBOS_VM_HANDLE_REVOKED ||
        view->ownership > RIBOS_SCHEMA_OWNERSHIP_LINEAR) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    return RIBOS_VM_STATUS_OK;
}

static RibosVmStatus
ribos_vm_handle_record_from_token(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHandleHostTable *host_table,
    const uint8_t token[RIBOS_VM_HANDLE_TOKEN_BYTES_V1],
    uint32_t expected_type_id,
    uint32_t expected_lifecycle,
    RibosVmHandleRecordView *view)
{
    uint32_t index;
    uint32_t generation;
    uint32_t ownership;
    RibosVmStatus status = ribos_vm_handle_token_decode_v1(
        token,
        &index,
        &generation);

    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_handle_record_at(
        prepared_program,
        storage,
        arena_size,
        host_table,
        index,
        view);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (view->lifecycle == RIBOS_VM_HANDLE_REVOKED ||
        view->lifecycle == RIBOS_VM_HANDLE_IN_FLIGHT) {
        return RIBOS_VM_STATUS_ALREADY_CONSUMED;
    }
    if (view->generation != generation ||
        view->lifecycle != expected_lifecycle ||
        view->type_id != expected_type_id ||
        !ribos_vm_handle_host_entry_is_bound(
            view->host,
            generation)) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    status = ribos_vm_handle_type_semantics(
        prepared_program,
        view->type_id,
        &ownership);
    if (status != RIBOS_VM_STATUS_OK ||
        ownership != view->ownership ||
        (ownership == RIBOS_SCHEMA_OWNERSHIP_LINEAR &&
         view->host->drop == NULL)) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    return RIBOS_VM_STATUS_OK;
}

static int
ribos_vm_handle_drop(
    RibosVmHandleHostEntry *entry,
    uint32_t *drop_calls,
    uint32_t *drop_failures)
{
    uint32_t result;

    if (entry == NULL || entry->drop == NULL) {
        return 1;
    }
    result = entry->drop(
        entry->drop_context,
        entry->trusted_object);
    if (drop_calls != NULL) {
        ++*drop_calls;
    }
    if (result != RIBOS_VM_HANDLE_DROP_COMPLETE) {
        if (drop_failures != NULL) {
            ++*drop_failures;
        }
        return 0;
    }
    return 1;
}

RibosVmStatus
ribos_vm_handle_host_table_initialize_v1(
    RibosVmHandleHostTable *table,
    RibosVmHandleHostEntry *entries,
    uint32_t capacity)
{
    if (table == NULL ||
        (capacity != 0 && entries == NULL) ||
        capacity > RIBOS_ARTIFACT_MAX_SLOTS) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    if (capacity != 0) {
        memset(
            entries,
            0,
            (size_t)capacity * sizeof(*entries));
    }
    *table = (RibosVmHandleHostTable){
        .size = sizeof(*table),
        .handles_major = RIBOS_VM_HANDLES_V1_MAJOR,
        .handles_minor = RIBOS_VM_HANDLES_V1_MINOR,
        .capacity = capacity,
        .entries = entries,
    };
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_handle_token_encode_v1(
    uint32_t index,
    uint32_t generation,
    uint8_t bytes[RIBOS_VM_HANDLE_TOKEN_BYTES_V1])
{
    if (bytes == NULL || generation == 0) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    ribos_vm_handle_write_u32(bytes, index);
    ribos_vm_handle_write_u32(bytes + 4, generation);
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_handle_token_decode_v1(
    const uint8_t bytes[RIBOS_VM_HANDLE_TOKEN_BYTES_V1],
    uint32_t *index,
    uint32_t *generation)
{
    if (bytes == NULL || index == NULL || generation == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    *index = ribos_vm_handle_read_u32(bytes);
    *generation = ribos_vm_handle_read_u32(bytes + 4);
    return *generation == 0 ?
        RIBOS_VM_STATUS_INVALID_STATE :
        RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_handle_create_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHandleHostTable *host_table,
    uint32_t type_id,
    void *trusted_object,
    RibosVmHandleDropFn drop,
    void *drop_context,
    uint8_t token[RIBOS_VM_HANDLE_TOKEN_BYTES_V1])
{
    uint8_t *records;
    uint32_t count;
    uint32_t ownership;
    uint32_t index;
    RibosVmStatus status;

    if (trusted_object == NULL || token == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    status = ribos_vm_handle_type_semantics(
        prepared_program,
        type_id,
        &ownership);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (ownership == RIBOS_SCHEMA_OWNERSHIP_LINEAR &&
        drop == NULL) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    status = ribos_vm_handle_region(
        prepared_program,
        storage,
        arena_size,
        host_table,
        &records,
        &count);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    for (index = 0; index < count; ++index) {
        uint8_t *record = records +
            (size_t)index * RIBOS_VM_HANDLE_RECORD_BYTES;
        uint32_t lifecycle = ribos_vm_handle_read_u32(
            record + RIBOS_VM_HANDLE_LIFECYCLE_OFFSET);
        uint32_t previous_generation =
            ribos_vm_handle_read_u32(
                record + RIBOS_VM_HANDLE_GENERATION_OFFSET);
        uint32_t generation;

        if ((lifecycle != RIBOS_VM_HANDLE_EMPTY &&
             lifecycle != RIBOS_VM_HANDLE_REVOKED) ||
            !ribos_vm_handle_record_reserved_is_zero(record) ||
            !ribos_vm_handle_host_entry_is_clear(
                &host_table->entries[index]) ||
            !ribos_vm_handle_next_generation(
                previous_generation,
                &generation)) {
            continue;
        }
        ribos_vm_handle_record_write(
            record,
            generation,
            RIBOS_VM_HANDLE_AVAILABLE,
            type_id,
            ownership,
            0);
        host_table->entries[index] =
            (RibosVmHandleHostEntry){
                .trusted_object = trusted_object,
                .drop_context = drop_context,
                .drop = drop,
                .generation = generation,
            };
        return ribos_vm_handle_token_encode_v1(
            index,
            generation,
            token);
    }
    return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
}

RibosVmStatus
ribos_vm_handle_lookup_v1(
    const RibosPreparedProgram *prepared_program,
    const RibosVmStorage *storage,
    size_t arena_size,
    const RibosVmHandleHostTable *host_table,
    const uint8_t token[RIBOS_VM_HANDLE_TOKEN_BYTES_V1],
    uint32_t expected_type_id,
    RibosVmHandleSnapshot *snapshot)
{
    RibosVmHandleRecordView view;
    RibosVmStatus status;

    if (snapshot == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    status = ribos_vm_handle_record_from_token(
        prepared_program,
        (RibosVmStorage *)(void *)storage,
        arena_size,
        (RibosVmHandleHostTable *)(void *)host_table,
        token,
        expected_type_id,
        RIBOS_VM_HANDLE_AVAILABLE,
        &view);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    *snapshot = (RibosVmHandleSnapshot){
        .size = sizeof(*snapshot),
        .handles_major = RIBOS_VM_HANDLES_V1_MAJOR,
        .handles_minor = RIBOS_VM_HANDLES_V1_MINOR,
        .index = view.index,
        .generation = view.generation,
        .lifecycle = view.lifecycle,
        .type_id = view.type_id,
        .ownership = view.ownership,
        .move_count = view.move_count,
    };
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_handle_borrow_begin_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHandleHostTable *host_table,
    const uint8_t token[RIBOS_VM_HANDLE_TOKEN_BYTES_V1],
    uint32_t expected_type_id,
    RibosVmHandleBorrow *borrow)
{
    RibosVmHandleRecordView view;
    RibosVmStatus status;

    if (borrow == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    memset(borrow, 0, sizeof(*borrow));
    status = ribos_vm_handle_record_from_token(
        prepared_program,
        storage,
        arena_size,
        host_table,
        token,
        expected_type_id,
        RIBOS_VM_HANDLE_AVAILABLE,
        &view);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    ribos_vm_handle_write_u32(
        view.bytes + RIBOS_VM_HANDLE_LIFECYCLE_OFFSET,
        RIBOS_VM_HANDLE_BORROWED);
    *borrow = (RibosVmHandleBorrow){
        .size = sizeof(*borrow),
        .handles_major = RIBOS_VM_HANDLES_V1_MAJOR,
        .handles_minor = RIBOS_VM_HANDLES_V1_MINOR,
        .index = view.index,
        .generation = view.generation,
        .type_id = view.type_id,
        .ownership = view.ownership,
        .trusted_object = view.host->trusted_object,
    };
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_handle_borrow_end_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHandleHostTable *host_table,
    const RibosVmHandleBorrow *borrow)
{
    RibosVmHandleRecordView view;
    RibosVmStatus status;

    if (borrow == NULL || borrow->size != sizeof(*borrow) ||
        borrow->handles_major != RIBOS_VM_HANDLES_V1_MAJOR ||
        borrow->handles_minor != RIBOS_VM_HANDLES_V1_MINOR ||
        borrow->trusted_object == NULL ||
        !ribos_vm_handle_reserved_is_zero(borrow->reserved)) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    status = ribos_vm_handle_record_at(
        prepared_program,
        storage,
        arena_size,
        host_table,
        borrow->index,
        &view);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (view.generation != borrow->generation ||
        view.lifecycle != RIBOS_VM_HANDLE_BORROWED ||
        view.type_id != borrow->type_id ||
        view.ownership != borrow->ownership ||
        !ribos_vm_handle_host_entry_is_bound(
            view.host,
            view.generation) ||
        view.host->trusted_object != borrow->trusted_object) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    ribos_vm_handle_write_u32(
        view.bytes + RIBOS_VM_HANDLE_LIFECYCLE_OFFSET,
        RIBOS_VM_HANDLE_AVAILABLE);
    return RIBOS_VM_STATUS_OK;
}

RibosVmStatus
ribos_vm_handle_move_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHandleHostTable *host_table,
    const uint8_t source_token[RIBOS_VM_HANDLE_TOKEN_BYTES_V1],
    uint32_t expected_type_id,
    uint8_t destination_token[RIBOS_VM_HANDLE_TOKEN_BYTES_V1])
{
    RibosVmHandleRecordView view;
    uint32_t generation;
    RibosVmStatus status;

    if (destination_token == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    status = ribos_vm_handle_record_from_token(
        prepared_program,
        storage,
        arena_size,
        host_table,
        source_token,
        expected_type_id,
        RIBOS_VM_HANDLE_AVAILABLE,
        &view);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (view.ownership == RIBOS_SCHEMA_OWNERSHIP_COPY) {
        memcpy(
            destination_token,
            source_token,
            RIBOS_VM_HANDLE_TOKEN_BYTES_V1);
        return RIBOS_VM_STATUS_OK;
    }
    if (view.move_count == UINT32_MAX ||
        !ribos_vm_handle_next_generation(
            view.generation,
            &generation)) {
        return RIBOS_VM_STATUS_LIMIT_EXCEEDED;
    }
    ribos_vm_handle_record_write(
        view.bytes,
        generation,
        RIBOS_VM_HANDLE_AVAILABLE,
        view.type_id,
        view.ownership,
        view.move_count + 1u);
    view.host->generation = generation;
    return ribos_vm_handle_token_encode_v1(
        view.index,
        generation,
        destination_token);
}

RibosVmStatus
ribos_vm_handle_consume_begin_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHandleHostTable *host_table,
    const uint8_t token[RIBOS_VM_HANDLE_TOKEN_BYTES_V1],
    uint32_t expected_type_id,
    RibosVmHandleConsumeLease *lease)
{
    RibosVmHandleRecordView view;
    uint32_t generation;
    RibosVmStatus status;

    if (lease == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    memset(lease, 0, sizeof(*lease));
    status = ribos_vm_handle_record_from_token(
        prepared_program,
        storage,
        arena_size,
        host_table,
        token,
        expected_type_id,
        RIBOS_VM_HANDLE_AVAILABLE,
        &view);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (view.ownership == RIBOS_SCHEMA_OWNERSHIP_COPY ||
        !ribos_vm_handle_next_generation(
            view.generation,
            &generation)) {
        return RIBOS_VM_STATUS_INVALID_STATE;
    }
    ribos_vm_handle_record_write(
        view.bytes,
        generation,
        RIBOS_VM_HANDLE_IN_FLIGHT,
        view.type_id,
        view.ownership,
        view.move_count);
    view.host->generation = generation;
    *lease = (RibosVmHandleConsumeLease){
        .size = sizeof(*lease),
        .handles_major = RIBOS_VM_HANDLES_V1_MAJOR,
        .handles_minor = RIBOS_VM_HANDLES_V1_MINOR,
        .index = view.index,
        .generation = generation,
        .source_type_id = view.type_id,
        .ownership = view.ownership,
        .trusted_object = view.host->trusted_object,
    };
    return RIBOS_VM_STATUS_OK;
}

static RibosVmStatus
ribos_vm_handle_consume_lease_record(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHandleHostTable *host_table,
    const RibosVmHandleConsumeLease *lease,
    RibosVmHandleRecordView *view)
{
    RibosVmStatus status;

    if (lease == NULL || lease->size != sizeof(*lease) ||
        lease->handles_major != RIBOS_VM_HANDLES_V1_MAJOR ||
        lease->handles_minor != RIBOS_VM_HANDLES_V1_MINOR ||
        lease->trusted_object == NULL ||
        lease->ownership <= RIBOS_SCHEMA_OWNERSHIP_COPY ||
        lease->ownership > RIBOS_SCHEMA_OWNERSHIP_LINEAR ||
        !ribos_vm_handle_reserved_is_zero(lease->reserved)) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    status = ribos_vm_handle_record_at(
        prepared_program,
        storage,
        arena_size,
        host_table,
        lease->index,
        view);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (view->generation != lease->generation ||
        view->lifecycle != RIBOS_VM_HANDLE_IN_FLIGHT ||
        view->type_id != lease->source_type_id ||
        view->ownership != lease->ownership ||
        !ribos_vm_handle_host_entry_is_bound(
            view->host,
            view->generation) ||
        view->host->trusted_object != lease->trusted_object) {
        return RIBOS_VM_STATUS_ALREADY_CONSUMED;
    }
    return RIBOS_VM_STATUS_OK;
}

static void
ribos_vm_handle_record_revoke(RibosVmHandleRecordView *view)
{
    uint32_t generation = view->generation;

    (void)ribos_vm_handle_next_generation(
        view->generation,
        &generation);
    ribos_vm_handle_record_write(
        view->bytes,
        generation,
        RIBOS_VM_HANDLE_REVOKED,
        0,
        RIBOS_SCHEMA_OWNERSHIP_COPY,
        view->move_count);
    ribos_vm_handle_host_entry_clear(view->host);
}

static void
ribos_vm_handle_record_force_revoke(
    uint8_t *record,
    RibosVmHandleHostEntry *host)
{
    uint32_t generation =
        ribos_vm_handle_read_u32(
            record + RIBOS_VM_HANDLE_GENERATION_OFFSET);

    (void)ribos_vm_handle_next_generation(
        generation,
        &generation);
    ribos_vm_handle_record_write(
        record,
        generation,
        RIBOS_VM_HANDLE_REVOKED,
        0,
        RIBOS_SCHEMA_OWNERSHIP_COPY,
        ribos_vm_handle_read_u32(
            record + RIBOS_VM_HANDLE_MOVE_COUNT_OFFSET));
    ribos_vm_handle_host_entry_clear(host);
}

RibosVmStatus
ribos_vm_handle_consume_finish_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHandleHostTable *host_table,
    const RibosVmHandleConsumeLease *lease,
    uint32_t disposition)
{
    RibosVmHandleRecordView view;
    int drop_ok = 1;
    RibosVmStatus status;

    if (disposition != RIBOS_VM_HANDLE_CONSUME_TRANSFERRED &&
        disposition != RIBOS_VM_HANDLE_CONSUME_DROP) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    status = ribos_vm_handle_consume_lease_record(
        prepared_program,
        storage,
        arena_size,
        host_table,
        lease,
        &view);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (disposition == RIBOS_VM_HANDLE_CONSUME_DROP) {
        drop_ok = ribos_vm_handle_drop(
            view.host,
            NULL,
            NULL);
    }
    ribos_vm_handle_record_revoke(&view);
    return drop_ok ?
        RIBOS_VM_STATUS_OK :
        RIBOS_VM_STATUS_EMBEDDER_REJECTED;
}

RibosVmStatus
ribos_vm_handle_consume_replace_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHandleHostTable *host_table,
    const RibosVmHandleConsumeLease *lease,
    uint32_t target_type_id,
    void *trusted_object,
    RibosVmHandleDropFn drop,
    void *drop_context,
    uint8_t target_token[RIBOS_VM_HANDLE_TOKEN_BYTES_V1])
{
    RibosVmHandleRecordView view;
    uint32_t ownership;
    RibosVmStatus status;

    if (lease == NULL || trusted_object == NULL || target_token == NULL ||
        target_type_id == lease->source_type_id) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    status = ribos_vm_handle_type_semantics(
        prepared_program,
        target_type_id,
        &ownership);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (ownership == RIBOS_SCHEMA_OWNERSHIP_COPY ||
        (ownership == RIBOS_SCHEMA_OWNERSHIP_LINEAR &&
         drop == NULL)) {
        return RIBOS_VM_STATUS_INVALID_DESCRIPTOR;
    }
    status = ribos_vm_handle_consume_lease_record(
        prepared_program,
        storage,
        arena_size,
        host_table,
        lease,
        &view);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    ribos_vm_handle_record_write(
        view.bytes,
        view.generation,
        RIBOS_VM_HANDLE_AVAILABLE,
        target_type_id,
        ownership,
        view.move_count);
    *view.host = (RibosVmHandleHostEntry){
        .trusted_object = trusted_object,
        .drop_context = drop_context,
        .drop = drop,
        .generation = view.generation,
    };
    return ribos_vm_handle_token_encode_v1(
        view.index,
        view.generation,
        target_token);
}

RibosVmStatus
ribos_vm_handle_revoke_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHandleHostTable *host_table,
    const uint8_t token[RIBOS_VM_HANDLE_TOKEN_BYTES_V1])
{
    RibosVmHandleRecordView view;
    uint32_t index;
    uint32_t generation;
    int drop_ok;
    RibosVmStatus status = ribos_vm_handle_token_decode_v1(
        token,
        &index,
        &generation);

    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    status = ribos_vm_handle_record_at(
        prepared_program,
        storage,
        arena_size,
        host_table,
        index,
        &view);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    if (view.generation != generation ||
        view.lifecycle != RIBOS_VM_HANDLE_AVAILABLE ||
        !ribos_vm_handle_host_entry_is_bound(
            view.host,
            generation)) {
        return view.lifecycle == RIBOS_VM_HANDLE_REVOKED ||
                view.lifecycle == RIBOS_VM_HANDLE_IN_FLIGHT ?
            RIBOS_VM_STATUS_ALREADY_CONSUMED :
            RIBOS_VM_STATUS_INVALID_STATE;
    }
    ribos_vm_handle_write_u32(
        view.bytes + RIBOS_VM_HANDLE_LIFECYCLE_OFFSET,
        RIBOS_VM_HANDLE_IN_FLIGHT);
    drop_ok = ribos_vm_handle_drop(view.host, NULL, NULL);
    ribos_vm_handle_record_revoke(&view);
    return drop_ok ?
        RIBOS_VM_STATUS_OK :
        RIBOS_VM_STATUS_EMBEDDER_REJECTED;
}

RibosVmStatus
ribos_vm_handle_fault_cleanup_v1(
    const RibosPreparedProgram *prepared_program,
    RibosVmStorage *storage,
    size_t arena_size,
    RibosVmHandleHostTable *host_table,
    RibosVmHandleCleanupReport *report)
{
    uint8_t *records;
    uint32_t count;
    uint32_t index;
    RibosVmStatus status;

    if (report == NULL) {
        return RIBOS_VM_STATUS_INVALID_ARGUMENT;
    }
    memset(report, 0, sizeof(*report));
    status = ribos_vm_handle_region(
        prepared_program,
        storage,
        arena_size,
        host_table,
        &records,
        &count);
    if (status != RIBOS_VM_STATUS_OK) {
        return status;
    }
    *report = (RibosVmHandleCleanupReport){
        .size = sizeof(*report),
        .handles_major = RIBOS_VM_HANDLES_V1_MAJOR,
        .handles_minor = RIBOS_VM_HANDLES_V1_MINOR,
        .scanned = count,
    };
    for (index = 0; index < count; ++index) {
        RibosVmHandleRecordView view;
        uint8_t *record = records + (size_t)index *
            RIBOS_VM_HANDLE_RECORD_BYTES;
        RibosVmHandleHostEntry *host =
            &host_table->entries[index];
        uint32_t lifecycle = ribos_vm_handle_read_u32(
            record + RIBOS_VM_HANDLE_LIFECYCLE_OFFSET);

        if (lifecycle == RIBOS_VM_HANDLE_EMPTY ||
            lifecycle == RIBOS_VM_HANDLE_REVOKED) {
            if (!ribos_vm_handle_host_entry_is_clear(
                    host)) {
                if (host->flags == 0 &&
                    host->reserved[0] == 0 &&
                    host->reserved[1] == 0 &&
                    host->trusted_object != NULL) {
                    (void)ribos_vm_handle_drop(
                        host,
                        &report->drop_calls,
                        &report->drop_failures);
                } else {
                    ++report->drop_failures;
                }
                ribos_vm_handle_host_entry_clear(host);
            }
            continue;
        }
        status = ribos_vm_handle_record_at(
            prepared_program,
            storage,
            arena_size,
            host_table,
            index,
            &view);
        if (status != RIBOS_VM_STATUS_OK) {
            if (host->flags == 0 &&
                host->reserved[0] == 0 &&
                host->reserved[1] == 0 &&
                host->trusted_object != NULL) {
                (void)ribos_vm_handle_drop(
                    host,
                    &report->drop_calls,
                    &report->drop_failures);
            } else if (!ribos_vm_handle_host_entry_is_clear(
                    host)) {
                ++report->drop_failures;
            }
            ribos_vm_handle_record_force_revoke(
                record,
                host);
            ++report->revoked;
            continue;
        }
        if (!ribos_vm_handle_host_entry_is_bound(
                view.host,
                view.generation) ||
            (view.ownership == RIBOS_SCHEMA_OWNERSHIP_LINEAR &&
             view.host->drop == NULL)) {
            ++report->drop_failures;
        } else {
            (void)ribos_vm_handle_drop(
                view.host,
                &report->drop_calls,
                &report->drop_failures);
        }
        ribos_vm_handle_record_revoke(&view);
        ++report->revoked;
    }
    return report->drop_failures == 0 ?
        RIBOS_VM_STATUS_OK :
        RIBOS_VM_STATUS_EMBEDDER_REJECTED;
}
