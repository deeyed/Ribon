#ifndef RIBON_PROTOCOL_PROTOCOL_H
#define RIBON_PROTOCOL_PROTOCOL_H

#include <stdint.h>

#include <Ribon/arch/ops.h>
#include <Ribon/boot/image.h>
#include <Ribon/core/capability.h>
#include <Ribon/protocol/confirmation.h>
#include <Ribon/protocol/entry_contract.h>

struct RibonBootEnvironment;
struct RibonBootPlan;
struct RibonMutableMemoryMap;
struct RibonPluginDescriptor;

/** @brief Boot Protocol operation table ABI다. */
#define RIBON_BOOT_PROTOCOL_OPS_ABI_VERSION 4u

/** @brief Protocol terminal request ABI다. */
#define RIBON_TERMINAL_REQUEST_ABI_VERSION 1u

/** @brief OS-neutral terminal execution kind다. */
enum RibonTerminalExecutionKind {
    RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY = 0,
    RIBON_TERMINAL_EXECUTION_FIRMWARE_MANAGED_IMAGE = 1,
};

/** @brief Protocol이 Core에 반환하는 sealed terminal requirement다. */
struct RibonTerminalRequest {
    uint32_t size; /**< Descriptor byte 크기다. */
    uint32_t abi_version; /**< `RIBON_TERMINAL_REQUEST_ABI_VERSION`이다. */
    enum RibonTerminalExecutionKind kind; /**< 요청한 terminal model이다. */
    uint32_t reserved; /**< 반드시 0이다. */
    struct RibonEntryInvocation direct_entry; /**< Direct model에서만 유효하다. */
};

/** @brief Protocol이 environment에서 요구하거나 허용하는 input bit다. */
enum RibonProtocolExpectation {
    RIBON_PROTOCOL_EXPECT_MEMORY_MAP = 1u << 0,
    RIBON_PROTOCOL_EXPECT_KERNEL_IMAGE_LAYOUT = 1u << 1,
    RIBON_PROTOCOL_ALLOW_DEVICE_TREE = 1u << 2,
    RIBON_PROTOCOL_ALLOW_ACPI = 1u << 3,
    RIBON_PROTOCOL_ALLOW_BOOT_MODULES = 1u << 4,
    RIBON_PROTOCOL_ALLOW_DIRECT_HIGH_ENTRY = 1u << 5,
    RIBON_PROTOCOL_EXPECT_FRAMEBUFFER = 1u << 6,
};

/** @brief Component descriptor에서 허용하는 기본 flag 값이다. */
#define RIBON_COMPONENT_FLAGS_NONE 0u

/** @brief Boot Protocol semantic operation의 결과다. */
enum RibonProtocolStatus {
    RIBON_PROTOCOL_STATUS_OK = 0,
    RIBON_PROTOCOL_STATUS_BAD_ARGUMENT = -1,
    RIBON_PROTOCOL_STATUS_UNSUPPORTED = -2,
    RIBON_PROTOCOL_STATUS_BAD_MANIFEST = -3,
    RIBON_PROTOCOL_STATUS_BAD_COMPONENTS = -4,
    RIBON_PROTOCOL_STATUS_BAD_ENTRY_CONTRACT = -5,
    RIBON_PROTOCOL_STATUS_BAD_CONFIRMATION = -6,
};

/** @brief Protocol이 인식하는 component 역할이다. */
enum RibonComponentRole {
    RIBON_COMPONENT_ROLE_KERNEL = 0,
    RIBON_COMPONENT_ROLE_BOOT_MODULE = 1,
    RIBON_COMPONENT_ROLE_DEVICE_TREE = 2,
};

/** @brief 검증 대상 component의 typed descriptor다. */
struct RibonComponentDescriptor {
    enum RibonComponentRole role; /**< Protocol-defined 역할이다. */
    const char *name; /**< Parser가 빌려 주는 component 이름이다. */
    uint64_t size; /**< 검증된 component byte 수다. */
    uint32_t flags; /**< Protocol-defined flag다. */
};

/** @brief Wire parser가 검증한 protocol manifest view다. */
struct RibonManifestView {
    const char *protocol_id; /**< Manifest가 요구하는 protocol ID다. */
    uint32_t protocol_abi_min; /**< 허용 최소 protocol ABI다. */
    uint32_t protocol_abi_max; /**< 허용 최대 protocol ABI다. */
    const struct RibonComponentDescriptor *components; /**< Borrowed component array다. */
    uint32_t component_count; /**< Component element 수다. */
};

/** @brief Protocol handoff builder의 결과다. */
enum RibonProtocolHandoffStatus {
    RIBON_PROTOCOL_HANDOFF_STATUS_OK = 0,
    RIBON_PROTOCOL_HANDOFF_STATUS_BAD_ARGUMENT = -1,
    RIBON_PROTOCOL_HANDOFF_STATUS_OUT_OF_CAPACITY = -2,
    RIBON_PROTOCOL_HANDOFF_STATUS_UNSUPPORTED = -3,
    RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN = -4,
};

/** @brief Caller-owned buffer에 만들어진 handoff artifact view다. */
struct RibonHandoffArtifact {
    const void *data; /**< Caller-owned buffer 안의 artifact 시작이다. */
    uint64_t size; /**< 생성된 artifact byte 수다. */
    const char *format; /**< Protocol-owned format ID다. */
    uint32_t version_major; /**< Wire format major다. */
    uint32_t section_count; /**< Artifact section 수다. */
};

/** @brief Manifest ID와 protocol ABI 범위를 판별한다. */
typedef int (*RibonProtocolMatchFn)(const struct RibonManifestView *manifest);

/** @brief OS component 조합을 검증한다. */
typedef int (*RibonProtocolValidateComponentsFn)(const struct RibonManifestView *manifest);

/** @brief Protocol이 허용하는 image-format bitset을 반환한다. */
typedef uint64_t (*RibonProtocolSelectImageFormatsFn)(void);

/** @brief Validated plan에서 protocol-owned terminal requirement를 완성한다. */
typedef int (*RibonProtocolPrepareTerminalFn)(
    const struct RibonArchDescriptor *arch,
    const struct RibonBootPlan *plan,
    const struct RibonBootEnvironment *environment,
    const struct RibonHandoffArtifact *handoff,
    struct RibonTerminalRequest *out);

/** @brief Caller-owned buffer에 protocol handoff artifact를 생성한다. */
typedef int (*RibonProtocolPrepareHandoffFn)(
    const struct RibonBootPlan *plan,
    const struct RibonBootEnvironment *environment,
    const struct RibonMutableMemoryMap *normalized_memory_map,
    void *buffer,
    uint64_t capacity,
    struct RibonHandoffArtifact *out);

/** @brief Authenticated OS-specific health payload 의미를 검증한다. */
typedef int (*RibonProtocolValidateBootHealthFn)(
    const struct RibonBootHealthPayload *payload);

/** @brief 한 Boot Protocol의 immutable operation table이다. */
struct RibonBootProtocolOps {
    uint32_t size; /**< Operation table byte 크기다. */
    uint32_t abi_version; /**< `RIBON_BOOT_PROTOCOL_OPS_ABI_VERSION`과 일치해야 한다. */
    RibonProtocolMatchFn match; /**< Manifest match callback이다. */
    RibonProtocolValidateComponentsFn validate_components; /**< Component validator다. */
    RibonProtocolSelectImageFormatsFn select_image_formats; /**< Image allowlist callback이다. */
    RibonProtocolPrepareHandoffFn prepare_handoff; /**< Handoff serializer다. */
    RibonProtocolPrepareTerminalFn prepare_terminal; /**< Terminal requirement callback이다. */
    RibonProtocolValidateBootHealthFn validate_boot_health; /**< Health 의미 검증 callback이다. */
};

/** @brief OS 의미론을 Boot Library에서 분리하는 protocol descriptor다. */
struct RibonBootProtocol {
    uint32_t size; /**< Descriptor byte 크기다. */
    uint32_t abi_version; /**< Protocol semantic ABI다. */
    const char *id; /**< Stable protocol ID다. */
    const char *kernel_path; /**< Product-relative 기본 kernel source다. */
    enum RibonTerminalExecutionKind terminal_execution; /**< 요구 terminal model이다. */
    uint32_t expectations; /**< Environment input 요구와 허용 bit다. */
    uint32_t supported_modes; /**< 허용 mode bitset이다. */
    const char *handoff_format; /**< Handoff wire format ID다. */
    uint32_t handoff_major; /**< Handoff wire format major다. */
    const struct RibonBootProtocolOps *ops; /**< Immutable operation table이다. */
};

/** @brief Protocol descriptor와 callback 완전성을 검사한다. */
int ribon_boot_protocol_is_valid(const struct RibonBootProtocol *protocol);

/** @brief Terminal request의 kind별 불변식을 검사한다. */
int ribon_terminal_request_is_valid(const struct RibonTerminalRequest *request);

/** @brief Protocol expectation bit를 모두 가지는지 검사한다. */
int ribon_boot_protocol_has_expectation(
    const struct RibonBootProtocol *protocol,
    uint32_t expectation);

/** @brief Protocol confirmation callback을 fail-closed로 호출한다. */
int ribon_boot_protocol_validate_boot_health(
    const struct RibonBootProtocol *protocol,
    const struct RibonBootHealthPayload *payload);

/** @brief Boot Protocol plugin descriptor와 operation table을 함께 검사한다. */
int ribon_protocol_plugin_operations_are_valid(
    const struct RibonPluginDescriptor *descriptor);

/** @brief Synthetic contract-test Boot Protocol plugin descriptor다. */
extern const struct RibonPluginDescriptor ribon_synthetic_protocol_plugin_descriptor;

#endif
