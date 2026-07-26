#ifndef RIBON_BOOT_TRANSFER_H
#define RIBON_BOOT_TRANSFER_H

#include <Ribon/boot/plan.h>

/** @brief Environment service 종료와 final memory-map freeze 경계를 전진시킨다. */
int ribon_environment_quiesce(struct RibonBootSession *session);

/**
 * @brief Entry contract를 적용해 OS로 제어를 넘긴다.
 *
 * 이 함수는 성공 시 반환하지 않으며 quiesce 뒤 service callback을 호출하지 않는다.
 */
_Noreturn void ribon_boot_transfer(
    struct RibonBootSession *session,
    const struct RibonBootPlan *plan,
    uint64_t handoff_address,
    uint64_t entry_flags,
    uint64_t bootstrap0);

#endif
