#include <Ribon/arch/ops.h>
#include <Ribon/boot/plan.h>
#include <Ribon/policy/ribos.h>

#include <ribos/artifact/format.h>
#include <ribos/schema/schema.h>
#include <ribos/vm/runtime.h>

#include "../../src/environments/host/host.h"
#include "../../src/environments/host/ribos_policy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_IMAGE_CAPACITY 512u
#define TEST_HANDOFF_CAPACITY 256u
#define TEST_CONTEXT_CAPACITY 512u
#define TEST_ARENA_CAPACITY (512u * 1024u)

static int failures;
static uint8_t arena_bytes[TEST_ARENA_CAPACITY];
static RibosProductSchema alternate_schema;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (0)

struct TestTransaction {
    struct RibonArena arena;
    struct RibonCoreContext core;
    struct RibonBootTransaction transaction;
    uint8_t source[TEST_IMAGE_CAPACITY];
    uint8_t payload[TEST_IMAGE_CAPACITY];
    uint8_t handoff_bytes[TEST_HANDOFF_CAPACITY];
    struct RibonLoadSegment segments[4];
    struct RibonLoadedPayload layout;
    struct RibonMemoryRegion normalized_regions[16];
    struct RibonMutableMemoryMap normalized;
    struct RibonHandoffArtifact handoff;
};

struct TestArtifact {
    uint8_t *bytes;
    size_t size;
    uint32_t context_type;
    uint32_t context_size;
    uint32_t slot_type;
    uint32_t image_type;
    uint32_t verified_type;
    uint32_t action_type;
    uint32_t action_size;
};

static uint32_t
read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

static void
write_u16(uint8_t *bytes, uint32_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1u] = (uint8_t)(value >> 8);
}

static void
write_u32(uint8_t *bytes, uint32_t offset, uint32_t value)
{
    uint32_t index;

    for (index = 0; index < 4u; ++index) {
        bytes[offset + index] = (uint8_t)(value >> (index * 8u));
    }
}

static void
write_u64(uint8_t *bytes, uint32_t offset, uint64_t value)
{
    uint32_t index;

    for (index = 0; index < 8u; ++index) {
        bytes[offset + index] = (uint8_t)(value >> (index * 8u));
    }
}

static void
build_elf(
    uint8_t bytes[TEST_IMAGE_CAPACITY],
    const struct RibonArchDescriptor *arch)
{
    uint16_t machine = 62u;

    if (arch->id == RIBON_ARCHITECTURE_AARCH64) {
        machine = 183u;
    } else if (arch->id == RIBON_ARCHITECTURE_RISCV64) {
        machine = 243u;
    }
    memset(bytes, 0, TEST_IMAGE_CAPACITY);
    bytes[0] = 0x7fu;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 2u;
    bytes[5] = 1u;
    bytes[6] = 1u;
    write_u16(bytes, 16u, 2u);
    write_u16(bytes, 18u, machine);
    write_u32(bytes, 20u, 1u);
    write_u64(bytes, 24u, 0x200080u);
    write_u64(bytes, 32u, 64u);
    write_u16(bytes, 52u, 64u);
    write_u16(bytes, 54u, 56u);
    write_u16(bytes, 56u, 1u);
    write_u32(bytes, 64u, 1u);
    write_u32(bytes, 68u, 5u);
    write_u64(bytes, 72u, 0u);
    write_u64(bytes, 80u, 0x200000u);
    write_u64(bytes, 88u, 0x200000u);
    write_u64(bytes, 96u, 256u);
    write_u64(bytes, 104u, 4096u);
    write_u64(bytes, 112u, 4096u);
}

static const uint8_t *
section_row(
    const RibosArtifactSectionView *section,
    uint32_t index)
{
    size_t offset;

    if (section == NULL || index >= section->count ||
        section->row_size == 0u ||
        (size_t)index > SIZE_MAX / section->row_size) {
        return NULL;
    }
    offset = (size_t)index * section->row_size;
    if (offset > section->byte_length ||
        section->row_size > section->byte_length - offset) {
        return NULL;
    }
    return section->bytes + offset;
}

static int
find_named_type(
    const RibosArtifactView *view,
    const char *name,
    uint32_t *type_id,
    uint32_t *byte_size)
{
    const RibosArtifactSectionView *types =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_TYPES);
    const size_t name_size = strlen(name);
    uint32_t index;

    if (types == NULL || type_id == NULL || byte_size == NULL) {
        return 0;
    }
    for (index = 0; index < types->count; ++index) {
        const uint8_t *row = section_row(types, index);
        uint32_t length;

        if (row == NULL) {
            return 0;
        }
        length = read_u32(row + 56u);
        if (length == name_size &&
            length <= RIBOS_ARTIFACT_TYPE_ROW_BYTES - 60u &&
            memcmp(row + 60u, name, length) == 0) {
            *type_id = index;
            *byte_size = read_u32(row + 40u);
            return 1;
        }
    }
    return 0;
}

static int
entry_context(
    const RibosArtifactView *view,
    uint32_t *type_id,
    uint32_t *byte_size)
{
    const RibosArtifactSectionView *functions =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_FUNCTIONS);
    const RibosArtifactSectionView *slots =
        ribos_artifact_find_section(
            view,
            RIBOS_ARTIFACT_SECTION_SLOTS);
    const uint8_t *function =
        section_row(functions, view->entry_function);
    const uint8_t *slot;

    if (function == NULL || read_u32(function + 36u) != 1u) {
        return 0;
    }
    slot = section_row(slots, read_u32(function + 32u));
    if (slot == NULL) {
        return 0;
    }
    *type_id = read_u32(slot + 8u);
    *byte_size = read_u32(slot + 16u);
    return 1;
}

static int
load_artifact(const char *path, struct TestArtifact *artifact)
{
    RibosArtifactView view;
    FILE *file;
    long length;
    uint32_t unused_size;

    memset(artifact, 0, sizeof(*artifact));
    file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        return 0;
    }
    length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    artifact->bytes = malloc((size_t)length);
    if (artifact->bytes == NULL ||
        fread(artifact->bytes, 1u, (size_t)length, file) !=
            (size_t)length) {
        free(artifact->bytes);
        fclose(file);
        return 0;
    }
    fclose(file);
    artifact->size = (size_t)length;
    return ribos_artifact_open_v1(
               artifact->bytes,
               artifact->size,
               &view) == RIBOS_ARTIFACT_OK &&
           entry_context(
               &view,
               &artifact->context_type,
               &artifact->context_size) &&
           find_named_type(
               &view,
               "Slot",
               &artifact->slot_type,
               &unused_size) &&
           find_named_type(
               &view,
               "Image",
               &artifact->image_type,
               &unused_size) &&
           find_named_type(
               &view,
               "VerifiedImage",
               &artifact->verified_type,
               &unused_size) &&
           find_named_type(
               &view,
               "BootAction",
               &artifact->action_type,
               &artifact->action_size);
}

static int
initialize_transaction(struct TestTransaction *test)
{
    const struct RibonPluginRegistry *registry =
        ribon_generated_plugin_registry();
    const struct RibonPluginDescriptor *protocol =
        ribon_plugin_registry_find(
            registry,
            RIBON_PLUGIN_KIND_BOOT_PROTOCOL,
            "protocol.synthetic");
    const struct RibonPluginDescriptor *image =
        ribon_plugin_registry_find(
            registry,
            RIBON_PLUGIN_KIND_IMAGE_FORMAT,
            "image.elf64");
    struct RibonBootEnvironment environment;
    const struct RibonBootSource source = {
        .kind = RIBON_BOOT_MEDIA_MEMORY,
        .size = TEST_IMAGE_CAPACITY,
    };

    memset(test, 0, sizeof(*test));
    build_elf(test->source, ribon_arch_selected_ops()->descriptor);
    ribon_host_lifecycle_fixture_reset();
    if (ribon_host_boot_source_bind(
            test->source,
            sizeof(test->source)) != RIBON_SERVICE_STATUS_OK) {
        return 0;
    }
    ribon_arena_init(
        &test->arena,
        arena_bytes,
        sizeof(arena_bytes));
    if (ribon_context_initialize(
            &test->core,
            ribon_generated_product_descriptor(),
            registry,
            ribon_generated_service_directory(),
            ribon_mode_selected(),
            &test->arena) != RIBON_CORE_STATUS_OK ||
        protocol == NULL || image == NULL ||
        ribon_boot_transaction_initialize(
            &test->transaction,
            &test->core,
            ribon_arch_selected_ops(),
            protocol->operations,
            image->operations) != RIBON_BOOT_STATUS_OK ||
        ribon_host_environment_collect(
            ribon_arch_selected_ops()->descriptor->id,
            &environment) != RIBON_SERVICE_STATUS_OK) {
        return 0;
    }
    test->layout = (struct RibonLoadedPayload){
        .segments = test->segments,
        .segment_capacity =
            (uint32_t)(sizeof(test->segments) / sizeof(test->segments[0])),
    };
    test->normalized = (struct RibonMutableMemoryMap){
        .regions = test->normalized_regions,
        .capacity = (uint32_t)(
            sizeof(test->normalized_regions) /
            sizeof(test->normalized_regions[0])),
    };
    return ribon_boot_transaction_prepare(
               &test->transaction,
               &(const struct RibonBootTransactionInput){
                   .environment = &environment,
                   .normalized_memory_map = &test->normalized,
                   .source = &source,
                   .source_size = TEST_IMAGE_CAPACITY,
                   .payload_buffer = test->payload,
                   .payload_buffer_capacity = sizeof(test->payload),
                   .source_name = "r17-fixture.elf",
                   .kernel_layout = &test->layout,
                   .handoff_buffer = test->handoff_bytes,
                   .handoff_buffer_capacity = sizeof(test->handoff_bytes),
                   .handoff_artifact = &test->handoff,
               }) == RIBON_BOOT_STATUS_OK;
}

static void
initialize_fixture(
    struct RibonHostRibosFixture *fixture,
    const struct TestArtifact *artifact,
    const struct RibonRibosProductBinding *binding)
{
    *fixture = (struct RibonHostRibosFixture){
        .binding = binding,
        .slot_type = artifact->slot_type,
        .image_type = artifact->image_type,
        .verified_image_type = artifact->verified_type,
        .boot_action_type = artifact->action_type,
        .boot_action_size = artifact->action_size,
        .slot_object = 7u,
        .image_object = 11u,
    };
}

static int
execute_policy(
    struct TestTransaction *transaction,
    struct RibonHostRibosFixture *fixture,
    const struct TestArtifact *artifact,
    const struct RibonRibosProductBinding *binding,
    const uint8_t *artifact_bytes,
    size_t artifact_size,
    struct RibonRibosPolicyReceipt *receipt)
{
    uint8_t context[TEST_CONTEXT_CAPACITY] = {0};
    int status;

    if (artifact->context_size > sizeof(context)) {
        return RIBON_RIBOS_POLICY_STATUS_BAD_ARGUMENT;
    }
    fixture->binding = binding;
    status = ribon_ribos_policy_execute(
        &(const struct RibonRibosPolicyRequest){
            .core = &transaction->core,
            .binding = binding,
            .artifact = artifact_bytes,
            .artifact_size = artifact_size,
            .context_type_id = artifact->context_type,
            .context_bytes = context,
            .context_size = artifact->context_size,
            .context_generation = 17u,
            .product_context = fixture,
            .transaction = &transaction->transaction,
        },
        receipt);
    return status;
}

static const RibosProductSchema *
alternate_schema_provider(void)
{
    return &alternate_schema;
}

static void
test_success(const struct TestArtifact *artifact)
{
    struct TestTransaction test;
    struct RibonHostRibosFixture fixture;
    struct RibonRibosPolicyReceipt receipt;
    const struct RibonRibosProductBinding *binding =
        ribon_generated_ribos_policy_binding();

    CHECK(initialize_transaction(&test));
    initialize_fixture(&fixture, artifact, binding);
    ribon_host_lifecycle_fixture_set_timer_step(1u);
    CHECK(execute_policy(
              &test,
              &fixture,
              artifact,
              binding,
              artifact->bytes,
              artifact->size,
              &receipt) == RIBON_RIBOS_POLICY_STATUS_OK);
    CHECK(receipt.stage == RIBON_RIBOS_POLICY_STAGE_COMPLETE);
    CHECK(receipt.action_consumed == 1u);
    CHECK(receipt.recovery_notified == 0u);
    CHECK(receipt.terminal_helper_id == 21u);
    CHECK(receipt.transaction_stage ==
          RIBON_BOOT_STAGE_QUIESCE_ENVIRONMENT);
    CHECK(fixture.helper_calls == 4u);
    CHECK(fixture.fallback_calls == 0u);
    CHECK(ribon_host_lifecycle_fixture_write_count() == 1u);
    CHECK(ribon_host_lifecycle_fixture_flush_count() == 1u);
    CHECK(ribon_host_lifecycle_fixture_quiesce_count() == 1u);
    CHECK(ribon_host_lifecycle_fixture_watchdog_arm_count() == 1u);
    CHECK(ribon_host_lifecycle_fixture_watchdog_timeout_ms() == 30000u);
}

static void
test_unsigned_and_corrupt_fallback(const struct TestArtifact *artifact)
{
    struct TestTransaction test;
    struct RibonHostRibosFixture fixture;
    struct RibonRibosPolicyReceipt receipt;
    const struct RibonRibosProductBinding *binding =
        ribon_generated_ribos_policy_binding();
    uint8_t *corrupt = malloc(artifact->size);

    CHECK(corrupt != NULL);
    if (corrupt == NULL) {
        return;
    }
    memcpy(corrupt, artifact->bytes, artifact->size);
    corrupt[artifact->size - 1u] ^= 0x80u;
    CHECK(initialize_transaction(&test));
    initialize_fixture(&fixture, artifact, binding);
    fixture.reject_unsigned = 1u;
    CHECK(execute_policy(
              &test,
              &fixture,
              artifact,
              binding,
              artifact->bytes,
              artifact->size,
              &receipt) ==
          RIBON_RIBOS_POLICY_STATUS_AUTHORIZATION);
    CHECK(fixture.fallback_calls == 1u);
    CHECK(fixture.helper_calls == 0u);
    CHECK(receipt.action_consumed == 0u);
    CHECK(test.transaction.stage == RIBON_BOOT_STAGE_PREPARE_PROTOCOL);

    CHECK(initialize_transaction(&test));
    initialize_fixture(&fixture, artifact, binding);
    CHECK(execute_policy(
              &test,
              &fixture,
              artifact,
              binding,
              corrupt,
              artifact->size,
              &receipt) ==
          RIBON_RIBOS_POLICY_STATUS_AUTHORIZATION);
    CHECK(fixture.fallback_calls == 1u);
    CHECK(fixture.helper_calls == 0u);
    CHECK(test.transaction.stage == RIBON_BOOT_STAGE_PREPARE_PROTOCOL);
    free(corrupt);
}

static void
test_schema_budget_and_deadline_fallback(
    const struct TestArtifact *artifact)
{
    struct TestTransaction test;
    struct RibonHostRibosFixture fixture;
    struct RibonRibosPolicyReceipt receipt;
    const struct RibonRibosProductBinding *generated =
        ribon_generated_ribos_policy_binding();
    struct RibonRibosProductBinding binding = *generated;
    RibosVmLimits limits = *generated->limits;

    alternate_schema = *ribos_schema_reference_v1();
    alternate_schema.product_id = "ribon.schema-mismatch.fixture";
    binding.schema = alternate_schema_provider;
    CHECK(initialize_transaction(&test));
    initialize_fixture(&fixture, artifact, &binding);
    CHECK(execute_policy(
              &test,
              &fixture,
              artifact,
              &binding,
              artifact->bytes,
              artifact->size,
              &receipt) ==
          RIBON_RIBOS_POLICY_STATUS_AUTHORIZATION);
    CHECK(fixture.fallback_calls == 1u);

    binding = *generated;
    limits.maximum_instructions = 1u;
    binding.limits = &limits;
    CHECK(initialize_transaction(&test));
    initialize_fixture(&fixture, artifact, &binding);
    CHECK(execute_policy(
              &test,
              &fixture,
              artifact,
              &binding,
              artifact->bytes,
              artifact->size,
              &receipt) ==
          RIBON_RIBOS_POLICY_STATUS_VERIFICATION);
    CHECK(fixture.fallback_calls == 1u);
    CHECK(fixture.helper_calls == 0u);

    binding = *generated;
    CHECK(initialize_transaction(&test));
    initialize_fixture(&fixture, artifact, &binding);
    ribon_host_lifecycle_fixture_set_timer_step(20000u);
    CHECK(execute_policy(
              &test,
              &fixture,
              artifact,
              &binding,
              artifact->bytes,
              artifact->size,
              &receipt) ==
          RIBON_RIBOS_POLICY_STATUS_VM_FAULT);
    CHECK(receipt.outcome_kind == RIBOS_VM_OUTCOME_VM_FAULT);
    CHECK(fixture.fallback_calls == 1u);
    CHECK(receipt.recovery_notified == 1u);
    CHECK(test.transaction.stage == RIBON_BOOT_STAGE_PREPARE_PROTOCOL);
}

static void
test_action_commit_and_factory_fallback(
    const struct TestArtifact *artifact)
{
    struct TestTransaction test;
    struct RibonHostRibosFixture fixture;
    struct RibonRibosPolicyReceipt receipt;
    const struct RibonRibosProductBinding *binding =
        ribon_generated_ribos_policy_binding();

    CHECK(initialize_transaction(&test));
    initialize_fixture(&fixture, artifact, binding);
    fixture.reject_action = 1u;
    CHECK(execute_policy(
              &test,
              &fixture,
              artifact,
              binding,
              artifact->bytes,
              artifact->size,
              &receipt) ==
          RIBON_RIBOS_POLICY_STATUS_ACTION_REJECTED);
    CHECK(receipt.action_consumed == 0u);
    CHECK(fixture.fallback_calls == 1u);
    CHECK(ribon_host_lifecycle_fixture_write_count() == 0u);

    CHECK(initialize_transaction(&test));
    initialize_fixture(&fixture, artifact, binding);
    ribon_host_lifecycle_fixture_set_failures(0u, 1u, 0u, 0u);
    CHECK(execute_policy(
              &test,
              &fixture,
              artifact,
              binding,
              artifact->bytes,
              artifact->size,
              &receipt) ==
          RIBON_RIBOS_POLICY_STATUS_COMMIT_FAILED);
    CHECK(receipt.action_consumed == 1u);
    CHECK(fixture.fallback_calls == 1u);
    CHECK(test.transaction.stage == RIBON_BOOT_STAGE_FAILED);

    CHECK(initialize_transaction(&test));
    initialize_fixture(&fixture, artifact, binding);
    CHECK(execute_policy(
              &test,
              &fixture,
              artifact,
              binding,
              NULL,
              0u,
              &receipt) ==
          RIBON_RIBOS_POLICY_STATUS_BAD_ARGUMENT);
    CHECK(fixture.fallback_calls == 1u);
    CHECK(fixture.helper_calls == 0u);
    CHECK(fixture.last_failure.stage ==
          RIBON_RIBOS_POLICY_STAGE_VALIDATE);
    CHECK(test.transaction.stage == RIBON_BOOT_STAGE_PREPARE_PROTOCOL);
}

int
main(int argc, char **argv)
{
    struct TestArtifact artifact;

    if (argc != 2 || !load_artifact(argv[1], &artifact)) {
        fprintf(stderr, "usage: %s aggregate-ownership.rba\n", argv[0]);
        return 2;
    }
    test_success(&artifact);
    test_unsigned_and_corrupt_fallback(&artifact);
    test_schema_budget_and_deadline_fallback(&artifact);
    test_action_commit_and_factory_fallback(&artifact);
    free(artifact.bytes);
    if (failures != 0) {
        fprintf(stderr, "ribos Ribon integration failures=%d\n", failures);
        return 1;
    }
    puts("RIBOS-RIBON-INTEGRATION-OK adapter=generic binding=generated "
         "commit=single fallback=factory-only evidence=host-object");
    return 0;
}
