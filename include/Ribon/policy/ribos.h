#ifndef RIBON_POLICY_RIBOS_H
#define RIBON_POLICY_RIBOS_H

#include <stddef.h>
#include <stdint.h>

#include <Ribon/boot/plan.h>
#include <Ribon/core/context.h>
#include <Ribon/plugin/descriptor.h>
#include <Ribon/service/directory.h>

struct RibosArtifactAuthorizationRequest;
struct RibosArtifactAuthorizationReceipt;
struct RibosProductSchema;
struct RibosVmBootAction;
struct RibosVmHelperCall;
struct RibosVmHelperContract;
struct RibosVmLimits;

/** @brief Ribon과 architecture-neutral Ribos VM 사이의 adapter ABI다. */
#define RIBON_RIBOS_POLICY_ABI_VERSION 1u

/** @brief Helper route가 native service를 요구하지 않음을 나타낸다. */
#define RIBON_RIBOS_NO_SERVICE_KIND UINT32_MAX

/** @brief Ribos policy plugin operation table ABI다. */
#define RIBON_RIBOS_POLICY_OPERATIONS_ABI 1u

/** @brief Ribon adapter가 실패한 단방향 lifecycle 단계다. */
enum RibonRibosPolicyStage {
    RIBON_RIBOS_POLICY_STAGE_VALIDATE = 0,
    RIBON_RIBOS_POLICY_STAGE_AUTHORIZE = 1,
    RIBON_RIBOS_POLICY_STAGE_PREPARE = 2,
    RIBON_RIBOS_POLICY_STAGE_EXECUTE = 3,
    RIBON_RIBOS_POLICY_STAGE_VALIDATE_ACTION = 4,
    RIBON_RIBOS_POLICY_STAGE_COMMIT = 5,
    RIBON_RIBOS_POLICY_STAGE_QUIESCE = 6,
    RIBON_RIBOS_POLICY_STAGE_COMPLETE = 7,
};

/** @brief Generic policy adapter의 stable 결과다. */
enum RibonRibosPolicyStatus {
    RIBON_RIBOS_POLICY_STATUS_OK = 0,
    RIBON_RIBOS_POLICY_STATUS_BAD_ARGUMENT = -1,
    RIBON_RIBOS_POLICY_STATUS_BAD_BINDING = -2,
    RIBON_RIBOS_POLICY_STATUS_MISSING_SERVICE = -3,
    RIBON_RIBOS_POLICY_STATUS_ARENA_EXHAUSTED = -4,
    RIBON_RIBOS_POLICY_STATUS_AUTHORIZATION = -5,
    RIBON_RIBOS_POLICY_STATUS_VERIFICATION = -6,
    RIBON_RIBOS_POLICY_STATUS_VM_FAULT = -7,
    RIBON_RIBOS_POLICY_STATUS_POLICY_ERROR = -8,
    RIBON_RIBOS_POLICY_STATUS_ACTION_REJECTED = -9,
    RIBON_RIBOS_POLICY_STATUS_COMMIT_FAILED = -10,
    RIBON_RIBOS_POLICY_STATUS_QUIESCE_FAILED = -11,
};

/**
 * @brief Factory recovery에 전달되는 pointer-free adapter failure receipt다.
 *
 * 이 receipt만으로 recovery policy를 실행할 수 있으며 외부 `.rba` byte를
 * 역참조하지 않는다.
 */
struct RibonRibosFailureReceipt {
    uint32_t size;
    uint32_t abi_version;
    enum RibonRibosPolicyStage stage;
    int32_t status;
    uint32_t vm_status;
    uint32_t vm_fault_code;
    uint32_t outcome_kind;
    uint32_t terminal_helper_id;
    uint32_t action_consumed;
    uint32_t recovery_notified;
    enum RibonBootLifecycleStage transaction_stage;
    uint64_t arena_bytes;
    uint64_t context_generation;
};

/**
 * @brief Product callback이 helper stable ID를 semantic operation으로 변환한다.
 *
 * `service`는 생성 graph가 exact kind와 ID로 선택한 descriptor 또는 NULL이다.
 * Callback은 raw MMIO, raw flash와 arbitrary jump를 이 경계로 노출할 수 없다.
 */
typedef uint32_t (*RibonRibosSemanticHelperFn)(
    void *product_context,
    const struct RibonServiceDescriptor *service,
    struct RibosVmHelperCall *call);

/** @brief Product root-of-trust가 policy artifact를 승인하는 callback이다. */
typedef uint32_t (*RibonRibosAuthorizeFn)(
    void *product_context,
    const struct RibosArtifactAuthorizationRequest *request,
    struct RibosArtifactAuthorizationReceipt *receipt);

/** @brief 외부 policy artifact 없이 실행 가능한 factory recovery callback이다. */
typedef void (*RibonRibosFactoryRecoveryFn)(
    void *product_context,
    const struct RibonRibosFailureReceipt *receipt);

/** @brief VM intent를 기존 boot transaction에 적용할 수 있는지 다시 검사한다. */
typedef int (*RibonRibosValidateBootActionFn)(
    void *product_context,
    const struct RibosVmBootAction *action,
    const struct RibonBootTransaction *transaction);

/** @brief Product graph가 한 helper를 service와 callback에 연결한 route다. */
struct RibonRibosHelperRoute {
    uint32_t stable_id;
    uint32_t service_kind;
    const char *service_id;
    uint64_t required_ribon_capabilities;
    RibonRibosSemanticHelperFn invoke;
};

/**
 * @brief Compiler, verifier와 adapter가 공유하는 product-generated policy binding이다.
 *
 * Schema provider와 helper contract는 같은 product graph에서 선택된다. Helper
 * contract digest는 callback 주소가 아니라 canonical execution descriptor만
 * 포함한다.
 */
struct RibonRibosProductBinding {
    uint32_t size;
    uint32_t abi_version;
    const char *product_id;
    const char *policy_id;
    const struct RibosProductSchema *(*schema)(void);
    const struct RibosVmHelperContract *helper_contract;
    const struct RibonRibosHelperRoute *routes;
    uint32_t route_count;
    uint32_t selected_phase;
    uint32_t mode_mask;
    uint32_t granted_ribos_capabilities;
    uint32_t watchdog_required;
    uint64_t required_ribon_capabilities;
    uint64_t arena_budget;
    const char *timer_service_id;
    const char *watchdog_service_id;
    const struct RibosVmLimits *limits;
    RibonRibosAuthorizeFn authorize;
    RibonRibosFactoryRecoveryFn factory_recovery;
    RibonRibosValidateBootActionFn validate_boot_action;
};

/** @brief 한 policy execution의 caller-owned immutable input이다. */
struct RibonRibosPolicyRequest {
    const struct RibonCoreContext *core;
    const struct RibonRibosProductBinding *binding;
    const uint8_t *artifact;
    size_t artifact_size;
    uint32_t context_type_id;
    const uint8_t *context_bytes;
    size_t context_size;
    uint64_t context_generation;
    void *product_context;
    struct RibonBootTransaction *transaction;
};

/** @brief Pointer-free terminal execution 결과다. */
struct RibonRibosPolicyReceipt {
    uint32_t size;
    uint32_t abi_version;
    enum RibonRibosPolicyStage stage;
    int32_t status;
    uint32_t vm_status;
    uint32_t outcome_kind;
    uint32_t terminal_helper_id;
    uint32_t action_consumed;
    uint32_t recovery_notified;
    enum RibonBootLifecycleStage transaction_stage;
    uint64_t arena_bytes;
    uint64_t context_generation;
};

/** @brief Policy plugin이 노출하는 typed operation table이다. */
struct RibonRibosPolicyOperations {
    uint32_t size;
    uint32_t abi_version;
    int (*execute)(
        const struct RibonRibosPolicyRequest *request,
        struct RibonRibosPolicyReceipt *receipt);
};

/** @brief 생성 binding과 Ribon object graph를 검증하고 policy를 한 번 실행한다. */
int ribon_ribos_policy_execute(
    const struct RibonRibosPolicyRequest *request,
    struct RibonRibosPolicyReceipt *receipt);

/** @brief Generated helper contract가 사용하는 단일 semantic dispatcher다. */
uint32_t ribon_ribos_policy_helper_dispatch(
    void *adapter_context,
    struct RibosVmHelperCall *call);

/** @brief Generic Ribos policy operation table을 검사한다. */
int ribon_ribos_policy_operations_are_valid(
    const struct RibonPluginDescriptor *descriptor);

/** @brief Generic Ribos policy plugin descriptor다. */
extern const struct RibonPluginDescriptor ribon_ribos_policy_plugin_descriptor;

/** @brief 선택 product graph가 생성한 Ribos policy binding을 반환한다. */
const struct RibonRibosProductBinding *ribon_generated_ribos_policy_binding(void);

#endif
