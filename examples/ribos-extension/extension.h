#ifndef RIBON_EXAMPLE_RIBOS_EXTENSION_H
#define RIBON_EXAMPLE_RIBOS_EXTENSION_H

#include <Ribon/policy/ribos_extension.h>

const struct RibonRibosExtensionDescriptor *
ribon_example_inspect_extension(void);

uint32_t ribon_example_alternate_vm_dispatch(
    void *context,
    struct RibosVmHelperCall *call);

uint32_t ribon_example_alternate_revision_code(
    void *product_context,
    const struct RibonServiceDescriptor *service,
    struct RibosVmHelperCall *call);

#endif
