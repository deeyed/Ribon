#ifndef RIBON_ARCH_ENTRY_H
#define RIBON_ARCH_ENTRY_H

#include <stdint.h>

#include <Ribon/protocol/entry_contract.h>

struct RibonArchDescriptor;

/** @brief Protocol invocation을 selected architecture의 prepared entry로 검증한다. */
int ribon_arch_prepare_entry(
    const struct RibonArchDescriptor *arch,
    const struct RibonEntryInvocation *invocation,
    struct RibonPreparedEntry *out);

/** @brief Prepared register ABI를 적용하고 반환하지 않는 transfer를 수행한다. */
_Noreturn void ribon_arch_transfer_prepared(
    const struct RibonPreparedEntry *prepared);

#endif
