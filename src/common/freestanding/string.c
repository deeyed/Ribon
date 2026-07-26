/**
 * @file
 * @brief Target-owned image가 공유하는 allocation-free byte primitive다.
 */
#include <stddef.h>

/** @brief 겹치지 않는 byte range를 caller가 요청한 크기만큼 복사한다. */
void *memcpy(void *destination, const void *source, size_t count) {
    unsigned char *dst = (unsigned char *)destination;
    const unsigned char *src = (const unsigned char *)source;
    for (size_t index = 0; index < count; ++index) {
        dst[index] = src[index];
    }
    return destination;
}

/** @brief 겹칠 수 있는 byte range를 방향에 맞춰 안전하게 복사한다. */
void *memmove(void *destination, const void *source, size_t count) {
    unsigned char *dst = (unsigned char *)destination;
    const unsigned char *src = (const unsigned char *)source;
    if (dst == src || count == 0u) {
        return destination;
    }
    if (dst < src) {
        for (size_t index = 0; index < count; ++index) {
            dst[index] = src[index];
        }
        return destination;
    }
    for (size_t index = count; index != 0u; --index) {
        dst[index - 1u] = src[index - 1u];
    }
    return destination;
}

/** @brief Caller-owned byte range를 한 byte 값으로 채운다. */
void *memset(void *destination, int value, size_t count) {
    unsigned char *dst = (unsigned char *)destination;
    for (size_t index = 0; index < count; ++index) {
        dst[index] = (unsigned char)value;
    }
    return destination;
}

/** @brief 두 bounded byte range를 unsigned byte order로 비교한다. */
int memcmp(const void *left, const void *right, size_t count) {
    const unsigned char *lhs = (const unsigned char *)left;
    const unsigned char *rhs = (const unsigned char *)right;
    for (size_t index = 0; index < count; ++index) {
        if (lhs[index] != rhs[index]) {
            return (int)lhs[index] - (int)rhs[index];
        }
    }
    return 0;
}

/** @brief 두 NUL-terminated string을 unsigned byte order로 비교한다. */
int strcmp(const char *left, const char *right) {
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return (int)(unsigned char)*left - (int)(unsigned char)*right;
}

/** @brief NUL-terminated string의 terminator 이전 byte 수를 반환한다. */
size_t strlen(const char *text) {
    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}
