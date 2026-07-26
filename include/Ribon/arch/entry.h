#ifndef RIBON_ARCH_ENTRY_H
#define RIBON_ARCH_ENTRY_H

#include <stdint.h>

/** @brief Protocol과 architecture가 합의하는 OS entry flag다. */
enum RibonKernelEntryFlag {
    RIBON_KERNEL_ENTRY_FLAG_RPH1 = 1ull << 0,
    RIBON_KERNEL_ENTRY_FLAG_DIRECT_DTB = 1ull << 1,
    RIBON_KERNEL_ENTRY_FLAG_ENTERED_HIGH = 1ull << 2,
    RIBON_KERNEL_ENTRY_FLAG_DIRECT_HIGH = 1ull << 3,
};

/** @brief Architecture register ABI를 적용하고 반환하지 않는 transfer를 수행한다. */
_Noreturn void ribon_arch_enter_kernel(
    uint64_t entry,
    uint64_t handoff,
    uint64_t entry_flags,
    uint64_t bootstrap0);

#endif
