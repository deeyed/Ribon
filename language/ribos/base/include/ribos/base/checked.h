#ifndef RIBOS_BASE_CHECKED_H
#define RIBOS_BASE_CHECKED_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file checked.h
 * @brief Target-safe size, offset와 alignment 산술 계약.
 *
 * 이 API는 allocation을 수행하지 않는다. `size_t` 함수는 process-local storage를,
 * `uint64_t` 함수는 host pointer 폭과 독립적인 runtime layout을 계산한다.
 */

/** Overflow 없이 두 process-local byte 수를 더한다. */
int ribos_checked_size_add(
    size_t left,
    size_t right,
    size_t *result);

/** Overflow 없이 process-local `count * element_size`를 계산한다. */
int ribos_checked_size_multiply(
    size_t count,
    size_t element_size,
    size_t *result);

/** Power-of-two alignment로 process-local byte offset을 올림한다. */
int ribos_checked_size_align(
    size_t value,
    size_t alignment,
    size_t *result);

/** Half-open process-local range가 capacity 안에 있는지 반환한다. */
int ribos_checked_size_range(
    size_t capacity,
    size_t offset,
    size_t length);

/** Overflow 없이 두 architecture-neutral byte 수를 더한다. */
int ribos_checked_u64_add(
    uint64_t left,
    uint64_t right,
    uint64_t *result);

/** Overflow 없이 architecture-neutral `count * element_size`를 계산한다. */
int ribos_checked_u64_multiply(
    uint64_t count,
    uint64_t element_size,
    uint64_t *result);

/** Power-of-two alignment로 architecture-neutral byte offset을 올림한다. */
int ribos_checked_u64_align(
    uint64_t value,
    uint64_t alignment,
    uint64_t *result);

/** Architecture-neutral byte 수를 현재 process의 `size_t`로 축소 검사한다. */
int ribos_checked_u64_to_size(
    uint64_t value,
    size_t *result);

#ifdef __cplusplus
}
#endif

#endif
