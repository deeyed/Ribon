#include <Ribon/core/memory.h>

#include <stdint.h>

/** @brief Pointer와 offset의 합이 표현 가능한지 검사한다. */
static int ribon_arena_pointer_range_is_valid(const struct RibonArena *arena) {
    const uintptr_t base = arena != 0 ? (uintptr_t)arena->base : 0u;
    return arena != 0 &&
           arena->base != 0 &&
           arena->capacity != 0u &&
           arena->used <= arena->capacity &&
           arena->high_watermark >= arena->used &&
           arena->high_watermark <= arena->capacity &&
           base <= UINTPTR_MAX - arena->capacity;
}

/** @brief Caller-owned storage로 빈 arena를 초기화한다. */
void ribon_arena_init(struct RibonArena *arena, void *storage, uint64_t capacity) {
    if (arena == 0) {
        return;
    }
    arena->base = (unsigned char *)storage;
    arena->capacity = storage != 0 ? capacity : 0u;
    arena->used = 0u;
    arena->high_watermark = 0u;
}

/** @brief 정렬 조건을 만족하는 고정 크기 영역을 arena에서 할당한다. */
int ribon_arena_allocate(
    struct RibonArena *arena,
    uint64_t size,
    uint64_t alignment,
    void **out) {
    uintptr_t current;
    uintptr_t aligned;
    uint64_t padding;
    uint64_t next_used;

    if (out == 0) {
        return RIBON_CORE_STATUS_BAD_ARGUMENT;
    }
    *out = 0;
    if (!ribon_arena_pointer_range_is_valid(arena) || size == 0u) {
        return RIBON_CORE_STATUS_BAD_ARGUMENT;
    }
    if (alignment == 0u || (alignment & (alignment - 1u)) != 0u) {
        return RIBON_CORE_STATUS_BAD_ALIGNMENT;
    }

    current = (uintptr_t)arena->base + arena->used;
    if (current > UINTPTR_MAX - (alignment - 1u)) {
        return RIBON_CORE_STATUS_OUT_OF_CAPACITY;
    }
    aligned = (current + alignment - 1u) & ~(uintptr_t)(alignment - 1u);
    padding = (uint64_t)(aligned - current);
    if (padding > arena->capacity - arena->used ||
        size > arena->capacity - arena->used - padding) {
        return RIBON_CORE_STATUS_OUT_OF_CAPACITY;
    }
    next_used = arena->used + padding + size;
    arena->used = next_used;
    if (next_used > arena->high_watermark) {
        arena->high_watermark = next_used;
    }
    *out = (void *)aligned;
    return RIBON_CORE_STATUS_OK;
}

/** @brief arena에 남은 byte 수를 반환한다. */
uint64_t ribon_arena_remaining(const struct RibonArena *arena) {
    if (!ribon_arena_pointer_range_is_valid(arena)) {
        return 0u;
    }
    return arena->capacity - arena->used;
}
