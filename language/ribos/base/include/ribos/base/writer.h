#ifndef RIBOS_BASE_WRITER_H
#define RIBOS_BASE_WRITER_H

#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file writer.h
 * @brief Architecture-neutral diagnostic dump sink 계약.
 */

/** Caller가 제공하는 formatted-write 연산이다. */
typedef int (*RibosWriterFormatFunction)(
    void *context,
    const char *format,
    va_list arguments);

/**
 * Frontend와 IR dump가 사용하는 explicit writer다.
 *
 * VM, artifact wire와 target runtime은 이 writer를 소비하지 않는다.
 */
typedef struct RibosWriter {
    void *context;
    RibosWriterFormatFunction format;
} RibosWriter;

/** Writer backend에 formatted bytes를 전달하고 음수 backend 실패를 보존한다. */
int ribos_writer_printf(
    RibosWriter *writer,
    const char *format,
    ...);

#ifdef __cplusplus
}
#endif

#endif
