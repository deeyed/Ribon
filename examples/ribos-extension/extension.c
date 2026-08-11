#include "extension.h"

#include <Ribon/plugin/phases.h>

#include <string.h>

static const RibosSchemaType example_types[] = {
    {1u, RIBOS_SCHEMA_TYPE_FACT, "BootContext", RIBOS_SCHEMA_OWNERSHIP_COPY},
    {2u, RIBOS_SCHEMA_TYPE_VALUE, "BootAction", RIBOS_SCHEMA_OWNERSHIP_LINEAR},
    {3u, RIBOS_SCHEMA_TYPE_ENUM, "BootError", RIBOS_SCHEMA_OWNERSHIP_COPY},
};

static const RibosSchemaHelper example_helpers[] = {
    {
        .stable_id = 1001u,
        .path = "board.revision_code",
        .capabilities = RIBOS_CAPABILITY_INSPECT,
        .result_type = "u32",
    },
};

static const RibosProductSchema example_schema = {
    .format_major = RIBOS_SCHEMA_V1_MAJOR,
    .format_minor = RIBOS_SCHEMA_V1_MINOR,
    .product_id = "example.board-inspect.v1",
    .policy_context_type = "BootContext",
    .policy_action_type = "BootAction",
    .policy_error_type = "BootError",
    .types = example_types,
    .type_count = sizeof(example_types) / sizeof(example_types[0]),
    .helpers = example_helpers,
    .helper_count = sizeof(example_helpers) / sizeof(example_helpers[0]),
};

static uint32_t
ribon_example_revision_code(
    void *product_context,
    const struct RibonServiceDescriptor *service,
    struct RibosVmHelperCall *call);

static uint32_t
ribon_example_vm_dispatch(void *context, struct RibosVmHelperCall *call)
{
    return ribon_example_revision_code(context, NULL, call);
}

uint32_t
ribon_example_alternate_vm_dispatch(void *context, struct RibosVmHelperCall *call)
{
    return ribon_example_revision_code(context, NULL, call);
}

uint32_t
ribon_example_alternate_revision_code(
    void *product_context,
    const struct RibonServiceDescriptor *service,
    struct RibosVmHelperCall *call)
{
    return ribon_example_revision_code(product_context, service, call);
}

static uint32_t
ribon_example_revision_code(
    void *product_context,
    const struct RibonServiceDescriptor *service,
    struct RibosVmHelperCall *call)
{
    const uint8_t value[4] = {3u, 0u, 0u, 0u};
    RibosVmHelperCallInfo info;

    (void)product_context;
    (void)service;
    if (ribos_vm_helper_call_info_v1(call, &info) != RIBOS_VM_STATUS_OK) {
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    if (ribos_vm_helper_call_consume_operations_v1(call, 1u) !=
        RIBOS_VM_STATUS_OK) {
        return RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
    }
    return ribos_vm_helper_call_set_success_value_v1(
               call, info.result_type_id, value, sizeof(value)) ==
            RIBOS_VM_STATUS_OK ?
        RIBOS_VM_HELPER_CALLBACK_OK :
        RIBOS_VM_HELPER_CALLBACK_CONTRACT_FAULT;
}

static RibosVmHelperBinding example_bindings[] = {
    {
        .execution = {
            .size = sizeof(RibosVmHelperExecutionDescriptor),
            .contract_major = RIBOS_VM_HELPER_EXECUTION_V1_MAJOR,
            .contract_minor = RIBOS_VM_HELPER_EXECUTION_V1_MINOR,
            .stable_id = 1001u,
            .required_capabilities = RIBOS_CAPABILITY_INSPECT,
            .effect = RIBOS_VM_HELPER_EFFECT_PURE,
            .execution_mode = RIBOS_VM_HELPER_EXECUTION_SYNCHRONOUS,
            .durability = RIBOS_VM_HELPER_DURABILITY_NONE,
            .handle_transition = RIBOS_VM_HANDLE_TRANSITION_NONE,
            .transition_parameter = RIBOS_VM_INVALID_ID,
            .allowed_mode_mask = UINT64_C(1),
            .allowed_phase_mask = UINT64_C(1) << RIBON_PLUGIN_PHASE_BOOT,
            .maximum_input_bytes = 4u,
            .maximum_output_bytes = 4u,
            .maximum_operations = 1u,
            .maximum_polls = 1u,
            .maximum_duration_ns = 100000u,
        },
        .invoke = ribon_example_vm_dispatch,
    },
};

static RibosVmHelperContract example_contract = {
    .size = sizeof(example_contract),
    .contract_major = RIBOS_VM_HELPER_EXECUTION_V1_MAJOR,
    .contract_minor = RIBOS_VM_HELPER_EXECUTION_V1_MINOR,
    .binding_count = 1u,
    .bindings = example_bindings,
};

static const struct RibonRibosHelperRoute example_routes[] = {
    {
        .stable_id = 1001u,
        .service_kind = RIBON_RIBOS_NO_SERVICE_KIND,
        .invoke = ribon_example_revision_code,
    },
};

static struct RibonRibosExtensionDescriptor example_extension = {
    .size = sizeof(example_extension),
    .abi_version = RIBON_RIBOS_EXTENSION_ABI_VERSION,
    .package_id = "example.ribos.board-inspect.v1",
    .schema = &example_schema,
    .helper_contract = &example_contract,
    .routes = example_routes,
    .route_count = 1u,
    .selected_phase = RIBON_PLUGIN_PHASE_BOOT,
    .granted_ribos_capabilities = RIBOS_CAPABILITY_INSPECT,
    .helper_call_budget = 2u,
};

const struct RibonRibosExtensionDescriptor *
ribon_example_inspect_extension(void)
{
    static int initialized;
    uint8_t digest[RIBOS_SCHEMA_DIGEST_BYTES];

    if (!initialized) {
        if (ribos_vm_helper_contract_compute_identity_v1(
                &example_contract, digest) != RIBOS_VM_STATUS_OK) {
            return NULL;
        }
        memcpy(example_contract.digest, digest, sizeof(digest));
        memcpy(example_extension.helper_execution_digest, digest, sizeof(digest));
        if (ribos_schema_compute_identity(&example_schema, digest) !=
            RIBOS_SCHEMA_OK) {
            return NULL;
        }
        memcpy(example_extension.schema_digest, digest, sizeof(digest));
        initialized = 1;
    }
    return &example_extension;
}
