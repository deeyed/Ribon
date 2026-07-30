#ifndef RIBOS_HOST_ALLOCATOR_H
#define RIBOS_HOST_ALLOCATOR_H

#include "ribos/base/allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file allocator.h
 * @brief Hosted C allocation을 Ribos compiler authority로 감싸는 adapter.
 */

/** C hosted allocator를 사용하는 immutable compiler allocator를 반환한다. */
const RibosAllocator *ribos_host_allocator(void);

#ifdef __cplusplus
}
#endif

#endif
