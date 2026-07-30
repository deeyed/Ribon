#ifndef RIBOS_HOST_WRITER_H
#define RIBOS_HOST_WRITER_H

#include <stdio.h>

#include "ribos/base/writer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file writer.h
 * @brief Hosted `FILE` stream을 explicit Ribos writer로 감싸는 adapter.
 */

/** Borrowed `FILE` stream을 writer descriptor로 초기화한다. */
RibosWriter ribos_host_file_writer(FILE *stream);

#ifdef __cplusplus
}
#endif

#endif
