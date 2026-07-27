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
};

/** @brief Prepare부터 terminal transfer까지의 단방향 session state다. */
enum RibonBootSessionState {
    RIBON_BOOT_SESSION_INITIALIZED = 0,
    RIBON_BOOT_SESSION_PREPARED = 1,
    RIBON_BOOT_SESSION_COMMITTED = 2,
    RIBON_BOOT_SESSION_QUIESCED = 3,
    RIBON_BOOT_SESSION_TRANSFERRED = 4,
};

/** @brief Validated Core, service, architecture와 protocol을 묶는 boot session이다. */
struct RibonBootSession {
    uint32_t size; /**< Session byte 크기다. */
    uint32_t abi_version; /**< `RIBON_CORE_ABI_VERSION`과 일치해야 한다. */
    enum RibonBootSessionState state; /**< 단방향 lifecycle state다. */
    const struct RibonCoreContext *core; /**< Validated Core context다. */
    const struct RibonServiceDirectory *services; /**< Generated typed service directory다. */
    const struct RibonArchOps *arch; /**< 선택한 architecture backend다. */
    const struct RibonBootProtocol *protocol; /**< 선택한 OS Boot Protocol이다. */
    const struct RibonImageFormatOps *image_format; /**< 선택한 image parser다. */
};

/** @brief Caller가 Boot Library prepare에 제공하는 immutable input이다. */
struct RibonBootRequest {
    const struct RibonBootEnvironment *environment;
    struct RibonMutableMemoryMap *normalized_memory_map;
    const struct RibonPayloadImage *kernel_payload;
    struct RibonLoadedPayload *kernel_layout;
    void *handoff_buffer;
    uint64_t handoff_buffer_capacity;
    struct RibonHandoffArtifact *handoff_artifact;
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
    const char *handoff_format;
    uint32_t handoff_major;
    const char *handoff_artifact_format;
    uint64_t handoff_artifact_size;
    uint32_t handoff_artifact_sections;
    enum RibonExecutableFormat kernel_format;
    uint16_t kernel_machine;
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
    struct RibonEntryContract entry_contract;
};

/** @brief Validated Core와 selected plugin operation으로 새 boot session을 만든다. */
int ribon_boot_session_initialize(
    struct RibonBootSession *out,
    const struct RibonCoreContext *core,
    const struct RibonArchOps *arch,
    const struct RibonBootProtocol *protocol,
    const struct RibonImageFormatOps *image_format);

/**
 * @brief Input을 검증하고 durable state를 바꾸지 않는 immutable plan을 만든다.
 *
 * 성공하면 session은 `PREPARED`가 되며 실패하면 `INITIALIZED`를 유지한다.
 */
int ribon_boot_prepare(
    struct RibonBootSession *session,
    const struct RibonBootRequest *request,
    struct RibonBootPlan *out);

/** @brief Prepared attempt의 durable metadata commit 경계를 전진시킨다. */
int ribon_boot_commit(struct RibonBootSession *session);

/**
 * @brief Commit 뒤 갱신된 final platform fact로 handoff plan만 다시 만든다.
 *
 * Source, payload, protocol 선택은 바꾸지 않으며 session은 `COMMITTED`를 유지한다.
 */
int ribon_boot_refresh_after_commit(
    struct RibonBootSession *session,
    const struct RibonBootRequest *request,
    struct RibonBootPlan *out);

#endif
