#include <Ribon/policy/ribos.h>

#include <ribos/artifact/format.h>
#include <ribos/schema/schema.h>
#include <ribos/vm/handles.h>
#include <ribos/vm/helpers.h>
#include <ribos/vm/prepared.h>
#include <ribos/vm/storage.h>
#include <ribos/vm/terminal.h>

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
    uint64_t arena_start;
};

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
        .transaction_stage =
            transaction != NULL ? transaction->stage : RIBON_BOOT_STAGE_FAILED,
        .arena_bytes =
            arena != NULL && arena->used >= adapter->arena_start ?
                arena->used - adapter->arena_start : 0u,
        .context_generation = adapter->request->context_generation,
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
    if (route->service_kind > RIBON_SERVICE_KIND_PAYLOAD_PLACEMENT ||
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
        service->phase > adapter->binding->selected_phase ||
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
        binding->authorize == NULL ||
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
        return RIBOS_VM_STATUS_NOT_AUTHORIZED;
    }
    status = adapter->binding->authorize(
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
        return RIBOS_VM_STATUS_NOT_AUTHORIZED;
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
        .transaction_stage =
            transaction != NULL ? transaction->stage : RIBON_BOOT_STAGE_FAILED,
        .arena_bytes =
            arena != NULL && arena->used >= adapter->arena_start ?
                arena->used - adapter->arena_start : 0u,
        .context_generation = adapter->request->context_generation,
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
