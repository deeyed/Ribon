#ifndef RIBOS_HOST_FORMAT_H
#define RIBOS_HOST_FORMAT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file format.h
 * @brief Frontend diagnostic formatting을 hosted libc에 격리하는 adapter.
 */

/** `snprintf`와 같은 bounded ASCII diagnostic formatting을 수행한다. */
int ribos_host_snprintf(
    char *output,
    size_t output_size,
    const char *format,
    ...);

#ifdef __cplusplus
}
#endif

#endif
