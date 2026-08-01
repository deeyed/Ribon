#include <Ribon/update/transaction.h>

#include "../security/sha256.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define RECORD_HEADER_BYTES 240u
#define RECORD_METADATA_OFFSET 240u
#define RECORD_DIGEST_OFFSET 752u
#define RECORD_CHECKSUM_OFFSET 784u
#define SELECTOR_DIGEST_OFFSET 128u
#define SELECTOR_CHECKSUM_OFFSET 160u
#define RECORD_PHASE_COMPLETE 1u
#define NO_COMPONENT UINT32_MAX

static const uint8_t record_magic[32] =
    "RIBON-UPDATE-TXN-RECORD-V1";
static const uint8_t selector_magic[32] =
    "RIBON-UPDATE-TXN-SELECT-V1";

struct TransactionRecordView {
    uint32_t target_slot;
    enum RibonUpdateSlotState target_state;
    uint64_t generation;
    uint64_t predecessor_generation;
    uint8_t predecessor_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t record_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    struct RibonUpdateSlotMetadata metadata;
};

struct TransactionSelectorView {
    uint32_t record_slot;
    uint64_t generation;
    uint64_t predecessor_generation;
    uint8_t record_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t predecessor_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
};

static uint16_t load_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t load_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t load_u64(const uint8_t *bytes)
{
    return (uint64_t)load_u32(bytes) | ((uint64_t)load_u32(bytes + 4u) << 32);
}

static void store_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void store_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void store_u64(uint8_t *bytes, uint64_t value)
{
    store_u32(bytes, (uint32_t)value);
    store_u32(bytes + 4u, (uint32_t)(value >> 32));
}

static int bytes_zero(const void *pointer, size_t size)
{
    const uint8_t *bytes = pointer;
    uint8_t value = 0u;
    size_t index;
    for (index = 0u; index < size; ++index) {
        value |= bytes[index];
    }
    return value == 0u;
}

static int bytes_equal(const void *left_pointer, const void *right_pointer, size_t size)
{
    const uint8_t *left = left_pointer;
    const uint8_t *right = right_pointer;
    uint8_t difference = 0u;
    size_t index;
    for (index = 0u; index < size; ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == 0u;
}

static uint32_t crc32c(const uint8_t *bytes, size_t size)
{
    uint32_t crc = UINT32_MAX;
    size_t index;
    uint32_t bit;
    for (index = 0u; index < size; ++index) {
        crc ^= bytes[index];
        for (bit = 0u; bit < 8u; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (UINT32_C(0x82f63b78) & mask);
        }
    }
    return ~crc;
}

static const struct RibonUpdateLayoutRegion *journal_region(
    const struct RibonUpdateLayout *layout)
{
    return &layout->regions[RIBON_UPDATE_REGION_UPDATE_JOURNAL - 1u];
}

static int add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (result == NULL || left > UINT64_MAX - right) {
        return 0;
    }
    *result = left + right;
    return 1;
}

static int journal_is_valid(const struct RibonUpdateTransactionJournal *journal)
{
    uint8_t identity[RIBON_UPDATE_LAYOUT_IDENTITY_BYTES];
    const struct RibonUpdateLayoutRegion *region;
    const struct RibonUpdateStorageProvider *provider;
    uint64_t end;
    if (journal == NULL || journal->size != sizeof(*journal) ||
        journal->abi_version != RIBON_UPDATE_TRANSACTION_ABI_VERSION ||
        journal->flags != 0u || journal->reserved0 != 0u ||
        journal->provider == NULL || journal->layout == NULL ||
        journal->minimum_generation == 0u || journal->deadline_ticks == 0u ||
        !bytes_zero(journal->reserved, sizeof(journal->reserved)) ||
        !ribon_update_storage_provider_is_valid(journal->provider) ||
        ribon_update_layout_identity_encode(journal->layout, identity) !=
            RIBON_UPDATE_STORAGE_STATUS_OK) {
        return 0;
    }
    provider = journal->provider;
    region = journal_region(journal->layout);
    return journal->layout->media_capacity_bytes == provider->capacity_bytes &&
        region->kind == RIBON_UPDATE_REGION_UPDATE_JOURNAL &&
        region->length >= RIBON_UPDATE_TRANSACTION_MINIMUM_JOURNAL_BYTES &&
        add_u64(region->offset, region->length, &end) &&
        end <= provider->capacity_bytes &&
        (region->offset & (provider->read_alignment - 1u)) == 0u &&
        (region->offset & (provider->write_alignment - 1u)) == 0u &&
        (RIBON_UPDATE_TRANSACTION_RECORD_BYTES &
            (provider->read_alignment - 1u)) == 0u &&
        (RIBON_UPDATE_TRANSACTION_RECORD_BYTES &
            (provider->write_alignment - 1u)) == 0u &&
        (RIBON_UPDATE_TRANSACTION_SELECTOR_BYTES &
            (provider->read_alignment - 1u)) == 0u &&
        (RIBON_UPDATE_TRANSACTION_SELECTOR_BYTES &
            (provider->write_alignment - 1u)) == 0u &&
        provider->maximum_transfer_bytes >= RIBON_UPDATE_TRANSACTION_RECORD_BYTES;
}

static uint64_t record_offset(
    const struct RibonUpdateTransactionJournal *journal,
    uint32_t slot)
{
    return journal_region(journal->layout)->offset +
        (uint64_t)slot * RIBON_UPDATE_TRANSACTION_RECORD_BYTES;
}

static uint64_t selector_offset(
    const struct RibonUpdateTransactionJournal *journal,
    uint32_t slot)
{
    return journal_region(journal->layout)->offset +
        RIBON_UPDATE_TRANSACTION_RECORD_SLOTS *
            RIBON_UPDATE_TRANSACTION_RECORD_BYTES +
        (uint64_t)slot * RIBON_UPDATE_TRANSACTION_SELECTOR_BYTES;
}

static int provider_read_exact(
    const struct RibonUpdateTransactionJournal *journal,
    uint64_t offset, uint8_t *bytes, uint64_t size)
{
    uint64_t transferred = 0u;
    const int status = journal->provider->read(journal->provider->context,
        offset, bytes, size, &transferred, journal->deadline_ticks);
    if (status != 0) {
        return RIBON_UPDATE_TRANSACTION_STATUS_IO;
    }
    return transferred == size ? RIBON_UPDATE_TRANSACTION_STATUS_OK :
        RIBON_UPDATE_TRANSACTION_STATUS_SHORT_IO;
}

static int provider_write_exact(
    const struct RibonUpdateTransactionJournal *journal,
    uint64_t offset, const uint8_t *bytes, uint64_t size)
{
    uint64_t transferred = 0u;
    const int status = journal->provider->write(journal->provider->context,
        offset, bytes, size, &transferred, journal->deadline_ticks);
    if (status != 0) {
        return RIBON_UPDATE_TRANSACTION_STATUS_IO;
    }
    return transferred == size ? RIBON_UPDATE_TRANSACTION_STATUS_OK :
        RIBON_UPDATE_TRANSACTION_STATUS_SHORT_IO;
}

static int provider_flush(const struct RibonUpdateTransactionJournal *journal)
{
    return journal->provider->flush(
        journal->provider->context, journal->deadline_ticks) == 0 ?
        RIBON_UPDATE_TRANSACTION_STATUS_OK : RIBON_UPDATE_TRANSACTION_STATUS_IO;
}

static int record_encode(
    const struct RibonUpdateSlotMetadata *metadata,
    uint32_t target_slot,
    uint64_t generation,
    uint64_t predecessor_generation,
    const uint8_t predecessor_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES],
    uint8_t output[RIBON_UPDATE_TRANSACTION_RECORD_BYTES])
{
    uint8_t metadata_wire[RIBON_UPDATE_SLOT_METADATA_BYTES];
    uint8_t digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    const struct RibonUpdateSlotEntry *entry;
    memset(output, 0, RIBON_UPDATE_TRANSACTION_RECORD_BYTES);
    if (metadata == NULL || target_slot >= RIBON_UPDATE_SLOT_COUNT ||
        generation == 0u || generation != metadata->metadata_generation ||
        predecessor_digest == NULL ||
        ribon_update_slot_metadata_encode(metadata, metadata_wire) !=
            RIBON_UPDATE_STORAGE_STATUS_OK ||
        (generation == 1u &&
         (predecessor_generation != 0u ||
          !bytes_zero(predecessor_digest, RIBON_UPDATE_MANIFEST_DIGEST_BYTES))) ||
        (generation > 1u &&
         (predecessor_generation != generation - 1u ||
          bytes_zero(predecessor_digest, RIBON_UPDATE_MANIFEST_DIGEST_BYTES)))) {
        return 0;
    }
    entry = &metadata->slots[target_slot];
    memcpy(output, record_magic, sizeof(record_magic));
    store_u16(output + 32u, RIBON_UPDATE_TRANSACTION_ABI_VERSION);
    store_u16(output + 34u, RECORD_HEADER_BYTES);
    store_u32(output + 36u, RIBON_UPDATE_TRANSACTION_RECORD_BYTES);
    store_u32(output + 40u, RECORD_PHASE_COMPLETE);
    store_u32(output + 44u, target_slot);
    store_u32(output + 48u, (uint32_t)entry->state);
    store_u32(output + 52u, 0u);
    store_u64(output + 56u, generation);
    store_u64(output + 64u, predecessor_generation);
    memcpy(output + 72u, predecessor_digest, 32u);
    ribon_security_sha256(metadata_wire, sizeof(metadata_wire), digest);
    memcpy(output + 104u, digest, 32u);
    memcpy(output + 136u, entry->manifest_digest, 32u);
    memcpy(output + 168u, entry->image_set_digest, 32u);
    memcpy(output + 200u, entry->layout_digest, 32u);
    memcpy(output + RECORD_METADATA_OFFSET, metadata_wire, sizeof(metadata_wire));
    ribon_security_sha256(output, RECORD_DIGEST_OFFSET, digest);
    memcpy(output + RECORD_DIGEST_OFFSET, digest, sizeof(digest));
    store_u32(output + RECORD_CHECKSUM_OFFSET,
        crc32c(output, RECORD_CHECKSUM_OFFSET));
    return 1;
}

static int record_decode(
    const uint8_t bytes[RIBON_UPDATE_TRANSACTION_RECORD_BYTES],
    struct TransactionRecordView *record)
{
    uint8_t digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    const struct RibonUpdateSlotEntry *entry;
    if (record != NULL) {
        memset(record, 0, sizeof(*record));
    }
    if (bytes == NULL || record == NULL ||
        !bytes_equal(bytes, record_magic, sizeof(record_magic)) ||
        load_u16(bytes + 32u) != RIBON_UPDATE_TRANSACTION_ABI_VERSION ||
        load_u16(bytes + 34u) != RECORD_HEADER_BYTES ||
        load_u32(bytes + 36u) != RIBON_UPDATE_TRANSACTION_RECORD_BYTES ||
        load_u32(bytes + 40u) != RECORD_PHASE_COMPLETE ||
        load_u32(bytes + 44u) >= RIBON_UPDATE_SLOT_COUNT ||
        load_u32(bytes + 48u) > RIBON_UPDATE_SLOT_BAD ||
        load_u32(bytes + 52u) != 0u ||
        load_u64(bytes + 56u) == 0u ||
        !bytes_zero(bytes + 232u, RECORD_HEADER_BYTES - 232u) ||
        !bytes_zero(bytes + RECORD_CHECKSUM_OFFSET + 4u,
            RIBON_UPDATE_TRANSACTION_RECORD_BYTES -
                RECORD_CHECKSUM_OFFSET - 4u) ||
        load_u32(bytes + RECORD_CHECKSUM_OFFSET) !=
            crc32c(bytes, RECORD_CHECKSUM_OFFSET)) {
        return 0;
    }
    ribon_security_sha256(bytes, RECORD_DIGEST_OFFSET, digest);
    if (!bytes_equal(digest, bytes + RECORD_DIGEST_OFFSET, sizeof(digest)) ||
        ribon_update_slot_metadata_open(bytes + RECORD_METADATA_OFFSET,
            RIBON_UPDATE_SLOT_METADATA_BYTES, &record->metadata) !=
            RIBON_UPDATE_STORAGE_STATUS_OK) {
        return 0;
    }
    ribon_security_sha256(bytes + RECORD_METADATA_OFFSET,
        RIBON_UPDATE_SLOT_METADATA_BYTES, digest);
    if (!bytes_equal(digest, bytes + 104u, sizeof(digest))) {
        memset(record, 0, sizeof(*record));
        return 0;
    }
    record->target_slot = load_u32(bytes + 44u);
    record->target_state =
        (enum RibonUpdateSlotState)load_u32(bytes + 48u);
    record->generation = load_u64(bytes + 56u);
    record->predecessor_generation = load_u64(bytes + 64u);
    memcpy(record->predecessor_digest, bytes + 72u, 32u);
    memcpy(record->record_digest, bytes + RECORD_DIGEST_OFFSET, 32u);
    entry = &record->metadata.slots[record->target_slot];
    if (record->metadata.metadata_generation != record->generation ||
        entry->state != record->target_state ||
        !bytes_equal(entry->manifest_digest, bytes + 136u, 32u) ||
        !bytes_equal(entry->image_set_digest, bytes + 168u, 32u) ||
        !bytes_equal(entry->layout_digest, bytes + 200u, 32u) ||
        (record->generation == 1u &&
         (record->predecessor_generation != 0u ||
          !bytes_zero(record->predecessor_digest, 32u))) ||
        (record->generation > 1u &&
         (record->predecessor_generation != record->generation - 1u ||
          bytes_zero(record->predecessor_digest, 32u)))) {
        memset(record, 0, sizeof(*record));
        return 0;
    }
    return 1;
}

static int selector_encode(
    uint32_t record_slot,
    uint64_t generation,
    uint64_t predecessor_generation,
    const uint8_t record_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES],
    const uint8_t predecessor_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES],
    uint8_t output[RIBON_UPDATE_TRANSACTION_SELECTOR_BYTES])
{
    uint8_t digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    memset(output, 0, RIBON_UPDATE_TRANSACTION_SELECTOR_BYTES);
    if (record_slot >= RIBON_UPDATE_TRANSACTION_RECORD_SLOTS ||
        generation == 0u || record_digest == NULL || predecessor_digest == NULL ||
        bytes_zero(record_digest, 32u)) {
        return 0;
    }
    memcpy(output, selector_magic, sizeof(selector_magic));
    store_u16(output + 32u, RIBON_UPDATE_TRANSACTION_ABI_VERSION);
    store_u16(output + 34u, 128u);
    store_u32(output + 36u, RIBON_UPDATE_TRANSACTION_SELECTOR_BYTES);
    store_u32(output + 40u, record_slot);
    store_u32(output + 44u, 0u);
    store_u64(output + 48u, generation);
    memcpy(output + 56u, record_digest, 32u);
    store_u64(output + 88u, predecessor_generation);
    memcpy(output + 96u, predecessor_digest, 32u);
    ribon_security_sha256(output, SELECTOR_DIGEST_OFFSET, digest);
    memcpy(output + SELECTOR_DIGEST_OFFSET, digest, 32u);
    store_u32(output + SELECTOR_CHECKSUM_OFFSET,
        crc32c(output, SELECTOR_CHECKSUM_OFFSET));
    return 1;
}

static int selector_decode(
    const uint8_t bytes[RIBON_UPDATE_TRANSACTION_SELECTOR_BYTES],
    struct TransactionSelectorView *selector)
{
    uint8_t digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    if (selector != NULL) {
        memset(selector, 0, sizeof(*selector));
    }
    if (bytes == NULL || selector == NULL ||
        !bytes_equal(bytes, selector_magic, sizeof(selector_magic)) ||
        load_u16(bytes + 32u) != RIBON_UPDATE_TRANSACTION_ABI_VERSION ||
        load_u16(bytes + 34u) != 128u ||
        load_u32(bytes + 36u) != RIBON_UPDATE_TRANSACTION_SELECTOR_BYTES ||
        load_u32(bytes + 40u) >= RIBON_UPDATE_TRANSACTION_RECORD_SLOTS ||
        load_u32(bytes + 44u) != 0u || load_u64(bytes + 48u) == 0u ||
        bytes_zero(bytes + 56u, 32u) ||
        !bytes_zero(bytes + SELECTOR_CHECKSUM_OFFSET + 4u,
            RIBON_UPDATE_TRANSACTION_SELECTOR_BYTES -
                SELECTOR_CHECKSUM_OFFSET - 4u) ||
        load_u32(bytes + SELECTOR_CHECKSUM_OFFSET) !=
            crc32c(bytes, SELECTOR_CHECKSUM_OFFSET)) {
        return 0;
    }
    ribon_security_sha256(bytes, SELECTOR_DIGEST_OFFSET, digest);
    if (!bytes_equal(digest, bytes + SELECTOR_DIGEST_OFFSET, 32u)) {
        return 0;
    }
    selector->record_slot = load_u32(bytes + 40u);
    selector->generation = load_u64(bytes + 48u);
    memcpy(selector->record_digest, bytes + 56u, 32u);
    selector->predecessor_generation = load_u64(bytes + 88u);
    memcpy(selector->predecessor_digest, bytes + 96u, 32u);
    return 1;
}

/** @brief Newest complete record를 독립 검증하고 replay floor를 적용한다. */
int ribon_update_transaction_open(
    const struct RibonUpdateTransactionJournal *journal,
    struct RibonUpdateTransactionSnapshot *snapshot)
{
    uint8_t selector_bytes[2][RIBON_UPDATE_TRANSACTION_SELECTOR_BYTES];
    uint8_t record_bytes[RIBON_UPDATE_TRANSACTION_RECORD_BYTES];
    struct TransactionSelectorView selectors[2];
    struct TransactionRecordView record;
    int valid[2] = {0, 0};
    uint32_t selected;
    uint32_t index;
    int status;
    if (snapshot != NULL) {
        memset(snapshot, 0, sizeof(*snapshot));
    }
    if (snapshot == NULL || !journal_is_valid(journal)) {
        return RIBON_UPDATE_TRANSACTION_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0u; index < 2u; ++index) {
        status = provider_read_exact(journal, selector_offset(journal, index),
            selector_bytes[index], sizeof(selector_bytes[index]));
        if (status != RIBON_UPDATE_TRANSACTION_STATUS_OK) {
            return status;
        }
        valid[index] = selector_decode(selector_bytes[index], &selectors[index]);
    }
    if (!valid[0] && !valid[1]) {
        return bytes_zero(selector_bytes, sizeof(selector_bytes)) ?
            RIBON_UPDATE_TRANSACTION_STATUS_UNINITIALIZED :
            RIBON_UPDATE_TRANSACTION_STATUS_CORRUPT;
    }
    if (valid[0] && valid[1] && selectors[0].generation == selectors[1].generation) {
        return RIBON_UPDATE_TRANSACTION_STATUS_CONFLICT;
    }
    selected = !valid[0] ? 1u : !valid[1] ? 0u :
        (selectors[1].generation > selectors[0].generation ? 1u : 0u);
    if (selectors[selected].generation < journal->minimum_generation) {
        return RIBON_UPDATE_TRANSACTION_STATUS_REPLAY;
    }
    status = provider_read_exact(journal,
        record_offset(journal, selectors[selected].record_slot),
        record_bytes, sizeof(record_bytes));
    if (status != RIBON_UPDATE_TRANSACTION_STATUS_OK) {
        return status;
    }
    if (!record_decode(record_bytes, &record) ||
        record.generation != selectors[selected].generation ||
        record.predecessor_generation !=
            selectors[selected].predecessor_generation ||
        !bytes_equal(record.record_digest,
            selectors[selected].record_digest, 32u) ||
        !bytes_equal(record.predecessor_digest,
            selectors[selected].predecessor_digest, 32u)) {
        return RIBON_UPDATE_TRANSACTION_STATUS_CORRUPT;
    }
    snapshot->size = sizeof(*snapshot);
    snapshot->abi_version = RIBON_UPDATE_TRANSACTION_ABI_VERSION;
    snapshot->selected_record_slot = selectors[selected].record_slot;
    snapshot->selected_selector_slot = selected;
    snapshot->target_slot = record.target_slot;
    snapshot->target_state = record.target_state;
    snapshot->journal_generation = record.generation;
    snapshot->predecessor_generation = record.predecessor_generation;
    memcpy(snapshot->record_digest, record.record_digest, 32u);
    memcpy(snapshot->predecessor_digest, record.predecessor_digest, 32u);
    snapshot->metadata = record.metadata;
    return RIBON_UPDATE_TRANSACTION_STATUS_OK;
}

static int commit_record(
    const struct RibonUpdateTransactionJournal *journal,
    const struct RibonUpdateTransactionSnapshot *current,
    const struct RibonUpdateSlotMetadata *metadata,
    uint32_t target_slot,
    struct RibonUpdateTransactionObserver *observer,
    struct RibonUpdateTransactionSnapshot *snapshot)
{
    uint8_t record[RIBON_UPDATE_TRANSACTION_RECORD_BYTES];
    uint8_t readback[RIBON_UPDATE_TRANSACTION_RECORD_BYTES];
    uint8_t selector[RIBON_UPDATE_TRANSACTION_SELECTOR_BYTES];
    uint8_t predecessor_digest[32] = {0};
    uint64_t generation = 1u;
    uint64_t predecessor_generation = 0u;
    uint32_t record_slot = 0u;
    uint32_t selector_slot = 0u;
    enum RibonUpdateSlotState state;
    int status;
    if (current != NULL) {
        if (current->journal_generation == UINT64_MAX ||
            metadata->metadata_generation != current->journal_generation + 1u) {
            return RIBON_UPDATE_TRANSACTION_STATUS_OVERFLOW;
        }
        generation = current->journal_generation + 1u;
        predecessor_generation = current->journal_generation;
        memcpy(predecessor_digest, current->record_digest, 32u);
        record_slot = 1u - current->selected_record_slot;
        selector_slot = 1u - current->selected_selector_slot;
    } else if (metadata->metadata_generation != 1u) {
        return RIBON_UPDATE_TRANSACTION_STATUS_STATE;
    }
    state = metadata->slots[target_slot].state;
    if (!record_encode(metadata, target_slot, generation,
            predecessor_generation, predecessor_digest, record) ||
        !selector_encode(record_slot, generation, predecessor_generation,
            record + RECORD_DIGEST_OFFSET, predecessor_digest, selector)) {
        return RIBON_UPDATE_TRANSACTION_STATUS_STATE;
    }
#define OBSERVE(operation_value, boundary_value) \
    do { \
        if (ribon_update_transaction_observe(observer, (operation_value), \
                (boundary_value), state, NO_COMPONENT, generation) != 0) { \
            return RIBON_UPDATE_TRANSACTION_STATUS_INTERRUPTED; \
        } \
    } while (0)
    OBSERVE(RIBON_UPDATE_TRANSACTION_OPERATION_JOURNAL_RECORD_WRITE,
        RIBON_UPDATE_TRANSACTION_BOUNDARY_BEFORE);
    status = provider_write_exact(journal, record_offset(journal, record_slot),
        record, sizeof(record));
    if (status != RIBON_UPDATE_TRANSACTION_STATUS_OK) {
        return status;
    }
    OBSERVE(RIBON_UPDATE_TRANSACTION_OPERATION_JOURNAL_RECORD_WRITE,
        RIBON_UPDATE_TRANSACTION_BOUNDARY_AFTER);
    OBSERVE(RIBON_UPDATE_TRANSACTION_OPERATION_JOURNAL_RECORD_FLUSH,
        RIBON_UPDATE_TRANSACTION_BOUNDARY_BEFORE);
    status = provider_flush(journal);
    if (status != RIBON_UPDATE_TRANSACTION_STATUS_OK) {
        return status;
    }
    OBSERVE(RIBON_UPDATE_TRANSACTION_OPERATION_JOURNAL_RECORD_FLUSH,
        RIBON_UPDATE_TRANSACTION_BOUNDARY_AFTER);
    OBSERVE(RIBON_UPDATE_TRANSACTION_OPERATION_JOURNAL_RECORD_READBACK,
        RIBON_UPDATE_TRANSACTION_BOUNDARY_BEFORE);
    status = provider_read_exact(journal, record_offset(journal, record_slot),
        readback, sizeof(readback));
    if (status != RIBON_UPDATE_TRANSACTION_STATUS_OK) {
        return status;
    }
    if (!bytes_equal(record, readback, sizeof(record))) {
        return RIBON_UPDATE_TRANSACTION_STATUS_CORRUPT;
    }
    OBSERVE(RIBON_UPDATE_TRANSACTION_OPERATION_JOURNAL_RECORD_READBACK,
        RIBON_UPDATE_TRANSACTION_BOUNDARY_AFTER);
    OBSERVE(RIBON_UPDATE_TRANSACTION_OPERATION_JOURNAL_SELECTOR_WRITE,
        RIBON_UPDATE_TRANSACTION_BOUNDARY_BEFORE);
    status = provider_write_exact(journal, selector_offset(journal, selector_slot),
        selector, sizeof(selector));
    if (status != RIBON_UPDATE_TRANSACTION_STATUS_OK) {
        return status;
    }
    OBSERVE(RIBON_UPDATE_TRANSACTION_OPERATION_JOURNAL_SELECTOR_WRITE,
        RIBON_UPDATE_TRANSACTION_BOUNDARY_AFTER);
    OBSERVE(RIBON_UPDATE_TRANSACTION_OPERATION_JOURNAL_SELECTOR_FLUSH,
        RIBON_UPDATE_TRANSACTION_BOUNDARY_BEFORE);
    status = provider_flush(journal);
    if (status != RIBON_UPDATE_TRANSACTION_STATUS_OK) {
        return status;
    }
    OBSERVE(RIBON_UPDATE_TRANSACTION_OPERATION_JOURNAL_SELECTOR_FLUSH,
        RIBON_UPDATE_TRANSACTION_BOUNDARY_AFTER);
    OBSERVE(RIBON_UPDATE_TRANSACTION_OPERATION_JOURNAL_REOPEN,
        RIBON_UPDATE_TRANSACTION_BOUNDARY_BEFORE);
    status = ribon_update_transaction_open(journal, snapshot);
    if (status != RIBON_UPDATE_TRANSACTION_STATUS_OK ||
        snapshot->journal_generation != generation ||
        snapshot->target_slot != target_slot || snapshot->target_state != state ||
        !bytes_equal(snapshot->record_digest,
            record + RECORD_DIGEST_OFFSET, 32u)) {
        return status == RIBON_UPDATE_TRANSACTION_STATUS_OK ?
            RIBON_UPDATE_TRANSACTION_STATUS_CORRUPT : status;
    }
    OBSERVE(RIBON_UPDATE_TRANSACTION_OPERATION_JOURNAL_REOPEN,
        RIBON_UPDATE_TRANSACTION_BOUNDARY_AFTER);
#undef OBSERVE
    return RIBON_UPDATE_TRANSACTION_STATUS_OK;
}

/** @brief 빈 journal을 generation 1의 confirmed metadata로 provision한다. */
int ribon_update_transaction_initialize(
    const struct RibonUpdateTransactionJournal *journal,
    const struct RibonUpdateSlotMetadata *initial_metadata,
    struct RibonUpdateTransactionObserver *observer,
    struct RibonUpdateTransactionSnapshot *snapshot)
{
    struct RibonUpdateTransactionSnapshot existing;
    int status;
    if (snapshot != NULL) {
        memset(snapshot, 0, sizeof(*snapshot));
    }
    if (snapshot == NULL || !journal_is_valid(journal) || initial_metadata == NULL ||
        initial_metadata->active_slot >= RIBON_UPDATE_SLOT_COUNT ||
        initial_metadata->pending_slot != RIBON_UPDATE_SLOT_NONE ||
        initial_metadata->metadata_generation != 1u ||
        (observer != NULL &&
         !ribon_update_transaction_observer_is_valid(observer))) {
        return RIBON_UPDATE_TRANSACTION_STATUS_INVALID_ARGUMENT;
    }
    status = ribon_update_transaction_open(journal, &existing);
    if (status != RIBON_UPDATE_TRANSACTION_STATUS_UNINITIALIZED) {
        return status == RIBON_UPDATE_TRANSACTION_STATUS_OK ?
            RIBON_UPDATE_TRANSACTION_STATUS_STATE : status;
    }
    return commit_record(journal, NULL, initial_metadata,
        initial_metadata->active_slot, observer, snapshot);
}

static int transition_pending(
    const struct RibonUpdateTransactionSnapshot *verified,
    uint32_t target_slot,
    uint32_t attempts,
    struct RibonUpdateSlotMetadata *pending)
{
    struct RibonUpdateSlotTransition transition = {0};
    const struct RibonUpdateSlotEntry *entry =
        &verified->metadata.slots[target_slot];
    transition.size = sizeof(transition);
    transition.abi_version = RIBON_UPDATE_STORAGE_ABI_VERSION;
    transition.slot_id = target_slot;
    transition.next_state = RIBON_UPDATE_SLOT_PENDING;
    transition.boot_attempts = attempts;
    transition.image_generation = entry->image_generation;
    memcpy(transition.manifest_digest, entry->manifest_digest, 32u);
    memcpy(transition.image_set_digest, entry->image_set_digest, 32u);
    memcpy(transition.layout_digest, entry->layout_digest, 32u);
    return ribon_update_slot_metadata_transition(
        &verified->metadata, &transition, pending) ==
        RIBON_UPDATE_STORAGE_STATUS_OK;
}

/** @brief Signed bundle을 STAGING, VERIFIED, PENDING journal commit으로 설치한다. */
int ribon_update_install_transactionally(
    const struct RibonUpdateTransactionalInstallRequest *request,
    struct RibonUpdateTransactionalInstallResult *result)
{
    struct RibonUpdateTransactionSnapshot current;
    struct RibonUpdateTransactionSnapshot next;
    struct RibonUpdateInstallRequest install;
    struct RibonUpdateInstallPlan plan;
    struct RibonUpdateInstallResult installed;
    struct RibonUpdateSlotMetadata pending;
    enum RibonUpdateSlotState resumed_from;
    int status;
    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
    if (request == NULL || result == NULL || request->size != sizeof(*request) ||
        request->abi_version != RIBON_UPDATE_TRANSACTION_ABI_VERSION ||
        request->flags != 0u || request->pending_attempts == 0u ||
        request->pending_attempts > 32u || request->install == NULL ||
        request->journal == NULL || request->install->current_metadata != NULL ||
        !bytes_zero(request->reserved, sizeof(request->reserved)) ||
        (request->observer != NULL &&
         !ribon_update_transaction_observer_is_valid(request->observer))) {
        return RIBON_UPDATE_TRANSACTION_STATUS_INVALID_ARGUMENT;
    }
    status = ribon_update_transaction_open(request->journal, &current);
    if (status != RIBON_UPDATE_TRANSACTION_STATUS_OK) {
        return status;
    }
    install = *request->install;
    install.current_metadata = &current.metadata;
    install.observer = request->observer;
    status = ribon_update_install_prepare(&install, &plan);
    if (status != RIBON_UPDATE_INSTALL_STATUS_OK) {
        return status == RIBON_UPDATE_INSTALL_STATUS_AUTHORIZATION ?
            RIBON_UPDATE_TRANSACTION_STATUS_AUTHORIZATION :
            RIBON_UPDATE_TRANSACTION_STATUS_INSTALL;
    }
    resumed_from = plan.resume_state;
    if (plan.resume_state == RIBON_UPDATE_SLOT_PENDING) {
        result->size = sizeof(*result);
        result->abi_version = RIBON_UPDATE_TRANSACTION_ABI_VERSION;
        result->resumed_from = resumed_from;
        result->target_slot = install.target_slot;
        result->component_count = plan.component_count;
        result->snapshot = current;
        return RIBON_UPDATE_TRANSACTION_STATUS_OK;
    }
    if (plan.resume_state != RIBON_UPDATE_SLOT_STAGING &&
        plan.resume_state != RIBON_UPDATE_SLOT_VERIFIED) {
        status = commit_record(request->journal, &current,
            &plan.staging_metadata, install.target_slot,
            request->observer, &next);
        if (status != RIBON_UPDATE_TRANSACTION_STATUS_OK) {
            return status;
        }
        current = next;
        install.current_metadata = &current.metadata;
    }
    if (current.metadata.slots[install.target_slot].state ==
            RIBON_UPDATE_SLOT_STAGING) {
        status = ribon_update_install_signed_bundle(&install, &installed);
        if (status != RIBON_UPDATE_INSTALL_STATUS_OK) {
            return status == RIBON_UPDATE_INSTALL_STATUS_INTERRUPTED ?
                RIBON_UPDATE_TRANSACTION_STATUS_INTERRUPTED :
                RIBON_UPDATE_TRANSACTION_STATUS_INSTALL;
        }
        status = commit_record(request->journal, &current,
            &installed.verified_metadata, install.target_slot,
            request->observer, &next);
        if (status != RIBON_UPDATE_TRANSACTION_STATUS_OK) {
            return status;
        }
        current = next;
    } else {
        memset(&installed, 0, sizeof(installed));
    }
    if (current.metadata.slots[install.target_slot].state !=
            RIBON_UPDATE_SLOT_VERIFIED ||
        !transition_pending(&current, install.target_slot,
            request->pending_attempts, &pending)) {
        return RIBON_UPDATE_TRANSACTION_STATUS_STATE;
    }
    status = commit_record(request->journal, &current, &pending,
        install.target_slot, request->observer, &next);
    if (status != RIBON_UPDATE_TRANSACTION_STATUS_OK) {
        return status;
    }
    result->size = sizeof(*result);
    result->abi_version = RIBON_UPDATE_TRANSACTION_ABI_VERSION;
    result->resumed_from = resumed_from;
    result->target_slot = install.target_slot;
    result->component_count = plan.component_count;
    result->installed_exact_bytes = installed.installed_exact_bytes;
    result->installed_backing_bytes = installed.installed_backing_bytes;
    result->snapshot = next;
    return RIBON_UPDATE_TRANSACTION_STATUS_OK;
}

/** @brief Transaction status의 stable diagnostic name을 반환한다. */
const char *ribon_update_transaction_status_name(
    enum RibonUpdateTransactionStatus status)
{
    switch (status) {
    case RIBON_UPDATE_TRANSACTION_STATUS_OK: return "ok";
    case RIBON_UPDATE_TRANSACTION_STATUS_INVALID_ARGUMENT: return "invalid-argument";
    case RIBON_UPDATE_TRANSACTION_STATUS_UNINITIALIZED: return "uninitialized";
    case RIBON_UPDATE_TRANSACTION_STATUS_IO: return "io";
    case RIBON_UPDATE_TRANSACTION_STATUS_SHORT_IO: return "short-io";
    case RIBON_UPDATE_TRANSACTION_STATUS_CORRUPT: return "corrupt";
    case RIBON_UPDATE_TRANSACTION_STATUS_CONFLICT: return "conflict";
    case RIBON_UPDATE_TRANSACTION_STATUS_REPLAY: return "replay";
    case RIBON_UPDATE_TRANSACTION_STATUS_OVERFLOW: return "overflow";
    case RIBON_UPDATE_TRANSACTION_STATUS_STATE: return "state";
    case RIBON_UPDATE_TRANSACTION_STATUS_IDENTITY: return "identity";
    case RIBON_UPDATE_TRANSACTION_STATUS_AUTHORIZATION: return "authorization";
    case RIBON_UPDATE_TRANSACTION_STATUS_INSTALL: return "install";
    case RIBON_UPDATE_TRANSACTION_STATUS_INTERRUPTED: return "interrupted";
    default: return "unknown";
    }
}
