#ifndef RIBON_PROTOCOLS_OS_LINUX_FDT_H
#define RIBON_PROTOCOLS_OS_LINUX_FDT_H

#include <stdint.h>

/** @brief Linux handoff FDT builder가 받을 수 있는 root address-cell 수다. */
#define RIBON_LINUX_FDT_MIN_ADDRESS_CELLS 1u
#define RIBON_LINUX_FDT_MAX_ADDRESS_CELLS 2u

/**
 * @brief Validated source FDT를 compact하게 복사하고 선택적 initramfs를 게시한다.
 *
 * `initrd_size == 0`이면 initrd property를 생성하지 않는다. 그 외에는
 * `[initrd_start, initrd_start + initrd_size)`를 `/chosen`에 big-endian cell로
 * 직렬화한다. Source와 destination은 겹치면 안 된다.
 */
int ribon_linux_fdt_build(
    const void *source,
    uint64_t source_capacity,
    uint64_t initrd_start,
    uint64_t initrd_size,
    void *destination,
    uint64_t destination_capacity,
    uint64_t *output_size);

#endif
