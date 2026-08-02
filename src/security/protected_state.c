#include <Ribon/security/protected_state.h>

#include "sha256.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define RECORD_MAGIC "RIBON-PSTATE-R1"
#define SELECTOR_MAGIC "RIBON-PSTATE-S1"
#define WIRE_MAGIC_BYTES 16u
#define RECORD_CHECKSUM_OFFSET 144u
#define SELECTOR_CHECKSUM_OFFSET 144u
#define WIRE_CHECKSUM_BYTES 4u
#define RECORD_PHASE_PREPARED 1u

struct RibonProtectedRecord {
    enum RibonProtectedStateKind kind;
    uint64_t confirmed_floor;
    uint64_t pending_sequence;
    uint32_t attempts;
    uint64_t generation;
    uint8_t domain[RIBON_PROTECTED_STATE_DIGEST_BYTES];
    uint8_t binding[RIBON_PROTECTED_STATE_DIGEST_BYTES];
    uint64_t attempt_sequence;
};

struct RibonProtectedSelector {
    uint32_t slot;
    uint64_t generation;
    uint8_t domain[RIBON_PROTECTED_STATE_DIGEST_BYTES];
    uint8_t record_digest[RIBON_PROTECTED_STATE_DIGEST_BYTES];
};

/** @brief Little-endian byte view에서 u32를 읽는다. */
static uint32_t
load_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

/** @brief Little-endian byte view에서 u64를 읽는다. */
static uint64_t
load_u64(const uint8_t *bytes)
{
    return (uint64_t)load_u32(bytes) |
        ((uint64_t)load_u32(bytes + 4u) << 32);
}

/** @brief u32를 canonical little-endian byte 순서로 쓴다. */
static void
store_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

/** @brief u64를 canonical little-endian byte 순서로 쓴다. */
static void
store_u64(uint8_t *bytes, uint64_t value)
{
    store_u32(bytes, (uint32_t)value);
    store_u32(bytes + 4u, (uint32_t)(value >> 32));
}

/** @brief Byte view가 전부 0인지 allocation 없이 검사한다. */
static int
bytes_zero(const uint8_t *bytes, size_t size)
{
    size_t index;
    uint8_t value = 0u;

    for (index = 0u; index < size; ++index) {
        value |= bytes[index];
    }
    return value == 0u;
}

/** @brief 두 byte view를 data-independent loop로 비교한다. */
static int
bytes_equal(const uint8_t *left, const uint8_t *right, size_t size)
{
    size_t index;
    uint8_t difference = 0u;

    for (index = 0u; index < size; ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == 0u;
}

/** @brief Wire object의 torn/corrupt byte 검출용 CRC32C를 계산한다. */
static uint32_t
crc32c(const uint8_t *bytes, size_t size)
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

/** @brief Exact 160-byte record의 selector identity SHA-256을 계산한다. */
static void
record_identity(
    const uint8_t record[RIBON_PROTECTED_STATE_RECORD_BYTES],
    uint8_t digest[RIBON_PROTECTED_STATE_DIGEST_BYTES])
{
    ribon_security_sha256(record, RIBON_PROTECTED_STATE_RECORD_BYTES, digest);
}

/** @brief Provider callback status를 engine의 stable status로 축약한다. */
static int
provider_status(int status)
{
    if (status == RIBON_PROTECTED_STATE_PROVIDER_UNAVAILABLE) {
        return RIBON_PROTECTED_STATE_STATUS_UNAVAILABLE;
    }
    return status == RIBON_PROTECTED_STATE_PROVIDER_OK
        ? RIBON_PROTECTED_STATE_STATUS_OK
        : RIBON_PROTECTED_STATE_STATUS_IO_ERROR;
}

/** @brief Journal domain namespace에서 logical object를 exact-read한다. */
static int
provider_read(
    const struct RibonProtectedStateJournal *journal,
    enum RibonProtectedStateObject object,
    uint32_t slot,
    uint8_t *bytes,
    size_t size)
{
    return provider_status(journal->provider->read(
        journal->provider, journal->domain_digest, object, slot, bytes, size));
}

/** @brief Journal domain namespace의 pending logical object를 exact-write한다. */
static int
provider_write(
    const struct RibonProtectedStateJournal *journal,
    enum RibonProtectedStateObject object,
    uint32_t slot,
    const uint8_t *bytes,
    size_t size)
{
    return provider_status(journal->provider->write(
        journal->provider, journal->domain_digest, object, slot, bytes, size));
}

/** @brief Journal domain의 선행 pending write를 durable하게 flush한다. */
static int
provider_flush(const struct RibonProtectedStateJournal *journal)
{
    return provider_status(journal->provider->flush(
        journal->provider, journal->domain_digest));
}

/** @brief Pointer-free logical state를 160-byte record로 encode한다. */
static void
record_encode(
    const struct RibonProtectedRecord *record,
    uint8_t bytes[RIBON_PROTECTED_STATE_RECORD_BYTES])
{
    memset(bytes, 0, RIBON_PROTECTED_STATE_RECORD_BYTES);
    memcpy(bytes, RECORD_MAGIC, sizeof(RECORD_MAGIC) - 1u);
    bytes[16] = 1u;
    store_u32(bytes + 20u, RIBON_PROTECTED_STATE_RECORD_BYTES);
    store_u32(bytes + 24u, RECORD_PHASE_PREPARED);
    store_u32(bytes + 28u, (uint32_t)record->kind);
    store_u64(bytes + 32u, record->generation);
    store_u64(bytes + 40u, record->confirmed_floor);
    store_u64(bytes + 48u, record->pending_sequence);
    store_u32(bytes + 56u, record->attempts);
    memcpy(bytes + 64u, record->domain, RIBON_PROTECTED_STATE_DIGEST_BYTES);
    memcpy(bytes + 96u, record->binding, RIBON_PROTECTED_STATE_DIGEST_BYTES);
    store_u64(bytes + 128u, record->attempt_sequence);
    store_u32(bytes + RECORD_CHECKSUM_OFFSET, crc32c(bytes, RECORD_CHECKSUM_OFFSET));
}

/** @brief 160-byte record를 재검산하고 logical state로 decode한다. */
static int
record_decode(
    const uint8_t bytes[RIBON_PROTECTED_STATE_RECORD_BYTES],
    struct RibonProtectedRecord *record)
{
    const enum RibonProtectedStateKind kind =
        (enum RibonProtectedStateKind)load_u32(bytes + 28u);
    const uint32_t attempts = load_u32(bytes + 56u);
    const uint64_t confirmed = load_u64(bytes + 40u);
    const uint64_t pending = load_u64(bytes + 48u);
    const int binding_is_zero = bytes_zero(
        bytes + 96u, RIBON_PROTECTED_STATE_DIGEST_BYTES);
    const uint64_t attempt_sequence = load_u64(bytes + 128u);

    if (!bytes_equal(bytes, (const uint8_t *)RECORD_MAGIC, sizeof(RECORD_MAGIC) - 1u) ||
        bytes[15] != 0u || bytes[16] != 1u || bytes[17] != 0u ||
        bytes[18] != 0u || bytes[19] != 0u ||
        load_u32(bytes + 20u) != RIBON_PROTECTED_STATE_RECORD_BYTES ||
        load_u32(bytes + 24u) != RECORD_PHASE_PREPARED ||
        load_u32(bytes + 60u) != 0u || load_u64(bytes + 136u) != 0u ||
        load_u32(bytes + RECORD_CHECKSUM_OFFSET) !=
            crc32c(bytes, RECORD_CHECKSUM_OFFSET) ||
        !bytes_zero(bytes + 148u, 12u) ||
        bytes_zero(bytes + 64u, RIBON_PROTECTED_STATE_DIGEST_BYTES) ||
        load_u64(bytes + 32u) == 0u ||
        (kind != RIBON_PROTECTED_STATE_KIND_CONFIRMED &&
         kind != RIBON_PROTECTED_STATE_KIND_TRIAL) ||
        (kind == RIBON_PROTECTED_STATE_KIND_CONFIRMED &&
         (pending != 0u || attempts != 0u ||
          (binding_is_zero != (attempt_sequence == 0u)))) ||
        (kind == RIBON_PROTECTED_STATE_KIND_TRIAL &&
         (confirmed == UINT64_MAX || pending != confirmed + 1u ||
          attempts > RIBON_PROTECTED_STATE_MAX_TRIAL_ATTEMPTS ||
          (binding_is_zero != (attempt_sequence == 0u))))) {
        return RIBON_PROTECTED_STATE_STATUS_CORRUPT;
    }
    *record = (struct RibonProtectedRecord){
        .kind = kind,
        .confirmed_floor = confirmed,
        .pending_sequence = pending,
        .attempts = attempts,
        .generation = load_u64(bytes + 32u),
        .attempt_sequence = attempt_sequence,
    };
    memcpy(record->domain, bytes + 64u, sizeof(record->domain));
    memcpy(record->binding, bytes + 96u, sizeof(record->binding));
    return RIBON_PROTECTED_STATE_STATUS_OK;
}

/** @brief Commit selector를 canonical 160-byte representation으로 encode한다. */
static void
selector_encode(
    const struct RibonProtectedSelector *selector,
    uint8_t bytes[RIBON_PROTECTED_STATE_SELECTOR_BYTES])
{
    memset(bytes, 0, RIBON_PROTECTED_STATE_SELECTOR_BYTES);
    memcpy(bytes, SELECTOR_MAGIC, sizeof(SELECTOR_MAGIC) - 1u);
    bytes[16] = 1u;
    store_u32(bytes + 20u, RIBON_PROTECTED_STATE_SELECTOR_BYTES);
    store_u32(bytes + 24u, selector->slot);
    store_u64(bytes + 32u, selector->generation);
    memcpy(bytes + 40u, selector->domain, RIBON_PROTECTED_STATE_DIGEST_BYTES);
    memcpy(bytes + 72u, selector->record_digest, RIBON_PROTECTED_STATE_DIGEST_BYTES);
    store_u32(bytes + SELECTOR_CHECKSUM_OFFSET, crc32c(bytes, SELECTOR_CHECKSUM_OFFSET));
}

/** @brief Selector wire bytes의 shape와 checksum을 독립 검증한다. */
static int
selector_decode(
    const uint8_t bytes[RIBON_PROTECTED_STATE_SELECTOR_BYTES],
    struct RibonProtectedSelector *selector)
{
    if (!bytes_equal(bytes, (const uint8_t *)SELECTOR_MAGIC, sizeof(SELECTOR_MAGIC) - 1u) ||
        bytes[15] != 0u || bytes[16] != 1u || bytes[17] != 0u ||
        bytes[18] != 0u || bytes[19] != 0u ||
        load_u32(bytes + 20u) != RIBON_PROTECTED_STATE_SELECTOR_BYTES ||
        load_u32(bytes + 24u) >= RIBON_PROTECTED_STATE_RECORD_SLOTS ||
        load_u32(bytes + 28u) != 0u || load_u64(bytes + 32u) == 0u ||
        bytes_zero(bytes + 40u, RIBON_PROTECTED_STATE_DIGEST_BYTES) ||
        bytes_zero(bytes + 72u, RIBON_PROTECTED_STATE_DIGEST_BYTES) ||
        !bytes_zero(bytes + 104u, SELECTOR_CHECKSUM_OFFSET - 104u) ||
        load_u32(bytes + SELECTOR_CHECKSUM_OFFSET) !=
            crc32c(bytes, SELECTOR_CHECKSUM_OFFSET) ||
        !bytes_zero(bytes + 148u, 12u)) {
        return RIBON_PROTECTED_STATE_STATUS_CORRUPT;
    }
    *selector = (struct RibonProtectedSelector){
        .slot = load_u32(bytes + 24u),
        .generation = load_u64(bytes + 32u),
    };
    memcpy(selector->domain, bytes + 40u, sizeof(selector->domain));
    memcpy(selector->record_digest, bytes + 72u, sizeof(selector->record_digest));
    return RIBON_PROTECTED_STATE_STATUS_OK;
}

/** @brief Journal view의 ABI, domain과 provider descriptor를 검사한다. */
static int
journal_validate(const struct RibonProtectedStateJournal *journal)
{
    if (journal == NULL || journal->size != sizeof(*journal) ||
        journal->abi_version != RIBON_PROTECTED_STATE_ABI_VERSION ||
        bytes_zero(journal->domain_digest, sizeof(journal->domain_digest)) ||
        !bytes_zero((const uint8_t *)journal->reserved, sizeof(journal->reserved))) {
        return RIBON_PROTECTED_STATE_STATUS_INVALID_ARGUMENT;
    }
    return ribon_protected_state_provider_validate(journal->provider);
}

/** @brief Decoded record를 caller-owned public snapshot으로 복사한다. */
static void
snapshot_from_record(
    const struct RibonProtectedRecord *record,
    uint32_t slot,
    struct RibonProtectedStateSnapshot *snapshot)
{
    *snapshot = (struct RibonProtectedStateSnapshot){
        .size = sizeof(*snapshot),
        .abi_version = RIBON_PROTECTED_STATE_ABI_VERSION,
        .kind = record->kind,
        .selected_slot = slot,
        .confirmed_floor = record->confirmed_floor,
        .pending_sequence = record->pending_sequence,
        .trial_attempts_remaining = record->attempts,
        .generation = record->generation,
    };
    memcpy(snapshot->domain_digest, record->domain, sizeof(snapshot->domain_digest));
    memcpy(snapshot->trial_binding_digest, record->binding,
           sizeof(snapshot->trial_binding_digest));
    snapshot->attempt_sequence = record->attempt_sequence;
}

/** @brief Inactive record와 selector를 ordered write로 commit한다. */
static int
commit_record(
    const struct RibonProtectedStateJournal *journal,
    uint32_t slot,
    const struct RibonProtectedRecord *record,
    struct RibonProtectedStateSnapshot *snapshot)
{
    struct RibonProtectedSelector selector = {
        .slot = slot,
        .generation = record->generation,
    };
    uint8_t record_bytes[RIBON_PROTECTED_STATE_RECORD_BYTES];
    uint8_t readback[RIBON_PROTECTED_STATE_RECORD_BYTES];
    uint8_t selector_bytes[RIBON_PROTECTED_STATE_SELECTOR_BYTES];
    struct RibonProtectedStateSnapshot opened;
    int status;

    memcpy(selector.domain, record->domain, sizeof(selector.domain));
    record_encode(record, record_bytes);
    record_identity(record_bytes, selector.record_digest);
    selector_encode(&selector, selector_bytes);
    status = provider_write(journal, RIBON_PROTECTED_STATE_OBJECT_RECORD,
                            slot, record_bytes, sizeof(record_bytes));
    if (status != RIBON_PROTECTED_STATE_STATUS_OK) {
        return status;
    }
    status = provider_flush(journal);
    if (status != RIBON_PROTECTED_STATE_STATUS_OK) {
        return status;
    }
    status = provider_read(journal, RIBON_PROTECTED_STATE_OBJECT_RECORD,
                           slot, readback, sizeof(readback));
    if (status != RIBON_PROTECTED_STATE_STATUS_OK) {
        return status;
    }
    if (!bytes_equal(record_bytes, readback, sizeof(record_bytes))) {
        return RIBON_PROTECTED_STATE_STATUS_READBACK_MISMATCH;
    }
    status = provider_write(journal, RIBON_PROTECTED_STATE_OBJECT_SELECTOR,
                            slot, selector_bytes, sizeof(selector_bytes));
    if (status != RIBON_PROTECTED_STATE_STATUS_OK) {
        return status;
    }
    status = provider_flush(journal);
    if (status != RIBON_PROTECTED_STATE_STATUS_OK) {
        return status;
    }
    status = ribon_protected_state_open(journal, &opened);
    if (status != RIBON_PROTECTED_STATE_STATUS_OK ||
        opened.generation != record->generation || opened.selected_slot != slot) {
        return status == RIBON_PROTECTED_STATE_STATUS_OK
            ? RIBON_PROTECTED_STATE_STATUS_READBACK_MISMATCH : status;
    }
    if (snapshot != NULL) {
        *snapshot = opened;
    }
    return RIBON_PROTECTED_STATE_STATUS_OK;
}

/** @brief Current generation의 반대 slot에 다음 logical state를 commit한다. */
static int
transition_record(
    const struct RibonProtectedStateJournal *journal,
    const struct RibonProtectedStateSnapshot *current,
    struct RibonProtectedRecord *next,
    struct RibonProtectedStateSnapshot *snapshot)
{
    if (current->generation == UINT64_MAX) {
        return RIBON_PROTECTED_STATE_STATUS_OVERFLOW;
    }
    next->generation = current->generation + 1u;
    memcpy(next->domain, current->domain_digest, sizeof(next->domain));
    return commit_record(journal, 1u - current->selected_slot, next, snapshot);
}

/** @brief Provider descriptor의 exact v1 callback/shape contract를 검사한다. */
int
ribon_protected_state_provider_validate(
    const struct RibonProtectedStateProvider *provider)
{
    if (provider == NULL ||
        provider->magic != RIBON_PROTECTED_STATE_PROVIDER_MAGIC ||
        provider->size != sizeof(*provider) ||
        provider->abi_version != RIBON_PROTECTED_STATE_ABI_VERSION ||
        (provider->provider_class != RIBON_PROTECTED_STATE_PROVIDER_CLASS_HARDWARE &&
         provider->provider_class != RIBON_PROTECTED_STATE_PROVIDER_CLASS_REFERENCE &&
         provider->provider_class != RIBON_PROTECTED_STATE_PROVIDER_CLASS_FIXTURE) ||
        provider->flags != 0u ||
        provider->record_slots != RIBON_PROTECTED_STATE_RECORD_SLOTS ||
        provider->selector_slots != RIBON_PROTECTED_STATE_RECORD_SLOTS ||
        provider->reserved0 != 0u ||
        provider->record_bytes != RIBON_PROTECTED_STATE_RECORD_BYTES ||
        provider->selector_bytes != RIBON_PROTECTED_STATE_SELECTOR_BYTES ||
        provider->id == NULL || provider->id[0] == '\0' ||
        provider->read == NULL || provider->write == NULL || provider->flush == NULL ||
        !bytes_zero((const uint8_t *)provider->reserved, sizeof(provider->reserved))) {
        return RIBON_PROTECTED_STATE_STATUS_INVALID_PROVIDER;
    }
    return RIBON_PROTECTED_STATE_STATUS_OK;
}

/** @brief Product binding의 provider와 sorted domain closure를 검사한다. */
int
ribon_protected_state_binding_validate(
    const struct RibonProtectedStateProductBinding *binding)
{
    uint32_t index;

    if (binding == NULL || binding->size != sizeof(*binding) ||
        binding->abi_version != RIBON_PROTECTED_STATE_ABI_VERSION ||
        binding->flags != 0u || binding->reserved0 != 0u ||
        !bytes_zero((const uint8_t *)binding->reserved, sizeof(binding->reserved)) ||
        binding->domain_digests == NULL || binding->domain_count == 0u ||
        binding->domain_count > RIBON_PROTECTED_STATE_MAX_DOMAINS ||
        ribon_protected_state_provider_validate(binding->provider) !=
            RIBON_PROTECTED_STATE_STATUS_OK ||
        binding->provider_class != binding->provider->provider_class) {
        return RIBON_PROTECTED_STATE_STATUS_INVALID_PROVIDER;
    }
    for (index = 0u; index < binding->domain_count; ++index) {
        if (bytes_zero(binding->domain_digests[index],
                       RIBON_PROTECTED_STATE_DIGEST_BYTES) ||
            (index > 0u && memcmp(binding->domain_digests[index - 1u],
                                 binding->domain_digests[index],
                                 RIBON_PROTECTED_STATE_DIGEST_BYTES) >= 0)) {
            return RIBON_PROTECTED_STATE_STATUS_INVALID_PROVIDER;
        }
    }
    return RIBON_PROTECTED_STATE_STATUS_OK;
}

/** @brief Product가 승인한 exact domain digest를 journal namespace로 묶는다. */
int
ribon_protected_state_journal_bind(
    const struct RibonProtectedStateProductBinding *binding,
    const uint8_t domain_digest[RIBON_PROTECTED_STATE_DIGEST_BYTES],
    struct RibonProtectedStateJournal *journal)
{
    uint32_t index;

    if (journal == NULL || domain_digest == NULL ||
        ribon_protected_state_binding_validate(binding) !=
            RIBON_PROTECTED_STATE_STATUS_OK) {
        return RIBON_PROTECTED_STATE_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0u; index < binding->domain_count; ++index) {
        if (bytes_equal(binding->domain_digests[index], domain_digest,
                        RIBON_PROTECTED_STATE_DIGEST_BYTES)) {
            *journal = (struct RibonProtectedStateJournal){
                .size = sizeof(*journal),
                .abi_version = RIBON_PROTECTED_STATE_ABI_VERSION,
                .provider = binding->provider,
            };
            memcpy(journal->domain_digest, domain_digest,
                   sizeof(journal->domain_digest));
            return RIBON_PROTECTED_STATE_STATUS_OK;
        }
    }
    return RIBON_PROTECTED_STATE_STATUS_DOMAIN_MISMATCH;
}

/** @brief 두 selector와 두 record에서 현재 authoritative generation을 연다. */
int
ribon_protected_state_open(
    const struct RibonProtectedStateJournal *journal,
    struct RibonProtectedStateSnapshot *snapshot)
{
    uint8_t selector_bytes[2][RIBON_PROTECTED_STATE_SELECTOR_BYTES];
    uint8_t record_bytes[2][RIBON_PROTECTED_STATE_RECORD_BYTES];
    uint8_t digest[RIBON_PROTECTED_STATE_DIGEST_BYTES];
    struct RibonProtectedSelector selectors[2];
    struct RibonProtectedRecord records[2];
    int selector_status[2];
    int record_status[2];
    uint32_t selected_selector;
    uint32_t index;
    int status = journal_validate(journal);

    if (snapshot == NULL) {
        return RIBON_PROTECTED_STATE_STATUS_INVALID_ARGUMENT;
    }
    if (status != RIBON_PROTECTED_STATE_STATUS_OK) {
        return status;
    }
    for (index = 0u; index < 2u; ++index) {
        status = provider_read(journal,
                               RIBON_PROTECTED_STATE_OBJECT_SELECTOR,
                               index, selector_bytes[index],
                               sizeof(selector_bytes[index]));
        if (status != RIBON_PROTECTED_STATE_STATUS_OK) {
            return status;
        }
        selector_status[index] = bytes_zero(selector_bytes[index],
                                             sizeof(selector_bytes[index]))
            ? RIBON_PROTECTED_STATE_STATUS_UNINITIALIZED
            : selector_decode(selector_bytes[index], &selectors[index]);
        status = provider_read(journal,
                               RIBON_PROTECTED_STATE_OBJECT_RECORD,
                               index, record_bytes[index], sizeof(record_bytes[index]));
        if (status != RIBON_PROTECTED_STATE_STATUS_OK) {
            return status;
        }
        record_status[index] = record_decode(record_bytes[index], &records[index]);
    }
    if (selector_status[0] != RIBON_PROTECTED_STATE_STATUS_OK &&
        selector_status[1] != RIBON_PROTECTED_STATE_STATUS_OK) {
        return selector_status[0] == RIBON_PROTECTED_STATE_STATUS_UNINITIALIZED &&
               selector_status[1] == RIBON_PROTECTED_STATE_STATUS_UNINITIALIZED
            ? RIBON_PROTECTED_STATE_STATUS_UNINITIALIZED
            : RIBON_PROTECTED_STATE_STATUS_CORRUPT;
    }
    if (selector_status[0] == RIBON_PROTECTED_STATE_STATUS_OK &&
        selector_status[1] == RIBON_PROTECTED_STATE_STATUS_OK) {
        if (selectors[0].generation == selectors[1].generation &&
            !bytes_equal(selector_bytes[0], selector_bytes[1],
                         RIBON_PROTECTED_STATE_SELECTOR_BYTES)) {
            return RIBON_PROTECTED_STATE_STATUS_CONFLICT;
        }
        selected_selector = selectors[1].generation > selectors[0].generation
            ? 1u : 0u;
    } else {
        selected_selector = selector_status[0] == RIBON_PROTECTED_STATE_STATUS_OK
            ? 0u : 1u;
    }
    index = selectors[selected_selector].slot;
    record_identity(record_bytes[index], digest);
    if (record_status[index] != RIBON_PROTECTED_STATE_STATUS_OK ||
        selectors[selected_selector].generation != records[index].generation ||
        !bytes_equal(selectors[selected_selector].domain, journal->domain_digest,
                     RIBON_PROTECTED_STATE_DIGEST_BYTES) ||
        !bytes_equal(records[index].domain, journal->domain_digest,
                     RIBON_PROTECTED_STATE_DIGEST_BYTES) ||
        !bytes_equal(selectors[selected_selector].record_digest, digest,
                     sizeof(digest))) {
        return RIBON_PROTECTED_STATE_STATUS_CORRUPT;
    }
    if (record_status[1u - index] == RIBON_PROTECTED_STATE_STATUS_OK &&
        records[1u - index].generation == records[index].generation &&
        !bytes_equal(record_bytes[1u - index], record_bytes[index],
                     RIBON_PROTECTED_STATE_RECORD_BYTES)) {
        return RIBON_PROTECTED_STATE_STATUS_CONFLICT;
    }
    snapshot_from_record(&records[index], index, snapshot);
    return RIBON_PROTECTED_STATE_STATUS_OK;
}

/** @brief 빈 journal을 one-time confirmed floor로 provision한다. */
int
ribon_protected_state_initialize(
    const struct RibonProtectedStateJournal *journal,
    uint64_t confirmed_floor,
    struct RibonProtectedStateSnapshot *snapshot)
{
    struct RibonProtectedStateSnapshot current;
    struct RibonProtectedRecord record = {
        .kind = RIBON_PROTECTED_STATE_KIND_CONFIRMED,
        .confirmed_floor = confirmed_floor,
        .generation = 1u,
    };
    const int status = ribon_protected_state_open(journal, &current);

    if (status != RIBON_PROTECTED_STATE_STATUS_UNINITIALIZED) {
        return status == RIBON_PROTECTED_STATE_STATUS_OK
            ? RIBON_PROTECTED_STATE_STATUS_ALREADY_INITIALIZED : status;
    }
    memcpy(record.domain, journal->domain_digest, sizeof(record.domain));
    return commit_record(journal, 0u, &record, snapshot);
}

/** @brief Optional boot binding과 함께 exact successor trial을 연다. */
static int
begin_trial(
    const struct RibonProtectedStateJournal *journal,
    uint64_t candidate_sequence,
    uint32_t attempts,
    const uint8_t *binding_digest,
    uint64_t attempt_sequence,
    struct RibonProtectedStateSnapshot *snapshot)
{
    struct RibonProtectedStateSnapshot current;
    struct RibonProtectedRecord next;
    int status = ribon_protected_state_open(journal, &current);

    if (status != RIBON_PROTECTED_STATE_STATUS_OK) {
        return status;
    }
    if (current.kind != RIBON_PROTECTED_STATE_KIND_CONFIRMED) {
        return RIBON_PROTECTED_STATE_STATUS_ROLLBACK;
    }
    if (current.confirmed_floor == UINT64_MAX) {
        return RIBON_PROTECTED_STATE_STATUS_OVERFLOW;
    }
    if (candidate_sequence != current.confirmed_floor + 1u) {
        return candidate_sequence <= current.confirmed_floor
            ? RIBON_PROTECTED_STATE_STATUS_ROLLBACK
            : RIBON_PROTECTED_STATE_STATUS_SEQUENCE_GAP;
    }
    if (attempts == 0u || attempts > RIBON_PROTECTED_STATE_MAX_TRIAL_ATTEMPTS) {
        return RIBON_PROTECTED_STATE_STATUS_INVALID_ARGUMENT;
    }
    if ((binding_digest == NULL && attempt_sequence != 0u) ||
        (binding_digest != NULL &&
         (bytes_zero(binding_digest, RIBON_PROTECTED_STATE_DIGEST_BYTES) ||
          attempt_sequence == 0u))) {
        return RIBON_PROTECTED_STATE_STATUS_INVALID_ARGUMENT;
    }
    next = (struct RibonProtectedRecord){
        .kind = RIBON_PROTECTED_STATE_KIND_TRIAL,
        .confirmed_floor = current.confirmed_floor,
        .pending_sequence = candidate_sequence,
        .attempts = attempts,
        .attempt_sequence = attempt_sequence,
    };
    if (binding_digest != NULL) {
        memcpy(next.binding, binding_digest, sizeof(next.binding));
    }
    return transition_record(journal, &current, &next, snapshot);
}

/** @brief Confirmed floor의 exact unbound successor trial을 durable하게 연다. */
int
ribon_protected_state_begin_trial(
    const struct RibonProtectedStateJournal *journal,
    uint64_t candidate_sequence,
    uint32_t attempts,
    struct RibonProtectedStateSnapshot *snapshot)
{
    return begin_trial(journal, candidate_sequence, attempts, NULL, 0u, snapshot);
}

/** @brief Exact successor trial을 canonical boot-attempt binding에 결속한다. */
int
ribon_protected_state_begin_bound_trial(
    const struct RibonProtectedStateJournal *journal,
    uint64_t candidate_sequence,
    uint32_t attempts,
    const uint8_t binding_digest[RIBON_PROTECTED_STATE_DIGEST_BYTES],
    uint64_t attempt_sequence,
    struct RibonProtectedStateSnapshot *snapshot)
{
    return begin_trial(journal, candidate_sequence, attempts,
                       binding_digest, attempt_sequence, snapshot);
}

/** @brief Existing trial을 strictly newer boot-attempt binding으로 재결속한다. */
int
ribon_protected_state_rebind_trial_attempt(
    const struct RibonProtectedStateJournal *journal,
    uint64_t pending_sequence,
    const uint8_t binding_digest[RIBON_PROTECTED_STATE_DIGEST_BYTES],
    uint64_t attempt_sequence,
    struct RibonProtectedStateSnapshot *snapshot)
{
    struct RibonProtectedStateSnapshot current;
    struct RibonProtectedRecord next;
    int status;

    if (binding_digest == NULL ||
        bytes_zero(binding_digest, RIBON_PROTECTED_STATE_DIGEST_BYTES) ||
        attempt_sequence == 0u) {
        return RIBON_PROTECTED_STATE_STATUS_INVALID_ARGUMENT;
    }
    status = ribon_protected_state_open(journal, &current);
    if (status != RIBON_PROTECTED_STATE_STATUS_OK) {
        return status;
    }
    if (current.kind != RIBON_PROTECTED_STATE_KIND_TRIAL ||
        current.pending_sequence != pending_sequence) {
        return RIBON_PROTECTED_STATE_STATUS_ROLLBACK;
    }
    if (current.trial_attempts_remaining == 0u) {
        return RIBON_PROTECTED_STATE_STATUS_ATTEMPTS_EXHAUSTED;
    }
    if (attempt_sequence <= current.attempt_sequence) {
        return RIBON_PROTECTED_STATE_STATUS_BINDING_MISMATCH;
    }
    next = (struct RibonProtectedRecord){
        .kind = RIBON_PROTECTED_STATE_KIND_TRIAL,
        .confirmed_floor = current.confirmed_floor,
        .pending_sequence = current.pending_sequence,
        .attempts = current.trial_attempts_remaining,
        .attempt_sequence = attempt_sequence,
    };
    memcpy(next.binding, binding_digest, sizeof(next.binding));
    return transition_record(journal, &current, &next, snapshot);
}

/** @brief Current state에서 한 sequence의 execution authority를 판정한다. */
int
ribon_protected_state_authorize(
    const struct RibonProtectedStateJournal *journal,
    uint64_t sequence,
    struct RibonProtectedStateDecision *decision)
{
    struct RibonProtectedStateSnapshot current;
    enum RibonProtectedStateAuthority authority;
    int status;

    if (decision == NULL) {
        return RIBON_PROTECTED_STATE_STATUS_INVALID_ARGUMENT;
    }
    status = ribon_protected_state_open(journal, &current);
    if (status != RIBON_PROTECTED_STATE_STATUS_OK) {
        return status;
    }
    if (sequence == current.confirmed_floor) {
        authority = RIBON_PROTECTED_STATE_AUTHORITY_CONFIRMED;
    } else if (current.kind == RIBON_PROTECTED_STATE_KIND_TRIAL &&
               sequence == current.pending_sequence) {
        if (current.trial_attempts_remaining == 0u) {
            return RIBON_PROTECTED_STATE_STATUS_ATTEMPTS_EXHAUSTED;
        }
        authority = RIBON_PROTECTED_STATE_AUTHORITY_TRIAL;
    } else {
        return sequence < current.confirmed_floor ||
               (current.kind == RIBON_PROTECTED_STATE_KIND_CONFIRMED &&
                sequence <= current.confirmed_floor)
            ? RIBON_PROTECTED_STATE_STATUS_ROLLBACK
            : RIBON_PROTECTED_STATE_STATUS_SEQUENCE_GAP;
    }
    *decision = (struct RibonProtectedStateDecision){
        .size = sizeof(*decision),
        .abi_version = RIBON_PROTECTED_STATE_ABI_VERSION,
        .authority = authority,
        .trial_attempts_remaining = current.trial_attempts_remaining,
        .sequence = sequence,
        .generation = current.generation,
    };
    memcpy(decision->domain_digest, current.domain_digest,
           sizeof(decision->domain_digest));
    return RIBON_PROTECTED_STATE_STATUS_OK;
}

/** @brief Pending transfer 전에 trial attempt 감소를 먼저 commit한다. */
int
ribon_protected_state_consume_trial_attempt(
    const struct RibonProtectedStateJournal *journal,
    uint64_t pending_sequence,
    struct RibonProtectedStateSnapshot *snapshot)
{
    struct RibonProtectedStateSnapshot current;
    struct RibonProtectedRecord next;
    int status = ribon_protected_state_open(journal, &current);

    if (status != RIBON_PROTECTED_STATE_STATUS_OK) {
        return status;
    }
    if (current.kind != RIBON_PROTECTED_STATE_KIND_TRIAL ||
        current.pending_sequence != pending_sequence) {
        return RIBON_PROTECTED_STATE_STATUS_ROLLBACK;
    }
    if (current.trial_attempts_remaining == 0u) {
        return RIBON_PROTECTED_STATE_STATUS_ATTEMPTS_EXHAUSTED;
    }
    next = (struct RibonProtectedRecord){
        .kind = RIBON_PROTECTED_STATE_KIND_TRIAL,
        .confirmed_floor = current.confirmed_floor,
        .pending_sequence = current.pending_sequence,
        .attempts = current.trial_attempts_remaining - 1u,
        .attempt_sequence = current.attempt_sequence,
    };
    memcpy(next.binding, current.trial_binding_digest, sizeof(next.binding));
    return transition_record(journal, &current, &next, snapshot);
}

/** @brief Exact pending sequence를 새 confirmed floor로 승격한다. */
int
ribon_protected_state_confirm(
    const struct RibonProtectedStateJournal *journal,
    uint64_t pending_sequence,
    struct RibonProtectedStateSnapshot *snapshot)
{
    struct RibonProtectedStateSnapshot current;
    struct RibonProtectedRecord next;
    int status = ribon_protected_state_open(journal, &current);

    if (status != RIBON_PROTECTED_STATE_STATUS_OK) {
        return status;
    }
    if (current.kind != RIBON_PROTECTED_STATE_KIND_TRIAL ||
        current.pending_sequence != pending_sequence ||
        !bytes_zero(current.trial_binding_digest,
                    sizeof(current.trial_binding_digest)) ||
        current.attempt_sequence != 0u) {
        return RIBON_PROTECTED_STATE_STATUS_ROLLBACK;
    }
    next = (struct RibonProtectedRecord){
        .kind = RIBON_PROTECTED_STATE_KIND_CONFIRMED,
        .confirmed_floor = pending_sequence,
    };
    return transition_record(journal, &current, &next, snapshot);
}

/** @brief Exact bound pending attempt를 idempotent하게 confirmed로 승격한다. */
int
ribon_protected_state_confirm_bound(
    const struct RibonProtectedStateJournal *journal,
    uint64_t pending_sequence,
    const uint8_t binding_digest[RIBON_PROTECTED_STATE_DIGEST_BYTES],
    uint64_t attempt_sequence,
    struct RibonProtectedStateSnapshot *snapshot)
{
    struct RibonProtectedStateSnapshot current;
    struct RibonProtectedRecord next;
    int status;

    if (snapshot == NULL || binding_digest == NULL ||
        bytes_zero(binding_digest, RIBON_PROTECTED_STATE_DIGEST_BYTES) ||
        attempt_sequence == 0u) {
        return RIBON_PROTECTED_STATE_STATUS_INVALID_ARGUMENT;
    }
    status = ribon_protected_state_open(journal, &current);
    if (status != RIBON_PROTECTED_STATE_STATUS_OK) {
        return status;
    }
    if (current.kind == RIBON_PROTECTED_STATE_KIND_CONFIRMED &&
        current.confirmed_floor == pending_sequence &&
        current.attempt_sequence == attempt_sequence &&
        bytes_equal(current.trial_binding_digest, binding_digest,
                    RIBON_PROTECTED_STATE_DIGEST_BYTES)) {
        *snapshot = current;
        return RIBON_PROTECTED_STATE_STATUS_OK;
    }
    if (current.kind != RIBON_PROTECTED_STATE_KIND_TRIAL ||
        current.pending_sequence != pending_sequence ||
        current.attempt_sequence != attempt_sequence ||
        !bytes_equal(current.trial_binding_digest, binding_digest,
                     RIBON_PROTECTED_STATE_DIGEST_BYTES)) {
        return RIBON_PROTECTED_STATE_STATUS_BINDING_MISMATCH;
    }
    next = (struct RibonProtectedRecord){
        .kind = RIBON_PROTECTED_STATE_KIND_CONFIRMED,
        .confirmed_floor = pending_sequence,
        .attempt_sequence = attempt_sequence,
    };
    memcpy(next.binding, binding_digest, sizeof(next.binding));
    return transition_record(journal, &current, &next, snapshot);
}

/** @brief Pending trial을 제거하고 기존 confirmed floor를 보존한다. */
int
ribon_protected_state_fail_trial(
    const struct RibonProtectedStateJournal *journal,
    struct RibonProtectedStateSnapshot *snapshot)
{
    struct RibonProtectedStateSnapshot current;
    struct RibonProtectedRecord next;
    int status = ribon_protected_state_open(journal, &current);

    if (status != RIBON_PROTECTED_STATE_STATUS_OK) {
        return status;
    }
    if (current.kind != RIBON_PROTECTED_STATE_KIND_TRIAL) {
        return RIBON_PROTECTED_STATE_STATUS_ROLLBACK;
    }
    next = (struct RibonProtectedRecord){
        .kind = RIBON_PROTECTED_STATE_KIND_CONFIRMED,
        .confirmed_floor = current.confirmed_floor,
        .attempt_sequence = current.attempt_sequence,
    };
    memcpy(next.binding, current.trial_binding_digest, sizeof(next.binding));
    return transition_record(journal, &current, &next, snapshot);
}
