#ifndef RIBON_PRODUCTS_VALIDATION_RIBOS_QEMU_PRODUCT_H
#define RIBON_PRODUCTS_VALIDATION_RIBOS_QEMU_PRODUCT_H

#include <Ribon/firmware/environment.h>
#include <Ribon/policy/ribos.h>

#include <ribos/vm/runtime.h>

/** @brief QEMU validation product가 관찰하는 policy callback state다. */
struct RibonValidationRibosFixture {
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

int ribon_validation_source_bind(const void *data, uint64_t size);
void ribon_validation_lifecycle_reset(void);
void ribon_validation_timer_step_set(uint64_t step);
uint32_t ribon_validation_write_count(void);
uint32_t ribon_validation_flush_count(void);
uint32_t ribon_validation_quiesce_count(void);
uint32_t ribon_validation_watchdog_count(void);
int ribon_validation_environment_collect(
    enum RibonArchitectureId architecture,
    struct RibonBootEnvironment *out);

void ribon_validation_ribos_factory_recovery(
    void *context,
    const struct RibonRibosFailureReceipt *receipt);
int ribon_validation_ribos_validate_boot_action(
    void *context,
    const struct RibosVmBootAction *action,
    const struct RibonBootTransaction *transaction);
uint32_t ribon_validation_ribos_slot_selected(
    void *context,
    const struct RibonServiceDescriptor *service,
    struct RibosVmHelperCall *call);
uint32_t ribon_validation_ribos_slot_image(
    void *context,
    const struct RibonServiceDescriptor *service,
    struct RibosVmHelperCall *call);
uint32_t ribon_validation_ribos_image_verify(
    void *context,
    const struct RibonServiceDescriptor *service,
    struct RibosVmHelperCall *call);
uint32_t ribon_validation_ribos_boot_slot(
    void *context,
    const struct RibonServiceDescriptor *service,
    struct RibosVmHelperCall *call);
uint32_t ribon_validation_ribos_boot_recovery(
    void *context,
    const struct RibonServiceDescriptor *service,
    struct RibosVmHelperCall *call);

#endif
