#include "ribos/base/allocator.h"

#include <stdint.h>
#include <string.h>

static int
ribos_allocator_arguments_are_valid(
    const RibosAllocator *allocator,
    size_t alignment)
{
    return allocator != NULL &&
        allocator->allocate != NULL &&
        allocator->resize != NULL &&
        allocator->deallocate != NULL &&
        alignment != 0 &&
        (alignment & (alignment - 1)) == 0;
}

void *
ribos_allocator_allocate(
    const RibosAllocator *allocator,
    size_t size,
    size_t alignment)
{
    if (!ribos_allocator_arguments_are_valid(allocator, alignment) ||
        size == 0) {
        return NULL;
    }
    return allocator->allocate(allocator->context, size, alignment);
}

void *
ribos_allocator_allocate_zeroed(
    const RibosAllocator *allocator,
    size_t count,
    size_t element_size,
    size_t alignment)
{
    void *storage;
    size_t size;

    if (count == 0 || element_size == 0) {
        return NULL;
    }
    if (count > SIZE_MAX / element_size) {
        return NULL;
    }
    size = count * element_size;
    storage = ribos_allocator_allocate(allocator, size, alignment);
    if (storage != NULL) {
        memset(storage, 0, size);
    }
    return storage;
}

void *
ribos_allocator_resize(
    const RibosAllocator *allocator,
    void *storage,
    size_t old_size,
    size_t new_size,
    size_t alignment)
{
    if (!ribos_allocator_arguments_are_valid(allocator, alignment) ||
        new_size == 0) {
        return NULL;
    }
    if (storage == NULL) {
        return ribos_allocator_allocate(allocator, new_size, alignment);
    }
    return allocator->resize(
        allocator->context,
        storage,
        old_size,
        new_size,
        alignment);
}

void
ribos_allocator_release(
    const RibosAllocator *allocator,
    void *storage,
    size_t size,
    size_t alignment)
{
    if (storage == NULL ||
        !ribos_allocator_arguments_are_valid(allocator, alignment)) {
        return;
    }
    allocator->deallocate(
        allocator->context,
        storage,
        size,
        alignment);
}
