#ifndef RIBON_BOOT_PLAN_H
#define RIBON_BOOT_PLAN_H

#include <stdint.h>

#include <Ribon/boot/image.h>
#include <Ribon/core/context.h>
#include <Ribon/firmware/environment.h>
#include <Ribon/service/directory.h>
#include <Ribon/protocol/protocol.h>

/** @brief Boot Library lifecycle operation의 결과다. */
enum RibonBootStatus {
    RIBON_BOOT_STATUS_OK = 0,
    RIBON_BOOT_STATUS_BAD_ARGUMENT = -1,
    RIBON_BOOT_STATUS_BAD_STATE = -2,
    RIBON_BOOT_STATUS_MISSING_CAPABILITY = -3,
    RIBON_BOOT_STATUS_INCOMPLETE_ENVIRONMENT = -4,
    RIBON_BOOT_STATUS_INVALID_PAYLOAD = -5,
    RIBON_BOOT_STATUS_INVALID_HANDOFF = -6,
    RIBON_BOOT_STATUS_BUDGET_EXCEEDED = -7,
    RIBON_BOOT_STATUS_UNSUPPORTED = -8,
    RIBON_BOOT_STATUS_TIMEOUT = -9,
    RIBON_BOOT_STATUS_PROVIDER_FAILURE = -10,
};

/** @brief Bounded boot transaction의 단방향 lifecycle stage다. */
enum RibonBootLifecycleStage {
    RIBON_BOOT_STAGE_CAPTURE = 0,
    RIBON_BOOT_STAGE_VALIDATE_PRODUCT = 1,
    RIBON_BOOT_STAGE_NORMALIZE_ENVIRONMENT = 2,
    RIBON_BOOT_STAGE_SELECT_SOURCE = 3,
    RIBON_BOOT_STAGE_VERIFY_MANIFEST = 4,
    RIBON_BOOT_STAGE_LOAD_IMAGE = 5,
    RIBON_BOOT_STAGE_PREPARE_PROTOCOL = 6,
    RIBON_BOOT_STAGE_COMMIT_ATTEMPT = 7,
    RIBON_BOOT_STAGE_QUIESCE_ENVIRONMENT = 8,
    RIBON_BOOT_STAGE_EXECUTE_TERMINAL = 9,
    RIBON_BOOT_STAGE_TRANSFER = 10,
    RIBON_BOOT_STAGE_FAILED = 11,
};

/** @brief Terminal failure receipt의 stable reason이다. */
enum RibonBootFailureReason {
    RIBON_BOOT_FAILURE_NONE = 0,
    RIBON_BOOT_FAILURE_BAD_INPUT = 1,
    RIBON_BOOT_FAILURE_PRODUCT = 2,
    RIBON_BOOT_FAILURE_BUDGET = 3,
    RIBON_BOOT_FAILURE_TIMEOUT = 4,
    RIBON_BOOT_FAILURE_SOURCE = 5,
    RIBON_BOOT_FAILURE_IMAGE = 6,
    RIBON_BOOT_FAILURE_PROTOCOL = 7,
    RIBON_BOOT_FAILURE_COMMIT = 8,
    RIBON_BOOT_FAILURE_QUIESCE = 9,
    RIBON_BOOT_FAILURE_TERMINAL = 10,
};

/** @brief Failure 뒤에도 native pointer 없이 보존되는 deterministic receipt다. */
struct RibonBootFailureReceipt {
    enum RibonBootLifecycleStage stage; /**< 실패한 stage다. */
    enum RibonBootFailureReason reason; /**< Stable failure reason이다. */
    const char *provider_id; /**< 실패를 보고한 static provider ID다. */
    uint64_t consumed_input_bytes; /**< 누적 source input byte다. */
    uint64_t consumed_output_bytes; /**< 누적 handoff output byte다. */
    uint32_t consumed_components; /**< Image component 소비 수다. */
    uint32_t consumed_retries; /**< 수행한 bounded retry 수다. */
};

/** @brief Caller가 source와 output storage를 transaction에 제공하는 immutable input이다. */
struct RibonBootTransactionInput {
    const struct RibonBootEnvironment *environment; /**< Capture할 environment facts다. */
    struct RibonMutableMemoryMap *normalized_memory_map; /**< Caller-owned normalized map이다. */
    const struct RibonBootSource *source; /**< 선택할 immutable boot source다. */
    uint64_t source_offset; /**< Source 안의 candidate 시작 byte다. */
    uint64_t source_size; /**< 읽을 candidate byte 수다. */
    void *payload_buffer; /**< Source read 결과를 받을 caller-owned buffer다. */
    uint64_t payload_buffer_capacity; /**< Payload buffer byte 상한이다. */
    const char *source_name; /**< Stable candidate 이름이다. */
    struct RibonValidatedImage *validated_image; /**< Caller-owned validation artifact다. */
    struct RibonDirectLoadPlan *direct_load_plan; /**< Direct model에서만 제공하는 plan이다. */
    void *handoff_buffer; /**< Protocol handoff 출력 buffer다. */
    uint64_t handoff_buffer_capacity; /**< Handoff buffer byte 상한이다. */
    struct RibonHandoffArtifact *handoff_artifact; /**< Protocol handoff 결과다. */
};

/** @brief Prepare가 caller-owned storage와 borrowed views로 만드는 immutable boot plan이다. */
struct RibonBootPlan {
    enum RibonEnvironmentKind environment;
    const struct RibonArchDescriptor *arch;
    uint32_t environment_flags;
    uint32_t memory_region_count;
    uint32_t normalized_memory_region_count;
    uint64_t usable_memory_bytes;
    enum RibonBootMediaKind boot_media;
    uint32_t boot_module_count;
    uint64_t device_tree_address;
    uint64_t device_tree_size;
    uint64_t framebuffer_address;
    const char *command_line;
    const char *protocol_id;
    const char *kernel_path;
    const char *kernel_source_name;
    struct RibonValidatedImage kernel_image;
    const struct RibonDirectLoadPlan *kernel_direct_load_plan;
    const char *handoff_format;
    uint32_t handoff_major;
    const char *handoff_artifact_format;
    uint64_t handoff_artifact_size;
    uint32_t handoff_artifact_sections;
    uint32_t kernel_load_segment_count;
    uint32_t kernel_load_plan_flags;
    uint64_t kernel_entry_point;
    uint64_t kernel_entry_load_address;
    uint64_t kernel_runtime_entry_address;
    uint64_t kernel_load_base;
    uint64_t kernel_load_end;
    uint64_t kernel_runtime_load_base;
    uint64_t kernel_runtime_load_end;
    uint64_t kernel_memory_size;
    uint64_t kernel_linked_virtual_base;
    uint64_t kernel_linked_virtual_end;
    uint64_t kernel_linked_physical_base;
    uint64_t kernel_linked_physical_end;
    uint64_t kernel_high_entry_virtual_address;
    uint64_t kernel_high_entry_load_address;
    const struct RibonLoadSegment *kernel_load_segments;
    uint32_t expectations;
};

/** @brief Validated Core와 selected service를 묶는 caller-owned boot transaction이다. */
struct RibonBootTransaction {
    uint32_t size; /**< Transaction byte 크기다. */
    uint32_t abi_version; /**< `RIBON_CORE_ABI_VERSION`과 일치해야 한다. */
    enum RibonBootLifecycleStage stage; /**< 마지막으로 성공한 lifecycle stage다. */
    const struct RibonCoreContext *core; /**< Validated Core context다. */
    const struct RibonArchOps *arch; /**< 선택한 architecture backend다. */
    const struct RibonBootProtocol *protocol; /**< 선택한 OS Boot Protocol이다. */
    const struct RibonImageFormatOps *image_format; /**< 선택한 image parser다. */
    const struct RibonServiceDescriptor *boot_source; /**< Selected source authority다. */
    const struct RibonServiceDescriptor *timer; /**< Deadline timer authority다. */
    const struct RibonServiceDescriptor *metadata; /**< Attempt metadata authority다. */
    const struct RibonServiceDescriptor *flush; /**< Metadata flush authority다. */
    const struct RibonServiceDescriptor *quiesce; /**< Environment closure authority다. */
    struct RibonBootEnvironment environment; /**< Frozen environment descriptor다. */
    struct RibonBootSource source; /**< Frozen selected source descriptor다. */
    struct RibonBootTransactionInput input; /**< Borrowed caller-owned transaction buffers다. */
    struct RibonPayloadImage payload; /**< Source read 뒤 immutable payload view다. */
    struct RibonValidatedImage validated_image; /**< Pointer-free image validation 결과다. */
    struct RibonBootPlan plan; /**< Prepared immutable boot plan이다. */
    struct RibonTerminalRequest terminal_request; /**< Protocol-owned terminal requirement다. */
    struct RibonPreparedEntry prepared_entry; /**< Architecture-owned sealed entry다. */
    struct RibonBootFailureReceipt receipt; /**< Terminal failure receipt다. */
    uint64_t consumed_input_bytes; /**< Runtime source byte 소비량이다. */
    uint64_t consumed_output_bytes; /**< Runtime handoff byte 소비량이다. */
    uint32_t consumed_components; /**< Runtime component 소비량이다. */
    uint32_t consumed_retries; /**< Runtime retry 소비량이다. */
};

/** @brief Validated Core와 selected operation으로 bounded transaction을 초기화한다. */
int ribon_boot_transaction_initialize(
    struct RibonBootTransaction *out,
    const struct RibonCoreContext *core,
    const struct RibonArchOps *arch,
    const struct RibonBootProtocol *protocol,
    const struct RibonImageFormatOps *image_format);

/**
 * @brief Input을 검증하고 durable state를 바꾸지 않는 immutable plan을 만든다.
 *
 * 성공하면 `PREPARE_PROTOCOL`까지 전진하고 failure는 terminal receipt로 고정된다.
 */
int ribon_boot_transaction_prepare(
    struct RibonBootTransaction *transaction,
    const struct RibonBootTransactionInput *input);

/** @brief Prepared attempt를 metadata write와 flush 뒤 durable하게 commit한다. */
int ribon_boot_transaction_commit_attempt(struct RibonBootTransaction *transaction);

/**
 * @brief Commit 뒤 갱신된 final platform fact로 handoff plan만 다시 만든다.
 *
 * Source, payload, protocol 선택은 바꾸지 않으며 transaction은 `COMMIT_ATTEMPT`를 유지한다.
 */
int ribon_boot_transaction_refresh_after_commit(
    struct RibonBootTransaction *transaction,
    const struct RibonBootEnvironment *environment);

/** @brief Selected environment closure operation을 실행하고 service lifetime을 닫는다. */
int ribon_boot_transaction_quiesce_environment(
    struct RibonBootTransaction *transaction);

/** @brief Prepared plan의 borrowed immutable view를 반환한다. */
const struct RibonBootPlan *ribon_boot_transaction_plan(
    const struct RibonBootTransaction *transaction);

/** @brief Terminal failure receipt의 borrowed immutable view를 반환한다. */
const struct RibonBootFailureReceipt *ribon_boot_transaction_failure_receipt(
    const struct RibonBootTransaction *transaction);

#endif
