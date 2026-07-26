#ifndef RIBON_PROTOCOL_ENTRY_CONTRACT_H
#define RIBON_PROTOCOL_ENTRY_CONTRACT_H

#include <stdint.h>

/** @brief OS entry register 배치를 식별한다. */
enum RibonRegisterAbi {
    RIBON_REGISTER_ABI_X86_64_RDI_RSI = 0,
    RIBON_REGISTER_ABI_AARCH64_X0_X1 = 1,
    RIBON_REGISTER_ABI_RISCV64_A0_A1 = 2,
};

/** @brief Protocol과 architecture가 합의한 entry precondition이다. */
struct RibonEntryContract {
    enum RibonRegisterAbi register_abi; /**< Register 배치다. */
    uint64_t required_entry_flags; /**< 항상 설정해야 하는 flag다. */
    uint64_t supported_entry_flags; /**< Protocol이 해석하는 flag 전체다. */
};

#endif
