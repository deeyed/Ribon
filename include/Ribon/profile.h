#ifndef RIBON_PROFILE_H
#define RIBON_PROFILE_H

#include <stdint.h>

#include <Ribon/core.h>

struct RibonArchDescriptor;
struct RibonBootEnvironment;
struct RibonBootPlan;
struct RibonMutableMemoryMap;

/** @brief Profile이 boot environment에서 요구하거나 허용하는 입력 bit다. */
#define RIBON_PROFILE_EXPECT_MEMORY_MAP (1u << 0)
#define RIBON_PROFILE_EXPECT_KERNEL_IMAGE_LAYOUT (1u << 1)
#define RIBON_PROFILE_ALLOW_DEVICE_TREE (1u << 2)
#define RIBON_PROFILE_ALLOW_ACPI (1u << 3)
#define RIBON_PROFILE_ALLOW_BOOT_MODULES (1u << 4)
#define RIBON_PROFILE_ALLOW_DIRECT_HIGH_ENTRY (1u << 5)
#define RIBON_PROFILE_EXPECT_FRAMEBUFFER (1u << 6)

/** @brief Profile operation table ABI의 첫 번째 안정 버전이다. */
#define RIBON_PROFILE_OPS_ABI_VERSION 1u

/** @brief Boot confirmation nonce의 고정 byte 수다. */
#define RIBON_BOOT_CONFIRMATION_NONCE_SIZE 32u

/** @brief R2 component descriptor에서 허용하는 유일한 flag 값이다. */
#define RIBON_COMPONENT_FLAGS_NONE 0u

/** @brief Profile capability bit다. */
enum RibonProfileCapability {
    RIBON_PROFILE_CAP_MANIFEST_MATCH = 1ull << 0,
    RIBON_PROFILE_CAP_COMPONENT_VALIDATION = 1ull << 1,
    RIBON_PROFILE_CAP_ENTRY_CONTRACT = 1ull << 2,
    RIBON_PROFILE_CAP_HANDOFF = 1ull << 3,
    RIBON_PROFILE_CAP_BOOT_CONFIRMATION = 1ull << 4,
};

/** @brief R2가 정의하는 Profile capability 전체다. */
#define RIBON_PROFILE_CAP_ALL ((1ull << 5) - 1ull)

/** @brief Profile semantic operation의 공통 결과다. */
enum RibonProfileStatus {
    RIBON_PROFILE_STATUS_OK = 0,
    RIBON_PROFILE_STATUS_BAD_ARGUMENT = -1,
    RIBON_PROFILE_STATUS_UNSUPPORTED = -2,
    RIBON_PROFILE_STATUS_BAD_MANIFEST = -3,
    RIBON_PROFILE_STATUS_BAD_COMPONENTS = -4,
    RIBON_PROFILE_STATUS_BAD_ENTRY_CONTRACT = -5,
    RIBON_PROFILE_STATUS_BAD_CONFIRMATION = -6,
};

/** @brief Profile이 인식하는 component 역할이다. */
enum RibonComponentRole {
    RIBON_COMPONENT_ROLE_KERNEL = 0,
    RIBON_COMPONENT_ROLE_BOOT_MODULE = 1,
    RIBON_COMPONENT_ROLE_DEVICE_TREE = 2,
};

/** @brief 검증 대상 component의 typed descriptor다. */
struct RibonComponentDescriptor {
    enum RibonComponentRole role; /**< Profile이 해석할 component 역할이다. */
    const char *name; /**< Manifest parser가 빌려 주는 component 이름이다. */
    uint64_t size; /**< 검증된 component byte 수다. */
    uint32_t flags; /**< R2에서는 `RIBON_COMPONENT_FLAGS_NONE`이어야 한다. */
};

/**
 * @brief Wire parser가 검증한 뒤 Profile에 넘기는 manifest view다.
 *
 * 이 구조체는 wire encoding이 아니며 모든 pointer lifetime은 호출자에게 있다.
 */
struct RibonManifestView {
    const char *profile_id; /**< Manifest가 요구하는 OS profile ID다. */
    uint32_t profile_abi_min; /**< Manifest가 허용하는 최소 profile ABI다. */
    uint32_t profile_abi_max; /**< Manifest가 허용하는 최대 profile ABI다. */
    const struct RibonComponentDescriptor *components; /**< 검증할 component array다. */
    uint32_t component_count; /**< Component array element 수다. */
};

/** @brief Kernel entry register ABI를 식별한다. */
enum RibonRegisterAbi {
    RIBON_REGISTER_ABI_X86_64_RDI_RSI = 0,
    RIBON_REGISTER_ABI_AARCH64_X0_X1 = 1,
    RIBON_REGISTER_ABI_RISCV64_A0_A1 = 2,
};

/** @brief Profile이 선택한 kernel entry 계약이다. */
struct RibonEntryContract {
    enum RibonRegisterAbi register_abi; /**< Kernel entry register 배치다. */
    uint64_t required_entry_flags; /**< 모든 entry에서 설정해야 하는 flag다. */
    uint64_t supported_entry_flags; /**< Profile이 해석할 수 있는 flag 전체다. */
};

/** @brief OS가 기록할 수 있는 boot confirmation 결과다. */
enum RibonBootConfirmationResult {
    RIBON_BOOT_CONFIRMATION_HEALTHY = 1,
};

/**
 * @brief Metadata parser가 검증한 boot confirmation의 semantic descriptor다.
 *
 * Durable wire encoding, checksum, 서명은 R4 update 계약이 소유한다.
 */
struct RibonBootConfirmation {
    const char *profile_id; /**< Confirmation을 생성한 OS profile ID다. */
    uint64_t generation; /**< 확인 대상 boot slot generation이다. */
    uint8_t nonce[RIBON_BOOT_CONFIRMATION_NONCE_SIZE]; /**< Boot attempt nonce다. */
    uint32_t nonce_size; /**< 유효 nonce byte 수며 반드시 고정 크기여야 한다. */
    enum RibonBootConfirmationResult result; /**< OS health service의 결과다. */
};

/** @brief 선택한 boot attempt와 confirmation을 묶는 기대값이다. */
struct RibonBootConfirmationExpectation {
    uint64_t generation; /**< 선택된 boot attempt generation이다. */
    uint8_t nonce[RIBON_BOOT_CONFIRMATION_NONCE_SIZE]; /**< 선택 시 생성한 nonce다. */
    uint32_t nonce_size; /**< 유효 nonce byte 수며 반드시 고정 크기여야 한다. */
};

/** @brief Profile handoff artifact 종류다. */
enum RibonHandoffKind {
    RIBON_HANDOFF_NONE = 0,
    RIBON_HANDOFF_PROFILE_DEFINED = 1,
};

/** @brief Profile handoff builder의 결과다. */
enum RibonProfileHandoffStatus {
    RIBON_PROFILE_HANDOFF_STATUS_OK = 0,
    RIBON_PROFILE_HANDOFF_STATUS_BAD_ARGUMENT = -1,
    RIBON_PROFILE_HANDOFF_STATUS_OUT_OF_CAPACITY = -2,
    RIBON_PROFILE_HANDOFF_STATUS_UNSUPPORTED = -3,
    RIBON_PROFILE_HANDOFF_STATUS_INVALID_PLAN = -4,
};

/** @brief Caller-owned buffer에 만들어진 handoff artifact view다. */
struct RibonHandoffArtifact {
    const void *data; /**< Caller-owned buffer 안의 artifact 시작 주소다. */
    uint64_t size; /**< 생성된 artifact byte 수다. */
    const char *format; /**< Profile이 소유하는 고정 format 이름이다. */
    uint32_t version_major; /**< Wire format major version이다. */
    uint32_t section_count; /**< Artifact에 기록된 section 수다. */
};

/** @brief Manifest profile ID와 ABI 범위를 판별하는 operation이다. */
typedef int (*RibonProfileMatchManifestFn)(const struct RibonManifestView *manifest);

/** @brief OS component 조합을 검증하는 operation이다. */
typedef int (*RibonProfileValidateComponentsFn)(const struct RibonManifestView *manifest);

/** @brief Architecture에 맞는 register/entry flag ABI를 선택하는 operation이다. */
typedef int (*RibonProfileSelectEntryContractFn)(
    const struct RibonArchDescriptor *arch,
    struct RibonEntryContract *out);

/** @brief Caller-owned buffer에 Profile handoff를 만드는 operation이다. */
typedef int (*RibonProfileBuildHandoffFn)(
    const struct RibonBootPlan *plan,
    const struct RibonBootEnvironment *environment,
    const struct RibonMutableMemoryMap *normalized_memory_map,
    void *buffer,
    uint64_t capacity,
    struct RibonHandoffArtifact *out);

/** @brief Boot generation과 nonce를 묶은 confirmation을 검증하는 operation이다. */
typedef int (*RibonProfileValidateConfirmationFn)(
    const struct RibonBootConfirmation *confirmation,
    const struct RibonBootConfirmationExpectation *expected);

/** @brief OS 의미론을 Core에서 분리하는 Profile operation table이다. */
struct RibonProfileOps {
    uint32_t abi_version; /**< `RIBON_PROFILE_OPS_ABI_VERSION`과 일치해야 한다. */
    RibonProfileMatchManifestFn match_manifest; /**< Manifest ID/ABI match callback이다. */
    RibonProfileValidateComponentsFn validate_components; /**< Component 조합 callback이다. */
    RibonProfileSelectEntryContractFn select_entry_contract; /**< Entry ABI 선택 callback이다. */
    RibonProfileBuildHandoffFn build_handoff; /**< Handoff artifact builder callback이다. */
    RibonProfileValidateConfirmationFn validate_confirmation; /**< Confirmation callback이다. */
};

/** @brief Builtin OS profile의 immutable descriptor다. */
struct RibonProfile {
    const char *name; /**< Builtin registry key와 manifest profile ID다. */
    const char *description; /**< 사람이 읽는 고정 설명이다. */
    const char *kernel_path; /**< 기본 boot source 안의 kernel 경로다. */
    uint32_t expectations; /**< Boot environment 요구 및 허용 bit다. */
    uint32_t supported_modes; /**< Profile을 사용할 수 있는 mode bitset이다. */
    uint64_t capabilities; /**< 제공하는 Profile operation bitset이다. */
    enum RibonHandoffKind handoff; /**< Handoff artifact 종류다. */
    const char *handoff_name; /**< Profile-defined handoff format 이름이다. */
    uint32_t handoff_major; /**< Profile-defined handoff major version이다. */
    const struct RibonProfileOps *ops; /**< Immutable operation table이다. */
};

/** @brief Builtin Parus profile descriptor를 반환한다. */
const struct RibonProfile *ribon_profile_parus(void);

/** @brief 이름과 일치하는 builtin profile을 반환한다. */
const struct RibonProfile *ribon_find_builtin_profile(const char *name);

/** @brief handoff 종류의 고정 문자열을 반환한다. */
const char *ribon_handoff_name(enum RibonHandoffKind handoff);

/** @brief Profile이 요청한 expectation bit를 모두 가지는지 검사한다. */
int ribon_profile_has_expectation(const struct RibonProfile *profile, uint32_t expectation);

/** @brief Profile이 요청한 capability bit를 모두 가지는지 검사한다. */
int ribon_profile_has_capability(const struct RibonProfile *profile, uint64_t capability);

/** @brief Profile descriptor, capability, operation table의 일관성을 검사한다. */
int ribon_profile_is_valid(const struct RibonProfile *profile);

/** @brief Profile operation을 통해 boot confirmation을 fail-closed로 검증한다. */
int ribon_profile_validate_confirmation(
    const struct RibonProfile *profile,
    const struct RibonBootConfirmation *confirmation,
    const struct RibonBootConfirmationExpectation *expected);

#endif
