#include <Ribon/security/ed25519.h>
#include <Ribon/update/transaction.h>

#include "../../src/security/sha256.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCRATCH_BYTES 65536u
#define MAX_EVENTS 128u

static const uint8_t fixture_key_id[] = "ribon-update-release-2026q3";
static const uint8_t fixture_public_key[RIBON_ED25519_PUBLIC_KEY_BYTES] = {
    0xd7u,0x5au,0x98u,0x01u,0x82u,0xb1u,0x0au,0xb7u,
    0xd5u,0x4bu,0xfeu,0xd3u,0xc9u,0x64u,0x07u,0x3au,
    0x0eu,0xe1u,0x72u,0xf3u,0xdau,0xa6u,0x23u,0x25u,
    0xafu,0x02u,0x1au,0x68u,0xf7u,0x07u,0x51u,0x1au,
};
static const uint8_t fixture_key_identity[32] = {
    0x21u,0xfeu,0x31u,0xdfu,0xa1u,0x54u,0xa2u,0x61u,
    0x62u,0x6bu,0xf8u,0x54u,0x04u,0x6fu,0xd2u,0x27u,
    0x1bu,0x7bu,0xedu,0x4bu,0x6au,0xbeu,0x45u,0xaau,
    0x58u,0x87u,0x7eu,0xf4u,0x7fu,0x97u,0x21u,0xb9u,
};
static const uint8_t store_id[] = "update.power-cut.tests.v1";

struct FileBytes {
    uint8_t *bytes;
    size_t size;
};

struct CrashStorage {
    uint8_t *durable;
    uint8_t *volatile_bytes;
    size_t size;
    struct RibonUpdateStorageProvider provider;
    const struct RibonUpdateLayout *layout;
    uint32_t flush_count;
    uint32_t slot_write_count;
    uint32_t fail_flush_call;
    uint32_t short_slot_write_call;
    int mutate_slot_read;
};

struct BundleContext {
    const uint8_t *bytes;
    size_t size;
};

struct TraceContext {
    struct RibonUpdateTransactionEvent events[MAX_EVENTS];
    uint32_t count;
    uint32_t cut_sequence;
};

struct Fixture {
    struct FileBytes manifest;
    struct FileBytes envelope;
    struct FileBytes bundle_bytes;
    struct FileBytes layout_bytes;
    struct FileBytes disk;
    struct RibonUpdateManifestView manifest_view;
    struct RibonUpdateManifestExpectation expectation;
    struct RibonKeyPolicyRecord key_record;
    struct RibonKeyPolicyStore key_store;
    struct BundleContext bundle_context;
    struct RibonUpdateBundleSource bundle;
    struct RibonUpdateLayout layout;
};

static int failures;

static void expect(int condition, const char *name)
{
    if (!condition) {
        (void)fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

static int read_file(const char *path, struct FileBytes *file)
{
    FILE *stream;
    long length;
    memset(file, 0, sizeof(*file));
    stream = fopen(path, "rb");
    if (stream == NULL || fseek(stream, 0L, SEEK_END) != 0 ||
        (length = ftell(stream)) <= 0 || fseek(stream, 0L, SEEK_SET) != 0) {
        if (stream != NULL) {
            (void)fclose(stream);
        }
        return 0;
    }
    file->bytes = malloc((size_t)length);
    file->size = (size_t)length;
    if (file->bytes == NULL ||
        fread(file->bytes, 1u, file->size, stream) != file->size ||
        fgetc(stream) != EOF || fclose(stream) != 0) {
        free(file->bytes);
        memset(file, 0, sizeof(*file));
        return 0;
    }
    return 1;
}

static int write_file(const char *path, const uint8_t *bytes, size_t size)
{
    FILE *stream = fopen(path, "wb");
    return stream != NULL && fwrite(bytes, 1u, size, stream) == size &&
        fclose(stream) == 0;
}

static int range_valid(const struct CrashStorage *storage,
    uint64_t offset, uint64_t size)
{
    return size != 0u && offset <= storage->size &&
        size <= storage->size - offset && size <= SIZE_MAX;
}

static int in_slot_b(const struct CrashStorage *storage,
    uint64_t offset, uint64_t size)
{
    const struct RibonUpdateLayoutRegion *slot =
        &storage->layout->regions[RIBON_UPDATE_REGION_SLOT_B - 1u];
    return offset >= slot->offset && offset - slot->offset <= slot->length &&
        size <= slot->length - (offset - slot->offset);
}

static int crash_read(void *opaque, uint64_t offset, void *buffer,
    uint64_t size, uint64_t *transferred, uint64_t deadline_ticks)
{
    struct CrashStorage *storage = opaque;
    (void)deadline_ticks;
    if (transferred != NULL) {
        *transferred = 0u;
    }
    if (storage == NULL || buffer == NULL || transferred == NULL ||
        !range_valid(storage, offset, size)) {
        return -1;
    }
    memcpy(buffer, storage->volatile_bytes + (size_t)offset, (size_t)size);
    if (storage->mutate_slot_read && in_slot_b(storage, offset, size)) {
        ((uint8_t *)buffer)[0] ^= 1u;
        storage->mutate_slot_read = 0;
    }
    *transferred = size;
    return 0;
}

static int crash_write(void *opaque, uint64_t offset, const void *buffer,
    uint64_t size, uint64_t *transferred, uint64_t deadline_ticks)
{
    struct CrashStorage *storage = opaque;
    uint64_t copied = size;
    (void)deadline_ticks;
    if (transferred != NULL) {
        *transferred = 0u;
    }
    if (storage == NULL || buffer == NULL || transferred == NULL ||
        !range_valid(storage, offset, size)) {
        return -1;
    }
    if (in_slot_b(storage, offset, size)) {
        ++storage->slot_write_count;
        if (storage->short_slot_write_call == storage->slot_write_count && copied > 0u) {
            --copied;
        }
    }
    memcpy(storage->volatile_bytes + (size_t)offset, buffer, (size_t)copied);
    *transferred = copied;
    return 0;
}

static int crash_erase(void *opaque, uint64_t offset, uint64_t size,
    uint64_t deadline_ticks)
{
    struct CrashStorage *storage = opaque;
    (void)deadline_ticks;
    if (storage == NULL || !range_valid(storage, offset, size)) {
        return -1;
    }
    memset(storage->volatile_bytes + (size_t)offset, 0, (size_t)size);
    return 0;
}

static int crash_flush(void *opaque, uint64_t deadline_ticks)
{
    struct CrashStorage *storage = opaque;
    (void)deadline_ticks;
    if (storage == NULL) {
        return -1;
    }
    ++storage->flush_count;
    if (storage->fail_flush_call == storage->flush_count) {
        return -1;
    }
    memcpy(storage->durable, storage->volatile_bytes, storage->size);
    return 0;
}

static void crash_storage_reset(struct CrashStorage *storage,
    const struct Fixture *fixture)
{
    memset(storage, 0, sizeof(*storage));
    storage->size = fixture->disk.size;
    storage->durable = malloc(storage->size);
    storage->volatile_bytes = malloc(storage->size);
    storage->layout = &fixture->layout;
    if (storage->durable == NULL || storage->volatile_bytes == NULL) {
        return;
    }
    memcpy(storage->durable, fixture->disk.bytes, storage->size);
    memcpy(storage->volatile_bytes, fixture->disk.bytes, storage->size);
    storage->provider = (struct RibonUpdateStorageProvider){
        .size = sizeof(storage->provider),
        .abi_version = RIBON_UPDATE_STORAGE_ABI_VERSION,
        .capabilities = RIBON_UPDATE_STORAGE_CAP_ALL,
        .capacity_bytes = storage->size,
        .read_alignment = 512u,
        .write_alignment = 512u,
        .erase_alignment = 512u,
        .maximum_transfer_bytes = SCRATCH_BYTES,
        .context = storage,
        .read = crash_read,
        .write = crash_write,
        .erase = crash_erase,
        .flush = crash_flush,
    };
    memcpy(storage->provider.media_identity_digest,
        fixture->disk.bytes + 64u * 1024u + 64u, 32u);
}

static void crash_storage_destroy(struct CrashStorage *storage)
{
    free(storage->durable);
    free(storage->volatile_bytes);
    memset(storage, 0, sizeof(*storage));
}

static void power_cut(struct CrashStorage *storage)
{
    memcpy(storage->volatile_bytes, storage->durable, storage->size);
}

static int bundle_read(void *opaque, uint64_t offset, void *buffer,
    uint64_t size, uint64_t *transferred, uint64_t deadline_ticks)
{
    const struct BundleContext *bundle = opaque;
    (void)deadline_ticks;
    if (transferred != NULL) {
        *transferred = 0u;
    }
    if (bundle == NULL || buffer == NULL || transferred == NULL || size == 0u ||
        offset > bundle->size || size > bundle->size - offset) {
        return -1;
    }
    memcpy(buffer, bundle->bytes + (size_t)offset, (size_t)size);
    *transferred = size;
    return 0;
}

static int observe(void *opaque, const struct RibonUpdateTransactionEvent *event)
{
    struct TraceContext *trace = opaque;
    if (trace == NULL || event == NULL || trace->count >= MAX_EVENTS) {
        return -1;
    }
    trace->events[trace->count++] = *event;
    return event->sequence == trace->cut_sequence ? -1 : 0;
}

static void expectation_from_view(const struct RibonUpdateManifestView *view,
    struct RibonUpdateManifestExpectation *expectation)
{
    *expectation = (struct RibonUpdateManifestExpectation){
        .size = sizeof(*expectation),
        .abi_version = RIBON_UPDATE_MANIFEST_ABI_VERSION,
        .mode = view->mode,
        .protocol_major = view->protocol_major,
        .protocol_minor = view->protocol_minor,
        .hardware_revision = view->minimum_hardware_revision,
    };
    memcpy(expectation->schema_digest, view->schema_digest, 32u);
    memcpy(expectation->product_digest, view->product_digest, 32u);
    memcpy(expectation->architecture_digest, view->architecture_digest, 32u);
    memcpy(expectation->platform_digest, view->platform_digest, 32u);
    memcpy(expectation->environment_digest, view->environment_digest, 32u);
    memcpy(expectation->protocol_digest, view->protocol_digest, 32u);
    memcpy(expectation->rollback_domain_digest, view->rollback_domain_digest, 32u);
}

static int key_store_from_view(struct Fixture *fixture)
{
    fixture->key_record = (struct RibonKeyPolicyRecord){
        .size = sizeof(fixture->key_record),
        .abi_version = RIBON_KEY_POLICY_ABI_VERSION,
        .flags = RIBON_KEY_POLICY_RECORD_ROOT,
        .algorithm = RIBON_SIGNATURE_ALGORITHM_ED25519,
        .lifecycle = RIBON_KEY_POLICY_LIFECYCLE_ACTIVE,
        .mode_mask = UINT32_C(1) << (RIBON_KEY_POLICY_MODE_RECOVERY - 1u),
        .usage_mask = UINT64_C(1) <<
            (RIBON_KEY_POLICY_USAGE_UPDATE_MANIFEST - 1u),
        .key_id = fixture_key_id,
        .key_id_size = sizeof(fixture_key_id) - 1u,
        .rollback_domain_digests =
            (const uint8_t (*)[RIBON_KEY_POLICY_DIGEST_BYTES])
                &fixture->manifest_view.rollback_domain_digest,
        .rollback_domain_count = 1u,
        .minimum_sequence = fixture->manifest_view.rollback_sequence,
        .maximum_sequence = fixture->manifest_view.rollback_sequence,
    };
    memcpy(fixture->key_record.public_key, fixture_public_key, 32u);
    memcpy(fixture->key_record.key_identity_digest, fixture_key_identity, 32u);
    memcpy(fixture->key_record.product_digest,
        fixture->manifest_view.product_digest, 32u);
    fixture->key_store = (struct RibonKeyPolicyStore){
        .magic = RIBON_KEY_POLICY_STORE_MAGIC,
        .size = sizeof(fixture->key_store),
        .abi_version = RIBON_KEY_POLICY_ABI_VERSION,
        .id = store_id,
        .id_size = sizeof(store_id) - 1u,
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
    memset(fixture, 0, sizeof(*fixture));
    if (!read_file(argv[1], &fixture->manifest) ||
        !read_file(argv[2], &fixture->envelope) ||
        !read_file(argv[3], &fixture->bundle_bytes) ||
        !read_file(argv[4], &fixture->layout_bytes) ||
        !read_file(argv[5], &fixture->disk) ||
        ribon_update_manifest_open(fixture->manifest.bytes,
            fixture->manifest.size, &fixture->manifest_view) !=
            RIBON_UPDATE_MANIFEST_STATUS_OK ||
        ribon_update_layout_identity_open(fixture->layout_bytes.bytes,
            fixture->layout_bytes.size, &fixture->layout) !=
            RIBON_UPDATE_STORAGE_STATUS_OK ||
        !key_store_from_view(fixture)) {
        return 0;
    }
    expectation_from_view(&fixture->manifest_view, &fixture->expectation);
    fixture->bundle_context.bytes = fixture->bundle_bytes.bytes;
    fixture->bundle_context.size = fixture->bundle_bytes.size;
    fixture->bundle = (struct RibonUpdateBundleSource){
        .size = sizeof(fixture->bundle),
        .abi_version = RIBON_UPDATE_INSTALLER_ABI_VERSION,
        .byte_size = fixture->bundle_bytes.size,
        .context = &fixture->bundle_context,
        .read = bundle_read,
    };
    return 1;
}

static void fixture_close(struct Fixture *fixture)
{
    free(fixture->manifest.bytes);
    free(fixture->envelope.bytes);
    free(fixture->bundle_bytes.bytes);
    free(fixture->layout_bytes.bytes);
    free(fixture->disk.bytes);
}

static struct RibonUpdateTransactionJournal make_journal(
    const struct Fixture *fixture, const struct CrashStorage *storage,
    uint64_t minimum_generation)
{
    const struct RibonUpdateTransactionJournal journal = {
        .size = sizeof(journal),
        .abi_version = RIBON_UPDATE_TRANSACTION_ABI_VERSION,
        .provider = &storage->provider,
        .layout = &fixture->layout,
        .minimum_generation = minimum_generation,
        .deadline_ticks = 1u,
    };
    return journal;
}

static struct RibonUpdateInstallRequest make_install(
    const struct Fixture *fixture, const struct CrashStorage *storage,
    void *scratch)
{
    const struct RibonUpdateInstallRequest request = {
        .size = sizeof(request),
        .abi_version = RIBON_UPDATE_INSTALLER_ABI_VERSION,
        .target_slot = 1u,
        .authorization = {
            .size = sizeof(struct RibonUpdateManifestAuthorization),
            .abi_version = RIBON_UPDATE_MANIFEST_ABI_VERSION,
            .manifest = fixture->manifest.bytes,
            .manifest_size = fixture->manifest.size,
            .signature_envelope = fixture->envelope.bytes,
            .signature_envelope_size = fixture->envelope.size,
            .expectation = &fixture->expectation,
            .key_policy = &fixture->key_store,
            .signature_provider = &ribon_ed25519_signature_provider_descriptor,
        },
        .bundle = &fixture->bundle,
        .provider = &storage->provider,
        .layout = &fixture->layout,
        .scratch = scratch,
        .scratch_size = SCRATCH_BYTES,
        .deadline_ticks = 1u,
    };
    return request;
}

static int run_transaction(const struct Fixture *fixture,
    struct CrashStorage *storage, void *scratch,
    struct RibonUpdateTransactionObserver *observer,
    uint64_t minimum_generation,
    struct RibonUpdateTransactionalInstallResult *result)
{
    struct RibonUpdateTransactionJournal journal =
        make_journal(fixture, storage, minimum_generation);
    struct RibonUpdateInstallRequest install =
        make_install(fixture, storage, scratch);
    const struct RibonUpdateTransactionalInstallRequest request = {
        .size = sizeof(request),
        .abi_version = RIBON_UPDATE_TRANSACTION_ABI_VERSION,
        .pending_attempts = 3u,
        .install = &install,
        .journal = &journal,
        .observer = observer,
    };
    return ribon_update_install_transactionally(&request, result);
}

static int active_unchanged(const struct Fixture *fixture,
    const struct CrashStorage *storage)
{
    const struct RibonUpdateLayoutRegion *slot =
        &fixture->layout.regions[RIBON_UPDATE_REGION_SLOT_A - 1u];
    return memcmp(storage->durable + (size_t)slot->offset,
        fixture->disk.bytes + (size_t)slot->offset,
        (size_t)slot->length) == 0;
}

static int save_case(const char *root, const char *name,
    const struct CrashStorage *storage)
{
    char path[1024];
    const int length = snprintf(path, sizeof(path), "%s/%s.raw", root, name);
    return length > 0 && (size_t)length < sizeof(path) &&
        write_file(path, storage->durable, storage->size);
}

static int selected_case(const struct RibonUpdateTransactionEvent *event,
    const char **name)
{
    if (event->boundary != RIBON_UPDATE_TRANSACTION_BOUNDARY_AFTER) {
        return 0;
    }
    if (event->operation ==
            RIBON_UPDATE_TRANSACTION_OPERATION_JOURNAL_SELECTOR_FLUSH &&
        event->durable_state == RIBON_UPDATE_SLOT_STAGING) {
        *name = "after-staging-commit";
        return 1;
    }
    if (event->operation == RIBON_UPDATE_TRANSACTION_OPERATION_PAYLOAD_FLUSH) {
        *name = "after-payload-flush";
        return 1;
    }
    if (event->operation ==
            RIBON_UPDATE_TRANSACTION_OPERATION_JOURNAL_SELECTOR_FLUSH &&
        event->durable_state == RIBON_UPDATE_SLOT_VERIFIED) {
        *name = "after-verified-commit";
        return 1;
    }
    return 0;
}

static void write_coverage(const char *path,
    const struct TraceContext *trace, uint32_t selected_count)
{
    FILE *stream = fopen(path, "w");
    uint32_t index;
    if (stream == NULL) {
        ++failures;
        return;
    }
    (void)fprintf(stream,
        "{\n  \"schema\": \"ribon-update-power-cut-coverage-v1\",\n"
        "  \"event_count\": %u,\n  \"selected_qemu_cases\": %u,\n"
        "  \"events\": [\n", trace->count, selected_count);
    for (index = 0u; index < trace->count; ++index) {
        const struct RibonUpdateTransactionEvent *event = &trace->events[index];
        (void)fprintf(stream,
            "    {\"sequence\": %u, \"operation\": %u, \"boundary\": %u, "
            "\"state\": %u, \"component\": %u, \"generation\": %llu}%s\n",
            event->sequence, (unsigned)event->operation,
            (unsigned)event->boundary, (unsigned)event->durable_state,
            event->component_index,
            (unsigned long long)event->journal_generation,
            index + 1u == trace->count ? "" : ",");
    }
    (void)fprintf(stream, "  ]\n}\n");
    if (fclose(stream) != 0) {
        ++failures;
    }
}

static void exhaustive_faults(const struct Fixture *fixture,
    void *scratch, const char *case_root, const char *coverage_path)
{
    struct CrashStorage storage;
    struct TraceContext clean_trace = {.cut_sequence = UINT32_MAX};
    struct RibonUpdateTransactionObserver observer = {
        .size = sizeof(observer),
        .abi_version = RIBON_UPDATE_TRANSACTION_ABI_VERSION,
        .context = &clean_trace,
        .observe = observe,
    };
    struct RibonUpdateTransactionalInstallResult result;
    uint32_t selected_count = 0u;
    uint32_t index;
    const char *saved[3] = {NULL, NULL, NULL};

    crash_storage_reset(&storage, fixture);
    expect(storage.durable != NULL && storage.volatile_bytes != NULL,
        "allocate clean crash model");
    expect(run_transaction(fixture, &storage, scratch, &observer, 1u, &result) ==
            RIBON_UPDATE_TRANSACTION_STATUS_OK &&
        result.snapshot.target_state == RIBON_UPDATE_SLOT_PENDING &&
        result.snapshot.journal_generation == 4u && active_unchanged(fixture, &storage),
        "clean transaction reaches one pending generation");
    expect(clean_trace.count != 0u && clean_trace.count < MAX_EVENTS,
        "clean trace enumerates bounded operation edges");
    {
        const uint64_t generation = result.snapshot.journal_generation;
        expect(run_transaction(fixture, &storage, scratch, NULL, 1u, &result) ==
                RIBON_UPDATE_TRANSACTION_STATUS_OK &&
            result.resumed_from == RIBON_UPDATE_SLOT_PENDING &&
            result.snapshot.journal_generation == generation,
            "clean retry is pending-idempotent");
    }
    crash_storage_destroy(&storage);

    for (index = 0u; index < clean_trace.count; ++index) {
        struct TraceContext cut = {.cut_sequence = index};
        struct RibonUpdateTransactionObserver cut_observer = {
            .size = sizeof(cut_observer),
            .abi_version = RIBON_UPDATE_TRANSACTION_ABI_VERSION,
            .context = &cut,
            .observe = observe,
        };
        struct RibonUpdateTransactionJournal journal;
        struct RibonUpdateTransactionSnapshot reopened;
        const char *case_name = NULL;
        uint64_t pending_generation;
        crash_storage_reset(&storage, fixture);
        expect(run_transaction(fixture, &storage, scratch, &cut_observer, 1u,
                   &result) == RIBON_UPDATE_TRANSACTION_STATUS_INTERRUPTED,
            "every enumerated edge can fail-stop");
        power_cut(&storage);
        journal = make_journal(fixture, &storage, 1u);
        expect(ribon_update_transaction_open(&journal, &reopened) ==
                RIBON_UPDATE_TRANSACTION_STATUS_OK &&
            reopened.metadata.active_slot == 0u &&
            reopened.metadata.slots[0].state == RIBON_UPDATE_SLOT_CONFIRMED &&
            reopened.metadata.slots[1].state != RIBON_UPDATE_SLOT_CONFIRMED &&
            active_unchanged(fixture, &storage),
            "cut preserves confirmed predecessor and excludes partial confirmation");
        if (selected_case(&clean_trace.events[index], &case_name)) {
            uint32_t selected_index =
                strcmp(case_name, "after-staging-commit") == 0 ? 0u :
                strcmp(case_name, "after-payload-flush") == 0 ? 1u : 2u;
            if (saved[selected_index] == NULL) {
                expect(save_case(case_root, case_name, &storage),
                    "persist selected QEMU crash image");
                saved[selected_index] = case_name;
                ++selected_count;
            }
        }
        expect(run_transaction(fixture, &storage, scratch, NULL, 1u, &result) ==
                RIBON_UPDATE_TRANSACTION_STATUS_OK &&
            result.snapshot.target_state == RIBON_UPDATE_SLOT_PENDING,
            "clean retry closes interrupted transaction");
        pending_generation = result.snapshot.journal_generation;
        expect(run_transaction(fixture, &storage, scratch, NULL, 1u, &result) ==
                RIBON_UPDATE_TRANSACTION_STATUS_OK &&
            result.resumed_from == RIBON_UPDATE_SLOT_PENDING &&
            result.snapshot.journal_generation == pending_generation,
            "second retry does not duplicate semantic generation");
        crash_storage_destroy(&storage);
    }
    expect(selected_count == 3u, "all selected QEMU crash states emitted");
    write_coverage(coverage_path, &clean_trace, selected_count);
}

static void hostile_replay_and_io(const struct Fixture *fixture, void *scratch)
{
    struct CrashStorage storage;
    struct RibonUpdateTransactionalInstallResult result;
    struct RibonUpdateTransactionJournal journal;
    struct RibonUpdateTransactionSnapshot snapshot;
    const struct RibonUpdateLayoutRegion *region =
        &fixture->layout.regions[RIBON_UPDATE_REGION_UPDATE_JOURNAL - 1u];
    uint8_t *clean_pending;

    crash_storage_reset(&storage, fixture);
    storage.short_slot_write_call = 2u;
    expect(run_transaction(fixture, &storage, scratch, NULL, 1u, &result) ==
            RIBON_UPDATE_TRANSACTION_STATUS_INSTALL,
        "final component short write fails closed");
    power_cut(&storage);
    journal = make_journal(fixture, &storage, 1u);
    expect(ribon_update_transaction_open(&journal, &snapshot) ==
            RIBON_UPDATE_TRANSACTION_STATUS_OK &&
        snapshot.target_state == RIBON_UPDATE_SLOT_STAGING,
        "short write reopens only staging");
    crash_storage_destroy(&storage);

    crash_storage_reset(&storage, fixture);
    storage.fail_flush_call = 3u;
    expect(run_transaction(fixture, &storage, scratch, NULL, 1u, &result) ==
            RIBON_UPDATE_TRANSACTION_STATUS_INSTALL,
        "payload flush failure fails closed");
    power_cut(&storage);
    journal = make_journal(fixture, &storage, 1u);
    expect(ribon_update_transaction_open(&journal, &snapshot) ==
            RIBON_UPDATE_TRANSACTION_STATUS_OK &&
        snapshot.target_state == RIBON_UPDATE_SLOT_STAGING,
        "flush failure keeps durable staging");
    crash_storage_destroy(&storage);

    crash_storage_reset(&storage, fixture);
    storage.mutate_slot_read = 1;
    expect(run_transaction(fixture, &storage, scratch, NULL, 1u, &result) ==
            RIBON_UPDATE_TRANSACTION_STATUS_INSTALL,
        "readback mutation fails digest closure");
    crash_storage_destroy(&storage);

    crash_storage_reset(&storage, fixture);
    expect(run_transaction(fixture, &storage, scratch, NULL, 1u, &result) ==
            RIBON_UPDATE_TRANSACTION_STATUS_OK,
        "build clean pending media for replay corpus");
    clean_pending = malloc(storage.size);
    expect(clean_pending != NULL, "allocate replay corpus snapshot");
    if (clean_pending == NULL) {
        crash_storage_destroy(&storage);
        return;
    }
    memcpy(clean_pending, storage.durable, storage.size);
    journal = make_journal(fixture, &storage, 1u);
    expect(ribon_update_transaction_open(&journal, &snapshot) ==
            RIBON_UPDATE_TRANSACTION_STATUS_OK &&
        snapshot.journal_generation == 4u,
        "stale lower selector cannot override newest complete selector");
    memcpy(storage.volatile_bytes + (size_t)region->offset + 2048u,
        storage.volatile_bytes + (size_t)region->offset + 2560u, 512u);
    journal = make_journal(fixture, &storage, 1u);
    expect(ribon_update_transaction_open(&journal, &snapshot) ==
            RIBON_UPDATE_TRANSACTION_STATUS_CONFLICT,
        "duplicate generation selector is rejected");
    memcpy(storage.volatile_bytes, fixture->disk.bytes, storage.size);
    journal = make_journal(fixture, &storage, 4u);
    expect(ribon_update_transaction_open(&journal, &snapshot) ==
            RIBON_UPDATE_TRANSACTION_STATUS_REPLAY,
        "whole-media replay below external floor is rejected");
    memcpy(storage.volatile_bytes, clean_pending, storage.size);
    storage.volatile_bytes[(size_t)region->offset + 1024u + 240u] ^= 1u;
    journal = make_journal(fixture, &storage, 1u);
    expect(ribon_update_transaction_open(&journal, &snapshot) ==
            RIBON_UPDATE_TRANSACTION_STATUS_CORRUPT,
        "newest selector never falls back from torn selected record");
    memcpy(storage.volatile_bytes, fixture->disk.bytes, storage.size);
    memset(storage.volatile_bytes + (size_t)region->offset + 2560u, 0xa5, 73u);
    journal = make_journal(fixture, &storage, 1u);
    expect(ribon_update_transaction_open(&journal, &snapshot) ==
            RIBON_UPDATE_TRANSACTION_STATUS_OK &&
        snapshot.journal_generation == 1u,
        "torn inactive selector preserves predecessor authority");
    free(clean_pending);
    crash_storage_destroy(&storage);
}

int main(int argc, char **argv)
{
    struct Fixture fixture;
    void *scratch;
    if (argc != 8) {
        (void)fprintf(stderr,
            "usage: %s manifest envelope bundle layout disk case-dir coverage.json\n",
            argv[0]);
        return 2;
    }
    if (!fixture_open(&fixture, argv)) {
        (void)fprintf(stderr, "FAIL: open power-cut fixtures\n");
        fixture_close(&fixture);
        return 1;
    }
    scratch = aligned_alloc(4096u, SCRATCH_BYTES);
    expect(scratch != NULL, "allocate aligned transaction scratch");
    if (scratch != NULL) {
        exhaustive_faults(&fixture, scratch, argv[6], argv[7]);
        hostile_replay_and_io(&fixture, scratch);
    }
    free(scratch);
    fixture_close(&fixture);
    if (failures != 0) {
        (void)fprintf(stderr, "RIBON-UPDATE-POWER-CUT-FAIL count=%d\n", failures);
        return 1;
    }
    (void)printf(
        "RIBON-UPDATE-POWER-CUT-OK exhaustive=all confirmed-loss=0 duplicate-pending=0 selected=3 hostile=8\n");
    return 0;
}
