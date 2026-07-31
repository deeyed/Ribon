#include <Ribon/policy/ribos.h>
#include <Ribon/security/key_policy.h>
#include <Ribon/security/protected_state.h>
#include <Ribon/security/signature.h>

#include <ribos/artifact/format.h>
#include <ribos/schema/schema.h>
#include <ribos/vm/handles.h>
#include <ribos/vm/helpers.h>
#include <ribos/vm/prepared.h>
#include <ribos/vm/storage.h>
#include <ribos/vm/terminal.h>
#include <ribos/vm/verifier.h>

#include <limits.h>
#include <string.h>

#define RIBON_RIBOS_NANOSECONDS_PER_SECOND UINT64_C(1000000000)
#define RIBON_RIBOS_NANOSECONDS_PER_MILLISECOND UINT64_C(1000000)
#define RIBON_RIBOS_PLUGIN_ARENA_BUDGET (UINT64_C(128) * UINT64_C(1024))

struct RibonRibosAdapterContext {
    const struct RibonRibosPolicyRequest *request;
    const struct RibonRibosProductBinding *binding;
    const struct RibonPluginDescriptor *plugin;
    const struct RibonServiceDescriptor *timer;
    const struct RibonServiceDescriptor *watchdog;
    uint8_t schema_digest[RIBOS_SCHEMA_DIGEST_BYTES];
    enum RibonRibosPolicyStage stage;
    int status;
    uint32_t vm_status;
    uint32_t vm_fault_code;
    uint32_t outcome_kind;
    uint32_t terminal_helper_id;
    uint32_t action_consumed;
    uint32_t recovery_notified;
    enum RibonRibosAuthorizationFailure authorization_failure;
    uint32_t rollback_authority;
    uint64_t manifest_sequence;
    uint64_t rollback_floor;
    uint64_t rollback_generation;
    uint32_t trial_attempts_remaining;
    uint64_t arena_start;
};

static int ribon_ribos_arena_allocate(
    struct RibonRibosAdapterContext *adapter,
    size_t size,
    size_t alignment,
    void **memory);

static int
ribon_ribos_streq(const char *left, const char *right)
{
    if (left == NULL || right == NULL) {
        return 0;
    }
    while (*left != '\0' && *right != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static int
ribon_ribos_digest_equal(
    const uint8_t left[RIBOS_SCHEMA_DIGEST_BYTES],
    const uint8_t right[RIBOS_SCHEMA_DIGEST_BYTES])
{
    uint8_t difference = 0;
    uint32_t index;

    if (left == NULL || right == NULL) {
        return 0;
    }
    for (index = 0; index < RIBOS_SCHEMA_DIGEST_BYTES; ++index) {
        difference |= (uint8_t)(left[index] ^ right[index]);
    }
    return difference == 0;
}

static int
ribon_ribos_digest_is_nonzero(const uint8_t digest[RIBOS_SCHEMA_DIGEST_BYTES])
{
    uint8_t value = 0u;
    uint32_t index;

    if (digest == NULL) {
        return 0;
    }
    for (index = 0u; index < RIBOS_SCHEMA_DIGEST_BYTES; ++index) {
        value |= digest[index];
    }
    return value != 0u;
}

/** @brief Native Ribon mode를 stable signed-policy registry로 명시 변환한다. */
static uint32_t
ribon_ribos_trust_mode(enum RibonMode mode)
{
    switch (mode) {
    case RIBON_MODE_NORMAL:
        return RIBON_KEY_POLICY_MODE_NORMAL;
    case RIBON_MODE_RECOVERY:
        return RIBON_KEY_POLICY_MODE_RECOVERY;
    case RIBON_MODE_PROVISIONING:
        return RIBON_KEY_POLICY_MODE_PROVISIONING;
    case RIBON_MODE_DIAGNOSTIC:
        return RIBON_KEY_POLICY_MODE_DIAGNOSTIC;
    default:
        return RIBON_KEY_POLICY_MODE_INVALID;
    }
}

/**
 * Signed binding의 immutable product authority와 stable registry를 재검산한다.
 * 이 검사는 storage state를 읽거나 artifact byte를 승인하지 않는다.
 */
static int
ribon_ribos_signed_binding_is_valid(
    const struct RibonRibosSignedPolicyBinding *binding)
{
    if (binding == NULL || binding->size != sizeof(*binding) ||
        binding->abi_version != RIBON_RIBOS_POLICY_ABI_VERSION ||
        binding->trust_mode < RIBON_KEY_POLICY_MODE_NORMAL ||
        binding->trust_mode > RIBON_KEY_POLICY_MODE_DIAGNOSTIC ||
        binding->key_usage < RIBON_KEY_POLICY_USAGE_POLICY_NORMAL ||
        binding->key_usage > RIBON_KEY_POLICY_USAGE_POLICY_DIAGNOSTIC ||
        binding->trust_mode != binding->key_usage ||
        !ribon_ribos_digest_is_nonzero(binding->product_digest) ||
        !ribon_ribos_digest_is_nonzero(binding->rollback_domain_digest) ||
        !ribon_ribos_digest_is_nonzero(binding->policy_identity_digest) ||
        binding->signature_provider == NULL ||
        binding->signature_provider->provider_class !=
            RIBON_SIGNATURE_PROVIDER_CLASS_PRODUCTION ||
        !ribon_signature_provider_is_valid(binding->signature_provider) ||
        binding->key_policy == NULL ||
        ribon_key_policy_store_validate(binding->key_policy) !=
            RIBON_KEY_POLICY_STATUS_OK ||
        binding->protected_state == NULL ||
        ribon_protected_state_binding_validate(binding->protected_state) !=
            RIBON_PROTECTED_STATE_STATUS_OK) {
        return 0;
    }
    return 1;
}

static const struct RibonPluginDescriptor *
ribon_ribos_selected_plugin(const struct RibonCoreContext *core)
{
    const struct RibonPluginDescriptor *plugin;

    plugin = ribon_plugin_registry_find_selected(
        core->registry,
        core->product,
        RIBON_PLUGIN_KIND_POLICY);
    if (plugin != &ribon_ribos_policy_plugin_descriptor ||
        !ribon_ribos_streq(plugin->id, "policy.ribos")) {
        return NULL;
    }
    return plugin;
}

static void
ribon_ribos_fill_failure(
    const struct RibonRibosAdapterContext *adapter,
    struct RibonRibosFailureReceipt *failure)
{
    const struct RibonBootTransaction *transaction = adapter->request->transaction;
    const struct RibonCoreContext *core = adapter->request->core;
    const struct RibonArena *arena = core != NULL ? core->arena : NULL;

    *failure = (struct RibonRibosFailureReceipt){
        .size = sizeof(*failure),
        .abi_version = RIBON_RIBOS_POLICY_ABI_VERSION,
        .stage = adapter->stage,
        .status = adapter->status,
        .vm_status = adapter->vm_status,
        .vm_fault_code = adapter->vm_fault_code,
        .outcome_kind = adapter->outcome_kind,
        .terminal_helper_id = adapter->terminal_helper_id,
        .action_consumed = adapter->action_consumed,
        .recovery_notified = 1u,
        .authorization_failure = adapter->authorization_failure,
        .rollback_authority = adapter->rollback_authority,
        .transaction_stage =
            transaction != NULL ? transaction->stage : RIBON_BOOT_STAGE_FAILED,
        .arena_bytes =
            arena != NULL && arena->used >= adapter->arena_start ?
                arena->used - adapter->arena_start : 0u,
        .context_generation = adapter->request->context_generation,
        .manifest_sequence = adapter->manifest_sequence,
        .rollback_floor = adapter->rollback_floor,
        .rollback_generation = adapter->rollback_generation,
        .trial_attempts_remaining = adapter->trial_attempts_remaining,
    };
}

static void
ribon_ribos_notify_recovery(struct RibonRibosAdapterContext *adapter)
{
    struct RibonRibosFailureReceipt failure;

    if (adapter == NULL || adapter->recovery_notified != 0u ||
        adapter->binding == NULL ||
        adapter->binding->factory_recovery == NULL) {
        return;
    }
    adapter->recovery_notified = 1u;
    ribon_ribos_fill_failure(adapter, &failure);
    adapter->binding->factory_recovery(
        adapter->request->product_context,
        &failure);
}

static int
ribon_ribos_fail(
    struct RibonRibosAdapterContext *adapter,
    enum RibonRibosPolicyStage stage,
    int status,
    uint32_t vm_status)
{
    adapter->stage = stage;
    adapter->status = status;
    adapter->vm_status = vm_status;
    ribon_ribos_notify_recovery(adapter);
    return status;
}

static const struct RibonRibosHelperRoute *
ribon_ribos_route(
    const struct RibonRibosProductBinding *binding,
    uint32_t stable_id)
{
    uint32_t low = 0;
    uint32_t high = binding->route_count;

    while (low < high) {
        const uint32_t middle = low + (high - low) / 2u;
        const uint32_t candidate = binding->routes[middle].stable_id;

        if (candidate == stable_id) {
            return &binding->routes[middle];
        }
        if (candidate < stable_id) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    return NULL;
}

static const RibosVmHelperExecutionDescriptor *
ribon_ribos_execution(
    const RibosVmHelperContract *contract,
    uint32_t stable_id)
{
    uint32_t low = 0;
    uint32_t high = contract->binding_count;

    while (low < high) {
        const uint32_t middle = low + (high - low) / 2u;
        const uint32_t candidate =
            contract->bindings[middle].execution.stable_id;

        if (candidate == stable_id) {
            return &contract->bindings[middle].execution;
        }
        if (candidate < stable_id) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    return NULL;
}

static int
ribon_ribos_service_route_is_valid(
    const struct RibonRibosAdapterContext *adapter,
    const struct RibonRibosHelperRoute *route,
    const RibosVmHelperExecutionDescriptor *execution)
{
    const struct RibonServiceDescriptor *service;
    uint64_t maximum_duration_ns;

    if (route->service_kind == RIBON_RIBOS_NO_SERVICE_KIND) {
        return route->service_id == NULL &&
               route->required_ribon_capabilities == 0u;
    }
    if (route->service_kind > RIBON_SERVICE_KIND_BOOT_MODULE_BUNDLE ||
        route->service_kind == RIBON_SERVICE_KIND_BOOT_MODULE_BUNDLE ||
        route->service_id == NULL ||
        route->required_ribon_capabilities == 0u) {
        return 0;
    }
    service = ribon_service_directory_find_exact(
        adapter->request->core->services,
        (enum RibonServiceKind)route->service_kind,
        route->service_id);
    if (service == NULL ||
        (service->provides & route->required_ribon_capabilities) !=
            route->required_ribon_capabilities ||
        (route->required_ribon_capabilities &
         ~adapter->request->core->product->allowed_capabilities) != 0u ||
        (uint32_t)service->phase > adapter->binding->selected_phase ||
        execution->maximum_input_bytes > service->input_budget ||
        execution->maximum_output_bytes > service->output_budget ||
        service->deadline_ms >
            UINT64_MAX / RIBON_RIBOS_NANOSECONDS_PER_MILLISECOND) {
        return 0;
    }
    maximum_duration_ns =
        service->deadline_ms * RIBON_RIBOS_NANOSECONDS_PER_MILLISECOND;
    return execution->maximum_duration_ns <= maximum_duration_ns;
}

static int
ribon_ribos_binding_validate(
    struct RibonRibosAdapterContext *adapter)
{
    const struct RibonRibosPolicyRequest *request = adapter->request;
    const struct RibonRibosProductBinding *binding = request->binding;
    const struct RibosProductSchema *schema;
    uint8_t helper_digest[RIBOS_SCHEMA_DIGEST_BYTES];
    uint32_t index;

    if (binding == NULL ||
        binding->size != sizeof(*binding) ||
        binding->abi_version != RIBON_RIBOS_POLICY_ABI_VERSION ||
        binding->product_id == NULL ||
        !ribon_ribos_streq(binding->product_id, request->core->product->id) ||
        binding->policy_id == NULL ||
        binding->schema == NULL ||
        binding->helper_contract == NULL ||
        binding->routes == NULL ||
        binding->route_count == 0u ||
        binding->route_count != binding->helper_contract->binding_count ||
        binding->selected_phase != RIBON_PLUGIN_PHASE_BOOT ||
        (binding->mode_mask &
         RIBON_MODE_MASK(request->core->mode->mode)) == 0u ||
        binding->granted_ribos_capabilities == 0u ||
        binding->arena_budget == 0u ||
        binding->arena_budget > adapter->plugin->arena_budget ||
        binding->limits == NULL ||
        binding->limits->maximum_arena_bytes > binding->arena_budget ||
        binding->timer_service_id == NULL ||
        binding->factory_recovery == NULL ||
        binding->validate_boot_action == NULL ||
        (binding->required_ribon_capabilities &
         ~request->core->product->allowed_capabilities) != 0u ||
        ribos_vm_limits_validate_v1(binding->limits) !=
            RIBOS_VM_STATUS_OK ||
        ribos_vm_helper_contract_validate_v1(binding->helper_contract) !=
            RIBOS_VM_STATUS_OK ||
        ribos_vm_helper_contract_compute_identity_v1(
            binding->helper_contract,
            helper_digest) != RIBOS_VM_STATUS_OK ||
        !ribon_ribos_digest_equal(
            helper_digest,
            binding->helper_contract->digest)) {
        return 0;
    }
    if (binding->authorization_class ==
            RIBON_RIBOS_AUTHORIZATION_SIGNED_POLICY) {
        if (binding->fixture_authorize != NULL ||
            !ribon_ribos_signed_binding_is_valid(binding->signed_policy) ||
            binding->signed_policy->trust_mode !=
                ribon_ribos_trust_mode(request->core->mode->mode)) {
            return 0;
        }
    } else if (binding->authorization_class ==
                   RIBON_RIBOS_AUTHORIZATION_FIXTURE_CALLBACK) {
        if (binding->signed_policy != NULL ||
            binding->fixture_authorize == NULL) {
            return 0;
        }
    } else {
        return 0;
    }
    if (request->core->mode->mode == RIBON_MODE_NORMAL &&
        (binding->granted_ribos_capabilities &
         (RIBOS_CAPABILITY_NETWORK | RIBOS_CAPABILITY_FLASH)) != 0u) {
        return 0;
    }
    schema = binding->schema();
    if (schema == NULL ||
        ribos_schema_validate(schema) != RIBOS_SCHEMA_OK ||
        ribos_schema_compute_identity(
            schema,
            adapter->schema_digest) != RIBOS_SCHEMA_OK) {
        return 0;
    }
    for (index = 0; index < binding->route_count; ++index) {
        const struct RibonRibosHelperRoute *route = &binding->routes[index];
        const RibosVmHelperExecutionDescriptor *execution =
            ribon_ribos_execution(binding->helper_contract, route->stable_id);

        if (route->stable_id == RIBOS_VM_INVALID_ID ||
            route->invoke == NULL ||
            execution == NULL ||
            binding->helper_contract->bindings[index].invoke !=
                ribon_ribos_policy_helper_dispatch ||
            binding->helper_contract->bindings[index].execution.stable_id !=
                route->stable_id ||
            (index != 0u &&
             binding->routes[index - 1u].stable_id >= route->stable_id) ||
            (execution->required_capabilities &
             ~binding->granted_ribos_capabilities) != 0u ||
            (execution->allowed_mode_mask &
             (UINT64_C(1) << request->core->mode->mode)) == 0u ||
            (execution->allowed_phase_mask &
             (UINT64_C(1) << binding->selected_phase)) == 0u ||
            !ribon_ribos_service_route_is_valid(
                adapter,
                route,
                execution)) {
            return 0;
        }
    }
    adapter->timer = ribon_service_directory_find_exact(
        request->core->services,
        RIBON_SERVICE_KIND_MONOTONIC_TIMER,
        binding->timer_service_id);
    if (adapter->timer == NULL) {
        return 0;
    }
    if (binding->watchdog_required != 0u) {
        if (binding->watchdog_service_id == NULL) {
            return 0;
        }
        adapter->watchdog = ribon_service_directory_find_exact(
            request->core->services,
            RIBON_SERVICE_KIND_WATCHDOG,
            binding->watchdog_service_id);
        if (adapter->watchdog == NULL ||
            !ribon_watchdog_service_operations_are_valid(
                adapter->watchdog)) {
            return 0;
        }
    } else if (binding->watchdog_service_id != NULL) {
        return 0;
    }
    return 1;
}

static uint32_t
ribon_ribos_authorization_reject(
    struct RibonRibosAdapterContext *adapter,
    enum RibonRibosAuthorizationFailure failure)
{
    if (adapter != NULL &&
        adapter->authorization_failure ==
            RIBON_RIBOS_AUTHORIZATION_FAILURE_NONE) {
        adapter->authorization_failure = failure;
    }
    return RIBOS_VM_STATUS_NOT_AUTHORIZED;
}

/**
 * Candidate trial state를 쓰기 전에 copied artifact의 Stage-1/2를 독립 실행한다.
 * Scratch는 Core arena에서 단방향 할당하며 실패 뒤 rewind하지 않는다.
 */
static int
ribon_ribos_candidate_is_verified(
    struct RibonRibosAdapterContext *adapter,
    const RibosArtifactAuthorizationRequest *request)
{
    RibosVerifierReport report;
    const struct RibosProductSchema *schema = adapter->binding->schema();
    void *workspace = NULL;
    size_t workspace_size = 0u;

    if (request->artifact_size > SIZE_MAX ||
        ribos_verifier_workspace_size_v1(
            request->artifact,
            (size_t)request->artifact_size,
            &workspace_size,
            &report) != RIBOS_VERIFIER_OK ||
        !ribon_ribos_arena_allocate(
            adapter,
            workspace_size,
            8u,
            &workspace) ||
        ribos_verify_artifact_stage1_v1(
            request->artifact,
            (size_t)request->artifact_size,
            schema,
            workspace,
            workspace_size,
            &report) != RIBOS_VERIFIER_OK ||
        ribos_verify_artifact_stage2_v1(
            request->artifact,
            (size_t)request->artifact_size,
            schema,
            workspace,
            workspace_size,
            &report) != RIBOS_VERIFIER_OK) {
        return 0;
    }
    return 1;
}

/**
 * Generated trust tuple, real signature/key policy와 protected journal을 결합한다.
 * Pending authority는 verifier preflight 뒤 열고 attempt 감소를 commit한 뒤 승인한다.
 */
static uint32_t
ribon_ribos_authorize_signed(
    struct RibonRibosAdapterContext *adapter,
    const RibosArtifactAuthorizationRequest *request,
    RibosArtifactAuthorizationReceipt *receipt)
{
    const struct RibonRibosSignedPolicyBinding *binding =
        adapter->binding->signed_policy;
    RibosArtifactView view;
    RibosArtifactTrustContextV1 trust = {
        .size = sizeof(trust),
        .trust_major = RIBOS_ARTIFACT_TRUST_MESSAGE_V1_MAJOR,
        .trust_minor = RIBOS_ARTIFACT_TRUST_MESSAGE_V1_MINOR,
    };
    struct RibonKeyPolicyRequest key_request = {
        .size = sizeof(key_request),
        .abi_version = RIBON_KEY_POLICY_ABI_VERSION,
    };
    struct RibonKeyPolicySignatureVerification verification = {
        .size = sizeof(verification),
        .abi_version = RIBON_KEY_POLICY_ABI_VERSION,
    };
    struct RibonKeyPolicyDecision key_decision;
    struct RibonProtectedStateJournal journal;
    struct RibonProtectedStateSnapshot snapshot;
    struct RibonProtectedStateDecision state_decision;
    uint8_t message[RIBOS_ARTIFACT_TRUST_MESSAGE_V1_BYTES];
    void *signature_workspace = NULL;
    int key_status;
    int state_status;

    if (!ribon_ribos_signed_binding_is_valid(binding) ||
        request->envelope_flags != RIBOS_ARTIFACT_ENVELOPE_SIGNED ||
        request->signature_algorithm != RIBOS_ARTIFACT_SIGNATURE_ED25519 ||
        request->artifact_size > SIZE_MAX || request->key_id == NULL ||
        request->key_id_size == 0u || request->key_id_size > SIZE_MAX ||
        request->signature == NULL || request->signature_size > SIZE_MAX ||
        ribos_artifact_open_v1(
            request->artifact,
            (size_t)request->artifact_size,
            &view) != RIBOS_ARTIFACT_OK) {
        return ribon_ribos_authorization_reject(
            adapter,
            RIBON_RIBOS_AUTHORIZATION_FAILURE_MALFORMED);
    }
    if (!ribon_ribos_digest_equal(
            request->schema_digest,
            adapter->schema_digest)) {
        return ribon_ribos_authorization_reject(
            adapter,
            RIBON_RIBOS_AUTHORIZATION_FAILURE_IDENTITY);
    }
    if ((adapter->request->activation !=
             RIBON_RIBOS_POLICY_ACTIVATION_EXISTING &&
         adapter->request->activation !=
             RIBON_RIBOS_POLICY_ACTIVATION_START_TRIAL) ||
        (adapter->request->activation ==
             RIBON_RIBOS_POLICY_ACTIVATION_EXISTING &&
         adapter->request->trial_attempts != 0u) ||
        (adapter->request->activation ==
             RIBON_RIBOS_POLICY_ACTIVATION_START_TRIAL &&
         (adapter->request->trial_attempts == 0u ||
          adapter->request->trial_attempts >
              RIBON_PROTECTED_STATE_MAX_TRIAL_ATTEMPTS))) {
        return ribon_ribos_authorization_reject(
            adapter,
            RIBON_RIBOS_AUTHORIZATION_FAILURE_ROLLBACK);
    }

    trust.mode = (uint16_t)binding->trust_mode;
    trust.key_usage = (uint16_t)binding->key_usage;
    trust.sequence = adapter->request->manifest_sequence;
    memcpy(trust.product_digest, binding->product_digest,
           sizeof(trust.product_digest));
    memcpy(trust.rollback_domain_digest, binding->rollback_domain_digest,
           sizeof(trust.rollback_domain_digest));
    if (ribos_artifact_trust_message_v1(
            &view,
            RIBOS_ARTIFACT_SIGNATURE_ED25519,
            request->key_id,
            (size_t)request->key_id_size,
            &trust,
            message) != RIBOS_ARTIFACT_TRUST_OK) {
        return ribon_ribos_authorization_reject(
            adapter,
            RIBON_RIBOS_AUTHORIZATION_FAILURE_MODE_USAGE);
    }

    key_request.mode = (enum RibonKeyPolicyMode)binding->trust_mode;
    key_request.usage = (enum RibonKeyPolicyUsage)binding->key_usage;
    key_request.key_id = request->key_id;
    key_request.key_id_size = (size_t)request->key_id_size;
    key_request.sequence = adapter->request->manifest_sequence;
    memcpy(key_request.product_digest, binding->product_digest,
           sizeof(key_request.product_digest));
    memcpy(key_request.rollback_domain_digest, binding->rollback_domain_digest,
           sizeof(key_request.rollback_domain_digest));
    if (binding->signature_provider->workspace_bytes != 0u &&
        !ribon_ribos_arena_allocate(
            adapter,
            binding->signature_provider->workspace_bytes,
            binding->signature_provider->workspace_alignment,
            &signature_workspace)) {
        return ribon_ribos_authorization_reject(
            adapter,
            RIBON_RIBOS_AUTHORIZATION_FAILURE_SIGNATURE);
    }
    verification.policy = &key_request;
    verification.provider = binding->signature_provider;
    verification.message = message;
    verification.message_size = sizeof(message);
    verification.signature = request->signature;
    verification.signature_size = (size_t)request->signature_size;
    verification.workspace = signature_workspace;
    verification.workspace_size =
        binding->signature_provider->workspace_bytes;
    key_status = ribon_key_policy_verify(
        binding->key_policy,
        &verification,
        &key_decision);
    if (key_status != RIBON_KEY_POLICY_STATUS_OK) {
        return ribon_ribos_authorization_reject(
            adapter,
            key_status == RIBON_KEY_POLICY_STATUS_SIGNATURE_INVALID ||
                    key_status ==
                        RIBON_KEY_POLICY_STATUS_UNSUPPORTED_ALGORITHM ?
                RIBON_RIBOS_AUTHORIZATION_FAILURE_SIGNATURE :
                RIBON_RIBOS_AUTHORIZATION_FAILURE_KEY_POLICY);
    }

    state_status = ribon_protected_state_journal_bind(
        binding->protected_state,
        binding->rollback_domain_digest,
        &journal);
    if (state_status == RIBON_PROTECTED_STATE_STATUS_OK) {
        state_status = ribon_protected_state_open(&journal, &snapshot);
    }
    if (state_status != RIBON_PROTECTED_STATE_STATUS_OK) {
        return ribon_ribos_authorization_reject(
            adapter,
            state_status == RIBON_PROTECTED_STATE_STATUS_UNAVAILABLE ||
                    state_status == RIBON_PROTECTED_STATE_STATUS_IO_ERROR ?
                RIBON_RIBOS_AUTHORIZATION_FAILURE_STATE_UNAVAILABLE :
                RIBON_RIBOS_AUTHORIZATION_FAILURE_STATE_INVALID);
    }
    if (adapter->request->activation ==
        RIBON_RIBOS_POLICY_ACTIVATION_START_TRIAL) {
        if (snapshot.kind != RIBON_PROTECTED_STATE_KIND_CONFIRMED ||
            snapshot.confirmed_floor == UINT64_MAX ||
            adapter->request->manifest_sequence !=
                snapshot.confirmed_floor + 1u) {
            return ribon_ribos_authorization_reject(
                adapter,
                RIBON_RIBOS_AUTHORIZATION_FAILURE_ROLLBACK);
        }
        if (!ribon_ribos_candidate_is_verified(adapter, request)) {
            return ribon_ribos_authorization_reject(
                adapter,
                RIBON_RIBOS_AUTHORIZATION_FAILURE_VERIFIER);
        }
        state_status = ribon_protected_state_begin_trial(
            &journal,
            adapter->request->manifest_sequence,
            adapter->request->trial_attempts,
            &snapshot);
        if (state_status != RIBON_PROTECTED_STATE_STATUS_OK) {
            return ribon_ribos_authorization_reject(
                adapter,
                RIBON_RIBOS_AUTHORIZATION_FAILURE_STATE_INVALID);
        }
    }
    state_status = ribon_protected_state_authorize(
        &journal,
        adapter->request->manifest_sequence,
        &state_decision);
    if (state_status != RIBON_PROTECTED_STATE_STATUS_OK) {
        return ribon_ribos_authorization_reject(
            adapter,
            state_status == RIBON_PROTECTED_STATE_STATUS_UNAVAILABLE ||
                    state_status == RIBON_PROTECTED_STATE_STATUS_IO_ERROR ?
                RIBON_RIBOS_AUTHORIZATION_FAILURE_STATE_UNAVAILABLE :
                RIBON_RIBOS_AUTHORIZATION_FAILURE_ROLLBACK);
    }
    if (state_decision.authority == RIBON_PROTECTED_STATE_AUTHORITY_TRIAL) {
        state_status = ribon_protected_state_consume_trial_attempt(
            &journal,
            adapter->request->manifest_sequence,
            &snapshot);
        if (state_status != RIBON_PROTECTED_STATE_STATUS_OK) {
            return ribon_ribos_authorization_reject(
                adapter,
                RIBON_RIBOS_AUTHORIZATION_FAILURE_STATE_INVALID);
        }
        state_decision.generation = snapshot.generation;
        state_decision.trial_attempts_remaining =
            snapshot.trial_attempts_remaining;
    }

    adapter->manifest_sequence = adapter->request->manifest_sequence;
    adapter->rollback_floor = snapshot.confirmed_floor;
    adapter->rollback_generation = state_decision.generation;
    adapter->rollback_authority = state_decision.authority;
    adapter->trial_attempts_remaining =
        state_decision.trial_attempts_remaining;
    *receipt = (RibosArtifactAuthorizationReceipt){
        .size = sizeof(*receipt),
        .authorization_major = RIBOS_VM_AUTHORIZATION_V1_MAJOR,
        .authorization_minor = RIBOS_VM_AUTHORIZATION_V1_MINOR,
        .decision = RIBOS_ARTIFACT_AUTHORIZATION_GRANTED,
        .authority_generation = state_decision.generation,
        .manifest_sequence = adapter->request->manifest_sequence,
        .rollback_floor = snapshot.confirmed_floor,
    };
    memcpy(receipt->artifact_hash, request->artifact_hash,
           sizeof(receipt->artifact_hash));
    memcpy(receipt->schema_digest, request->schema_digest,
           sizeof(receipt->schema_digest));
    memcpy(receipt->helper_execution_digest,
           adapter->binding->helper_contract->digest,
           sizeof(receipt->helper_execution_digest));
    memcpy(receipt->key_identity_digest, key_decision.key_identity_digest,
           sizeof(receipt->key_identity_digest));
    memcpy(receipt->policy_identity_digest, binding->policy_identity_digest,
           sizeof(receipt->policy_identity_digest));
    return RIBOS_VM_STATUS_OK;
}

static uint32_t
ribon_ribos_authorize(
    void *context,
    const RibosArtifactAuthorizationRequest *request,
    RibosArtifactAuthorizationReceipt *receipt)
{
    struct RibonRibosAdapterContext *adapter = context;
    uint32_t status;

    if (adapter == NULL || request == NULL || receipt == NULL ||
        !ribon_ribos_digest_equal(
            request->schema_digest,
            adapter->schema_digest)) {
        return ribon_ribos_authorization_reject(
            adapter,
            RIBON_RIBOS_AUTHORIZATION_FAILURE_IDENTITY);
    }
    if (adapter->binding->authorization_class ==
        RIBON_RIBOS_AUTHORIZATION_SIGNED_POLICY) {
        return ribon_ribos_authorize_signed(adapter, request, receipt);
    }
    status = adapter->binding->fixture_authorize(
        adapter->request->product_context,
        request,
        receipt);
    if (status != RIBOS_VM_STATUS_OK ||
        !ribon_ribos_digest_equal(
            receipt->schema_digest,
            adapter->schema_digest) ||
        !ribon_ribos_digest_equal(
            receipt->helper_execution_digest,
            adapter->binding->helper_contract->digest)) {
        return ribon_ribos_authorization_reject(
            adapter,
            status == RIBOS_VM_STATUS_OK ?
                RIBON_RIBOS_AUTHORIZATION_FAILURE_IDENTITY :
                RIBON_RIBOS_AUTHORIZATION_FAILURE_FIXTURE);
    }
    return RIBOS_VM_STATUS_OK;
}

static uint32_t
ribon_ribos_now_ns(void *context, uint64_t *now_ns)
{
    struct RibonRibosAdapterContext *adapter = context;
    const struct RibonMonotonicTimerServiceOperations *operations;
    uint64_t ticks;
    uint64_t seconds;
    uint64_t remainder;
    uint64_t fractional;

    if (adapter == NULL || now_ns == NULL || adapter->timer == NULL ||
        adapter->timer->operations_size != sizeof(*operations)) {
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    operations = adapter->timer->operations;
    if (operations == NULL ||
        operations->size != sizeof(*operations) ||
        operations->abi_version != RIBON_SERVICE_ABI_VERSION ||
        operations->frequency_hz == 0u ||
        operations->frequency_hz >
            RIBON_RIBOS_NANOSECONDS_PER_SECOND ||
        operations->now == NULL ||
        operations->now(operations->context, &ticks) !=
            RIBON_SERVICE_STATUS_OK) {
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    seconds = ticks / operations->frequency_hz;
    remainder = ticks % operations->frequency_hz;
    if (seconds > UINT64_MAX / RIBON_RIBOS_NANOSECONDS_PER_SECOND ||
        remainder >
            UINT64_MAX / RIBON_RIBOS_NANOSECONDS_PER_SECOND) {
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    fractional =
        (remainder * RIBON_RIBOS_NANOSECONDS_PER_SECOND) /
        operations->frequency_hz;
    if (seconds * RIBON_RIBOS_NANOSECONDS_PER_SECOND >
        UINT64_MAX - fractional) {
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    *now_ns =
        seconds * RIBON_RIBOS_NANOSECONDS_PER_SECOND + fractional;
    return RIBOS_VM_HELPER_CALLBACK_OK;
}

static void
ribon_ribos_vm_recovery(
    void *context,
    const RibosVmFaultReceipt *receipt)
{
    struct RibonRibosAdapterContext *adapter = context;

    if (adapter == NULL || receipt == NULL) {
        return;
    }
    adapter->stage = RIBON_RIBOS_POLICY_STAGE_EXECUTE;
    adapter->status = RIBON_RIBOS_POLICY_STATUS_VM_FAULT;
    adapter->vm_fault_code = receipt->fault_code;
    adapter->outcome_kind = RIBOS_VM_OUTCOME_VM_FAULT;
    ribon_ribos_notify_recovery(adapter);
}

static int
ribon_ribos_arena_allocate(
    struct RibonRibosAdapterContext *adapter,
    size_t size,
    size_t alignment,
    void **memory)
{
    struct RibonArena *arena = adapter->request->core->arena;
    uint64_t consumed;

    if (size > UINT64_MAX || alignment > UINT64_MAX ||
        ribon_arena_allocate(
            arena,
            (uint64_t)size,
            (uint64_t)alignment,
            memory) != RIBON_MEMORY_STATUS_OK ||
        arena->used < adapter->arena_start) {
        return 0;
    }
    consumed = arena->used - adapter->arena_start;
    return consumed <= adapter->binding->arena_budget;
}

static int
ribon_ribos_arm_watchdog(struct RibonRibosAdapterContext *adapter)
{
    const struct RibonWatchdogServiceOperations *operations;

    if (adapter->watchdog == NULL) {
        return adapter->binding->watchdog_required == 0u;
    }
    operations = adapter->watchdog->operations;
    return operations != NULL &&
           operations->arm(
               operations->context,
               adapter->plugin->deadline_ms) ==
               RIBON_SERVICE_STATUS_OK;
}

uint32_t
ribon_ribos_policy_helper_dispatch(
    void *context,
    RibosVmHelperCall *call)
{
    struct RibonRibosAdapterContext *adapter = context;
    RibosVmHelperCallInfo info;
    const struct RibonRibosHelperRoute *route;
    const struct RibonServiceDescriptor *service = NULL;

    if (adapter == NULL ||
        ribos_vm_helper_call_info_v1(call, &info) !=
            RIBOS_VM_STATUS_OK) {
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    route = ribon_ribos_route(adapter->binding, info.stable_id);
    if (route == NULL) {
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    if (route->service_kind != RIBON_RIBOS_NO_SERVICE_KIND) {
        service = ribon_service_directory_find_exact(
            adapter->request->core->services,
            (enum RibonServiceKind)route->service_kind,
            route->service_id);
        if (service == NULL ||
            (service->provides & route->required_ribon_capabilities) !=
                route->required_ribon_capabilities) {
            return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
        }
    }
    return route->invoke(
        adapter->request->product_context,
        service,
        call);
}

static void
ribon_ribos_fill_receipt(
    const struct RibonRibosAdapterContext *adapter,
    struct RibonRibosPolicyReceipt *receipt)
{
    const struct RibonBootTransaction *transaction = adapter->request->transaction;
    const struct RibonCoreContext *core = adapter->request->core;
    const struct RibonArena *arena = core != NULL ? core->arena : NULL;

    *receipt = (struct RibonRibosPolicyReceipt){
        .size = sizeof(*receipt),
        .abi_version = RIBON_RIBOS_POLICY_ABI_VERSION,
        .stage = adapter->stage,
        .status = adapter->status,
        .vm_status = adapter->vm_status,
        .outcome_kind = adapter->outcome_kind,
        .terminal_helper_id = adapter->terminal_helper_id,
        .action_consumed = adapter->action_consumed,
        .recovery_notified = adapter->recovery_notified,
        .authorization_failure = adapter->authorization_failure,
        .rollback_authority = adapter->rollback_authority,
        .transaction_stage =
            transaction != NULL ? transaction->stage : RIBON_BOOT_STAGE_FAILED,
        .arena_bytes =
            arena != NULL && arena->used >= adapter->arena_start ?
                arena->used - adapter->arena_start : 0u,
        .context_generation = adapter->request->context_generation,
        .manifest_sequence = adapter->manifest_sequence,
        .rollback_floor = adapter->rollback_floor,
        .rollback_generation = adapter->rollback_generation,
        .trial_attempts_remaining = adapter->trial_attempts_remaining,
    };
}

int
ribon_ribos_policy_execute(
    const struct RibonRibosPolicyRequest *request,
    struct RibonRibosPolicyReceipt *receipt)
{
    struct RibonRibosAdapterContext adapter = {
        .request = request,
        .binding = request != NULL ? request->binding : NULL,
        .stage = RIBON_RIBOS_POLICY_STAGE_VALIDATE,
        .status = RIBON_RIBOS_POLICY_STATUS_BAD_ARGUMENT,
        .manifest_sequence = request != NULL ? request->manifest_sequence : 0u,
    };
    RibosArtifactAuthorizer authorizer;
    const RibosAuthorizedArtifact *authorized = NULL;
    const RibosPreparedProgram *prepared = NULL;
    RibosVerifierReport verifier_report;
    RibosVmStoragePlan storage_plan;
    RibosVmStorage *storage = NULL;
    RibosVmHandleHostTable handle_table;
    RibosVmHandleHostEntry *handle_entries = NULL;
    RibosVmContext context;
    RibosVmEmbedder embedder;
    RibosVmHelperEnvironment environment;
    RibosVmOutcome outcome;
    void *authorized_workspace = NULL;
    void *prepared_workspace = NULL;
    void *runtime_arena = NULL;
    size_t authorized_size = 0;
    size_t prepared_size = 0;
    size_t runtime_size = 0;
    size_t handle_bytes = 0;
    RibosVmStatus vm_status;
    int status;

    if (receipt == NULL) {
        return RIBON_RIBOS_POLICY_STATUS_BAD_ARGUMENT;
    }
    *receipt = (struct RibonRibosPolicyReceipt){0};
    if (request == NULL || request->core == NULL ||
        request->transaction == NULL ||
        request->context_generation == 0u ||
        (request->context_size != 0u &&
         request->context_bytes == NULL) ||
        ribon_core_context_validate(request->core) !=
            RIBON_CORE_STATUS_OK ||
        request->transaction->core != request->core ||
        request->transaction->stage !=
            RIBON_BOOT_STAGE_PREPARE_PROTOCOL) {
        if (request != NULL && request->binding != NULL &&
            request->binding->size == sizeof(*request->binding) &&
            request->binding->factory_recovery != NULL) {
            adapter.binding = request->binding;
            adapter.arena_start =
                request->core != NULL && request->core->arena != NULL ?
                    request->core->arena->used : 0u;
            ribon_ribos_notify_recovery(&adapter);
            ribon_ribos_fill_receipt(&adapter, receipt);
        }
        return RIBON_RIBOS_POLICY_STATUS_BAD_ARGUMENT;
    }
    adapter.arena_start = request->core->arena->used;
    adapter.plugin = ribon_ribos_selected_plugin(request->core);
    if (adapter.plugin == NULL ||
        request->artifact == NULL ||
        request->artifact_size == 0u ||
        request->artifact_size > adapter.plugin->input_budget) {
        status = ribon_ribos_fail(
            &adapter,
            RIBON_RIBOS_POLICY_STAGE_VALIDATE,
            adapter.plugin == NULL ?
                RIBON_RIBOS_POLICY_STATUS_BAD_BINDING :
                RIBON_RIBOS_POLICY_STATUS_BAD_ARGUMENT,
            RIBOS_VM_STATUS_INVALID_ARGUMENT);
        ribon_ribos_fill_receipt(&adapter, receipt);
        return status;
    }
    if (!ribon_ribos_binding_validate(&adapter)) {
        status = ribon_ribos_fail(
            &adapter,
            RIBON_RIBOS_POLICY_STAGE_VALIDATE,
            RIBON_RIBOS_POLICY_STATUS_BAD_BINDING,
            RIBOS_VM_STATUS_INVALID_DESCRIPTOR);
        ribon_ribos_fill_receipt(&adapter, receipt);
        return status;
    }
    if (!ribon_ribos_arm_watchdog(&adapter)) {
        status = ribon_ribos_fail(
            &adapter,
            RIBON_RIBOS_POLICY_STAGE_VALIDATE,
            RIBON_RIBOS_POLICY_STATUS_MISSING_SERVICE,
            RIBOS_VM_STATUS_EMBEDDER_REJECTED);
        ribon_ribos_fill_receipt(&adapter, receipt);
        return status;
    }
    authorizer = (RibosArtifactAuthorizer){
        .size = sizeof(authorizer),
        .authorization_major = RIBOS_VM_AUTHORIZATION_V1_MAJOR,
        .authorization_minor = RIBOS_VM_AUTHORIZATION_V1_MINOR,
        .authority_context = &adapter,
        .authorize = ribon_ribos_authorize,
    };
    vm_status = ribos_authorized_artifact_workspace_size_v1(
        request->artifact_size,
        &authorized_size);
    if (vm_status != RIBOS_VM_STATUS_OK ||
        !ribon_ribos_arena_allocate(
            &adapter,
            authorized_size,
            ribos_authorized_artifact_workspace_alignment_v1(),
            &authorized_workspace)) {
        status = ribon_ribos_fail(
            &adapter,
            RIBON_RIBOS_POLICY_STAGE_AUTHORIZE,
            RIBON_RIBOS_POLICY_STATUS_ARENA_EXHAUSTED,
            vm_status);
        ribon_ribos_fill_receipt(&adapter, receipt);
        return status;
    }
    vm_status = ribos_authorize_artifact_v1(
        request->artifact,
        request->artifact_size,
        &authorizer,
        authorized_workspace,
        authorized_size,
        &authorized);
    if (vm_status != RIBOS_VM_STATUS_OK) {
        status = ribon_ribos_fail(
            &adapter,
            RIBON_RIBOS_POLICY_STAGE_AUTHORIZE,
            RIBON_RIBOS_POLICY_STATUS_AUTHORIZATION,
            vm_status);
        ribon_ribos_fill_receipt(&adapter, receipt);
        return status;
    }
    vm_status = ribos_prepared_program_workspace_size_v1(
        authorized,
        adapter.binding->helper_contract,
        &prepared_size);
    if (vm_status != RIBOS_VM_STATUS_OK ||
        !ribon_ribos_arena_allocate(
            &adapter,
            prepared_size,
            ribos_prepared_program_workspace_alignment_v1(),
            &prepared_workspace)) {
        status = ribon_ribos_fail(
            &adapter,
            RIBON_RIBOS_POLICY_STAGE_PREPARE,
            RIBON_RIBOS_POLICY_STATUS_ARENA_EXHAUSTED,
            vm_status);
        ribon_ribos_fill_receipt(&adapter, receipt);
        return status;
    }
    vm_status = ribos_prepare_program_v1(
        authorized,
        adapter.binding->schema(),
        adapter.binding->helper_contract,
        adapter.binding->limits,
        prepared_workspace,
        prepared_size,
        &verifier_report,
        &prepared);
    if (vm_status != RIBOS_VM_STATUS_OK) {
        status = ribon_ribos_fail(
            &adapter,
            RIBON_RIBOS_POLICY_STAGE_PREPARE,
            RIBON_RIBOS_POLICY_STATUS_VERIFICATION,
            vm_status);
        ribon_ribos_fill_receipt(&adapter, receipt);
        return status;
    }
    vm_status = ribos_vm_runtime_size_v1(
        prepared,
        &storage_plan,
        &runtime_size);
    if (vm_status != RIBOS_VM_STATUS_OK ||
        !ribon_ribos_arena_allocate(
            &adapter,
            runtime_size,
            ribos_vm_runtime_alignment_v1(),
            &runtime_arena)) {
        status = ribon_ribos_fail(
            &adapter,
            RIBON_RIBOS_POLICY_STAGE_PREPARE,
            RIBON_RIBOS_POLICY_STATUS_ARENA_EXHAUSTED,
            vm_status);
        ribon_ribos_fill_receipt(&adapter, receipt);
        return status;
    }
    if (adapter.binding->limits->maximum_handles != 0u) {
        handle_bytes =
            (size_t)adapter.binding->limits->maximum_handles *
            sizeof(*handle_entries);
        if (handle_bytes / sizeof(*handle_entries) !=
            adapter.binding->limits->maximum_handles) {
            status = ribon_ribos_fail(
                &adapter,
                RIBON_RIBOS_POLICY_STAGE_PREPARE,
                RIBON_RIBOS_POLICY_STATUS_ARENA_EXHAUSTED,
                RIBOS_VM_STATUS_LIMIT_EXCEEDED);
            ribon_ribos_fill_receipt(&adapter, receipt);
            return status;
        }
        if (!ribon_ribos_arena_allocate(
                &adapter,
                handle_bytes,
                _Alignof(RibosVmHandleHostEntry),
                (void **)&handle_entries)) {
            status = ribon_ribos_fail(
                &adapter,
                RIBON_RIBOS_POLICY_STAGE_PREPARE,
                RIBON_RIBOS_POLICY_STATUS_ARENA_EXHAUSTED,
                RIBOS_VM_STATUS_ARENA_TOO_SMALL);
            ribon_ribos_fill_receipt(&adapter, receipt);
            return status;
        }
    }
    vm_status = ribos_vm_handle_host_table_initialize_v1(
        &handle_table,
        handle_entries,
        adapter.binding->limits->maximum_handles);
    if (vm_status == RIBOS_VM_STATUS_OK) {
        vm_status = ribos_vm_storage_initialize_v1(
            prepared,
            &storage_plan,
            runtime_arena,
            runtime_size,
            0u,
            &storage);
    }
    if (vm_status != RIBOS_VM_STATUS_OK) {
        status = ribon_ribos_fail(
            &adapter,
            RIBON_RIBOS_POLICY_STAGE_PREPARE,
            RIBON_RIBOS_POLICY_STATUS_VERIFICATION,
            vm_status);
        ribon_ribos_fill_receipt(&adapter, receipt);
        return status;
    }
    context = (RibosVmContext){
        .size = sizeof(context),
        .runtime_abi_major = RIBOS_VM_RUNTIME_ABI_V1_MAJOR,
        .runtime_abi_minor = RIBOS_VM_RUNTIME_ABI_V1_MINOR,
        .context_type_id = request->context_type_id,
        .selected_mode = request->core->mode->mode,
        .selected_phase = adapter.binding->selected_phase,
        .generation = request->context_generation,
        .bytes = request->context_bytes,
        .byte_size = request->context_size,
    };
    ribos_artifact_digest_bytes_v1(
        request->context_bytes,
        request->context_size,
        context.digest);
    embedder = (RibosVmEmbedder){
        .size = sizeof(embedder),
        .runtime_abi_major = RIBOS_VM_RUNTIME_ABI_V1_MAJOR,
        .runtime_abi_minor = RIBOS_VM_RUNTIME_ABI_V1_MINOR,
        .selected_mode = request->core->mode->mode,
        .selected_phase = adapter.binding->selected_phase,
        .granted_capabilities =
            adapter.binding->granted_ribos_capabilities,
        .helper_contract = adapter.binding->helper_contract,
        .embedder_context = &adapter,
        .monotonic_now_ns = ribon_ribos_now_ns,
        .factory_recovery = ribon_ribos_vm_recovery,
    };
    environment = (RibosVmHelperEnvironment){
        .size = sizeof(environment),
        .helpers_major = RIBOS_VM_HELPERS_V1_MAJOR,
        .helpers_minor = RIBOS_VM_HELPERS_V1_MINOR,
        .embedder = &embedder,
        .handle_table = &handle_table,
    };
    adapter.stage = RIBON_RIBOS_POLICY_STAGE_EXECUTE;
    vm_status = ribos_vm_policy_execute_v1(
        prepared,
        &context,
        &environment,
        storage,
        runtime_size,
        &outcome);
    adapter.vm_status = vm_status;
    if (vm_status != RIBOS_VM_STATUS_OK) {
        status = ribon_ribos_fail(
            &adapter,
            RIBON_RIBOS_POLICY_STAGE_EXECUTE,
            RIBON_RIBOS_POLICY_STATUS_VM_FAULT,
            vm_status);
        ribon_ribos_fill_receipt(&adapter, receipt);
        return status;
    }
    adapter.outcome_kind = outcome.kind;
    if (outcome.kind == RIBOS_VM_OUTCOME_VM_FAULT) {
        adapter.vm_fault_code = outcome.value.vm_fault.fault_code;
        status = ribon_ribos_fail(
            &adapter,
            RIBON_RIBOS_POLICY_STAGE_EXECUTE,
            RIBON_RIBOS_POLICY_STATUS_VM_FAULT,
            RIBOS_VM_STATUS_OK);
        ribon_ribos_fill_receipt(&adapter, receipt);
        return status;
    }
    if (outcome.kind == RIBOS_VM_OUTCOME_POLICY_ERROR) {
        status = ribon_ribos_fail(
            &adapter,
            RIBON_RIBOS_POLICY_STAGE_EXECUTE,
            RIBON_RIBOS_POLICY_STATUS_POLICY_ERROR,
            RIBOS_VM_STATUS_OK);
        ribon_ribos_fill_receipt(&adapter, receipt);
        return status;
    }
    if (outcome.kind != RIBOS_VM_OUTCOME_BOOT_ACTION) {
        status = ribon_ribos_fail(
            &adapter,
            RIBON_RIBOS_POLICY_STAGE_EXECUTE,
            RIBON_RIBOS_POLICY_STATUS_VM_FAULT,
            RIBOS_VM_STATUS_INTERNAL_ERROR);
        ribon_ribos_fill_receipt(&adapter, receipt);
        return status;
    }
    adapter.terminal_helper_id =
        outcome.value.boot_action.terminal_helper_id;
    adapter.stage = RIBON_RIBOS_POLICY_STAGE_VALIDATE_ACTION;
    if (!adapter.binding->validate_boot_action(
            request->product_context,
            &outcome.value.boot_action,
            request->transaction) ||
        ribos_vm_boot_action_consume_v1(
            prepared,
            storage,
            runtime_size,
            &outcome.value.boot_action) != RIBOS_VM_STATUS_OK) {
        status = ribon_ribos_fail(
            &adapter,
            RIBON_RIBOS_POLICY_STAGE_VALIDATE_ACTION,
            RIBON_RIBOS_POLICY_STATUS_ACTION_REJECTED,
            RIBOS_VM_STATUS_EMBEDDER_REJECTED);
        ribon_ribos_fill_receipt(&adapter, receipt);
        return status;
    }
    adapter.action_consumed = 1u;
    adapter.stage = RIBON_RIBOS_POLICY_STAGE_COMMIT;
    status = ribon_boot_transaction_commit_attempt(request->transaction);
    if (status != RIBON_BOOT_STATUS_OK) {
        status = ribon_ribos_fail(
            &adapter,
            RIBON_RIBOS_POLICY_STAGE_COMMIT,
            RIBON_RIBOS_POLICY_STATUS_COMMIT_FAILED,
            RIBOS_VM_STATUS_OK);
        ribon_ribos_fill_receipt(&adapter, receipt);
        return status;
    }
    adapter.stage = RIBON_RIBOS_POLICY_STAGE_QUIESCE;
    status = ribon_boot_transaction_quiesce_environment(
        request->transaction);
    if (status != RIBON_BOOT_STATUS_OK) {
        status = ribon_ribos_fail(
            &adapter,
            RIBON_RIBOS_POLICY_STAGE_QUIESCE,
            RIBON_RIBOS_POLICY_STATUS_QUIESCE_FAILED,
            RIBOS_VM_STATUS_OK);
        ribon_ribos_fill_receipt(&adapter, receipt);
        return status;
    }
    adapter.stage = RIBON_RIBOS_POLICY_STAGE_COMPLETE;
    adapter.status = RIBON_RIBOS_POLICY_STATUS_OK;
    ribon_ribos_fill_receipt(&adapter, receipt);
    return RIBON_RIBOS_POLICY_STATUS_OK;
}

/** @brief Signed binding의 protected-state 전이 receipt를 pointer-free로 채운다. */
static void
ribon_ribos_fill_state_receipt(
    struct RibonRibosPolicyStateReceipt *receipt,
    int status,
    const struct RibonProtectedStateSnapshot *snapshot)
{
    *receipt = (struct RibonRibosPolicyStateReceipt){
        .size = sizeof(*receipt),
        .abi_version = RIBON_RIBOS_POLICY_ABI_VERSION,
        .status = status,
        .state_kind = snapshot != NULL ? snapshot->kind : 0u,
        .confirmed_floor = snapshot != NULL ? snapshot->confirmed_floor : 0u,
        .pending_sequence = snapshot != NULL ? snapshot->pending_sequence : 0u,
        .generation = snapshot != NULL ? snapshot->generation : 0u,
        .trial_attempts_remaining =
            snapshot != NULL ? snapshot->trial_attempts_remaining : 0u,
    };
}

int
ribon_ribos_policy_confirm(
    const struct RibonRibosProductBinding *binding,
    uint64_t pending_sequence,
    struct RibonRibosPolicyStateReceipt *receipt)
{
    struct RibonProtectedStateJournal journal;
    struct RibonProtectedStateSnapshot snapshot;
    int state_status;

    if (receipt == NULL) {
        return RIBON_RIBOS_POLICY_STATUS_BAD_ARGUMENT;
    }
    ribon_ribos_fill_state_receipt(
        receipt,
        RIBON_RIBOS_POLICY_STATUS_BAD_BINDING,
        NULL);
    if (binding == NULL ||
        binding->authorization_class !=
            RIBON_RIBOS_AUTHORIZATION_SIGNED_POLICY ||
        !ribon_ribos_signed_binding_is_valid(binding->signed_policy)) {
        return RIBON_RIBOS_POLICY_STATUS_BAD_BINDING;
    }
    state_status = ribon_protected_state_journal_bind(
        binding->signed_policy->protected_state,
        binding->signed_policy->rollback_domain_digest,
        &journal);
    if (state_status == RIBON_PROTECTED_STATE_STATUS_OK) {
        state_status = ribon_protected_state_confirm(
            &journal,
            pending_sequence,
            &snapshot);
    }
    if (state_status != RIBON_PROTECTED_STATE_STATUS_OK) {
        ribon_ribos_fill_state_receipt(
            receipt,
            RIBON_RIBOS_POLICY_STATUS_ROLLBACK_STATE,
            NULL);
        return RIBON_RIBOS_POLICY_STATUS_ROLLBACK_STATE;
    }
    ribon_ribos_fill_state_receipt(
        receipt,
        RIBON_RIBOS_POLICY_STATUS_OK,
        &snapshot);
    return RIBON_RIBOS_POLICY_STATUS_OK;
}

int
ribon_ribos_policy_fail_trial(
    const struct RibonRibosProductBinding *binding,
    struct RibonRibosPolicyStateReceipt *receipt)
{
    struct RibonProtectedStateJournal journal;
    struct RibonProtectedStateSnapshot snapshot;
    int state_status;

    if (receipt == NULL) {
        return RIBON_RIBOS_POLICY_STATUS_BAD_ARGUMENT;
    }
    ribon_ribos_fill_state_receipt(
        receipt,
        RIBON_RIBOS_POLICY_STATUS_BAD_BINDING,
        NULL);
    if (binding == NULL ||
        binding->authorization_class !=
            RIBON_RIBOS_AUTHORIZATION_SIGNED_POLICY ||
        !ribon_ribos_signed_binding_is_valid(binding->signed_policy)) {
        return RIBON_RIBOS_POLICY_STATUS_BAD_BINDING;
    }
    state_status = ribon_protected_state_journal_bind(
        binding->signed_policy->protected_state,
        binding->signed_policy->rollback_domain_digest,
        &journal);
    if (state_status == RIBON_PROTECTED_STATE_STATUS_OK) {
        state_status = ribon_protected_state_fail_trial(&journal, &snapshot);
    }
    if (state_status != RIBON_PROTECTED_STATE_STATUS_OK) {
        ribon_ribos_fill_state_receipt(
            receipt,
            RIBON_RIBOS_POLICY_STATUS_ROLLBACK_STATE,
            NULL);
        return RIBON_RIBOS_POLICY_STATUS_ROLLBACK_STATE;
    }
    ribon_ribos_fill_state_receipt(
        receipt,
        RIBON_RIBOS_POLICY_STATUS_OK,
        &snapshot);
    return RIBON_RIBOS_POLICY_STATUS_OK;
}

static const struct RibonRibosPolicyOperations ribon_ribos_policy_operations = {
    .size = sizeof(ribon_ribos_policy_operations),
    .abi_version = RIBON_RIBOS_POLICY_OPERATIONS_ABI,
    .execute = ribon_ribos_policy_execute,
};

int
ribon_ribos_policy_operations_are_valid(
    const struct RibonPluginDescriptor *descriptor)
{
    const struct RibonRibosPolicyOperations *operations;

    if (descriptor == NULL ||
        descriptor->kind != RIBON_PLUGIN_KIND_POLICY ||
        descriptor->operations != &ribon_ribos_policy_operations ||
        descriptor->operations_size != sizeof(*operations) ||
        descriptor->operations_abi !=
            RIBON_RIBOS_POLICY_OPERATIONS_ABI) {
        return 0;
    }
    operations = descriptor->operations;
    return operations->size == sizeof(*operations) &&
           operations->abi_version ==
               RIBON_RIBOS_POLICY_OPERATIONS_ABI &&
           operations->execute == ribon_ribos_policy_execute;
}

const struct RibonPluginDescriptor ribon_ribos_policy_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_ribos_policy_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_POLICY,
    .phase = RIBON_PLUGIN_PHASE_BOOT,
    .id = "policy.ribos",
    .provides = RIBON_CAP_BOOT_CONFIRMATION,
    .requires =
        RIBON_CAP_BOOT_SOURCE_READ |
        RIBON_CAP_STORAGE_FLUSH |
        RIBON_CAP_MONOTONIC_TIMER |
        RIBON_CAP_PERSISTENT_METADATA |
        RIBON_CAP_ENVIRONMENT_QUIESCE,
    .architecture_mask = RIBON_ARCH_MASK_ALL,
    .environment_mask = RIBON_ENV_MASK_ALL,
    .personality_mask = RIBON_PERSONALITY_MASK_ALL,
    .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = RIBON_RIBOS_PLUGIN_ARENA_BUDGET,
    .input_budget = 32ull * 1024ull * 1024ull,
    .output_budget = 64ull * 1024ull,
    .deadline_ms = 30000u,
    .operations = &ribon_ribos_policy_operations,
    .operations_size = sizeof(ribon_ribos_policy_operations),
    .operations_abi = RIBON_RIBOS_POLICY_OPERATIONS_ABI,
    .validate_operations = ribon_ribos_policy_operations_are_valid,
};
