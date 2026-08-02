#include <Ribon/security/protected_state.h>

#include "../../src/security/sha256.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_DOMAINS 2u
#define TEST_OBJECTS 2u
#define TEST_SLOTS 2u
#define RECORD_CHECKSUM_OFFSET 144u
#define SELECTOR_CHECKSUM_OFFSET 144u

struct TestDomainStorage {
    uint8_t domain[RIBON_PROTECTED_STATE_DIGEST_BYTES];
    uint8_t durable[TEST_OBJECTS][TEST_SLOTS][RIBON_PROTECTED_STATE_RECORD_BYTES];
    uint8_t pending[TEST_OBJECTS][TEST_SLOTS][RIBON_PROTECTED_STATE_RECORD_BYTES];
    uint8_t pending_valid[TEST_OBJECTS][TEST_SLOTS];
};

struct TestProvider {
    struct RibonProtectedStateProvider descriptor;
    struct TestDomainStorage domains[TEST_DOMAINS];
    uint32_t flush_count;
    uint32_t fault_flush;
    size_t fault_prefix;
    int unavailable;
};

static int failures;
static uint8_t normal_domain[RIBON_PROTECTED_STATE_DIGEST_BYTES];
static uint8_t recovery_domain[RIBON_PROTECTED_STATE_DIGEST_BYTES];

static void
expect(int condition, const char *name)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

static void
store_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void
store_u64(uint8_t *bytes, uint64_t value)
{
    store_u32(bytes, (uint32_t)value);
    store_u32(bytes + 4u, (uint32_t)(value >> 32));
}

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

static void
test_sha256_known_answer(void)
{
    static const uint8_t expected[RIBON_PROTECTED_STATE_DIGEST_BYTES] = {
        0xbau, 0x78u, 0x16u, 0xbfu, 0x8fu, 0x01u, 0xcfu, 0xeau,
        0x41u, 0x41u, 0x40u, 0xdeu, 0x5du, 0xaeu, 0x22u, 0x23u,
        0xb0u, 0x03u, 0x61u, 0xa3u, 0x96u, 0x17u, 0x7au, 0x9cu,
        0xb4u, 0x10u, 0xffu, 0x61u, 0xf2u, 0x00u, 0x15u, 0xadu,
    };
    uint8_t digest[RIBON_PROTECTED_STATE_DIGEST_BYTES];

    ribon_security_sha256((const uint8_t *)"abc", 3u, digest);
    expect(memcmp(digest, expected, sizeof(expected)) == 0,
           "SHA-256 abc known-answer vector");
}

static struct TestDomainStorage *
find_domain(struct TestProvider *provider, const uint8_t *domain)
{
    uint32_t index;

    for (index = 0u; index < TEST_DOMAINS; ++index) {
        if (memcmp(provider->domains[index].domain, domain,
                   RIBON_PROTECTED_STATE_DIGEST_BYTES) == 0) {
            return &provider->domains[index];
        }
    }
    return NULL;
}

static int
test_read(
    const struct RibonProtectedStateProvider *descriptor,
    const uint8_t domain[RIBON_PROTECTED_STATE_DIGEST_BYTES],
    enum RibonProtectedStateObject object,
    uint32_t slot,
    uint8_t *bytes,
    size_t size)
{
    struct TestProvider *provider = descriptor->context;
    struct TestDomainStorage *storage = find_domain(provider, domain);
    const uint32_t object_index = (uint32_t)object - 1u;

    if (provider->unavailable || storage == NULL) {
        return RIBON_PROTECTED_STATE_PROVIDER_UNAVAILABLE;
    }
    if (object_index >= TEST_OBJECTS || slot >= TEST_SLOTS ||
        size != RIBON_PROTECTED_STATE_RECORD_BYTES || bytes == NULL) {
        return RIBON_PROTECTED_STATE_PROVIDER_IO_ERROR;
    }
    memcpy(bytes, storage->durable[object_index][slot], size);
    return RIBON_PROTECTED_STATE_PROVIDER_OK;
}

static int
test_write(
    const struct RibonProtectedStateProvider *descriptor,
    const uint8_t domain[RIBON_PROTECTED_STATE_DIGEST_BYTES],
    enum RibonProtectedStateObject object,
    uint32_t slot,
    const uint8_t *bytes,
    size_t size)
{
    struct TestProvider *provider = descriptor->context;
    struct TestDomainStorage *storage = find_domain(provider, domain);
    const uint32_t object_index = (uint32_t)object - 1u;

    if (provider->unavailable || storage == NULL) {
        return RIBON_PROTECTED_STATE_PROVIDER_UNAVAILABLE;
    }
    if (object_index >= TEST_OBJECTS || slot >= TEST_SLOTS ||
        size != RIBON_PROTECTED_STATE_RECORD_BYTES || bytes == NULL) {
        return RIBON_PROTECTED_STATE_PROVIDER_IO_ERROR;
    }
    memcpy(storage->pending[object_index][slot], bytes, size);
    storage->pending_valid[object_index][slot] = 1u;
    return RIBON_PROTECTED_STATE_PROVIDER_OK;
}

static int
test_flush(
    const struct RibonProtectedStateProvider *descriptor,
    const uint8_t domain[RIBON_PROTECTED_STATE_DIGEST_BYTES])
{
    struct TestProvider *provider = descriptor->context;
    struct TestDomainStorage *storage = find_domain(provider, domain);
    uint32_t object;
    uint32_t slot;

    if (provider->unavailable || storage == NULL) {
        return RIBON_PROTECTED_STATE_PROVIDER_UNAVAILABLE;
    }
    ++provider->flush_count;
    for (object = 0u; object < TEST_OBJECTS; ++object) {
        for (slot = 0u; slot < TEST_SLOTS; ++slot) {
            if (storage->pending_valid[object][slot] == 0u) {
                continue;
            }
            if (provider->fault_flush == provider->flush_count) {
                memcpy(storage->durable[object][slot],
                       storage->pending[object][slot], provider->fault_prefix);
            } else {
                memcpy(storage->durable[object][slot],
                       storage->pending[object][slot],
                       RIBON_PROTECTED_STATE_RECORD_BYTES);
                storage->pending_valid[object][slot] = 0u;
            }
        }
    }
    return provider->fault_flush == provider->flush_count
        ? RIBON_PROTECTED_STATE_PROVIDER_UNAVAILABLE
        : RIBON_PROTECTED_STATE_PROVIDER_OK;
}

static void
provider_initialize(struct TestProvider *provider)
{
    uint32_t index;

    memset(provider, 0, sizeof(*provider));
    for (index = 0u; index < RIBON_PROTECTED_STATE_DIGEST_BYTES; ++index) {
        normal_domain[index] = (uint8_t)(index + 1u);
        recovery_domain[index] = (uint8_t)(0xa0u + index);
    }
    memcpy(provider->domains[0].domain, normal_domain, sizeof(normal_domain));
    memcpy(provider->domains[1].domain, recovery_domain, sizeof(recovery_domain));
    provider->descriptor = (struct RibonProtectedStateProvider){
        .magic = RIBON_PROTECTED_STATE_PROVIDER_MAGIC,
        .size = sizeof(provider->descriptor),
        .abi_version = RIBON_PROTECTED_STATE_ABI_VERSION,
        .provider_class = RIBON_PROTECTED_STATE_PROVIDER_CLASS_REFERENCE,
        .record_slots = RIBON_PROTECTED_STATE_RECORD_SLOTS,
        .selector_slots = RIBON_PROTECTED_STATE_RECORD_SLOTS,
        .record_bytes = RIBON_PROTECTED_STATE_RECORD_BYTES,
        .selector_bytes = RIBON_PROTECTED_STATE_SELECTOR_BYTES,
        .id = "security.protected-state.reference.test",
        .context = provider,
        .read = test_read,
        .write = test_write,
        .flush = test_flush,
    };
}

static void
provider_clone(struct TestProvider *destination, const struct TestProvider *source)
{
    *destination = *source;
    destination->descriptor.context = destination;
    destination->flush_count = 0u;
    destination->fault_flush = 0u;
    destination->fault_prefix = 0u;
    destination->unavailable = 0;
    memset(destination->domains[0].pending_valid, 0,
           sizeof(destination->domains[0].pending_valid));
    memset(destination->domains[1].pending_valid, 0,
           sizeof(destination->domains[1].pending_valid));
}

static void
provider_reboot(struct TestProvider *provider)
{
    memset(provider->domains[0].pending_valid, 0,
           sizeof(provider->domains[0].pending_valid));
    memset(provider->domains[1].pending_valid, 0,
           sizeof(provider->domains[1].pending_valid));
    provider->fault_flush = 0u;
    provider->unavailable = 0;
}

static struct RibonProtectedStateProductBinding
binding_for(struct TestProvider *provider)
{
    static uint8_t domains[2][RIBON_PROTECTED_STATE_DIGEST_BYTES];

    /* Digest byte 순서가 stable ascending이므로 normal, recovery 순서다. */
    memcpy(domains[0], normal_domain, sizeof(domains[0]));
    memcpy(domains[1], recovery_domain, sizeof(domains[1]));
    return (struct RibonProtectedStateProductBinding){
        .size = sizeof(struct RibonProtectedStateProductBinding),
        .abi_version = RIBON_PROTECTED_STATE_ABI_VERSION,
        .provider_class = RIBON_PROTECTED_STATE_PROVIDER_CLASS_REFERENCE,
        .provider = &provider->descriptor,
        .domain_digests = domains,
        .domain_count = 2u,
    };
}

static struct RibonProtectedStateJournal
journal_for(struct TestProvider *provider, const uint8_t *domain)
{
    struct RibonProtectedStateProductBinding binding = binding_for(provider);
    struct RibonProtectedStateJournal journal;

    expect(ribon_protected_state_journal_bind(&binding, domain, &journal) ==
           RIBON_PROTECTED_STATE_STATUS_OK, "bind journal");
    return journal;
}

static void
test_state_machine(void)
{
    struct TestProvider provider;
    struct RibonProtectedStateJournal journal;
    struct RibonProtectedStateSnapshot state;
    struct RibonProtectedStateDecision decision;

    provider_initialize(&provider);
    journal = journal_for(&provider, normal_domain);
    expect(ribon_protected_state_open(&journal, &state) ==
           RIBON_PROTECTED_STATE_STATUS_UNINITIALIZED, "empty journal");
    expect(ribon_protected_state_initialize(&journal, 7u, &state) ==
           RIBON_PROTECTED_STATE_STATUS_OK, "initialize floor");
    expect(state.kind == RIBON_PROTECTED_STATE_KIND_CONFIRMED &&
           state.confirmed_floor == 7u && state.generation == 1u,
           "initial confirmed state");
    expect(ribon_protected_state_initialize(&journal, 1u, &state) ==
           RIBON_PROTECTED_STATE_STATUS_ALREADY_INITIALIZED,
           "reject reinitialize floor lowering");
    expect(ribon_protected_state_begin_trial(&journal, 9u, 2u, &state) ==
           RIBON_PROTECTED_STATE_STATUS_SEQUENCE_GAP, "reject sequence skip");
    expect(ribon_protected_state_begin_trial(&journal, 7u, 2u, &state) ==
           RIBON_PROTECTED_STATE_STATUS_ROLLBACK, "reject old trial");
    expect(ribon_protected_state_begin_trial(&journal, 8u, 2u, &state) ==
           RIBON_PROTECTED_STATE_STATUS_OK, "begin exact successor trial");
    expect(state.kind == RIBON_PROTECTED_STATE_KIND_TRIAL &&
           state.pending_sequence == 8u && state.trial_attempts_remaining == 2u &&
           state.generation == 2u, "trial state");
    expect(ribon_protected_state_authorize(&journal, 7u, &decision) ==
           RIBON_PROTECTED_STATE_STATUS_OK &&
           decision.authority == RIBON_PROTECTED_STATE_AUTHORITY_CONFIRMED,
           "trial permits confirmed fallback");
    expect(ribon_protected_state_authorize(&journal, 8u, &decision) ==
           RIBON_PROTECTED_STATE_STATUS_OK &&
           decision.authority == RIBON_PROTECTED_STATE_AUTHORITY_TRIAL,
           "trial permits pending");
    expect(ribon_protected_state_authorize(&journal, 6u, &decision) ==
           RIBON_PROTECTED_STATE_STATUS_ROLLBACK, "reject below floor");
    expect(ribon_protected_state_authorize(&journal, 9u, &decision) ==
           RIBON_PROTECTED_STATE_STATUS_SEQUENCE_GAP, "reject above pending");
    expect(ribon_protected_state_consume_trial_attempt(&journal, 8u, &state) ==
           RIBON_PROTECTED_STATE_STATUS_OK &&
           state.trial_attempts_remaining == 1u && state.generation == 3u,
           "consume before first transfer");
    expect(ribon_protected_state_consume_trial_attempt(&journal, 8u, &state) ==
           RIBON_PROTECTED_STATE_STATUS_OK &&
           state.trial_attempts_remaining == 0u && state.generation == 4u,
           "consume last attempt");
    expect(ribon_protected_state_authorize(&journal, 8u, &decision) ==
           RIBON_PROTECTED_STATE_STATUS_ATTEMPTS_EXHAUSTED,
           "exhausted pending denied");
    expect(ribon_protected_state_confirm(&journal, 8u, &state) ==
           RIBON_PROTECTED_STATE_STATUS_OK &&
           state.confirmed_floor == 8u && state.generation == 5u,
           "confirm exact pending");
    expect(ribon_protected_state_authorize(&journal, 7u, &decision) ==
           RIBON_PROTECTED_STATE_STATUS_ROLLBACK,
           "old confirmed denied after confirmation");
    expect(ribon_protected_state_begin_trial(&journal, 9u, 1u, &state) ==
           RIBON_PROTECTED_STATE_STATUS_OK, "start second trial");
    expect(ribon_protected_state_fail_trial(&journal, &state) ==
           RIBON_PROTECTED_STATE_STATUS_OK &&
           state.kind == RIBON_PROTECTED_STATE_KIND_CONFIRMED &&
           state.confirmed_floor == 8u, "failed trial preserves floor");
}

static void
test_power_cut_closure(void)
{
    struct TestProvider base;
    struct RibonProtectedStateJournal journal;
    struct RibonProtectedStateSnapshot state;
    size_t prefix;
    uint32_t flush;

    provider_initialize(&base);
    journal = journal_for(&base, normal_domain);
    expect(ribon_protected_state_initialize(&journal, 20u, &state) ==
           RIBON_PROTECTED_STATE_STATUS_OK, "powercut base initialize");
    for (flush = 1u; flush <= 2u; ++flush) {
        for (prefix = 0u; prefix <= RIBON_PROTECTED_STATE_RECORD_BYTES; ++prefix) {
            struct TestProvider cut;

            provider_clone(&cut, &base);
            journal = journal_for(&cut, normal_domain);
            cut.fault_flush = flush;
            cut.fault_prefix = prefix;
            expect(ribon_protected_state_begin_trial(&journal, 21u, 3u, &state) ==
                   RIBON_PROTECTED_STATE_STATUS_UNAVAILABLE,
                   "injected flush failure reported");
            provider_reboot(&cut);
            expect(ribon_protected_state_open(&journal, &state) ==
                   RIBON_PROTECTED_STATE_STATUS_OK,
                   "powercut reboot finds authority");
            if (flush == 2u && prefix >= SELECTOR_CHECKSUM_OFFSET + 4u) {
                expect(state.kind == RIBON_PROTECTED_STATE_KIND_TRIAL &&
                       state.pending_sequence == 21u,
                       "complete selector meaning commits despite error receipt");
            } else {
                expect(state.kind == RIBON_PROTECTED_STATE_KIND_CONFIRMED &&
                       state.confirmed_floor == 20u,
                       "partial write retains previous authority");
            }
        }
    }
}

static void
test_corruption_conflict_and_wrap(void)
{
    struct TestProvider provider;
    struct RibonProtectedStateJournal journal;
    struct RibonProtectedStateSnapshot state;
    uint8_t *record;
    uint8_t *selector;
    uint8_t digest[RIBON_PROTECTED_STATE_DIGEST_BYTES];

    provider_initialize(&provider);
    journal = journal_for(&provider, normal_domain);
    expect(ribon_protected_state_initialize(&journal, 30u, &state) ==
           RIBON_PROTECTED_STATE_STATUS_OK, "corruption base initialize");
    expect(ribon_protected_state_begin_trial(&journal, 31u, 2u, &state) ==
           RIBON_PROTECTED_STATE_STATUS_OK && state.generation == 2u,
           "stale selector setup");
    expect(ribon_protected_state_open(&journal, &state) ==
           RIBON_PROTECTED_STATE_STATUS_OK && state.generation == 2u,
           "newest valid selector wins over stale selector");

    record = provider.domains[0].durable[0][1];
    record[64] ^= 0x80u;
    expect(ribon_protected_state_open(&journal, &state) ==
           RIBON_PROTECTED_STATE_STATUS_CORRUPT,
           "selected record corruption rejected");
    record[64] ^= 0x80u;

    memcpy(provider.domains[0].durable[1][0],
           provider.domains[0].durable[1][1],
           RIBON_PROTECTED_STATE_SELECTOR_BYTES);
    selector = provider.domains[0].durable[1][0];
    selector[72] ^= 1u;
    store_u32(selector + SELECTOR_CHECKSUM_OFFSET,
              crc32c(selector, SELECTOR_CHECKSUM_OFFSET));
    expect(ribon_protected_state_open(&journal, &state) ==
           RIBON_PROTECTED_STATE_STATUS_CONFLICT,
           "conflicting valid generation rejected");

    provider_initialize(&provider);
    journal = journal_for(&provider, normal_domain);
    expect(ribon_protected_state_initialize(&journal, 40u, &state) ==
           RIBON_PROTECTED_STATE_STATUS_OK, "wrap base initialize");
    record = provider.domains[0].durable[0][0];
    selector = provider.domains[0].durable[1][0];
    store_u64(record + 32u, UINT64_MAX);
    store_u32(record + RECORD_CHECKSUM_OFFSET,
              crc32c(record, RECORD_CHECKSUM_OFFSET));
    ribon_security_sha256(record, RIBON_PROTECTED_STATE_RECORD_BYTES, digest);
    store_u64(selector + 32u, UINT64_MAX);
    memcpy(selector + 72u, digest, sizeof(digest));
    store_u32(selector + SELECTOR_CHECKSUM_OFFSET,
              crc32c(selector, SELECTOR_CHECKSUM_OFFSET));
    expect(ribon_protected_state_open(&journal, &state) ==
           RIBON_PROTECTED_STATE_STATUS_OK && state.generation == UINT64_MAX,
           "maximum generation decodes");
    expect(ribon_protected_state_begin_trial(&journal, 41u, 1u, &state) ==
           RIBON_PROTECTED_STATE_STATUS_OVERFLOW,
           "generation wrap rejected");
}

static void
test_domains_and_unavailable(void)
{
    struct TestProvider provider;
    struct RibonProtectedStateProductBinding binding;
    struct RibonProtectedStateJournal normal;
    struct RibonProtectedStateJournal recovery;
    struct RibonProtectedStateSnapshot state;
    uint8_t unknown[RIBON_PROTECTED_STATE_DIGEST_BYTES] = {0xffu};

    provider_initialize(&provider);
    binding = binding_for(&provider);
    normal = journal_for(&provider, normal_domain);
    recovery = journal_for(&provider, recovery_domain);
    expect(ribon_protected_state_journal_bind(&binding, unknown, &normal) ==
           RIBON_PROTECTED_STATE_STATUS_DOMAIN_MISMATCH,
           "unknown domain rejected");
    normal = journal_for(&provider, normal_domain);
    expect(ribon_protected_state_initialize(&normal, 100u, &state) ==
           RIBON_PROTECTED_STATE_STATUS_OK, "normal domain initialize");
    expect(ribon_protected_state_initialize(&recovery, 2u, &state) ==
           RIBON_PROTECTED_STATE_STATUS_OK, "recovery domain initialize");
    expect(ribon_protected_state_begin_trial(&recovery, 3u, 1u, &state) ==
           RIBON_PROTECTED_STATE_STATUS_OK, "recovery domain transition");
    expect(ribon_protected_state_open(&normal, &state) ==
           RIBON_PROTECTED_STATE_STATUS_OK && state.confirmed_floor == 100u,
           "recovery transition cannot lower normal floor");
    provider.unavailable = 1;
    expect(ribon_protected_state_open(&normal, &state) ==
           RIBON_PROTECTED_STATE_STATUS_UNAVAILABLE,
           "provider unavailable fails closed");
}

static void
test_bound_attempt_history(void)
{
    struct TestProvider provider;
    struct RibonProtectedStateJournal journal;
    struct RibonProtectedStateSnapshot state;
    uint8_t first_binding[RIBON_PROTECTED_STATE_DIGEST_BYTES] = {1u};
    uint8_t second_binding[RIBON_PROTECTED_STATE_DIGEST_BYTES] = {2u};

    provider_initialize(&provider);
    journal = journal_for(&provider, normal_domain);
    expect(ribon_protected_state_initialize(&journal, 50u, &state) ==
           RIBON_PROTECTED_STATE_STATUS_OK, "bound history initialize");
    expect(ribon_protected_state_begin_bound_trial(
               &journal, 51u, 2u, first_binding, 9u, &state) ==
           RIBON_PROTECTED_STATE_STATUS_OK,
           "bound history begins first attempt");
    expect(ribon_protected_state_consume_trial_attempt(&journal, 51u, &state) ==
           RIBON_PROTECTED_STATE_STATUS_OK,
           "bound history consumes first attempt");
    expect(ribon_protected_state_fail_trial(&journal, &state) ==
               RIBON_PROTECTED_STATE_STATUS_OK &&
           state.kind == RIBON_PROTECTED_STATE_KIND_CONFIRMED &&
           state.confirmed_floor == 50u && state.attempt_sequence == 9u &&
           memcmp(state.trial_binding_digest, first_binding,
                  sizeof(first_binding)) == 0,
           "failed bound trial preserves monotonic history");
    expect(ribon_protected_state_begin_bound_trial(
               &journal, 51u, 2u, second_binding, 10u, &state) ==
               RIBON_PROTECTED_STATE_STATUS_OK &&
           state.attempt_sequence == 10u &&
           memcmp(state.trial_binding_digest, second_binding,
                  sizeof(second_binding)) == 0,
           "retry uses strictly newer bound attempt");
    expect(ribon_protected_state_confirm_bound(
               &journal, 51u, first_binding, 9u, &state) ==
           RIBON_PROTECTED_STATE_STATUS_BINDING_MISMATCH,
           "failed attempt receipt stays stale after retry");
}

int
main(void)
{
    test_sha256_known_answer();
    test_state_machine();
    test_power_cut_closure();
    test_corruption_conflict_and_wrap();
    test_domains_and_unavailable();
    test_bound_attempt_history();
    if (failures != 0) {
        fprintf(stderr, "%d protected-state test(s) failed\n", failures);
        return 1;
    }
    puts("RIBON-PROTECTED-STATE-OK sha256=known-answer fault-prefixes=258 domains=2");
    return 0;
}
