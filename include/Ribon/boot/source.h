#ifndef RIBON_BOOT_SOURCE_H
#define RIBON_BOOT_SOURCE_H

#include <stdint.h>

/** @brief Boot source transport 종류다. */
enum RibonBootMediaKind {
    RIBON_BOOT_MEDIA_NONE = 0,
    RIBON_BOOT_MEDIA_FILE = 1,
    RIBON_BOOT_MEDIA_BLOCK = 2,
    RIBON_BOOT_MEDIA_MEMORY = 3,
    RIBON_BOOT_MEDIA_NETWORK = 4,
};

/** @brief Native handle을 노출하지 않는 bounded boot source다. */
struct RibonBootSource {
    enum RibonBootMediaKind kind; /**< Source transport 종류다. */
    uint32_t source_id; /**< Environment-private source table ID다. */
    uint64_t size; /**< 읽을 수 있는 전체 byte 수다. */
    uint32_t block_size; /**< Atomic block byte 수며 stream은 0이다. */
};

/** @brief Boot media kind의 안정적인 이름을 반환한다. */
const char *ribon_boot_media_name(enum RibonBootMediaKind media);

#endif
