#ifndef RIBON_ARCH_AARCH64_ENTRY_STATE_H
#define RIBON_ARCH_AARCH64_ENTRY_STATE_H

#include <stdint.h>

/** @brief AArch64 firmware-entry 관측과 post-exit bridge 계약 ABI다. */
#define RIBON_AARCH64_ENTRY_STATE_ABI_VERSION 1u

/** @brief Post-exit bridge에 게시할 수 있는 disjoint range 상한이다. */
#define RIBON_AARCH64_BRIDGE_RANGE_LIMIT 32u

/** @brief Root 한 page와 최대 여덟 L2 table을 위한 byte 수다. */
#define RIBON_AARCH64_BRIDGE_TABLE_BYTES (9u * 4096u)

/** @brief Post-exit bridge range의 memory attribute다. */
enum RibonAarch64BridgeMemoryKind {
    RIBON_AARCH64_BRIDGE_MEMORY_NORMAL = 0,
    RIBON_AARCH64_BRIDGE_MEMORY_DEVICE = 1,
};

/** @brief 한 physical identity range다. */
struct RibonAarch64BridgeRange {
    uint64_t base; /**< 4 KiB aligned physical 시작 주소다. */
    uint64_t length; /**< 4 KiB 배수인 nonzero 길이다. */
    enum RibonAarch64BridgeMemoryKind kind; /**< Stage-1 memory attribute다. */
};

/** @brief Caller-owned bridge range set이다. */
struct RibonAarch64BridgeRangeSet {
    struct RibonAarch64BridgeRange ranges[RIBON_AARCH64_BRIDGE_RANGE_LIMIT];
    uint32_t count;
};

/** @brief 현재 exception level에서만 읽은 active translation snapshot이다. */
struct RibonAarch64EntryState {
    uint32_t size;
    uint32_t abi_version;
    uint32_t current_el;
    uint32_t translation_enabled;
    uint64_t current_el_raw;
    uint64_t sctlr;
    uint64_t tcr;
    uint64_t mair;
    uint64_t ttbr0;
    uint64_t ttbr1;
    uint64_t vbar;
    uint64_t sp;
    uint64_t daif;
};

/** @brief Bounded identity bridge builder input이다. */
struct RibonAarch64BridgeRequest {
    uint32_t size;
    uint32_t abi_version;
    const struct RibonAarch64BridgeRange *ranges;
    uint32_t range_count;
    uint32_t reserved;
    void *table_buffer;
    uint64_t table_buffer_size;
};

/** @brief ExitBootServices 뒤 assembly가 소비하는 immutable transition plan이다. */
struct RibonAarch64PostExitTransition {
    uint64_t translation_root;
    uint64_t tcr_el1;
    uint64_t mair_el1;
    uint64_t sctlr_el1;
    uint64_t stack_top;
    void (*continuation)(void *context);
    void *context;
    uint32_t source_el;
    uint32_t mapped_l1_tables;
};

/** @brief Raw byte range를 page-aligned sorted/merged range set에 추가한다. */
int ribon_aarch64_bridge_range_add(
    struct RibonAarch64BridgeRangeSet *set,
    uint64_t base,
    uint64_t length,
    enum RibonAarch64BridgeMemoryKind kind);

/** @brief Model input을 현재-EL register snapshot으로 정규화한다. */
int ribon_aarch64_entry_state_from_registers(
    uint64_t current_el_raw,
    uint64_t sctlr,
    uint64_t tcr,
    uint64_t mair,
    uint64_t ttbr0,
    uint64_t ttbr1,
    uint64_t vbar,
    uint64_t sp,
    uint64_t daif,
    struct RibonAarch64EntryState *out);

/** @brief 실행 중인 EL에서 접근 가능한 active translation register만 읽는다. */
int ribon_aarch64_entry_state_capture(struct RibonAarch64EntryState *out);

/** @brief 현재 stage-1에서 virtual address가 같은 physical byte를 가리키는지 검사한다. */
int ribon_aarch64_entry_address_is_identity(
    const struct RibonAarch64EntryState *state,
    uint64_t address);

/** @brief Disjoint range만 mapping하는 bounded post-exit bridge를 만든다. */
int ribon_aarch64_prepare_post_exit_bridge(
    const struct RibonAarch64BridgeRequest *request,
    const struct RibonAarch64EntryState *entry,
    uint64_t stack_top,
    void (*continuation)(void *context),
    void *context,
    struct RibonAarch64PostExitTransition *out);

/**
 * @brief Firmware 종료 뒤 bridge를 설치하고 EL1h의 caller-owned stack으로 이동한다.
 *
 * 성공 시 반환하지 않는다. 이 함수 호출 전에는 firmware service를 다시 호출할 수
 * 있지만 호출 뒤에는 어떤 firmware-owned state도 사용할 수 없다.
 */
_Noreturn void ribon_aarch64_post_exit_transition(
    const struct RibonAarch64PostExitTransition *transition);

#endif
