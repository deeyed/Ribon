#include "product.h"

#include <Ribon/arch/ops.h>
#include <Ribon/boot/plan.h>
#include <Ribon/core/capability.h>
#include <Ribon/core/context.h>
#include <Ribon/plugin/registry.h>
#include <Ribon/port/port.h>
#include <Ribon/security/protected_state.h>

#include <ribos/artifact/format.h>
#include <ribos/schema/schema.h>
#include <ribos/vm/runtime.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define VALIDATION_IMAGE_CAPACITY 512u
#define VALIDATION_HANDOFF_CAPACITY 256u
#define VALIDATION_CONTEXT_CAPACITY 512u
#define VALIDATION_ARTIFACT_CAPACITY (64u * 1024u)
#define VALIDATION_ARENA_CAPACITY (512u * 1024u)

extern const unsigned char ribon_ribos_validation_artifact[];
extern const uint64_t ribon_ribos_validation_artifact_size;
extern const unsigned char ribon_ribos_validation_trial_artifact[];
extern const uint64_t ribon_ribos_validation_trial_artifact_size;

struct ValidationTransaction {
    struct RibonArena arena;
    struct RibonCoreContext core;
    struct RibonBootTransaction transaction;
    uint8_t source[VALIDATION_IMAGE_CAPACITY];
    uint8_t payload[VALIDATION_IMAGE_CAPACITY];
    uint8_t handoff_bytes[VALIDATION_HANDOFF_CAPACITY];
    struct RibonLoadSegment segments[4];
    struct RibonLoadedPayload layout;
    struct RibonMemoryRegion normalized_regions[16];
    struct RibonMutableMemoryMap normalized;
    struct RibonHandoffArtifact handoff;
};

struct ValidationArtifact {
    const uint8_t *bytes;
    size_t size;
    uint32_t context_type;
    uint32_t context_size;
    uint32_t slot_type;
    uint32_t image_type;
    uint32_t verified_type;
    uint32_t action_type;
    uint32_t action_size;
    size_t key_id_offset;
    size_t key_id_size;
    size_t signature_offset;
    size_t signature_size;
};

static _Alignas(16) uint8_t arena_bytes[VALIDATION_ARENA_CAPACITY];
static uint8_t altered_artifact[VALIDATION_ARTIFACT_CAPACITY];
static struct ValidationTransaction validation_transaction;
static RibosProductSchema alternate_schema;
static const struct RibonDiagnosticSinkServiceOperations *diagnostic;

static void
marker(const char *text)
{
    uint64_t size = 0u;

    if (diagnostic == NULL || text == NULL) {
        return;
    }
    while (text[size] != '\0') {
        ++size;
    }
    (void)diagnostic->write(diagnostic->context, text, size);
    (void)diagnostic->write(diagnostic->context, "\r\n", 2u);
}

static _Noreturn void
validation_halt(void)
{
    const struct RibonArchOps *arch = ribon_arch_selected_ops();

    if (arch != NULL && arch->halt != NULL) {
        arch->halt();
    }
    for (;;) {
    }
}

static _Noreturn void
validation_fail(const char *stage)
{
    marker("RIBOS-R18-QEMU-FAIL");
    marker(stage);
    validation_halt();
}

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

    for (index = 0u; index < 4u; ++index) {
        bytes[offset + index] = (uint8_t)(value >> (index * 8u));
    }
}

static void
write_u64(uint8_t *bytes, uint32_t offset, uint64_t value)
{
    uint32_t index;

    for (index = 0u; index < 8u; ++index) {
        bytes[offset + index] = (uint8_t)(value >> (index * 8u));
    }
}

static void
build_elf(
    uint8_t bytes[VALIDATION_IMAGE_CAPACITY],
    const struct RibonArchDescriptor *arch)
{
    uint16_t machine = 62u;

    if (arch->id == RIBON_ARCHITECTURE_AARCH64) {
        machine = 183u;
    } else if (arch->id == RIBON_ARCHITECTURE_RISCV64) {
        machine = 243u;
    }
    memset(bytes, 0, VALIDATION_IMAGE_CAPACITY);
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
section_row(const RibosArtifactSectionView *section, uint32_t index)
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
        ribos_artifact_find_section(view, RIBOS_ARTIFACT_SECTION_TYPES);
    const size_t name_size = strlen(name);
    uint32_t index;

    if (types == NULL || type_id == NULL || byte_size == NULL) {
        return 0;
    }
    for (index = 0u; index < types->count; ++index) {
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
        ribos_artifact_find_section(view, RIBOS_ARTIFACT_SECTION_SLOTS);
    const uint8_t *function = section_row(functions, view->entry_function);
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
inspect_artifact(
    struct ValidationArtifact *artifact,
    const uint8_t *bytes,
    uint64_t size)
{
    RibosArtifactView view;
    uint32_t unused_size;

    if (artifact == NULL || bytes == NULL || size == 0u ||
        size > VALIDATION_ARTIFACT_CAPACITY || size > SIZE_MAX) {
        return 0;
    }
    *artifact = (struct ValidationArtifact){
        .bytes = bytes,
        .size = (size_t)size,
    };
    if (ribos_artifact_open_v1(
            artifact->bytes,
            artifact->size,
            &view) != RIBOS_ARTIFACT_OK ||
        (view.envelope_flags & RIBOS_ARTIFACT_ENVELOPE_SIGNED) == 0u ||
        view.key_id < artifact->bytes || view.signature < artifact->bytes ||
        view.key_id_length > artifact->size ||
        view.signature_length > artifact->size ||
        (size_t)(view.key_id - artifact->bytes) >
            artifact->size - view.key_id_length ||
        (size_t)(view.signature - artifact->bytes) >
            artifact->size - view.signature_length ||
        !entry_context(
            &view,
            &artifact->context_type,
            &artifact->context_size) ||
        !find_named_type(
            &view,
            "Slot",
            &artifact->slot_type,
            &unused_size) ||
        !find_named_type(
            &view,
            "Image",
            &artifact->image_type,
            &unused_size) ||
        !find_named_type(
            &view,
            "VerifiedImage",
            &artifact->verified_type,
            &unused_size) ||
        !find_named_type(
            &view,
            "BootAction",
            &artifact->action_type,
            &artifact->action_size)) {
        return 0;
    }
    artifact->key_id_offset = (size_t)(view.key_id - artifact->bytes);
    artifact->key_id_size = view.key_id_length;
    artifact->signature_offset =
        (size_t)(view.signature - artifact->bytes);
    artifact->signature_size = view.signature_length;
    return 1;
}

static int
initialize_transaction(
    struct ValidationTransaction *test,
    int preserve_protected_state)
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
        .size = VALIDATION_IMAGE_CAPACITY,
    };

    memset(test, 0, sizeof(*test));
    build_elf(test->source, ribon_arch_selected_ops()->descriptor);
    if (ribon_validation_source_bind(
            test->source,
            sizeof(test->source)) != RIBON_SERVICE_STATUS_OK) {
        return 0;
    }
    if (preserve_protected_state != 0) {
        ribon_validation_boot_cycle_reset();
    } else {
        ribon_validation_lifecycle_reset();
    }
    ribon_arena_init(&test->arena, arena_bytes, sizeof(arena_bytes));
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
        ribon_validation_environment_collect(
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
                   .source_size = VALIDATION_IMAGE_CAPACITY,
                   .payload_buffer = test->payload,
                   .payload_buffer_capacity = sizeof(test->payload),
                   .source_name = "r18-validation.elf",
                   .kernel_layout = &test->layout,
                   .handoff_buffer = test->handoff_bytes,
                   .handoff_buffer_capacity = sizeof(test->handoff_bytes),
                   .handoff_artifact = &test->handoff,
               }) == RIBON_BOOT_STATUS_OK;
}

static void
initialize_fixture(
    struct RibonValidationRibosFixture *fixture,
    const struct ValidationArtifact *artifact,
    const struct RibonRibosProductBinding *binding)
{
    *fixture = (struct RibonValidationRibosFixture){
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
    struct ValidationTransaction *transaction,
    struct RibonValidationRibosFixture *fixture,
    const struct ValidationArtifact *artifact,
    const struct RibonRibosProductBinding *binding,
    const uint8_t *artifact_bytes,
    size_t artifact_size,
    enum RibonRibosPolicyActivation activation,
    uint32_t trial_attempts,
    uint64_t manifest_sequence,
    struct RibonRibosPolicyReceipt *receipt)
{
    uint8_t context[VALIDATION_CONTEXT_CAPACITY] = {0};

    if (artifact->context_size > sizeof(context)) {
        return RIBON_RIBOS_POLICY_STATUS_BAD_ARGUMENT;
    }
    fixture->binding = binding;
    return ribon_ribos_policy_execute(
        &(const struct RibonRibosPolicyRequest){
            .core = &transaction->core,
            .binding = binding,
            .artifact = artifact_bytes,
            .artifact_size = artifact_size,
            .context_type_id = artifact->context_type,
            .context_bytes = context,
            .context_size = artifact->context_size,
            .context_generation = 18u,
            .activation = activation,
            .trial_attempts = trial_attempts,
            .manifest_sequence = manifest_sequence,
            .product_context = fixture,
            .transaction = &transaction->transaction,
        },
        receipt);
}

static const RibosProductSchema *
alternate_schema_provider(void)
{
    return &alternate_schema;
}

static int
network_is_absent(void)
{
    const struct RibonProductDescriptor *product =
        ribon_generated_product_descriptor();
    const struct RibonServiceDirectory *services =
        ribon_generated_service_directory();
    const struct RibonRibosProductBinding *binding =
        ribon_generated_ribos_policy_binding();
    uint32_t index;

    if ((product->allowed_capabilities & RIBON_CAP_NETWORK_TRANSPORT) != 0u ||
        (binding->granted_ribos_capabilities &
         RIBOS_CAPABILITY_NETWORK) != 0u) {
        return 0;
    }
    for (index = 0u; index < services->service_count; ++index) {
        if (services->services[index]->kind ==
            RIBON_SERVICE_KIND_NETWORK_TRANSPORT) {
            return 0;
        }
    }
    return 1;
}

static void
expect_authorization_failure(
    const struct ValidationArtifact *artifact,
    const struct RibonRibosProductBinding *binding,
    const uint8_t *artifact_bytes,
    size_t artifact_size,
    uint64_t manifest_sequence,
    enum RibonRibosAuthorizationFailure expected_failure,
    int corrupt_state,
    const char *failure_stage,
    const char *success_marker)
{
    struct RibonValidationRibosFixture fixture;
    struct RibonRibosPolicyReceipt receipt;

    if (!initialize_transaction(&validation_transaction, 0)) {
        validation_fail(failure_stage);
    }
    if (corrupt_state != 0) {
        ribon_validation_protected_state_corrupt();
    }
    initialize_fixture(&fixture, artifact, binding);
    if (execute_policy(
            &validation_transaction,
            &fixture,
            artifact,
            binding,
            artifact_bytes,
            artifact_size,
            RIBON_RIBOS_POLICY_ACTIVATION_EXISTING,
            0u,
            manifest_sequence,
            &receipt) != RIBON_RIBOS_POLICY_STATUS_AUTHORIZATION ||
        receipt.authorization_failure != expected_failure ||
        receipt.action_consumed != 0u || receipt.recovery_notified != 1u ||
        fixture.helper_calls != 0u || fixture.fallback_calls != 1u ||
        validation_transaction.transaction.stage !=
            RIBON_BOOT_STAGE_PREPARE_PROTOCOL) {
        validation_fail(failure_stage);
    }
    marker(success_marker);
}

static void
run_validation(
    const struct ValidationArtifact *artifact,
    const struct ValidationArtifact *trial_artifact)
{
    struct RibonValidationRibosFixture fixture;
    struct RibonRibosPolicyReceipt receipt;
    struct RibonRibosPolicyStateReceipt state_receipt;
    const struct RibonRibosProductBinding *generated =
        ribon_generated_ribos_policy_binding();
    struct RibonRibosProductBinding binding;
    struct RibonRibosSignedPolicyBinding signed_binding;
    RibosVmLimits limits;

    if (!initialize_transaction(&validation_transaction, 0)) {
        validation_fail("transaction-success");
    }
    marker("RIBOS-R18-TRANSACTION-PREPARED");
    initialize_fixture(&fixture, artifact, generated);
    ribon_validation_timer_step_set(1u);
    marker("RIBOS-R18-POLICY-EXECUTE");
    if (execute_policy(
            &validation_transaction,
            &fixture,
            artifact,
            generated,
            artifact->bytes,
            artifact->size,
            RIBON_RIBOS_POLICY_ACTIVATION_EXISTING,
            0u,
            18u,
            &receipt) != RIBON_RIBOS_POLICY_STATUS_OK ||
        receipt.stage != RIBON_RIBOS_POLICY_STAGE_COMPLETE ||
        receipt.action_consumed != 1u ||
        receipt.recovery_notified != 0u ||
        receipt.terminal_helper_id != 21u ||
        receipt.transaction_stage !=
            RIBON_BOOT_STAGE_QUIESCE_ENVIRONMENT ||
        fixture.helper_calls != 4u || fixture.fallback_calls != 0u ||
        ribon_validation_write_count() != 1u ||
        ribon_validation_flush_count() != 1u ||
        ribon_validation_quiesce_count() != 1u ||
        ribon_validation_watchdog_count() != 1u) {
        validation_fail("signed-core-commit");
    }
    marker("RIBOS-R18-SIGNED-AUTH-OK");
    marker(
        "RIBOS-R18-CORE-COMMIT-OK "
        "receipt=v1-stage8-action21-helpers4-fallback0");

    memcpy(altered_artifact, artifact->bytes, artifact->size);
    altered_artifact[
        artifact->signature_offset + artifact->signature_size - 1u] ^= 0x01u;
    expect_authorization_failure(
        artifact, generated, altered_artifact, artifact->size, 18u,
        RIBON_RIBOS_AUTHORIZATION_FAILURE_SIGNATURE, 0,
        "signature-fallback", "RIBOS-R18-SIGNATURE-FALLBACK-OK");

    memcpy(altered_artifact, artifact->bytes, artifact->size);
    altered_artifact[RIBOS_ARTIFACT_ENVELOPE_BYTES] ^= 0x80u;
    expect_authorization_failure(
        artifact, generated, altered_artifact, artifact->size, 18u,
        RIBON_RIBOS_AUTHORIZATION_FAILURE_MALFORMED, 0,
        "corrupt-fallback", "RIBOS-R18-CORRUPT-FALLBACK-OK");

    expect_authorization_failure(
        artifact, generated, artifact->bytes, artifact->size - 1u, 18u,
        RIBON_RIBOS_AUTHORIZATION_FAILURE_MALFORMED, 0,
        "truncation-fallback", "RIBOS-R18-TRUNCATION-FALLBACK-OK");

    binding = *generated;
    signed_binding = *generated->signed_policy;
    signed_binding.product_digest[0] ^= 0x80u;
    binding.signed_policy = &signed_binding;
    expect_authorization_failure(
        artifact, &binding, artifact->bytes, artifact->size, 18u,
        RIBON_RIBOS_AUTHORIZATION_FAILURE_KEY_POLICY, 0,
        "product-fallback", "RIBOS-R18-PRODUCT-FALLBACK-OK");

    alternate_schema = *ribos_schema_reference_v1();
    alternate_schema.product_id = "ribon.schema-mismatch.r18";
    binding = *generated;
    binding.schema = alternate_schema_provider;
    expect_authorization_failure(
        artifact, &binding, artifact->bytes, artifact->size, 18u,
        RIBON_RIBOS_AUTHORIZATION_FAILURE_IDENTITY, 0,
        "schema-fallback", "RIBOS-R18-SCHEMA-FALLBACK-OK");

    memcpy(altered_artifact, artifact->bytes, artifact->size);
    altered_artifact[artifact->key_id_offset] ^= 0x20u;
    expect_authorization_failure(
        artifact, generated, altered_artifact, artifact->size, 18u,
        RIBON_RIBOS_AUTHORIZATION_FAILURE_KEY_POLICY, 0,
        "key-fallback", "RIBOS-R18-KEY-FALLBACK-OK");

    expect_authorization_failure(
        artifact, generated, artifact->bytes, artifact->size, 17u,
        RIBON_RIBOS_AUTHORIZATION_FAILURE_KEY_POLICY, 0,
        "sequence-fallback", "RIBOS-R18-SEQUENCE-FALLBACK-OK");

    expect_authorization_failure(
        artifact, generated, artifact->bytes, artifact->size, 18u,
        RIBON_RIBOS_AUTHORIZATION_FAILURE_STATE_INVALID, 1,
        "state-fallback", "RIBOS-R18-STATE-FALLBACK-OK");

    binding = *generated;
    limits = *generated->limits;
    limits.maximum_instructions = 1u;
    binding.limits = &limits;
    if (!initialize_transaction(&validation_transaction, 0)) {
        validation_fail("transaction-budget");
    }
    initialize_fixture(&fixture, artifact, &binding);
    if (execute_policy(
            &validation_transaction,
            &fixture,
            artifact,
            &binding,
            artifact->bytes,
            artifact->size,
            RIBON_RIBOS_POLICY_ACTIVATION_EXISTING,
            0u,
            18u,
            &receipt) != RIBON_RIBOS_POLICY_STATUS_VERIFICATION ||
        fixture.fallback_calls != 1u || fixture.helper_calls != 0u) {
        validation_fail("budget-fallback");
    }
    marker("RIBOS-R18-BUDGET-FALLBACK-OK");

    if (!initialize_transaction(&validation_transaction, 0)) {
        validation_fail("transaction-deadline");
    }
    initialize_fixture(&fixture, artifact, generated);
    ribon_validation_timer_step_set(20000u);
    if (execute_policy(
            &validation_transaction,
            &fixture,
            artifact,
            generated,
            artifact->bytes,
            artifact->size,
            RIBON_RIBOS_POLICY_ACTIVATION_EXISTING,
            0u,
            18u,
            &receipt) != RIBON_RIBOS_POLICY_STATUS_VM_FAULT ||
        receipt.outcome_kind != RIBOS_VM_OUTCOME_VM_FAULT ||
        receipt.recovery_notified != 1u ||
        fixture.fallback_calls != 1u ||
        validation_transaction.transaction.stage !=
            RIBON_BOOT_STAGE_PREPARE_PROTOCOL) {
        validation_fail("deadline-fallback");
    }
    marker("RIBOS-R18-DEADLINE-FALLBACK-OK");

    if (!initialize_transaction(&validation_transaction, 0)) {
        validation_fail("transaction-trial-confirm");
    }
    initialize_fixture(&fixture, trial_artifact, generated);
    ribon_validation_timer_step_set(1u);
    if (execute_policy(
            &validation_transaction,
            &fixture,
            trial_artifact,
            generated,
            trial_artifact->bytes,
            trial_artifact->size,
            RIBON_RIBOS_POLICY_ACTIVATION_START_TRIAL,
            2u,
            19u,
            &receipt) != RIBON_RIBOS_POLICY_STATUS_OK ||
        receipt.rollback_authority !=
            RIBON_PROTECTED_STATE_AUTHORITY_TRIAL ||
        receipt.rollback_floor != 18u ||
        receipt.trial_attempts_remaining != 1u ||
        ribon_ribos_policy_confirm(
            generated,
            19u,
            &state_receipt) != RIBON_RIBOS_POLICY_STATUS_OK ||
        state_receipt.confirmed_floor != 19u ||
        state_receipt.pending_sequence != 0u) {
        validation_fail("trial-confirm");
    }
    marker("RIBOS-R18-TRIAL-CONFIRM-OK");

    if (!initialize_transaction(&validation_transaction, 0)) {
        validation_fail("transaction-trial-failure");
    }
    initialize_fixture(&fixture, trial_artifact, generated);
    fixture.reject_action = 1u;
    ribon_validation_timer_step_set(1u);
    if (execute_policy(
            &validation_transaction,
            &fixture,
            trial_artifact,
            generated,
            trial_artifact->bytes,
            trial_artifact->size,
            RIBON_RIBOS_POLICY_ACTIVATION_START_TRIAL,
            2u,
            19u,
            &receipt) != RIBON_RIBOS_POLICY_STATUS_ACTION_REJECTED ||
        receipt.rollback_authority !=
            RIBON_PROTECTED_STATE_AUTHORITY_TRIAL ||
        receipt.trial_attempts_remaining != 1u ||
        fixture.fallback_calls != 1u ||
        ribon_ribos_policy_fail_trial(
            generated,
            &state_receipt) != RIBON_RIBOS_POLICY_STATUS_OK ||
        state_receipt.confirmed_floor != 18u ||
        state_receipt.pending_sequence != 0u ||
        !initialize_transaction(&validation_transaction, 1)) {
        validation_fail("trial-failure");
    }
    initialize_fixture(&fixture, artifact, generated);
    ribon_validation_timer_step_set(1u);
    if (execute_policy(
            &validation_transaction,
            &fixture,
            artifact,
            generated,
            artifact->bytes,
            artifact->size,
            RIBON_RIBOS_POLICY_ACTIVATION_EXISTING,
            0u,
            18u,
            &receipt) != RIBON_RIBOS_POLICY_STATUS_OK ||
        receipt.rollback_authority !=
            RIBON_PROTECTED_STATE_AUTHORITY_CONFIRMED ||
        receipt.rollback_floor != 18u || fixture.fallback_calls != 0u) {
        validation_fail("trial-confirmed-fallback");
    }
    marker("RIBOS-R18-TRIAL-ROLLBACK-OK");

    if (!network_is_absent()) {
        validation_fail("normal-network-authority");
    }
    marker("RIBOS-R18-NETWORK-ABSENT-OK");
}

_Noreturn void
ribon_raw_fdt_boot_main(uint64_t native0, uint64_t native1)
{
    const struct RibonPortDescriptor *port = ribon_port_selected();
    struct ValidationArtifact artifact;
    struct ValidationArtifact trial_artifact;

    (void)native0;
    (void)native1;
    if (!ribon_port_descriptor_is_valid(port) ||
        port->diagnostic_sink == NULL) {
        validation_halt();
    }
    diagnostic = port->diagnostic_sink->operations;
    if (diagnostic == NULL ||
        diagnostic->initialize(diagnostic->context) !=
            RIBON_SERVICE_STATUS_OK) {
        validation_halt();
    }
    marker("RIBOS-R18-QEMU-ENTRY");
    if (!inspect_artifact(
            &artifact,
            ribon_ribos_validation_artifact,
            ribon_ribos_validation_artifact_size) ||
        !inspect_artifact(
            &trial_artifact,
            ribon_ribos_validation_trial_artifact,
            ribon_ribos_validation_trial_artifact_size)) {
        validation_fail("artifact-open");
    }
    marker("RIBOS-R18-ARTIFACT-OPEN-OK");
    run_validation(&artifact, &trial_artifact);
    marker("RIBOS-R18-QEMU-VALIDATION-OK");
    validation_halt();
}
