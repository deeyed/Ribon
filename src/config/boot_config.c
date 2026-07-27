#include <Ribon/config/boot_config.h>

#include <stdint.h>

#define RIBON_BOOT_CONFIG_FIELD_PRIORITY (1u << 0u)
#define RIBON_BOOT_CONFIG_FIELD_PROTOCOL (1u << 1u)
#define RIBON_BOOT_CONFIG_FIELD_IMAGE (1u << 2u)
#define RIBON_BOOT_CONFIG_FIELD_KERNEL (1u << 3u)
#define RIBON_BOOT_CONFIG_REQUIRED_FIELDS \
    (RIBON_BOOT_CONFIG_FIELD_PRIORITY | RIBON_BOOT_CONFIG_FIELD_PROTOCOL | \
     RIBON_BOOT_CONFIG_FIELD_IMAGE | RIBON_BOOT_CONFIG_FIELD_KERNEL)

/** @brief ASCII printable non-whitespace identifier byte인지 검사한다. */
static int boot_config_identifier_byte(unsigned char byte) {
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
           (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' || byte == '.';
}

/** @brief Bounded string range를 destination에 NUL-terminated copy한다. */
static int boot_config_copy(
    char *destination,
    uint32_t destination_capacity,
    const unsigned char *source,
    uint64_t source_size,
    int identifier_only) {
    if (destination == 0 || source == 0 || destination_capacity == 0u ||
        source_size == 0u || source_size >= destination_capacity) {
        return 0;
    }
    for (uint64_t index = 0u; index < source_size; ++index) {
        const unsigned char byte = source[index];
        if (byte < 0x20u || byte > 0x7eu || (identifier_only && !boot_config_identifier_byte(byte))) {
            return 0;
        }
        destination[index] = (char)byte;
    }
    destination[source_size] = '\0';
    return 1;
}

/** @brief Canonical absolute path가 root escape와 empty component를 포함하지 않는지 검사한다. */
static int boot_config_path_is_valid(const char *path) {
    uint32_t component_bytes = 0u;
    uint32_t components = 0u;
    uint32_t dots = 0u;
    if (path == 0 || path[0] != '/' || path[1] == '\0') {
        return 0;
    }
    for (uint32_t index = 1u;; ++index) {
        const unsigned char byte = (unsigned char)path[index];
        if (byte == '/' || byte == '\0') {
            if (component_bytes == 0u || dots == component_bytes ||
                (component_bytes == 2u && dots == 2u) || ++components > RIBON_BOOT_CONFIG_MAX_MODULES) {
                return 0;
            }
            if (byte == '\0') {
                return 1;
            }
            component_bytes = 0u;
            dots = 0u;
            continue;
        }
        if (byte < 0x21u || byte > 0x7eu || byte == '\\' || ++component_bytes >= 64u) {
            return 0;
        }
        if (byte == '.') {
            ++dots;
        }
    }
}

/** @brief Decimal priority를 overflow 없이 변환한다. */
static int boot_config_parse_priority(
    const unsigned char *value,
    uint64_t value_size,
    uint32_t *out) {
    uint64_t result = 0u;
    if (value == 0 || out == 0 || value_size == 0u) {
        return 0;
    }
    for (uint64_t index = 0u; index < value_size; ++index) {
        if (value[index] < '0' || value[index] > '9') {
            return 0;
        }
        result = result * 10u + (uint64_t)(value[index] - '0');
        if (result > UINT32_MAX) {
            return 0;
        }
    }
    *out = (uint32_t)result;
    return 1;
}

/** @brief Candidate가 required singleton key를 모두 받았는지 검사한다. */
static int boot_config_entry_is_complete(const struct RibonBootConfigEntry *entry) {
    return entry != 0 && entry->required_fields == RIBON_BOOT_CONFIG_REQUIRED_FIELDS &&
           entry->id[0] != '\0' && entry->protocol[0] != '\0' &&
           entry->image_format[0] != '\0' && entry->kernel_path[0] != '\0' &&
           boot_config_path_is_valid(entry->kernel_path);
}

/** @brief Candidate key/value를 duplicate-free bounded field에 적용한다. */
static int boot_config_apply_key(
    struct RibonBootConfigEntry *entry,
    const unsigned char *key,
    uint64_t key_size,
    const unsigned char *value,
    uint64_t value_size) {
    uint32_t field = 0u;
    char *destination = 0;
    uint32_t capacity = 0u;
    int identifier_only = 0;

    if (key_size == 8u && key[0] == 'p' && key[1] == 'r' && key[2] == 'i' && key[3] == 'o' &&
        key[4] == 'r' && key[5] == 'i' && key[6] == 't' && key[7] == 'y') {
        if ((entry->required_fields & RIBON_BOOT_CONFIG_FIELD_PRIORITY) != 0u ||
            !boot_config_parse_priority(value, value_size, &entry->priority)) {
            return RIBON_BOOT_CONFIG_STATUS_DUPLICATE_KEY;
        }
        entry->required_fields |= RIBON_BOOT_CONFIG_FIELD_PRIORITY;
        return RIBON_BOOT_CONFIG_STATUS_OK;
    }
    if (key_size == 8u && key[0] == 'p' && key[1] == 'r' && key[2] == 'o' && key[3] == 't' &&
        key[4] == 'o' && key[5] == 'c' && key[6] == 'o' && key[7] == 'l') {
        field = RIBON_BOOT_CONFIG_FIELD_PROTOCOL;
        destination = entry->protocol;
        capacity = sizeof(entry->protocol);
        identifier_only = 1;
    } else if (key_size == 5u && key[0] == 'i' && key[1] == 'm' && key[2] == 'a' &&
               key[3] == 'g' && key[4] == 'e') {
        field = RIBON_BOOT_CONFIG_FIELD_IMAGE;
        destination = entry->image_format;
        capacity = sizeof(entry->image_format);
        identifier_only = 1;
    } else if (key_size == 6u && key[0] == 'k' && key[1] == 'e' && key[2] == 'r' &&
               key[3] == 'n' && key[4] == 'e' && key[5] == 'l') {
        field = RIBON_BOOT_CONFIG_FIELD_KERNEL;
        destination = entry->kernel_path;
        capacity = sizeof(entry->kernel_path);
    } else if (key_size == 7u && key[0] == 'c' && key[1] == 'm' && key[2] == 'd' &&
               key[3] == 'l' && key[4] == 'i' && key[5] == 'n' && key[6] == 'e') {
        if (entry->command_line[0] != '\0' ||
            !boot_config_copy(entry->command_line, sizeof(entry->command_line), value, value_size, 0)) {
            return RIBON_BOOT_CONFIG_STATUS_DUPLICATE_KEY;
        }
        return RIBON_BOOT_CONFIG_STATUS_OK;
    } else if (key_size == 6u && key[0] == 'm' && key[1] == 'o' && key[2] == 'd' &&
               key[3] == 'u' && key[4] == 'l' && key[5] == 'e') {
        if (entry->module_count == RIBON_BOOT_CONFIG_MAX_MODULES) {
            return RIBON_BOOT_CONFIG_STATUS_OUT_OF_CAPACITY;
        }
        if (!boot_config_copy(
                entry->module_paths[entry->module_count],
                sizeof(entry->module_paths[entry->module_count]), value, value_size, 0) ||
            !boot_config_path_is_valid(entry->module_paths[entry->module_count])) {
            return RIBON_BOOT_CONFIG_STATUS_BAD_PATH;
        }
        ++entry->module_count;
        return RIBON_BOOT_CONFIG_STATUS_OK;
    } else {
        return RIBON_BOOT_CONFIG_STATUS_UNKNOWN_KEY;
    }
    if ((entry->required_fields & field) != 0u) {
        return RIBON_BOOT_CONFIG_STATUS_DUPLICATE_KEY;
    }
    if (!boot_config_copy(destination, capacity, value, value_size, identifier_only)) {
        return RIBON_BOOT_CONFIG_STATUS_SYNTAX;
    }
    if (field == RIBON_BOOT_CONFIG_FIELD_KERNEL && !boot_config_path_is_valid(destination)) {
        return RIBON_BOOT_CONFIG_STATUS_BAD_PATH;
    }
    entry->required_fields |= field;
    return RIBON_BOOT_CONFIG_STATUS_OK;
}

/** @brief 비어 있지 않은 한 줄에서 첫 key/value 구분자를 찾아 반환한다. */
static int boot_config_find_equals(
    const unsigned char *line,
    uint64_t line_size,
    uint64_t *out_equals) {
    uint64_t equals = UINT64_MAX;
    for (uint64_t index = 0u; index < line_size; ++index) {
        if (line[index] == '=' && equals == UINT64_MAX) {
            equals = index;
        }
    }
    if (equals == UINT64_MAX || equals == 0u || equals + 1u >= line_size) {
        return 0;
    }
    *out_equals = equals;
    return 1;
}

/** @brief Configuration parser status를 stable diagnostic name으로 변환한다. */
const char *ribon_boot_config_status_name(enum RibonBootConfigStatus status) {
    switch (status) {
    case RIBON_BOOT_CONFIG_STATUS_OK:
        return "ok";
    case RIBON_BOOT_CONFIG_STATUS_BAD_ARGUMENT:
        return "bad-argument";
    case RIBON_BOOT_CONFIG_STATUS_TRUNCATED:
        return "truncated";
    case RIBON_BOOT_CONFIG_STATUS_SYNTAX:
        return "syntax";
    case RIBON_BOOT_CONFIG_STATUS_UNKNOWN_KEY:
        return "unknown-key";
    case RIBON_BOOT_CONFIG_STATUS_DUPLICATE_KEY:
        return "duplicate-key";
    case RIBON_BOOT_CONFIG_STATUS_OUT_OF_CAPACITY:
        return "out-of-capacity";
    case RIBON_BOOT_CONFIG_STATUS_BAD_PATH:
        return "bad-path";
    case RIBON_BOOT_CONFIG_STATUS_INCOMPLETE_ENTRY:
        return "incomplete-entry";
    case RIBON_BOOT_CONFIG_STATUS_AMBIGUOUS:
        return "ambiguous";
    }
    return "unknown";
}

/**
 * @brief Bounded line grammar를 parse해 complete candidate block만 반환한다.
 *
 * ASCII 바이트만 수용하고 닫힌 `entry` 블록을 요구하며, 반환 뒤 input buffer 포인터를 보존하지 않는다.
 */
int ribon_boot_configuration_parse(
    const void *bytes,
    uint64_t byte_count,
    struct RibonBootConfiguration *out) {
    const unsigned char *input = bytes;
    struct RibonBootConfigEntry *active = 0;
    uint64_t line_start = 0u;
    int version_seen = 0;
    if (out != 0) {
        *out = (struct RibonBootConfiguration){0};
    }
    if (input == 0 || out == 0 || byte_count == 0u ||
        byte_count > (uint64_t)RIBON_BOOT_CONFIG_MAX_ENTRIES * 2048u) {
        return RIBON_BOOT_CONFIG_STATUS_BAD_ARGUMENT;
    }
    for (uint64_t index = 0u; index <= byte_count; ++index) {
        const int end = index == byte_count || input[index] == '\n';
        uint64_t line_end;
        uint64_t equals;
        if (!end) {
            continue;
        }
        line_end = index;
        if (line_end > line_start && input[line_end - 1u] == '\r') {
            --line_end;
        }
        if (line_end == line_start || input[line_start] == '#') {
            line_start = index + 1u;
            continue;
        }
        for (uint64_t character = line_start; character < line_end; ++character) {
            if (input[character] < 0x20u || input[character] > 0x7eu) {
                return RIBON_BOOT_CONFIG_STATUS_SYNTAX;
            }
        }
        if (!version_seen) {
            if (line_end - line_start != 9u || input[line_start] != 'v' ||
                input[line_start + 1u] != 'e' || input[line_start + 2u] != 'r' ||
                input[line_start + 3u] != 's' || input[line_start + 4u] != 'i' ||
                input[line_start + 5u] != 'o' || input[line_start + 6u] != 'n' ||
                input[line_start + 7u] != '=' || input[line_start + 8u] != '1') {
                return RIBON_BOOT_CONFIG_STATUS_SYNTAX;
            }
            version_seen = 1;
        } else if (line_end - line_start >= 6u && input[line_start] == 'e' &&
                   input[line_start + 1u] == 'n' && input[line_start + 2u] == 't' &&
                   input[line_start + 3u] == 'r' && input[line_start + 4u] == 'y' &&
                   input[line_start + 5u] == '=') {
            const uint64_t id_size = line_end - (line_start + 6u);
            if (active != 0 || out->entry_count == RIBON_BOOT_CONFIG_MAX_ENTRIES ||
                !boot_config_copy(
                    out->entries[out->entry_count].id,
                    sizeof(out->entries[out->entry_count].id),
                    input + line_start + 6u,
                    id_size,
                    1)) {
                return RIBON_BOOT_CONFIG_STATUS_OUT_OF_CAPACITY;
            }
            for (uint32_t previous = 0u; previous < out->entry_count; ++previous) {
                const char *a = out->entries[previous].id;
                const char *b = out->entries[out->entry_count].id;
                uint32_t position = 0u;
                while (a[position] == b[position] && a[position] != '\0') {
                    ++position;
                }
                if (a[position] == b[position]) {
                    return RIBON_BOOT_CONFIG_STATUS_DUPLICATE_KEY;
                }
            }
            active = &out->entries[out->entry_count];
        } else if (line_end - line_start == 3u && input[line_start] == 'e' &&
                   input[line_start + 1u] == 'n' && input[line_start + 2u] == 'd') {
            if (active == 0 || !boot_config_entry_is_complete(active)) {
                return RIBON_BOOT_CONFIG_STATUS_INCOMPLETE_ENTRY;
            }
            ++out->entry_count;
            active = 0;
        } else {
            const int status = !boot_config_find_equals(
                input + line_start, line_end - line_start, &equals) || active == 0 ?
                RIBON_BOOT_CONFIG_STATUS_SYNTAX :
                boot_config_apply_key(
                    active,
                    input + line_start,
                    equals,
                    input + line_start + equals + 1u,
                    line_end - line_start - equals - 1u);
            if (status != RIBON_BOOT_CONFIG_STATUS_OK) {
                return status;
            }
        }
        line_start = index + 1u;
    }
    if (!version_seen || active != 0 || out->entry_count == 0u) {
        return RIBON_BOOT_CONFIG_STATUS_INCOMPLETE_ENTRY;
    }
    return RIBON_BOOT_CONFIG_STATUS_OK;
}

/** @brief Highest priority candidate를 deterministic하게 선택한다. */
int ribon_boot_configuration_select(
    const struct RibonBootConfiguration *configuration,
    const struct RibonBootConfigEntry **out) {
    const struct RibonBootConfigEntry *selected = 0;
    if (out != 0) {
        *out = 0;
    }
    if (configuration == 0 || out == 0 || configuration->entry_count == 0u ||
        configuration->entry_count > RIBON_BOOT_CONFIG_MAX_ENTRIES) {
        return RIBON_BOOT_CONFIG_STATUS_BAD_ARGUMENT;
    }
    for (uint32_t index = 0u; index < configuration->entry_count; ++index) {
        const struct RibonBootConfigEntry *candidate = &configuration->entries[index];
        if (!boot_config_entry_is_complete(candidate)) {
            return RIBON_BOOT_CONFIG_STATUS_INCOMPLETE_ENTRY;
        }
        if (selected == 0 || candidate->priority > selected->priority) {
            selected = candidate;
        } else if (candidate->priority == selected->priority) {
            return RIBON_BOOT_CONFIG_STATUS_AMBIGUOUS;
        }
    }
    *out = selected;
    return RIBON_BOOT_CONFIG_STATUS_OK;
}
