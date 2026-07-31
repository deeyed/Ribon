#ifndef RIBON_HOST_RIBOS_POLICY_H
#define RIBON_HOST_RIBOS_POLICY_H

#include <Ribon/policy/ribos.h>
#include <Ribon/security/protected_state.h>

#include <ribos/vm/runtime.h>

/** @brief Host-only Ribos product callback fixture state다. */
struct RibonHostRibosFixture {
    const struct RibonRibosProductBinding *binding;
    uint32_t slot_type;
    uint32_t image_type;
    uint32_t verified_image_type;
    uint32_t boot_action_type;
    uint32_t boot_action_size;
    uint32_t helper_calls;
    uint32_t fallback_calls;
    uint32_t drop_calls;
    uint32_t reject_action;
    uint32_t slot_object;
    uint32_t image_object;
    struct RibonRibosFailureReceipt last_failure;
};

/** @brief Host reference product의 volatile protected-state provider다. */
extern const struct RibonProtectedStateProvider
    ribon_host_protected_state_provider_descriptor;

/** @brief Host reference rollback journal을 exact confirmed floor로 재구성한다. */
int ribon_host_ribos_protected_state_provision(
    const struct RibonRibosProductBinding *binding,
    uint64_t confirmed_floor);

/** @brief Host negative test를 위해 durable journal byte를 손상시킨다. */
void ribon_host_ribos_protected_state_corrupt(void);

void ribon_host_ribos_factory_recovery(
    void *context,
    const struct RibonRibosFailureReceipt *receipt);

int ribon_host_ribos_validate_boot_action(
    void *context,
    const struct RibosVmBootAction *action,
    const struct RibonBootTransaction *transaction);

uint32_t ribon_host_ribos_slot_selected(
    void *context,
    const struct RibonServiceDescriptor *service,
    struct RibosVmHelperCall *call);

uint32_t ribon_host_ribos_slot_image(
    void *context,
    const struct RibonServiceDescriptor *service,
    struct RibosVmHelperCall *call);

uint32_t ribon_host_ribos_image_verify(
    void *context,
    const struct RibonServiceDescriptor *service,
    struct RibosVmHelperCall *call);

uint32_t ribon_host_ribos_boot_slot(
    void *context,
    const struct RibonServiceDescriptor *service,
    struct RibosVmHelperCall *call);

uint32_t ribon_host_ribos_boot_recovery(
    void *context,
    const struct RibonServiceDescriptor *service,
    struct RibosVmHelperCall *call);

#endif
