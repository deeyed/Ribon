#ifndef RIBON_ARCH_OPS_H
#define RIBON_ARCH_OPS_H

#include <stdint.h>

#include <Ribon/protocol/entry_contract.h>

struct RibonLoadedPayload;
struct RibonPluginDescriptor;

/** @brief Architecture operation table ABI다. */
#define RIBON_ARCH_OPS_ABI_VERSION 2u

/** @brief Ribon architecture의 stable ID다. */
enum RibonArchitectureId {
    RIBON_ARCHITECTURE_X86_64 = 0,
    RIBON_ARCHITECTURE_AARCH64 = 1,
    RIBON_ARCHITECTURE_RISCV64 = 2,
};

/** @brief Architecture 구현의 support tier다. */
enum RibonArchTier {
    RIBON_ARCH_TIER_PRIMARY = 0,
    RIBON_ARCH_TIER_FUTURE = 1,
};

/** @brief Architecture byte order다. */
enum RibonArchEndian {
    RIBON_ARCH_ENDIAN_LITTLE = 0,
};

/** @brief Architecture와 address-space의 immutable 사실이다. */
struct RibonArchDescriptor {
    uint32_t size; /**< Descriptor byte 크기다. */
    uint32_t abi_version; /**< `RIBON_ARCH_OPS_ABI_VERSION`과 일치해야 한다. */
    enum RibonArchitectureId id; /**< Stable architecture ID다. */
    const char *canonical_name; /**< Target tuple에서 쓰는 canonical ID다. */
    const char *family_name; /**< Architecture family 이름이다. */
    uint32_t word_bits; /**< Native register bit 수다. */
    uint32_t physical_address_bits; /**< 지원 physical address bit 수다. */
    uint32_t virtual_address_bits; /**< 지원 virtual address bit 수다. */
    uint64_t page_size; /**< Base page byte 수다. */
    uint64_t large_page_size; /**< Preferred large page byte 수다. */
    uint64_t kernel_alignment; /**< Kernel image 최소 alignment다. */
    uint64_t handoff_alignment; /**< Handoff artifact 최소 alignment다. */
    uint64_t boot_module_alignment; /**< Boot module 최소 alignment다. */
    enum RibonArchEndian endian; /**< Architecture byte order다. */
    enum RibonArchTier tier; /**< Support tier다. */
};

/** @brief Direct-high preparation의 결과다. */
enum RibonArchDirectHighStatus {
    RIBON_ARCH_DIRECT_HIGH_OK = 0,
    RIBON_ARCH_DIRECT_HIGH_UNSUPPORTED = -1,
    RIBON_ARCH_DIRECT_HIGH_BAD_ARGUMENT = -2,
    RIBON_ARCH_DIRECT_HIGH_BAD_LAYOUT = -3,
    RIBON_ARCH_DIRECT_HIGH_OUT_OF_CAPACITY = -4,
};

/** @brief Architecture operation capability다. */
enum RibonArchCapability {
    RIBON_ARCH_CAP_VALIDATE_PAYLOAD = 1ull << 0,
    RIBON_ARCH_CAP_CACHE_SYNC = 1ull << 1,
    RIBON_ARCH_CAP_PRIVILEGE_NORMALIZE = 1ull << 2,
    RIBON_ARCH_CAP_DIRECT_HIGH_ENTRY = 1ull << 3,
    RIBON_ARCH_CAP_ENTRY_BRIDGE = 1ull << 4,
    RIBON_ARCH_CAP_HALT = 1ull << 5,
    RIBON_ARCH_CAP_RESET = 1ull << 6,
    RIBON_ARCH_CAP_MONOTONIC_COUNTER = 1ull << 7,
};

/** @brief 알려진 architecture operation bit 전체다. */
#define RIBON_ARCH_CAP_ALL ((1ull << 8) - 1ull)

/** @brief Architecture operation의 결과다. */
enum RibonArchOperationStatus {
    RIBON_ARCH_OPERATION_OK = 0,
    RIBON_ARCH_OPERATION_BAD_ARGUMENT = -1,
    RIBON_ARCH_OPERATION_UNSUPPORTED = -2,
    RIBON_ARCH_OPERATION_INVALID_PAYLOAD = -3,
};

/** @brief Direct-high transfer에 필요한 caller-owned 결과다. */
struct RibonArchDirectHighHandoff {
    uint64_t entry; /**< 실제 branch target이다. */
    uint64_t translation_root; /**< Architecture transition table root다. */
    uint64_t high_entry_load; /**< High entry의 physical alias다. */
    uint64_t high_vaddr_start; /**< High virtual range 시작이다. */
    uint64_t high_vaddr_end; /**< High virtual range 끝이다. */
    uint64_t high_load_start; /**< Physical load range 시작이다. */
    uint64_t high_load_end; /**< Physical load range 끝이다. */
};

/** @brief Loaded payload의 machine과 canonical address를 검증한다. */
typedef int (*RibonArchValidatePayloadFn)(
    const struct RibonArchDescriptor *arch,
    const struct RibonLoadedPayload *payload);

/** @brief Data와 instruction view를 동기화한다. */
typedef int (*RibonArchCacheSyncFn)(uint64_t address, uint64_t size);

/** @brief Kernel entry privilege state를 정규화한다. */
typedef int (*RibonArchNormalizePrivilegeFn)(void);

/** @brief Direct-high page table page 상한을 계산한다. */
typedef uint64_t (*RibonArchDirectHighPagesFn)(const struct RibonLoadedPayload *payload);

/** @brief Caller-owned buffer에 direct-high entry state를 만든다. */
typedef int (*RibonArchPrepareDirectHighFn)(
    const struct RibonLoadedPayload *payload,
    uint64_t page_table_physical_address,
    void *page_table_buffer,
    uint64_t page_table_size,
    struct RibonArchDirectHighHandoff *out);

/** @brief Protocol invocation을 ISA-owned prepared entry로 검증한다. */
typedef int (*RibonArchPrepareEntryFn)(
    const struct RibonArchDescriptor *,
    const struct RibonEntryInvocation *,
    struct RibonPreparedEntry *);

/** @brief Prepared register ABI를 적용하고 OS entry로 제어를 넘긴다. */
typedef void (*RibonArchTransferPreparedFn)(
    const struct RibonPreparedEntry *);

/** @brief CPU를 terminal halt 상태로 전환한다. */
typedef void (*RibonArchHaltFn)(void);

/** @brief Architecture reset primitive를 요청한다. */
typedef int (*RibonArchResetFn)(uint32_t reason);

/** @brief Preboot에서 읽을 수 있는 architecture monotonic counter다. */
typedef uint64_t (*RibonArchMonotonicCounterFn)(void);

/** @brief 한 architecture backend의 immutable operation table이다. */
struct RibonArchOps {
    uint32_t size; /**< Operation table byte 크기다. */
    uint32_t abi_version; /**< `RIBON_ARCH_OPS_ABI_VERSION`과 일치해야 한다. */
    uint64_t capabilities; /**< 제공 operation bitset이다. */
    const struct RibonArchDescriptor *descriptor; /**< Immutable architecture facts다. */
    RibonArchValidatePayloadFn validate_payload; /**< Payload validator다. */
    RibonArchCacheSyncFn cache_sync; /**< Cache synchronization callback이다. */
    RibonArchNormalizePrivilegeFn normalize_privilege; /**< Privilege callback이다. */
    RibonArchDirectHighPagesFn direct_high_page_table_pages; /**< Page budget callback이다. */
    RibonArchPrepareDirectHighFn prepare_direct_high_entry; /**< Direct-high callback이다. */
    RibonArchPrepareEntryFn prepare_entry; /**< Invocation validator다. */
    RibonArchTransferPreparedFn transfer_prepared; /**< Terminal transfer callback이다. */
    RibonArchHaltFn halt; /**< Terminal halt callback이다. */
    RibonArchResetFn reset; /**< Optional reset callback이다. */
    RibonArchMonotonicCounterFn monotonic_counter; /**< Allocation-free counter read다. */
};

/** @brief Build target가 선택한 architecture descriptor를 반환한다. */
const struct RibonArchDescriptor *ribon_arch_selected(void);

/** @brief Build target가 선택한 architecture operation table을 반환한다. */
const struct RibonArchOps *ribon_arch_selected_ops(void);

/** @brief Build target가 registry에 제공하는 architecture plugin descriptor다. */
extern const struct RibonPluginDescriptor ribon_arch_plugin_descriptor;

/** @brief Architecture tier의 안정적인 이름을 반환한다. */
const char *ribon_arch_tier_name(enum RibonArchTier tier);

/** @brief Architecture endian의 안정적인 이름을 반환한다. */
const char *ribon_arch_endian_name(enum RibonArchEndian endian);

/** @brief Architecture ID를 product compatibility bit로 변환한다. */
uint32_t ribon_architecture_mask(enum RibonArchitectureId architecture);

/** @brief Architecture operation table의 capability와 callback을 검사한다. */
int ribon_arch_ops_are_valid(const struct RibonArchOps *ops);

/** @brief Architecture plugin descriptor와 operation table을 함께 검사한다. */
int ribon_arch_plugin_operations_are_valid(
    const struct RibonPluginDescriptor *descriptor);

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

#endif
