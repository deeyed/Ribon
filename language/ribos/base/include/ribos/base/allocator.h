#ifndef RIBOS_BASE_ALLOCATOR_H
#define RIBOS_BASE_ALLOCATOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file allocator.h
 * @brief Caller가 allocation authority를 주입하는 target-neutral 계약.
 */

/** Allocation backend의 한 번 할당 연산이다. */
typedef void *(*RibosAllocateFunction)(
    void *context,
    size_t size,
    size_t alignment);

/** Allocation backend의 크기 변경 연산이다. */
typedef void *(*RibosResizeFunction)(
    void *context,
    void *storage,
    size_t old_size,
    size_t new_size,
    size_t alignment);

/** Allocation backend의 해제 연산이다. */
typedef void (*RibosDeallocateFunction)(
    void *context,
    void *storage,
    size_t size,
    size_t alignment);

/**
 * Frontend와 Policy IR에 주입되는 allocation authority다.
 *
 * 이 descriptor는 architecture와 libc를 알지 않으며 artifact 또는 VM ABI가 아니다.
 */
typedef struct RibosAllocator {
    void *context;
    RibosAllocateFunction allocate;
    RibosResizeFunction resize;
    RibosDeallocateFunction deallocate;
} RibosAllocator;

/** Allocator descriptor와 alignment를 검사한 뒤 storage를 할당한다. */
void *ribos_allocator_allocate(
    const RibosAllocator *allocator,
    size_t size,
    size_t alignment);

/** Checked `count * element_size` 크기의 zeroed storage를 할당한다. */
void *ribos_allocator_allocate_zeroed(
    const RibosAllocator *allocator,
    size_t count,
    size_t element_size,
    size_t alignment);

/** 기존 storage의 크기를 backend authority로 변경한다. */
void *ribos_allocator_resize(
    const RibosAllocator *allocator,
    void *storage,
    size_t old_size,
    size_t new_size,
    size_t alignment);

/** NULL을 허용하며 storage를 동일 allocator authority에 반환한다. */
void ribos_allocator_release(
    const RibosAllocator *allocator,
    void *storage,
    size_t size,
    size_t alignment);

#ifdef __cplusplus
}
#endif

#endif
