#ifndef RIBON_CORE_H
#define RIBON_CORE_H

#include <stdint.h>

struct RibonArchOps;
struct RibonPlatformOps;
struct RibonProfile;

/** @brief Ribon Core service ABI의 첫 번째 안정 버전이다. */
#define RIBON_CORE_ABI_VERSION 1u

/** @brief 한 mode descriptor가 표현할 수 있는 알려진 mode bit 전체다. */
#define RIBON_MODE_MASK_ALL ((1u << 4) - 1u)

/** @brief Ribon 실행 mode를 식별한다. */
enum RibonMode {
    RIBON_MODE_NORMAL = 0,
    RIBON_MODE_RECOVERY = 1,
    RIBON_MODE_PROVISIONING = 2,
    RIBON_MODE_DIAGNOSTIC = 3,
};

/** @brief Core service 경계 검증과 arena 연산의 결과다. */
enum RibonCoreStatus {
    RIBON_CORE_STATUS_OK = 0,
    RIBON_CORE_STATUS_BAD_ARGUMENT = -1,
    RIBON_CORE_STATUS_BAD_ABI = -2,
    RIBON_CORE_STATUS_BAD_MODE = -3,
    RIBON_CORE_STATUS_BAD_LIMIT = -4,
    RIBON_CORE_STATUS_OUT_OF_CAPACITY = -5,
    RIBON_CORE_STATUS_BAD_ALIGNMENT = -6,
    RIBON_CORE_STATUS_MISSING_CAPABILITY = -7,
    RIBON_CORE_STATUS_FORBIDDEN_CAPABILITY = -8,
    RIBON_CORE_STATUS_INVALID_OPERATION_TABLE = -9,
    RIBON_CORE_STATUS_INVALID_PROFILE = -10,
};

/**
 * @brief Caller-owned 고정 용량 bump arena다.
 *
 * Core는 base를 소유하거나 해제하지 않는다. Allocation은 반환 영역을 초기화하지
 * 않으며 성공한 순서대로만 증가한다.
 */
struct RibonArena {
    unsigned char *base; /**< Caller가 소유하는 storage 시작 주소다. */
    uint64_t capacity; /**< Arena가 사용할 수 있는 전체 byte 수다. */
    uint64_t used; /**< 정렬 padding을 포함해 소비한 byte 수다. */
    uint64_t high_watermark; /**< Arena 수명 동안의 최대 used 값이다. */
};

/** @brief 한 mode에서 Core가 넘을 수 없는 자원 상한이다. */
struct RibonResourceLimits {
    uint32_t max_memory_regions; /**< 정규화된 memory region 수 상한이다. */
    uint32_t max_load_segments; /**< Executable load segment 수 상한이다. */
    uint32_t max_components; /**< Manifest component 수 상한이다. */
    uint32_t max_retries; /**< 한 operation의 retry 수 상한이다. */
    uint64_t max_input_bytes; /**< 한 입력 artifact의 byte 상한이다. */
    uint64_t max_handoff_bytes; /**< Profile handoff output byte 상한이다. */
    uint64_t arena_bytes; /**< Core에 제공해야 하는 최소 arena byte 수다. */
    uint64_t operation_deadline_ms; /**< 한 operation의 duration 상한이다. */
};

/**
 * @brief Link object graph가 제공하는 mode와 capability 경계를 설명한다.
 *
 * required와 forbidden platform capability는 겹칠 수 없다.
 */
struct RibonModeDescriptor {
    uint32_t abi_version; /**< `RIBON_CORE_ABI_VERSION`과 일치해야 한다. */
    enum RibonMode mode; /**< Link object graph가 선택한 mode다. */
    const char *name; /**< Log와 evidence에 쓰는 고정 mode 이름이다. */
    uint64_t required_platform_capabilities; /**< 시작 전에 필요한 Platform bit다. */
    uint64_t forbidden_platform_capabilities; /**< Graph에 존재하면 안 되는 Platform bit다. */
    uint64_t required_arch_capabilities; /**< 시작 전에 필요한 Architecture bit다. */
    uint64_t forbidden_arch_capabilities; /**< Graph에 존재하면 안 되는 Architecture bit다. */
    struct RibonResourceLimits limits; /**< 이 mode의 고정 자원 상한이다. */
};

/** @brief Core가 시작 전에 함께 검증하는 service descriptor 묶음이다. */
struct RibonCoreContext {
    const struct RibonModeDescriptor *mode; /**< 선택된 link mode descriptor다. */
    const struct RibonPlatformOps *platform; /**< Adapter가 채운 Platform operation table이다. */
    const struct RibonArchOps *arch; /**< 선택된 Architecture operation table이다. */
    const struct RibonProfile *profile; /**< 선택된 immutable OS profile이다. */
    struct RibonArena *arena; /**< 비어 있고 capacity가 검증될 caller-owned arena다. */
};

/** @brief mode 값을 mode bitset으로 변환한다. */
#define RIBON_MODE_MASK(mode) (1u << (uint32_t)(mode))

/**
 * @brief Caller-owned storage로 빈 arena를 초기화한다.
 *
 * @param arena 초기화할 arena.
 * @param storage arena가 빌려 쓰는 storage 시작 주소.
 * @param capacity storage byte 수.
 */
void ribon_arena_init(struct RibonArena *arena, void *storage, uint64_t capacity);

/**
 * @brief 정렬 조건을 만족하는 고정 크기 영역을 arena에서 할당한다.
 *
 * @param arena 사용할 arena.
 * @param size 요청 byte 수. 0은 허용하지 않는다.
 * @param alignment 2의 거듭제곱 정렬. 0은 허용하지 않는다.
 * @param out 성공 시 할당 주소를 받는다.
 * @return `RibonCoreStatus`.
 */
int ribon_arena_allocate(
    struct RibonArena *arena,
    uint64_t size,
    uint64_t alignment,
    void **out);

/** @brief arena에 남은 byte 수를 반환한다. 잘못된 arena는 0을 반환한다. */
uint64_t ribon_arena_remaining(const struct RibonArena *arena);

/** @brief mode 열거값의 고정 문자열을 반환한다. */
const char *ribon_mode_name(enum RibonMode mode);

/** @brief resource limit의 모든 필드가 유효한 상한인지 검사한다. */
int ribon_resource_limits_are_valid(const struct RibonResourceLimits *limits);

/** @brief mode descriptor ABI, capability 집합, 자원 상한을 검사한다. */
int ribon_mode_descriptor_is_valid(const struct RibonModeDescriptor *mode);

/**
 * @brief 현재 link object graph가 선택한 단 하나의 mode descriptor를 반환한다.
 *
 * 같은 binary에 둘 이상의 `src/modes/` mode source를 링크하면 이 symbol이 충돌하므로 build가
 * 실패한다.
 */
const struct RibonModeDescriptor *ribon_mode_selected(void);

/**
 * @brief Core가 service를 호출하기 전에 전체 경계를 fail-closed로 검증한다.
 *
 * Arena capacity는 mode의 `arena_bytes` 이상이어야 하며 Platform/Profile/Architecture
 * operation table은 각 ABI와 capability 계약을 만족해야 한다.
 */
int ribon_core_context_validate(const struct RibonCoreContext *context);

#endif
