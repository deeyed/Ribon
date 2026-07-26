#ifndef RIBON_ARCH_H
#define RIBON_ARCH_H

#include <stdint.h>

#define RIBON_ARCH_FIRMWARE_HOST_TEST (1u << 0)
#define RIBON_ARCH_FIRMWARE_UEFI (1u << 1)
#define RIBON_ARCH_FIRMWARE_BIOS (1u << 2)
#define RIBON_ARCH_FIRMWARE_RASPBERRY_PI (1u << 3)

#define RIBON_KERNEL_ENTRY_FLAG_RPH1 (1ull << 0)
#define RIBON_KERNEL_ENTRY_FLAG_DIRECT_DTB (1ull << 1)
#define RIBON_KERNEL_ENTRY_FLAG_ENTERED_HIGH (1ull << 2)
#define RIBON_KERNEL_ENTRY_FLAG_DIRECT_HIGH (1ull << 3)

/** @brief Architecture operation table ABI의 첫 번째 안정 버전이다. */
#define RIBON_ARCH_OPS_ABI_VERSION 1u

#if defined(__GNUC__) || defined(__clang__)
#define RIBON_NORETURN __attribute__((noreturn))
#else
#define RIBON_NORETURN
#endif

struct RibonLoadedPayload;
struct RibonArchOps;

enum RibonArchTier {
    RIBON_ARCH_TIER_PRIMARY = 0,
    RIBON_ARCH_TIER_FUTURE = 1,
};

enum RibonArchEndian {
    RIBON_ARCH_ENDIAN_LITTLE = 0,
};

struct RibonArchDescriptor {
    const char *canonical_name;
    const char *family_name;
    const char *uefi_binding_dir;
    uint32_t word_bits;
    uint32_t physical_address_bits;
    uint32_t virtual_address_bits;
    uint64_t page_size;
    uint64_t large_page_size;
    uint64_t kernel_alignment;
    uint64_t handoff_alignment;
    uint64_t boot_module_alignment;
    enum RibonArchEndian endian;
    enum RibonArchTier tier;
    uint32_t firmware_mask;
};

enum RibonArchDirectHighStatus {
    RIBON_ARCH_DIRECT_HIGH_OK = 0,
    RIBON_ARCH_DIRECT_HIGH_UNSUPPORTED = -1,
    RIBON_ARCH_DIRECT_HIGH_BAD_ARGUMENT = -2,
    RIBON_ARCH_DIRECT_HIGH_BAD_LAYOUT = -3,
    RIBON_ARCH_DIRECT_HIGH_OUT_OF_CAPACITY = -4,
};

/** @brief Architecture backend가 명시하는 operation capability다. */
enum RibonArchCapability {
    RIBON_ARCH_CAP_VALIDATE_PAYLOAD = 1ull << 0,
    RIBON_ARCH_CAP_CACHE_SYNC = 1ull << 1,
    RIBON_ARCH_CAP_PRIVILEGE_NORMALIZE = 1ull << 2,
    RIBON_ARCH_CAP_DIRECT_HIGH_ENTRY = 1ull << 3,
    RIBON_ARCH_CAP_ENTRY_BRIDGE = 1ull << 4,
    RIBON_ARCH_CAP_HALT = 1ull << 5,
    RIBON_ARCH_CAP_RESET = 1ull << 6,
};

/** @brief R2가 정의하는 Architecture capability 전체다. */
#define RIBON_ARCH_CAP_ALL ((1ull << 7) - 1ull)

/** @brief Architecture operation의 공통 결과다. */
enum RibonArchOperationStatus {
    RIBON_ARCH_OPERATION_OK = 0,
    RIBON_ARCH_OPERATION_BAD_ARGUMENT = -1,
    RIBON_ARCH_OPERATION_UNSUPPORTED = -2,
    RIBON_ARCH_OPERATION_INVALID_PAYLOAD = -3,
};

struct RibonArchDirectHighHandoff {
    uint64_t entry;
    uint64_t entry_flags;
    uint64_t bootstrap0;
    uint64_t high_entry_load;
    uint64_t high_vaddr_start;
    uint64_t high_vaddr_end;
    uint64_t high_load_start;
    uint64_t high_load_end;
};

/** @brief Loaded payload 구조와 machine/canonical address를 검증하는 operation이다. */
typedef int (*RibonArchValidatePayloadFn)(
    const struct RibonArchDescriptor *arch,
    const struct RibonLoadedPayload *payload);

/** @brief Data와 instruction view를 동기화하는 operation이다. */
typedef int (*RibonArchCacheSyncFn)(uint64_t address, uint64_t size);

/** @brief Kernel entry privilege state를 정규화하는 operation이다. */
typedef int (*RibonArchNormalizePrivilegeFn)(void);

/** @brief Direct-high page table 상한을 계산하는 operation이다. */
typedef uint64_t (*RibonArchDirectHighPagesFn)(const struct RibonLoadedPayload *payload);

/** @brief Direct-high entry state를 만드는 operation이다. */
typedef int (*RibonArchPrepareDirectHighFn)(
    const struct RibonLoadedPayload *payload,
    uint64_t page_table_physical_address,
    void *page_table_buffer,
    uint64_t page_table_size,
    struct RibonArchDirectHighHandoff *out);

/** @brief Register ABI를 적용하고 kernel로 제어를 넘기는 operation이다. */
typedef void (*RibonArchEnterKernelFn)(
    uint64_t entry,
    uint64_t handoff,
    uint64_t entry_flags,
    uint64_t bootstrap0);

/** @brief CPU를 terminal halt 상태로 전환하는 operation이다. */
typedef void (*RibonArchHaltFn)(void);

/** @brief Architecture reset primitive operation이다. */
typedef int (*RibonArchResetFn)(uint32_t reason);

/**
 * @brief Core가 사용할 Architecture operation table이다.
 *
 * Capability bit가 설정된 operation만 호출할 수 있다. `validate_payload`와 `halt`는
 * 모든 지원 backend에서 필수다.
 */
struct RibonArchOps {
    uint32_t abi_version; /**< `RIBON_ARCH_OPS_ABI_VERSION`과 일치해야 한다. */
    uint64_t capabilities; /**< 제공하는 Architecture operation bitset이다. */
    const struct RibonArchDescriptor *descriptor; /**< Backend의 immutable 사실이다. */
    RibonArchValidatePayloadFn validate_payload; /**< Machine/canonical 검증 callback이다. */
    RibonArchCacheSyncFn cache_sync; /**< Instruction view 동기화 callback이다. */
    RibonArchNormalizePrivilegeFn normalize_privilege; /**< Privilege 정규화 callback이다. */
    RibonArchDirectHighPagesFn direct_high_page_table_pages; /**< Page 수 계산 callback이다. */
    RibonArchPrepareDirectHighFn prepare_direct_high_entry; /**< Direct-high 준비 callback이다. */
    RibonArchEnterKernelFn enter_kernel; /**< Register ABI와 jump callback이다. */
    RibonArchHaltFn halt; /**< Terminal halt callback이다. */
    RibonArchResetFn reset; /**< Architecture reset callback이다. */
};

/** @brief 현재 build가 선택한 architecture descriptor를 반환한다. */
const struct RibonArchDescriptor *ribon_arch_selected(void);

/** @brief 현재 build가 선택한 architecture operation table을 반환한다. */
const struct RibonArchOps *ribon_arch_selected_ops(void);

/** @brief Architecture tier 열거값의 고정 문자열을 반환한다. */
const char *ribon_arch_tier_name(enum RibonArchTier tier);

/** @brief Architecture endian 열거값의 고정 문자열을 반환한다. */
const char *ribon_arch_endian_name(enum RibonArchEndian endian);

/** @brief Architecture가 요청한 firmware mask를 모두 지원하는지 검사한다. */
int ribon_arch_has_firmware_mask(const struct RibonArchDescriptor *arch, uint32_t mask);

/** @brief Architecture operation table ABI와 필수 callback을 검사한다. */
int ribon_arch_ops_are_valid(const struct RibonArchOps *ops);

/** @brief Loaded payload의 공통 machine/canonical address 계약을 검사한다. */
int ribon_arch_validate_loaded_payload(
    const struct RibonArchDescriptor *arch,
    const struct RibonLoadedPayload *payload);

/** @brief Direct-high page table에 필요한 page 수를 반환한다. */
uint64_t ribon_arch_direct_high_page_table_pages(const struct RibonLoadedPayload *payload);

/** @brief Caller-owned page table buffer에 direct-high entry state를 만든다. */
int ribon_arch_prepare_direct_high_entry(
    const struct RibonLoadedPayload *payload,
    uint64_t page_table_physical_address,
    void *page_table_buffer,
    uint64_t page_table_size,
    struct RibonArchDirectHighHandoff *out);

/** @brief Architecture register ABI를 적용하고 kernel로 제어를 넘긴다. */
RIBON_NORETURN void ribon_arch_enter_kernel(
    uint64_t entry,
    uint64_t handoff,
    uint64_t entry_flags,
    uint64_t bootstrap0);

#endif
