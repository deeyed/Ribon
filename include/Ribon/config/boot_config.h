#ifndef RIBON_CONFIG_BOOT_CONFIG_H
#define RIBON_CONFIG_BOOT_CONFIG_H

#include <stdint.h>

/** @brief 한 boot configuration이 보유할 bounded candidate 수다. */
#define RIBON_BOOT_CONFIG_MAX_ENTRIES 4u

/** @brief 한 candidate가 보유할 bounded module 수다. */
#define RIBON_BOOT_CONFIG_MAX_MODULES 8u

/** @brief Candidate stable ID의 NUL 포함 caller-owned storage 크기다. */
#define RIBON_BOOT_CONFIG_ID_CAPACITY 33u

/** @brief Protocol과 image-format identifier의 NUL 포함 storage 크기다. */
#define RIBON_BOOT_CONFIG_IDENTIFIER_CAPACITY 33u

/** @brief Canonical absolute path의 NUL 포함 storage 크기다. */
#define RIBON_BOOT_CONFIG_PATH_CAPACITY 128u

/** @brief Command line의 NUL 포함 storage 크기다. */
#define RIBON_BOOT_CONFIG_COMMAND_LINE_CAPACITY 256u

/** @brief Configuration parser의 안정적인 결과다. */
enum RibonBootConfigStatus {
    RIBON_BOOT_CONFIG_STATUS_OK = 0,
    RIBON_BOOT_CONFIG_STATUS_BAD_ARGUMENT = -1,
    RIBON_BOOT_CONFIG_STATUS_TRUNCATED = -2,
    RIBON_BOOT_CONFIG_STATUS_SYNTAX = -3,
    RIBON_BOOT_CONFIG_STATUS_UNKNOWN_KEY = -4,
    RIBON_BOOT_CONFIG_STATUS_DUPLICATE_KEY = -5,
    RIBON_BOOT_CONFIG_STATUS_OUT_OF_CAPACITY = -6,
    RIBON_BOOT_CONFIG_STATUS_BAD_PATH = -7,
    RIBON_BOOT_CONFIG_STATUS_INCOMPLETE_ENTRY = -8,
    RIBON_BOOT_CONFIG_STATUS_AMBIGUOUS = -9,
};

/** @brief Caller-owned bounded boot candidate다. */
struct RibonBootConfigEntry {
    char id[RIBON_BOOT_CONFIG_ID_CAPACITY]; /**< Stable candidate ID다. */
    char protocol[RIBON_BOOT_CONFIG_IDENTIFIER_CAPACITY]; /**< Selected Boot Protocol ID다. */
    char image_format[RIBON_BOOT_CONFIG_IDENTIFIER_CAPACITY]; /**< Selected image-format ID다. */
    char kernel_path[RIBON_BOOT_CONFIG_PATH_CAPACITY]; /**< Canonical absolute kernel path다. */
    char command_line[RIBON_BOOT_CONFIG_COMMAND_LINE_CAPACITY]; /**< Protocol-owned command line이다. */
    char init_image_path[RIBON_BOOT_CONFIG_PATH_CAPACITY]; /**< Optional singleton initial image다. */
    char module_paths[RIBON_BOOT_CONFIG_MAX_MODULES][RIBON_BOOT_CONFIG_PATH_CAPACITY]; /**< Module paths다. */
    uint32_t has_init_image; /**< `init_image`가 존재하면 1이다. */
    uint32_t module_count; /**< Parsed module 수다. */
    uint32_t priority; /**< Higher value가 먼저 선택된다. */
    uint32_t required_fields; /**< Parser-private completeness bitset이며 caller가 변경하지 않는다. */
};

/** @brief Caller-owned complete configuration storage다. */
struct RibonBootConfiguration {
    struct RibonBootConfigEntry entries[RIBON_BOOT_CONFIG_MAX_ENTRIES]; /**< Bounded candidates다. */
    uint32_t entry_count; /**< Complete candidate 수다. */
};

/**
 * @brief UTF-8 subset ASCII configuration을 bounded candidate set으로 parse한다.
 *
 * Grammar는 `version=1` 뒤 `entry=<id>` block에서 `priority`, `protocol`, `image`,
 * `kernel`, optional `cmdline`/`init_image`/repeated `module`, `end`를 요구한다. Unknown key와
 * traversal path는 fail-closed다.
 */
int ribon_boot_configuration_parse(
    const void *bytes,
    uint64_t byte_count,
    struct RibonBootConfiguration *out);

/** @brief Highest priority complete candidate를 반환하고 tie는 ambiguous로 거부한다. */
int ribon_boot_configuration_select(
    const struct RibonBootConfiguration *configuration,
    const struct RibonBootConfigEntry **out);

/** @brief Configuration parser status의 안정적인 이름을 반환한다. */
const char *ribon_boot_config_status_name(enum RibonBootConfigStatus status);

#endif
