#ifndef RIBON_PROTOCOL_ENTRY_CONTRACT_H
#define RIBON_PROTOCOL_ENTRY_CONTRACT_H

#include <stdint.h>

/** @brief Protocol-owned entry invocation ABI다. */
#define RIBON_ENTRY_INVOCATION_ABI_VERSION 1u

/** @brief 한 terminal entry에서 전달할 수 있는 register word 상한이다. */
#define RIBON_ENTRY_ARGUMENT_LIMIT 8u

/** @brief OS entry register 배치를 식별한다. */
enum RibonRegisterAbi {
    RIBON_REGISTER_ABI_X86_64_RDI_RSI_RDX_RCX = 0,
    RIBON_REGISTER_ABI_AARCH64_X0_X1_X2_X3 = 1,
    RIBON_REGISTER_ABI_RISCV64_A0_A1_A2_A3 = 2,
};

/** @brief OS entry가 요구하는 interrupt 상태다. */
enum RibonEntryInterruptRequirement {
    RIBON_ENTRY_INTERRUPTS_MASKED = 0,
};

/** @brief OS entry가 요구하는 privilege 정규화 수준이다. */
enum RibonEntryPrivilegeRequirement {
    RIBON_ENTRY_PRIVILEGE_CURRENT_SUPERVISOR = 0,
};

/** @brief OS entry가 요구하는 address-space bridge다. */
enum RibonEntryTranslationRequirement {
    RIBON_ENTRY_TRANSLATION_PRESERVE_REACHABLE = 0,
    RIBON_ENTRY_TRANSLATION_DIRECT_HIGH_BRIDGE = 1,
    RIBON_ENTRY_TRANSLATION_DISABLED = 2,
};

/**
 * @brief Boot Protocol이 완성하고 Architecture Backend가 소비하는 entry invocation이다.
 *
 * `arguments`의 의미는 protocol이 소유한다. Architecture Backend는 `register_abi`에 따라
 * word를 native register에 배치할 뿐 RPH1, DTB, ZBI 같은 wire 의미를 해석하지 않는다.
 */
struct RibonEntryInvocation {
    uint32_t size; /**< Descriptor byte 크기다. */
    uint32_t abi_version; /**< `RIBON_ENTRY_INVOCATION_ABI_VERSION`이다. */
    uint64_t entry_address; /**< Terminal branch target이다. */
    enum RibonRegisterAbi register_abi; /**< Native register layout이다. */
    uint32_t argument_count; /**< 유효한 `arguments` word 수다. */
    uint64_t arguments[RIBON_ENTRY_ARGUMENT_LIMIT]; /**< Protocol-owned register words다. */
    enum RibonEntryInterruptRequirement interrupts; /**< Entry interrupt precondition이다. */
    enum RibonEntryPrivilegeRequirement privilege; /**< Entry privilege precondition이다. */
    enum RibonEntryTranslationRequirement translation; /**< Entry translation precondition이다. */
};

/** @brief Architecture가 transfer 전에 봉인하는 ISA-owned entry state다. */
struct RibonPreparedEntry {
    struct RibonEntryInvocation invocation; /**< 검증된 protocol invocation 복사본이다. */
    uint64_t translation_root; /**< 0 또는 architecture bridge root다. */
};

#endif
