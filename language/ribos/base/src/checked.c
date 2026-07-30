#include "ribos/base/checked.h"

#include <stdint.h>

int
ribos_checked_size_add(
    size_t left,
    size_t right,
    size_t *result)
{
    if (result == NULL || right > SIZE_MAX - left) {
        return 0;
    }
    *result = left + right;
    return 1;
}

int
ribos_checked_size_multiply(
    size_t count,
    size_t element_size,
    size_t *result)
{
    if (result == NULL ||
        (count != 0 && element_size > SIZE_MAX / count)) {
        return 0;
    }
    *result = count * element_size;
    return 1;
}

int
ribos_checked_size_align(
    size_t value,
    size_t alignment,
    size_t *result)
{
    size_t mask;

    if (result == NULL || alignment == 0 ||
        (alignment & (alignment - 1)) != 0) {
        return 0;
    }
    mask = alignment - 1;
    if (value > SIZE_MAX - mask) {
        return 0;
    }
    *result = (value + mask) & ~mask;
    return 1;
}

int
ribos_checked_size_range(
    size_t capacity,
    size_t offset,
    size_t length)
{
    return offset <= capacity && length <= capacity - offset;
}

int
ribos_checked_u64_add(
    uint64_t left,
    uint64_t right,
    uint64_t *result)
{
    if (result == NULL || right > UINT64_MAX - left) {
        return 0;
    }
    *result = left + right;
    return 1;
}

int
ribos_checked_u64_multiply(
    uint64_t count,
    uint64_t element_size,
    uint64_t *result)
{
    if (result == NULL ||
        (count != 0 && element_size > UINT64_MAX / count)) {
        return 0;
    }
    *result = count * element_size;
    return 1;
}

int
ribos_checked_u64_align(
    uint64_t value,
    uint64_t alignment,
    uint64_t *result)
{
    uint64_t mask;

    if (result == NULL || alignment == 0 ||
        (alignment & (alignment - 1)) != 0) {
        return 0;
    }
    mask = alignment - 1;
    if (value > UINT64_MAX - mask) {
        return 0;
    }
    *result = (value + mask) & ~mask;
    return 1;
}

int
ribos_checked_u64_to_size(
    uint64_t value,
    size_t *result)
{
    if (result == NULL || value > (uint64_t)SIZE_MAX) {
        return 0;
    }
    *result = (size_t)value;
    return 1;
}
