#include "extension.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(void)
{
    const struct RibonRibosExtensionDescriptor *extension =
        ribon_example_inspect_extension();
    struct RibonRibosExtensionDescriptor changed;
    RibosVmHelperContract contract;
    RibosVmHelperBinding binding;
    struct RibonRibosHelperRoute route;
    uint8_t *artifact;
    size_t size = 0u;
    size_t written = 0u;

    assert(extension != NULL);
    assert(ribon_ribos_extension_validate_v1(extension) ==
           RIBON_RIBOS_EXTENSION_STATUS_OK);
    assert(ribon_ribos_extension_schema_size_v1(extension, &size) ==
           RIBON_RIBOS_EXTENSION_STATUS_OK);
    artifact = malloc(size);
    assert(artifact != NULL);
    assert(ribon_ribos_extension_schema_write_v1(
               extension, artifact, size, &written) ==
           RIBON_RIBOS_EXTENSION_STATUS_OK);
    assert(written == size);
    assert(ribon_ribos_extension_schema_read_v1(extension, artifact, size) ==
           RIBON_RIBOS_EXTENSION_STATUS_OK);
    artifact[size - 1u] ^= 1u;
    assert(ribon_ribos_extension_schema_read_v1(extension, artifact, size) ==
           RIBON_RIBOS_EXTENSION_STATUS_SCHEMA_ARTIFACT);
    artifact[size - 1u] ^= 1u;

    changed = *extension;
    changed.abi_version += 1u;
    assert(ribon_ribos_extension_validate_v1(&changed) ==
           RIBON_RIBOS_EXTENSION_STATUS_BAD_ABI);

    changed = *extension;
    changed.helper_call_budget = 0u;
    assert(ribon_ribos_extension_validate_v1(&changed) ==
           RIBON_RIBOS_EXTENSION_STATUS_BUDGET_EXCEEDED);

    changed = *extension;
    changed.schema_digest[0] ^= 1u;
    assert(ribon_ribos_extension_validate_v1(&changed) ==
           RIBON_RIBOS_EXTENSION_STATUS_DIGEST_MISMATCH);

    binding = extension->helper_contract->bindings[0];
    binding.invoke = ribon_example_alternate_vm_dispatch;
    contract = *extension->helper_contract;
    contract.bindings = &binding;
    assert(ribos_vm_helper_contract_compute_identity_v1(
               &contract, contract.digest) == RIBOS_VM_STATUS_OK);
    assert(memcmp(contract.digest,
                  extension->helper_execution_digest,
                  sizeof(contract.digest)) == 0);
    route = extension->routes[0];
    route.invoke = ribon_example_alternate_revision_code;
    changed = *extension;
    changed.helper_contract = &contract;
    changed.routes = &route;
    assert(ribon_ribos_extension_validate_v1(&changed) ==
           RIBON_RIBOS_EXTENSION_STATUS_OK);

    binding = extension->helper_contract->bindings[0];
    binding.execution.required_capabilities |= RIBOS_CAPABILITY_DEVICE;
    contract = *extension->helper_contract;
    contract.bindings = &binding;
    assert(ribos_vm_helper_contract_compute_identity_v1(
               &contract, contract.digest) == RIBOS_VM_STATUS_OK);
    changed = *extension;
    changed.helper_contract = &contract;
    memcpy(changed.helper_execution_digest, contract.digest, sizeof(contract.digest));
    assert(ribon_ribos_extension_validate_v1(&changed) ==
           RIBON_RIBOS_EXTENSION_STATUS_CAPABILITY_WIDENING);

    route = extension->routes[0];
    route.stable_id += 1u;
    changed = *extension;
    changed.routes = &route;
    assert(ribon_ribos_extension_validate_v1(&changed) ==
           RIBON_RIBOS_EXTENSION_STATUS_BAD_ROUTE);

    free(artifact);
    puts("RIBON-R01-RIBOS-EXTENSION-SDK-OK helpers=1 schema=canonical callbacks=local negatives=5");
    return 0;
}
