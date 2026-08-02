#include <Ribon/security/ed25519.h>
#include <Ribon/plugin/descriptor.h>
#include <Ribon/update/confirmation.h>

#include "reference_storage.h"
#include "../../src/security/sha256.h"
#include "../../third_party/monocypher/4.0.3/monocypher-ed25519.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t key_id[] = "ribon-update-release-2026q3";
static const uint8_t product_id[] =
    "validation.x86_64-uefi-update-recovery";
static const uint8_t protocol_id[] = "validation-update";
static const uint8_t wrong_product_id[] = "validation.other-product";
static const uint8_t seed_bytes[32] = {
    0x9du,0x61u,0xb1u,0x9du,0xefu,0xfdu,0x5au,0x60u,
    0xbau,0x84u,0x4au,0xf4u,0x92u,0xecu,0x2cu,0xc4u,
    0x44u,0x49u,0xc5u,0x69u,0x7bu,0x32u,0x69u,0x19u,
    0x70u,0x3bu,0xacu,0x03u,0x1cu,0xaeu,0x7fu,0x60u,
};
static const uint8_t key_identity[32] = {
    0x21u,0xfeu,0x31u,0xdfu,0xa1u,0x54u,0xa2u,0x61u,
    0x62u,0x6bu,0xf8u,0x54u,0x04u,0x6fu,0xd2u,0x27u,
    0x1bu,0x7bu,0xedu,0x4bu,0x6au,0xbeu,0x45u,0xaau,
    0x58u,0x87u,0x7eu,0xf4u,0x7fu,0x97u,0x21u,0xb9u,
};
static const uint8_t healthy_payload[8] = {'R','E','F','H',1u,1u,0u,0u};

extern const struct RibonPluginDescriptor
    ribon_uefi_update_validation_protocol_plugin_descriptor;

struct Bytes {
    uint8_t *data;
    size_t size;
};

struct ProtectedMemory {
    uint8_t durable[2][2][RIBON_PROTECTED_STATE_RECORD_BYTES];
    uint8_t pending[2][2][RIBON_PROTECTED_STATE_RECORD_BYTES];
    uint8_t pending_valid[2][2];
    uint8_t domain[32];
};

struct Fixture {
    struct Bytes manifest;
    struct Bytes layout;
    struct Bytes disk;
    struct Bytes confirmation;
    struct Bytes product;
    struct RibonUpdateManifestView manifest_view;
    struct RibonUpdateLayout layout_view;
    struct RibonTestStorage storage;
    struct ProtectedMemory protected_memory;
    struct RibonProtectedStateProvider protected_provider;
    struct RibonProtectedStateProductBinding protected_binding;
    struct RibonProtectedStateJournal protected_journal;
    struct RibonUpdateTransactionJournal update_journal;
    struct RibonKeyPolicyRecord key_record;
    struct RibonKeyPolicyStore key_store;
    struct RibonBootAttemptIdentity identity;
};

static int failures;

static void expect(int condition, const char *name)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

static int read_file(const char *path, struct Bytes *bytes)
{
    FILE *file = fopen(path, "rb");
    long length;
    memset(bytes, 0, sizeof(*bytes));
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0 ||
        (length = ftell(file)) <= 0 || fseek(file, 0L, SEEK_SET) != 0) {
        if (file != NULL) fclose(file);
        return 0;
    }
    bytes->data = malloc((size_t)length);
    bytes->size = (size_t)length;
    if (bytes->data == NULL ||
        fread(bytes->data, 1u, bytes->size, file) != bytes->size ||
        fclose(file) != 0) {
        free(bytes->data);
        memset(bytes, 0, sizeof(*bytes));
        return 0;
    }
    return 1;
}

static int protected_read(
    const struct RibonProtectedStateProvider *provider,
    const uint8_t domain[32], enum RibonProtectedStateObject object,
    uint32_t slot, uint8_t *bytes, size_t size)
{
    struct ProtectedMemory *memory = provider->context;
    uint32_t index = (uint32_t)object - 1u;
    if (memory == NULL || index >= 2u || slot >= 2u ||
        size != RIBON_PROTECTED_STATE_RECORD_BYTES ||
        memcmp(domain, memory->domain, 32u) != 0) {
        return RIBON_PROTECTED_STATE_PROVIDER_IO_ERROR;
    }
    memcpy(bytes, memory->durable[index][slot], size);
    return RIBON_PROTECTED_STATE_PROVIDER_OK;
}

static int protected_write(
    const struct RibonProtectedStateProvider *provider,
    const uint8_t domain[32], enum RibonProtectedStateObject object,
    uint32_t slot, const uint8_t *bytes, size_t size)
{
    struct ProtectedMemory *memory = provider->context;
    uint32_t index = (uint32_t)object - 1u;
    if (memory == NULL || index >= 2u || slot >= 2u ||
        size != RIBON_PROTECTED_STATE_RECORD_BYTES ||
        memcmp(domain, memory->domain, 32u) != 0) {
        return RIBON_PROTECTED_STATE_PROVIDER_IO_ERROR;
    }
    memcpy(memory->pending[index][slot], bytes, size);
    memory->pending_valid[index][slot] = 1u;
    return RIBON_PROTECTED_STATE_PROVIDER_OK;
}

static int protected_flush(
    const struct RibonProtectedStateProvider *provider,
    const uint8_t domain[32])
{
    struct ProtectedMemory *memory = provider->context;
    uint32_t object;
    uint32_t slot;
    if (memory == NULL || memcmp(domain, memory->domain, 32u) != 0) {
        return RIBON_PROTECTED_STATE_PROVIDER_IO_ERROR;
    }
    for (object = 0u; object < 2u; ++object) {
        for (slot = 0u; slot < 2u; ++slot) {
            if (memory->pending_valid[object][slot] != 0u) {
                memcpy(memory->durable[object][slot],
                       memory->pending[object][slot],
                       RIBON_PROTECTED_STATE_RECORD_BYTES);
                memory->pending_valid[object][slot] = 0u;
            }
        }
    }
    return RIBON_PROTECTED_STATE_PROVIDER_OK;
}

static int fixed_nonce(void *context, uint8_t nonce[32])
{
    uint8_t value = context != NULL ? *(const uint8_t *)context : 0x46u;
    uint32_t index;
    for (index = 0u; index < 32u; ++index) {
        nonce[index] = (uint8_t)(value + index);
    }
    return 0;
}

static int key_store_initialize(struct Fixture *fixture)
{
    uint8_t seed[32];
    uint8_t secret[64];
    uint8_t public_key[32];
    memcpy(seed, seed_bytes, sizeof(seed));
    crypto_ed25519_key_pair(secret, public_key, seed);
    fixture->key_record = (struct RibonKeyPolicyRecord){
        .size = sizeof(fixture->key_record),
        .abi_version = RIBON_KEY_POLICY_ABI_VERSION,
        .flags = RIBON_KEY_POLICY_RECORD_ROOT,
        .algorithm = RIBON_SIGNATURE_ALGORITHM_ED25519,
        .lifecycle = RIBON_KEY_POLICY_LIFECYCLE_ACTIVE,
        .mode_mask = UINT32_C(1) << (RIBON_KEY_POLICY_MODE_RECOVERY - 1u),
        .usage_mask = UINT64_C(1) <<
            (RIBON_KEY_POLICY_USAGE_BOOT_CONFIRMATION - 1u),
        .key_id = key_id,
        .key_id_size = sizeof(key_id) - 1u,
        .rollback_domain_digests =
            (const uint8_t (*)[32])&fixture->manifest_view.rollback_domain_digest,
        .rollback_domain_count = 1u,
        .minimum_sequence = fixture->manifest_view.rollback_sequence,
        .maximum_sequence = fixture->manifest_view.rollback_sequence,
    };
    memcpy(fixture->key_record.public_key, public_key, 32u);
    memcpy(fixture->key_record.key_identity_digest, key_identity, 32u);
    ribon_security_sha256(fixture->product.data, fixture->product.size,
                          fixture->key_record.product_digest);
    fixture->key_store = (struct RibonKeyPolicyStore){
        .magic = RIBON_KEY_POLICY_STORE_MAGIC,
        .size = sizeof(fixture->key_store),
        .abi_version = RIBON_KEY_POLICY_ABI_VERSION,
        .id = (const uint8_t *)"confirmation.tests.v1",
        .id_size = sizeof("confirmation.tests.v1") - 1u,
        .generation = 1u,
        .records = &fixture->key_record,
        .record_count = 1u,
    };
    return ribon_key_policy_store_canonical_digest(&fixture->key_store,
        fixture->key_store.canonical_digest) == RIBON_KEY_POLICY_STATUS_OK &&
        ribon_key_policy_store_validate(&fixture->key_store) ==
            RIBON_KEY_POLICY_STATUS_OK;
}

static int fixture_open(struct Fixture *fixture, char **argv)
{
    struct RibonProtectedStateSnapshot state;
    memset(fixture, 0, sizeof(*fixture));
    if (!read_file(argv[1], &fixture->manifest) ||
        !read_file(argv[2], &fixture->layout) ||
        !read_file(argv[3], &fixture->disk) ||
        !read_file(argv[4], &fixture->confirmation) ||
        !read_file(argv[5], &fixture->product) ||
        ribon_update_manifest_open(fixture->manifest.data,
            fixture->manifest.size, &fixture->manifest_view) !=
            RIBON_UPDATE_MANIFEST_STATUS_OK ||
        ribon_update_layout_identity_open(fixture->layout.data,
            fixture->layout.size, &fixture->layout_view) !=
            RIBON_UPDATE_STORAGE_STATUS_OK ||
        !ribon_test_storage_open_memory(&fixture->storage, fixture->disk.data,
            fixture->disk.size, 512u, 65536u) || !key_store_initialize(fixture)) {
        return 0;
    }
    fixture->update_journal = (struct RibonUpdateTransactionJournal){
        .size = sizeof(fixture->update_journal),
        .abi_version = RIBON_UPDATE_TRANSACTION_ABI_VERSION,
        .provider = &fixture->storage.provider,
        .layout = &fixture->layout_view,
        .minimum_generation = 1u,
        .deadline_ticks = 1u,
    };
    memcpy(fixture->protected_memory.domain,
           fixture->manifest_view.rollback_domain_digest, 32u);
    fixture->protected_provider = (struct RibonProtectedStateProvider){
        .magic = RIBON_PROTECTED_STATE_PROVIDER_MAGIC,
        .size = sizeof(fixture->protected_provider),
        .abi_version = RIBON_PROTECTED_STATE_ABI_VERSION,
        .provider_class = RIBON_PROTECTED_STATE_PROVIDER_CLASS_REFERENCE,
        .record_slots = 2u,
        .selector_slots = 2u,
        .record_bytes = RIBON_PROTECTED_STATE_RECORD_BYTES,
        .selector_bytes = RIBON_PROTECTED_STATE_SELECTOR_BYTES,
        .id = "confirmation.reference",
        .context = &fixture->protected_memory,
        .read = protected_read,
        .write = protected_write,
        .flush = protected_flush,
    };
    fixture->protected_binding = (struct RibonProtectedStateProductBinding){
        .size = sizeof(fixture->protected_binding),
        .abi_version = RIBON_PROTECTED_STATE_ABI_VERSION,
        .provider_class = RIBON_PROTECTED_STATE_PROVIDER_CLASS_REFERENCE,
        .provider = &fixture->protected_provider,
        .domain_digests = (const uint8_t (*)[32])
            &fixture->protected_memory.domain,
        .domain_count = 1u,
    };
    if (ribon_protected_state_journal_bind(&fixture->protected_binding,
            fixture->protected_memory.domain, &fixture->protected_journal) !=
            RIBON_PROTECTED_STATE_STATUS_OK ||
        ribon_protected_state_initialize(&fixture->protected_journal, 1u, &state) !=
            RIBON_PROTECTED_STATE_STATUS_OK) {
        return 0;
    }
    fixture->identity = (struct RibonBootAttemptIdentity){
        .size = sizeof(fixture->identity),
        .abi_version = RIBON_BOOT_CONFIRMATION_ABI_VERSION,
        .mode = RIBON_KEY_POLICY_MODE_RECOVERY,
        .slot_id = 1u,
        .protocol_major = 1u,
        .policy_version = (uint32_t)fixture->manifest_view.creation_policy_version,
        .image_generation = fixture->manifest_view.bundle_generation,
        .manifest_sequence = fixture->manifest_view.rollback_sequence,
        .product_id = product_id,
        .product_id_size = sizeof(product_id) - 1u,
        .protocol_id = protocol_id,
        .protocol_id_size = sizeof(protocol_id) - 1u,
    };
    ribon_security_sha256(fixture->manifest.data, fixture->manifest.size,
                          fixture->identity.manifest_digest);
    memcpy(fixture->identity.product_digest,
           fixture->key_record.product_digest, 32u);
    memcpy(fixture->identity.rollback_domain_digest,
           fixture->manifest_view.rollback_domain_digest, 32u);
    return 1;
}

static void fixture_close(struct Fixture *fixture)
{
    free(fixture->manifest.data);
    free(fixture->layout.data);
    free(fixture->disk.data);
    free(fixture->confirmation.data);
    free(fixture->product.data);
}

static int begin_attempt(struct Fixture *fixture, uint8_t nonce_base,
    struct RibonBootAttempt *attempt)
{
    struct RibonBootAttemptNonceSource nonce = {
        .size = sizeof(nonce),
        .abi_version = RIBON_BOOT_CONFIRMATION_ABI_VERSION,
        .context = &nonce_base,
        .fill = fixed_nonce,
    };
    struct RibonBootAttemptBeginRequest request = {
        .size = sizeof(request),
        .abi_version = RIBON_BOOT_CONFIRMATION_ABI_VERSION,
        .maximum_attempts = 3u,
        .identity = &fixture->identity,
        .update_journal = &fixture->update_journal,
        .protected_journal = &fixture->protected_journal,
        .nonce_source = &nonce,
    };
    return ribon_boot_confirmation_begin_attempt(&request, attempt);
}

static size_t signed_envelope(struct Fixture *fixture,
    const struct RibonBootAttempt *attempt, const uint8_t payload[8],
    uint8_t output[RIBON_BOOT_CONFIRMATION_MAX_WIRE_BYTES])
{
    struct RibonBootConfirmationEnvelopeSource source = {
        .size = sizeof(source),
        .abi_version = RIBON_BOOT_CONFIRMATION_ABI_VERSION,
        .slot_id = fixture->identity.slot_id,
        .protocol_major = fixture->identity.protocol_major,
        .protocol_minor = fixture->identity.protocol_minor,
        .policy_version = fixture->identity.policy_version,
        .image_generation = fixture->identity.image_generation,
        .manifest_sequence = fixture->identity.manifest_sequence,
        .attempt_sequence = attempt->attempt_sequence,
        .product_id = fixture->identity.product_id,
        .product_id_size = fixture->identity.product_id_size,
        .protocol_id = fixture->identity.protocol_id,
        .protocol_id_size = fixture->identity.protocol_id_size,
        .key_id = key_id,
        .key_id_size = sizeof(key_id) - 1u,
        .health_payload = payload,
        .health_payload_size = 8u,
    };
    struct RibonBootConfirmationEnvelopeView view;
    uint8_t seed[32];
    uint8_t secret[64];
    uint8_t public_key[32];
    size_t written = 0u;
    memcpy(source.manifest_digest, fixture->identity.manifest_digest, 32u);
    memcpy(source.nonce, attempt->nonce, 32u);
    expect(ribon_boot_confirmation_envelope_encode(&source, output,
        RIBON_BOOT_CONFIRMATION_MAX_WIRE_BYTES, &written) ==
        RIBON_BOOT_CONFIRMATION_STATUS_OK, "encode unsigned envelope");
    expect(ribon_boot_confirmation_envelope_open(output, written, &view) ==
        RIBON_BOOT_CONFIRMATION_STATUS_OK, "open unsigned envelope");
    memcpy(seed, seed_bytes, sizeof(seed));
    crypto_ed25519_key_pair(secret, public_key, seed);
    crypto_ed25519_sign(source.signature, secret,
                        view.authenticated_message, view.authenticated_message_size);
    expect(ribon_boot_confirmation_envelope_encode(&source, output,
        RIBON_BOOT_CONFIRMATION_MAX_WIRE_BYTES, &written) ==
        RIBON_BOOT_CONFIRMATION_STATUS_OK, "encode signed envelope");
    return written;
}

static int accept(struct Fixture *fixture, const uint8_t *envelope, size_t size,
    struct RibonBootConfirmationReceipt *receipt)
{
    const struct RibonBootProtocol *protocol =
        ribon_uefi_update_validation_protocol_plugin_descriptor.operations;
    struct RibonBootConfirmationAcceptRequest request = {
        .size = sizeof(request),
        .abi_version = RIBON_BOOT_CONFIRMATION_ABI_VERSION,
        .envelope = envelope,
        .envelope_size = size,
        .identity = &fixture->identity,
        .protocol = protocol,
        .key_policy = &fixture->key_store,
        .signature_provider = &ribon_ed25519_signature_provider_descriptor,
        .update_journal = &fixture->update_journal,
        .protected_journal = &fixture->protected_journal,
    };
    return ribon_boot_confirmation_accept(&request, receipt);
}

static void test_hostile_and_commit(struct Fixture *fixture)
{
    struct RibonBootAttempt first;
    struct RibonBootAttempt second;
    struct RibonBootConfirmationReceipt receipt;
    struct RibonProtectedStateSnapshot before;
    struct RibonProtectedStateSnapshot after;
    struct RibonUpdateTransactionSnapshot update_before;
    struct RibonUpdateTransactionSnapshot update_after;
    uint8_t wire[RIBON_BOOT_CONFIRMATION_MAX_WIRE_BYTES];
    uint8_t bad_health[8] = {'R','E','F','H',1u,0u,0u,0u};
    size_t wire_size;

    expect(begin_attempt(fixture, 0x10u, &first) ==
        RIBON_BOOT_CONFIRMATION_STATUS_OK && first.attempt_sequence == 1u &&
        first.attempts_remaining == 2u, "begin and consume first attempt");
    expect(ribon_protected_state_open(&fixture->protected_journal, &before) ==
        RIBON_PROTECTED_STATE_STATUS_OK, "snapshot before hostile corpus");
    expect(ribon_update_transaction_open(&fixture->update_journal, &update_before) ==
        RIBON_UPDATE_TRANSACTION_STATUS_OK, "update before hostile corpus");
    wire_size = signed_envelope(fixture, &first, healthy_payload, wire);
    wire[40] ^= 1u;
    expect(accept(fixture, wire, wire_size, &receipt) ==
        RIBON_BOOT_CONFIRMATION_STATUS_MALFORMED, "malformed size rejected");
    wire[40] ^= 1u;
    wire[44] = 0u;
    expect(accept(fixture, wire, wire_size, &receipt) ==
        RIBON_BOOT_CONFIRMATION_STATUS_IDENTITY, "wrong slot rejected");
    wire[44] = 1u;
    ++fixture->identity.image_generation;
    wire_size = signed_envelope(fixture, &first, healthy_payload, wire);
    --fixture->identity.image_generation;
    expect(accept(fixture, wire, wire_size, &receipt) ==
        RIBON_BOOT_CONFIRMATION_STATUS_IDENTITY,
        "wrong image generation rejected");
    fixture->identity.product_id = wrong_product_id;
    fixture->identity.product_id_size = sizeof(wrong_product_id) - 1u;
    wire_size = signed_envelope(fixture, &first, healthy_payload, wire);
    fixture->identity.product_id = product_id;
    fixture->identity.product_id_size = sizeof(product_id) - 1u;
    expect(accept(fixture, wire, wire_size, &receipt) ==
        RIBON_BOOT_CONFIRMATION_STATUS_IDENTITY, "wrong product rejected");
    wire_size = signed_envelope(fixture, &first, healthy_payload, wire);
    wire[wire_size - 1u] ^= 1u;
    expect(accept(fixture, wire, wire_size, &receipt) ==
        RIBON_BOOT_CONFIRMATION_STATUS_AUTHENTICATOR,
        "corrupt authenticator rejected");
    wire[wire_size - 1u] ^= 1u;
    wire_size = signed_envelope(fixture, &first, bad_health, wire);
    expect(accept(fixture, wire, wire_size, &receipt) ==
        RIBON_BOOT_CONFIRMATION_STATUS_HEALTH_REJECTED,
        "signed protocol-unhealthy payload rejected");
    expect(ribon_protected_state_open(&fixture->protected_journal, &after) ==
            RIBON_PROTECTED_STATE_STATUS_OK && after.generation == before.generation &&
        ribon_update_transaction_open(&fixture->update_journal, &update_after) ==
            RIBON_UPDATE_TRANSACTION_STATUS_OK &&
            update_after.journal_generation == update_before.journal_generation,
        "negative corpus leaves durable state unchanged");

    expect(begin_attempt(fixture, 0x40u, &second) ==
        RIBON_BOOT_CONFIRMATION_STATUS_OK && second.attempt_sequence == 2u &&
        second.attempts_remaining == 1u, "timeout opens strictly newer attempt");
    expect(accept(fixture, fixture->confirmation.data,
        fixture->confirmation.size, &receipt) ==
        RIBON_BOOT_CONFIRMATION_STATUS_STALE,
        "old signed receipt replay rejected after rebind");
    wire_size = signed_envelope(fixture, &second, healthy_payload, wire);
    expect(accept(fixture, wire, wire_size, &receipt) ==
        RIBON_BOOT_CONFIRMATION_STATUS_OK && receipt.active_slot == 1u &&
        receipt.duplicate == 0u, "exact current receipt confirms pending");
    expect(accept(fixture, wire, wire_size, &receipt) ==
        RIBON_BOOT_CONFIRMATION_STATUS_DUPLICATE && receipt.duplicate == 1u,
        "exact duplicate receipt is idempotent");
    expect(ribon_update_transaction_open(&fixture->update_journal, &update_after) ==
            RIBON_UPDATE_TRANSACTION_STATUS_OK &&
        update_after.metadata.active_slot == 1u &&
        update_after.metadata.pending_slot == RIBON_UPDATE_SLOT_NONE &&
        update_after.metadata.slots[1].state == RIBON_UPDATE_SLOT_CONFIRMED,
        "confirmed metadata reopens durably");
}

int main(int argc, char **argv)
{
    struct Fixture fixture;
    if (argc != 6 || !fixture_open(&fixture, argv)) {
        fprintf(stderr, "usage: %s manifest layout pending-disk confirmation product\n",
                argv[0]);
        return 2;
    }
    test_hostile_and_commit(&fixture);
    fixture_close(&fixture);
    if (failures != 0) {
        fprintf(stderr, "%d boot-confirmation test(s) failed\n", failures);
        return 1;
    }
    puts("RIBON-BOOT-CONFIRMATION-OK hostile=7 duplicate=idempotent active=slot-b");
    return 0;
}
