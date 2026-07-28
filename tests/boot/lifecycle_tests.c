#include <Ribon/arch/ops.h>
#include <Ribon/boot/plan.h>
#include <Ribon/core/context.h>
#include <Ribon/plugin/registry.h>

#include "../../src/environments/host/host.h"

#include <stdio.h>
#include <string.h>

#define LIFECYCLE_IMAGE_CAPACITY 512u
#define LIFECYCLE_HANDOFF_CAPACITY 256u
#define LIFECYCLE_SEGMENT_CAPACITY 4u

static int failures;
static unsigned char arena_storage[256u * 1024u];

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (0)

/** @brief Little-endian fixture field를 deterministic byte sequence로 기록한다. */
static void write_le16(unsigned char *bytes, uint32_t offset, uint16_t value) {
    bytes[offset] = (unsigned char)value;
    bytes[offset + 1u] = (unsigned char)(value >> 8u);
}

/** @brief Little-endian fixture field를 deterministic byte sequence로 기록한다. */
static void write_le32(unsigned char *bytes, uint32_t offset, uint32_t value) {
    for (uint32_t index = 0u; index < 4u; ++index) {
        bytes[offset + index] = (unsigned char)(value >> (index * 8u));
    }
}

/** @brief Little-endian fixture field를 deterministic byte sequence로 기록한다. */
static void write_le64(unsigned char *bytes, uint32_t offset, uint64_t value) {
    for (uint32_t index = 0u; index < 8u; ++index) {
        bytes[offset + index] = (unsigned char)(value >> (index * 8u));
    }
}

/** @brief Selected host architecture가 해석하는 최소 ELF64 payload를 만든다. */
static void build_fixture_elf(
    unsigned char *bytes,
    const struct RibonArchDescriptor *arch) {
    const uint16_t machine = arch->id == RIBON_ARCHITECTURE_AARCH64 ? 183u : 62u;
    memset(bytes, 0, LIFECYCLE_IMAGE_CAPACITY);
    bytes[0] = 0x7fu;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 2u;
    bytes[5] = 1u;
    bytes[6] = 1u;
    write_le16(bytes, 16u, 2u);
    write_le16(bytes, 18u, machine);
    write_le32(bytes, 20u, 1u);
    write_le64(bytes, 24u, 0x200080u);
    write_le64(bytes, 32u, 64u);
    write_le16(bytes, 52u, 64u);
    write_le16(bytes, 54u, 56u);
    write_le16(bytes, 56u, 1u);
    write_le32(bytes, 64u, 1u);
    write_le32(bytes, 68u, 5u);
    write_le64(bytes, 72u, 0u);
    write_le64(bytes, 80u, 0x200000u);
    write_le64(bytes, 88u, 0x200000u);
    write_le64(bytes, 96u, 256u);
    write_le64(bytes, 104u, 4096u);
    write_le64(bytes, 112u, 4096u);
}

/** @brief Generated host graph로 caller-owned transaction을 초기화한다. */
static int initialize_transaction(
    struct RibonBootTransaction *transaction,
    struct RibonCoreContext *core,
    struct RibonArena *arena) {
    const struct RibonPluginRegistry *registry = ribon_generated_plugin_registry();
    const struct RibonPluginDescriptor *protocol_plugin = ribon_plugin_registry_find(
        registry, RIBON_PLUGIN_KIND_BOOT_PROTOCOL, "protocol.synthetic");
    const struct RibonPluginDescriptor *image_plugin = ribon_plugin_registry_find(
        registry, RIBON_PLUGIN_KIND_IMAGE_FORMAT, "image.elf64");
    if (protocol_plugin == 0 || image_plugin == 0 || arena == 0) {
        return RIBON_BOOT_STATUS_BAD_ARGUMENT;
    }
    ribon_arena_init(arena, arena_storage, sizeof(arena_storage));
    if (ribon_context_initialize(
            core,
            ribon_generated_product_descriptor(),
            registry,
            ribon_generated_service_directory(),
            ribon_mode_selected(),
            arena) != RIBON_CORE_STATUS_OK) {
        return RIBON_BOOT_STATUS_BAD_ARGUMENT;
    }
    return ribon_boot_transaction_initialize(
        transaction,
        core,
        ribon_arch_selected_ops(),
        protocol_plugin->operations,
        image_plugin->operations);
}

/** @brief Fixture source와 caller-owned storage로 prepare stage를 실행한다. */
static int prepare_transaction(
    struct RibonBootTransaction *transaction,
    unsigned char *source_bytes,
    unsigned char *payload_bytes,
    struct RibonLoadedPayload *layout,
    struct RibonMutableMemoryMap *normalized,
    unsigned char *handoff_bytes,
    struct RibonHandoffArtifact *handoff) {
    struct RibonBootEnvironment environment;
    const struct RibonBootSource source = {
        .kind = RIBON_BOOT_MEDIA_MEMORY,
        .source_id = 0u,
        .size = LIFECYCLE_IMAGE_CAPACITY,
        .block_size = 0u,
    };
    if (ribon_host_environment_collect(
            ribon_arch_selected_ops()->descriptor->id, &environment) !=
        RIBON_SERVICE_STATUS_OK ||
        ribon_host_boot_source_bind(source_bytes, LIFECYCLE_IMAGE_CAPACITY) !=
            RIBON_SERVICE_STATUS_OK) {
        return RIBON_BOOT_STATUS_BAD_ARGUMENT;
    }
    return ribon_boot_transaction_prepare(transaction, &(struct RibonBootTransactionInput){
        .environment = &environment,
        .normalized_memory_map = normalized,
        .source = &source,
        .source_offset = 0u,
        .source_size = LIFECYCLE_IMAGE_CAPACITY,
        .payload_buffer = payload_bytes,
        .payload_buffer_capacity = LIFECYCLE_IMAGE_CAPACITY,
        .source_name = "fixture.elf",
        .kernel_layout = layout,
        .handoff_buffer = handoff_bytes,
        .handoff_buffer_capacity = LIFECYCLE_HANDOFF_CAPACITY,
        .handoff_artifact = handoff,
    });
}

/** @brief Unexpected prepare failure의 typed receipt를 test output에 보존한다. */
static void report_prepare_failure(
    int status,
    const struct RibonBootTransaction *transaction) {
    const struct RibonBootFailureReceipt *receipt;
    if (status == RIBON_BOOT_STATUS_OK) {
        return;
    }
    receipt = ribon_boot_transaction_failure_receipt(transaction);
    fprintf(stderr, "prepare failed: status=%d stage=%d reason=%d provider=%s\n",
            status,
            receipt != 0 ? (int)receipt->stage : -1,
            receipt != 0 ? (int)receipt->reason : -1,
            receipt != 0 && receipt->provider_id != 0 ? receipt->provider_id : "(none)");
}

/** @brief Success path가 durable commit, flush, closure까지 단방향으로 전진하는지 검사한다. */
static void test_success_path(void) {
    unsigned char source[LIFECYCLE_IMAGE_CAPACITY];
    unsigned char payload[LIFECYCLE_IMAGE_CAPACITY];
    unsigned char handoff_bytes[LIFECYCLE_HANDOFF_CAPACITY];
    struct RibonLoadSegment segments[LIFECYCLE_SEGMENT_CAPACITY];
    struct RibonMemoryRegion normalized_regions[16];
    struct RibonLoadedPayload layout = { .segments = segments, .segment_capacity = LIFECYCLE_SEGMENT_CAPACITY };
    struct RibonMutableMemoryMap normalized = { .regions = normalized_regions, .capacity = 16u };
    struct RibonHandoffArtifact handoff = {0};
    struct RibonBootTransaction transaction;
    struct RibonCoreContext core;
    struct RibonArena arena;
    int prepare_status;
    build_fixture_elf(source, ribon_arch_selected_ops()->descriptor);
    ribon_host_lifecycle_fixture_reset();
    CHECK(initialize_transaction(&transaction, &core, &arena) == RIBON_BOOT_STATUS_OK);
    prepare_status = prepare_transaction(
        &transaction, source, payload, &layout, &normalized, handoff_bytes, &handoff);
    report_prepare_failure(prepare_status, &transaction);
    CHECK(prepare_status == RIBON_BOOT_STATUS_OK);
    CHECK(transaction.stage == RIBON_BOOT_STAGE_PREPARE_PROTOCOL);
    CHECK(transaction.consumed_input_bytes == LIFECYCLE_IMAGE_CAPACITY);
    CHECK(transaction.consumed_components == 1u);
    CHECK(ribon_boot_transaction_plan(&transaction) != 0);
    CHECK(ribon_boot_transaction_commit_attempt(&transaction) == RIBON_BOOT_STATUS_OK);
    CHECK(ribon_host_lifecycle_fixture_write_count() == 1u);
    CHECK(ribon_host_lifecycle_fixture_flush_count() == 1u);
    CHECK(ribon_boot_transaction_quiesce_environment(&transaction) == RIBON_BOOT_STATUS_OK);
    CHECK(transaction.stage == RIBON_BOOT_STAGE_QUIESCE_ENVIRONMENT);
    CHECK(ribon_host_lifecycle_fixture_quiesce_count() == 1u);
}

/** @brief Transient source failure는 product retry budget 안에서만 재시도하는지 검사한다. */
static void test_source_retry_and_exhaustion(void) {
    unsigned char source[LIFECYCLE_IMAGE_CAPACITY];
    unsigned char payload[LIFECYCLE_IMAGE_CAPACITY];
    unsigned char handoff_bytes[LIFECYCLE_HANDOFF_CAPACITY];
    struct RibonLoadSegment segments[LIFECYCLE_SEGMENT_CAPACITY];
    struct RibonMemoryRegion normalized_regions[16];
    struct RibonLoadedPayload layout = { .segments = segments, .segment_capacity = LIFECYCLE_SEGMENT_CAPACITY };
    struct RibonMutableMemoryMap normalized = { .regions = normalized_regions, .capacity = 16u };
    struct RibonHandoffArtifact handoff = {0};
    struct RibonBootTransaction transaction;
    struct RibonCoreContext core;
    struct RibonArena arena;
    const struct RibonBootFailureReceipt *receipt;
    build_fixture_elf(source, ribon_arch_selected_ops()->descriptor);

    ribon_host_lifecycle_fixture_reset();
    ribon_host_lifecycle_fixture_set_failures(1u, 0u, 0u, 0u);
    CHECK(initialize_transaction(&transaction, &core, &arena) == RIBON_BOOT_STATUS_OK);
    CHECK(prepare_transaction(&transaction, source, payload, &layout, &normalized,
                              handoff_bytes, &handoff) == RIBON_BOOT_STATUS_OK);
    CHECK(transaction.consumed_retries == 1u);

    ribon_host_lifecycle_fixture_reset();
    ribon_host_lifecycle_fixture_set_failures(3u, 0u, 0u, 0u);
    CHECK(initialize_transaction(&transaction, &core, &arena) == RIBON_BOOT_STATUS_OK);
    CHECK(prepare_transaction(&transaction, source, payload, &layout, &normalized,
                              handoff_bytes, &handoff) == RIBON_BOOT_STATUS_PROVIDER_FAILURE);
    receipt = ribon_boot_transaction_failure_receipt(&transaction);
    CHECK(receipt != 0);
    CHECK(receipt->stage == RIBON_BOOT_STAGE_LOAD_IMAGE);
    CHECK(receipt->reason == RIBON_BOOT_FAILURE_SOURCE);
    CHECK(receipt->provider_id != 0);
    CHECK(receipt->consumed_retries == 2u);
}

/** @brief Expired deadline과 partial durable write가 terminal receipt를 남기는지 검사한다. */
static void test_timeout_and_partial_commit_failure(void) {
    unsigned char source[LIFECYCLE_IMAGE_CAPACITY];
    unsigned char payload[LIFECYCLE_IMAGE_CAPACITY];
    unsigned char handoff_bytes[LIFECYCLE_HANDOFF_CAPACITY];
    struct RibonLoadSegment segments[LIFECYCLE_SEGMENT_CAPACITY];
    struct RibonMemoryRegion normalized_regions[16];
    struct RibonLoadedPayload layout = { .segments = segments, .segment_capacity = LIFECYCLE_SEGMENT_CAPACITY };
    struct RibonMutableMemoryMap normalized = { .regions = normalized_regions, .capacity = 16u };
    struct RibonHandoffArtifact handoff = {0};
    struct RibonBootTransaction transaction;
    struct RibonCoreContext core;
    struct RibonArena arena;
    const struct RibonBootFailureReceipt *receipt;
    build_fixture_elf(source, ribon_arch_selected_ops()->descriptor);

    ribon_host_lifecycle_fixture_reset();
    ribon_host_lifecycle_fixture_set_timer_step(30000001u);
    CHECK(initialize_transaction(&transaction, &core, &arena) == RIBON_BOOT_STATUS_OK);
    CHECK(prepare_transaction(&transaction, source, payload, &layout, &normalized,
                              handoff_bytes, &handoff) == RIBON_BOOT_STATUS_TIMEOUT);
    receipt = ribon_boot_transaction_failure_receipt(&transaction);
    CHECK(receipt != 0 && receipt->reason == RIBON_BOOT_FAILURE_TIMEOUT);

    ribon_host_lifecycle_fixture_reset();
    CHECK(initialize_transaction(&transaction, &core, &arena) == RIBON_BOOT_STATUS_OK);
    CHECK(prepare_transaction(&transaction, source, payload, &layout, &normalized,
                              handoff_bytes, &handoff) == RIBON_BOOT_STATUS_OK);
    ribon_host_lifecycle_fixture_set_failures(0u, 1u, 0u, 0u);
    CHECK(ribon_boot_transaction_commit_attempt(&transaction) ==
          RIBON_BOOT_STATUS_PROVIDER_FAILURE);
    receipt = ribon_boot_transaction_failure_receipt(&transaction);
    CHECK(receipt != 0 && receipt->stage == RIBON_BOOT_STAGE_COMMIT_ATTEMPT);
    CHECK(receipt != 0 && receipt->reason == RIBON_BOOT_FAILURE_COMMIT);
    CHECK(ribon_host_lifecycle_fixture_metadata_size() > 0u);
    CHECK(ribon_host_lifecycle_fixture_metadata_size() < 40u);
    CHECK(ribon_host_lifecycle_fixture_flush_count() == 0u);
}

/** @brief Quiesce provider failure가 transfer 전 terminal state를 보존하는지 검사한다. */
static void test_quiesce_failure(void) {
    unsigned char source[LIFECYCLE_IMAGE_CAPACITY];
    unsigned char payload[LIFECYCLE_IMAGE_CAPACITY];
    unsigned char handoff_bytes[LIFECYCLE_HANDOFF_CAPACITY];
    struct RibonLoadSegment segments[LIFECYCLE_SEGMENT_CAPACITY];
    struct RibonMemoryRegion normalized_regions[16];
    struct RibonLoadedPayload layout = { .segments = segments, .segment_capacity = LIFECYCLE_SEGMENT_CAPACITY };
    struct RibonMutableMemoryMap normalized = { .regions = normalized_regions, .capacity = 16u };
    struct RibonHandoffArtifact handoff = {0};
    struct RibonBootTransaction transaction;
    struct RibonCoreContext core;
    struct RibonArena arena;
    const struct RibonBootFailureReceipt *receipt;
    build_fixture_elf(source, ribon_arch_selected_ops()->descriptor);
    ribon_host_lifecycle_fixture_reset();
    CHECK(initialize_transaction(&transaction, &core, &arena) == RIBON_BOOT_STATUS_OK);
    CHECK(prepare_transaction(&transaction, source, payload, &layout, &normalized,
                              handoff_bytes, &handoff) == RIBON_BOOT_STATUS_OK);
    CHECK(ribon_boot_transaction_commit_attempt(&transaction) == RIBON_BOOT_STATUS_OK);
    ribon_host_lifecycle_fixture_set_failures(0u, 0u, 0u, 1u);
    CHECK(ribon_boot_transaction_quiesce_environment(&transaction) ==
          RIBON_BOOT_STATUS_PROVIDER_FAILURE);
    receipt = ribon_boot_transaction_failure_receipt(&transaction);
    CHECK(receipt != 0 && receipt->stage == RIBON_BOOT_STAGE_QUIESCE_ENVIRONMENT);
    CHECK(receipt != 0 && receipt->reason == RIBON_BOOT_FAILURE_QUIESCE);
}

int main(void) {
    test_success_path();
    test_source_retry_and_exhaustion();
    test_timeout_and_partial_commit_failure();
    test_quiesce_failure();
    if (failures != 0) {
        fprintf(stderr, "lifecycle_tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("RIBON-R7-BOUNDED-LIFECYCLE-OK");
    return 0;
}
