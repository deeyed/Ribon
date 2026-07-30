#include "ribos/host/allocator.h"

#include <stddef.h>
#include <stdlib.h>

static void *
ribos_host_allocate(
    void *context,
    size_t size,
    size_t alignment)
{
    (void)context;
    if (alignment > _Alignof(max_align_t)) {
        return NULL;
    }
    return malloc(size);
}

static void *
ribos_host_resize(
    void *context,
    void *storage,
    size_t old_size,
    size_t new_size,
    size_t alignment)
{
    (void)context;
    (void)old_size;
    if (alignment > _Alignof(max_align_t)) {
        return NULL;
    }
    return realloc(storage, new_size);
}

static void
ribos_host_deallocate(
    void *context,
    void *storage,
    size_t size,
    size_t alignment)
{
    (void)context;
    (void)size;
    (void)alignment;
    free(storage);
}

const RibosAllocator *
ribos_host_allocator(void)
{
    static const RibosAllocator allocator = {
        .allocate = ribos_host_allocate,
        .resize = ribos_host_resize,
        .deallocate = ribos_host_deallocate,
    };

    return &allocator;
}
