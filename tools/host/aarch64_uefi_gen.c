#include "../../language/ribos/artifact/src/internal.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOKEN_CAPACITY 512u
#define JSON_DEPTH_LIMIT 8u
#define MANIFEST_BYTE_LIMIT (64u * 1024u)
#define FIXTURE_BYTE_CAPACITY 16384u

enum JsonTokenKind {
    JSON_TOKEN_OBJECT,
    JSON_TOKEN_ARRAY,
    JSON_TOKEN_STRING,
    JSON_TOKEN_NUMBER,
};

struct JsonToken {
    enum JsonTokenKind kind;
    size_t start;
    size_t end;
    size_t count;
    size_t after;
};

struct JsonDocument {
    const char *bytes;
    size_t size;
    size_t cursor;
    struct JsonToken tokens[TOKEN_CAPACITY];
    size_t token_count;
};

struct ExpectedObjectRow {
    const char *first;
    const char *second;
    const char *third;
};

struct Aarch64UefiProductProfile {
    const char *product_id;
    const char *target_id;
    const char *boot_protocol;
    const char *protocol_plugin_id;
    const char *protocol_package;
    const char *protocol_symbol;
    const char *evidence_claim;
};

static const struct Aarch64UefiProductProfile fixture_profile = {
    .product_id = "bootmgr.aarch64-uefi-parus-fixture",
    .target_id = "aarch64-uefi-parus-fixture",
    .boot_protocol = "luca",
    .protocol_plugin_id = "protocol.luca",
    .protocol_package = "ribon.protocol.luca",
    .protocol_symbol = "ribon_luca_protocol_plugin_descriptor",
    .evidence_claim =
        "AArch64 UEFI application reads a config-selected ELF64 fixture from the ESP",
};

static const struct Aarch64UefiProductProfile direct_fdt_profile = {
    .product_id = "bootmgr.aarch64-uefi-luca-direct-fdt-dev",
    .target_id = "aarch64-uefi-luca-direct-fdt-dev",
    .boot_protocol = "luca-direct-fdt-dev",
    .protocol_plugin_id = "protocol.luca-direct-fdt-dev",
    .protocol_package = "ribon.protocol.luca-direct-fdt-dev",
    .protocol_symbol = "ribon_luca_direct_fdt_protocol_plugin_descriptor",
    .evidence_claim =
        "AArch64 UEFI Ribon transfers exact LUCA kernel and World inputs by explicit development direct-FDT",
};

static void skip_space(struct JsonDocument *document) {
    while (document->cursor < document->size) {
        const char byte = document->bytes[document->cursor];
        if (byte != ' ' && byte != '\t' && byte != '\r' && byte != '\n') {
            break;
        }
        ++document->cursor;
    }
}

static int allocate_token(
    struct JsonDocument *document,
    enum JsonTokenKind kind,
    size_t *index_out) {
    if (document->token_count == TOKEN_CAPACITY || index_out == NULL) {
        return 0;
    }
    *index_out = document->token_count++;
    document->tokens[*index_out] = (struct JsonToken){
        .kind = kind,
        .start = document->cursor,
        .end = document->cursor,
        .count = 0u,
        .after = document->token_count,
    };
    return 1;
}

static int parse_value(struct JsonDocument *document, unsigned depth, size_t *index_out);

static int parse_string(struct JsonDocument *document, size_t *index_out) {
    size_t index;
    if (document->cursor >= document->size || document->bytes[document->cursor] != '"' ||
        !allocate_token(document, JSON_TOKEN_STRING, &index)) {
        return 0;
    }
    ++document->cursor;
    document->tokens[index].start = document->cursor;
    while (document->cursor < document->size && document->bytes[document->cursor] != '"') {
        const unsigned char byte = (unsigned char)document->bytes[document->cursor];
        if (byte < 0x20u || byte > 0x7eu || byte == '\\') {
            return 0;
        }
        ++document->cursor;
    }
    if (document->cursor >= document->size) {
        return 0;
    }
    document->tokens[index].end = document->cursor++;
    document->tokens[index].after = document->token_count;
    *index_out = index;
    return 1;
}

static int parse_number(struct JsonDocument *document, size_t *index_out) {
    size_t index;
    size_t start = document->cursor;
    if (!allocate_token(document, JSON_TOKEN_NUMBER, &index)) {
        return 0;
    }
    if (document->cursor < document->size && document->bytes[document->cursor] == '-') {
        ++document->cursor;
    }
    if (document->cursor >= document->size ||
        document->bytes[document->cursor] < '0' || document->bytes[document->cursor] > '9') {
        return 0;
    }
    if (document->bytes[document->cursor] == '0') {
        ++document->cursor;
    } else {
        while (document->cursor < document->size &&
               document->bytes[document->cursor] >= '0' &&
               document->bytes[document->cursor] <= '9') {
            ++document->cursor;
        }
    }
    document->tokens[index].start = start;
    document->tokens[index].end = document->cursor;
    document->tokens[index].after = document->token_count;
    *index_out = index;
    return 1;
}

static int parse_array(
    struct JsonDocument *document,
    unsigned depth,
    size_t *index_out) {
    size_t index;
    if (depth == JSON_DEPTH_LIMIT || !allocate_token(document, JSON_TOKEN_ARRAY, &index)) {
        return 0;
    }
    ++document->cursor;
    skip_space(document);
    if (document->cursor < document->size && document->bytes[document->cursor] == ']') {
        ++document->cursor;
        document->tokens[index].after = document->token_count;
        *index_out = index;
        return 1;
    }
    for (;;) {
        size_t child;
        if (!parse_value(document, depth + 1u, &child)) {
            return 0;
        }
        ++document->tokens[index].count;
        skip_space(document);
        if (document->cursor >= document->size) {
            return 0;
        }
        if (document->bytes[document->cursor] == ']') {
            ++document->cursor;
            break;
        }
        if (document->bytes[document->cursor++] != ',') {
            return 0;
        }
        skip_space(document);
    }
    document->tokens[index].after = document->token_count;
    *index_out = index;
    return 1;
}

static int parse_object(
    struct JsonDocument *document,
    unsigned depth,
    size_t *index_out) {
    size_t index;
    if (depth == JSON_DEPTH_LIMIT || !allocate_token(document, JSON_TOKEN_OBJECT, &index)) {
        return 0;
    }
    ++document->cursor;
    skip_space(document);
    if (document->cursor < document->size && document->bytes[document->cursor] == '}') {
        ++document->cursor;
        document->tokens[index].after = document->token_count;
        *index_out = index;
        return 1;
    }
    for (;;) {
        size_t key;
        size_t value;
        if (!parse_string(document, &key)) {
            return 0;
        }
        skip_space(document);
        if (document->cursor >= document->size || document->bytes[document->cursor++] != ':') {
            return 0;
        }
        skip_space(document);
        if (!parse_value(document, depth + 1u, &value)) {
            return 0;
        }
        ++document->tokens[index].count;
        skip_space(document);
        if (document->cursor >= document->size) {
            return 0;
        }
        if (document->bytes[document->cursor] == '}') {
            ++document->cursor;
            break;
        }
        if (document->bytes[document->cursor++] != ',') {
            return 0;
        }
        skip_space(document);
    }
    document->tokens[index].after = document->token_count;
    *index_out = index;
    return 1;
}

static int parse_value(struct JsonDocument *document, unsigned depth, size_t *index_out) {
    skip_space(document);
    if (document->cursor >= document->size) {
        return 0;
    }
    if (document->bytes[document->cursor] == '{') {
        return parse_object(document, depth, index_out);
    }
    if (document->bytes[document->cursor] == '[') {
        return parse_array(document, depth, index_out);
    }
    if (document->bytes[document->cursor] == '"') {
        return parse_string(document, index_out);
    }
    return parse_number(document, index_out);
}

static int token_equals(
    const struct JsonDocument *document,
    size_t token_index,
    const char *expected) {
    const struct JsonToken *token = &document->tokens[token_index];
    const size_t length = strlen(expected);
    return token->kind == JSON_TOKEN_STRING && token->end - token->start == length &&
           memcmp(document->bytes + token->start, expected, length) == 0;
}

static int object_value(
    const struct JsonDocument *document,
    size_t object_index,
    const char *key,
    size_t *value_out) {
    const struct JsonToken *object = &document->tokens[object_index];
    size_t cursor = object_index + 1u;
    int found = 0;
    if (object->kind != JSON_TOKEN_OBJECT || value_out == NULL) {
        return 0;
    }
    for (size_t pair = 0u; pair < object->count; ++pair) {
        const size_t key_index = cursor;
        const size_t value_index = document->tokens[key_index].after;
        if (token_equals(document, key_index, key)) {
            if (found) {
                return 0;
            }
            *value_out = value_index;
            found = 1;
        }
        cursor = document->tokens[value_index].after;
    }
    return found;
}

static int object_has_exact_keys(
    const struct JsonDocument *document,
    size_t object_index,
    const char *const *keys,
    size_t key_count) {
    const struct JsonToken *object = &document->tokens[object_index];
    size_t cursor = object_index + 1u;
    if (object->kind != JSON_TOKEN_OBJECT || object->count != key_count) {
        return 0;
    }
    for (size_t pair = 0u; pair < object->count; ++pair) {
        const size_t key_index = cursor;
        const size_t value_index = document->tokens[key_index].after;
        size_t matches = 0u;
        for (size_t expected = 0u; expected < key_count; ++expected) {
            matches += token_equals(document, key_index, keys[expected]) ? 1u : 0u;
        }
        if (matches != 1u) {
            return 0;
        }
        cursor = document->tokens[value_index].after;
    }
    return 1;
}

static int expect_string(
    const struct JsonDocument *document,
    size_t object,
    const char *key,
    const char *expected) {
    size_t value;
    return object_value(document, object, key, &value) &&
           token_equals(document, value, expected);
}

static int token_u64(
    const struct JsonDocument *document,
    size_t token_index,
    uint64_t *value_out) {
    const struct JsonToken *token = &document->tokens[token_index];
    uint64_t value = 0u;
    if (token->kind != JSON_TOKEN_NUMBER || value_out == NULL ||
        token->start == token->end || document->bytes[token->start] == '-') {
        return 0;
    }
    for (size_t index = token->start; index < token->end; ++index) {
        const unsigned digit = (unsigned)(document->bytes[index] - '0');
        if (digit > 9u || value > (UINT64_MAX - digit) / 10u) {
            return 0;
        }
        value = value * 10u + digit;
    }
    *value_out = value;
    return 1;
}

static int expect_u64(
    const struct JsonDocument *document,
    size_t object,
    const char *key,
    uint64_t expected) {
    size_t value_token;
    uint64_t value;
    return object_value(document, object, key, &value_token) &&
           token_u64(document, value_token, &value) && value == expected;
}

static int expect_string_array(
    const struct JsonDocument *document,
    size_t object,
    const char *key,
    const char *const *expected,
    size_t expected_count) {
    size_t array_index;
    size_t cursor;
    if (!object_value(document, object, key, &array_index) ||
        document->tokens[array_index].kind != JSON_TOKEN_ARRAY ||
        document->tokens[array_index].count != expected_count) {
        return 0;
    }
    cursor = array_index + 1u;
    for (size_t index = 0u; index < expected_count; ++index) {
        if (!token_equals(document, cursor, expected[index])) {
            return 0;
        }
        cursor = document->tokens[cursor].after;
    }
    return 1;
}

static int expect_object_array(
    const struct JsonDocument *document,
    size_t object,
    const char *key,
    const char *const field_names[3],
    const struct ExpectedObjectRow *rows,
    size_t row_count) {
    size_t array_index;
    size_t cursor;
    if (!object_value(document, object, key, &array_index) ||
        document->tokens[array_index].kind != JSON_TOKEN_ARRAY ||
        document->tokens[array_index].count != row_count) {
        return 0;
    }
    cursor = array_index + 1u;
    for (size_t row = 0u; row < row_count; ++row) {
        if (!object_has_exact_keys(document, cursor, field_names, 3u) ||
            !expect_string(document, cursor, field_names[0], rows[row].first) ||
            !expect_string(document, cursor, field_names[1], rows[row].second) ||
            !expect_string(document, cursor, field_names[2], rows[row].third)) {
            return 0;
        }
        cursor = document->tokens[cursor].after;
    }
    return 1;
}

static int parse_manifest(
    const char *bytes,
    size_t size,
    struct JsonDocument *document) {
    size_t root;
    *document = (struct JsonDocument){
        .bytes = bytes,
        .size = size,
    };
    if (!parse_value(document, 0u, &root) || root != 0u) {
        return 0;
    }
    skip_space(document);
    return document->cursor == document->size &&
           document->tokens[0].kind == JSON_TOKEN_OBJECT;
}

static int validate_manifest(
    const struct JsonDocument *document,
    const struct Aarch64UefiProductProfile *profile) {
    static const char *const top_keys[] = {
        "schema_version", "product_id", "product_kind", "target_id",
        "architecture", "environment", "port", "mode", "boot_protocols",
        "policies", "image", "evidence", "max_plugins", "services",
        "service_selections", "plugin_selections", "required_capabilities",
        "allowed_capabilities", "limits", "plugins",
    };
    static const char *const capabilities[] = {
        "ARCHITECTURE", "BOOT_CONFIRMATION", "BOOT_PROTOCOL", "BOOT_SOURCE_READ",
        "DIAGNOSTIC_SINK", "ENTRY_CONTRACT", "ENVIRONMENT_QUIESCE", "HANDOFF",
        "IMAGE_ELF64", "MONOTONIC_TIMER", "PERSISTENT_METADATA", "STORAGE_FLUSH",
    };
    static const char *const services_fields[] = {"id", "kind", "symbol"};
    static const struct ExpectedObjectRow services[] = {
        {"service.port.diagnostic-sink", "diagnostic-sink", "ribon_port_diagnostic_sink_service_descriptor"},
        {"service.uefi-app.boot-source", "boot-source", "ribon_uefi_app_boot_source_service_descriptor"},
        {"service.uefi-app.environment-quiesce", "environment-quiesce", "ribon_uefi_app_environment_quiesce_service_descriptor"},
        {"service.uefi-app.monotonic-timer", "monotonic-timer", "ribon_uefi_app_monotonic_timer_service_descriptor"},
        {"service.uefi-app.persistent-metadata", "persistent-metadata", "ribon_uefi_app_persistent_metadata_service_descriptor"},
        {"service.uefi-app.storage-flush", "storage-flush", "ribon_uefi_app_storage_flush_service_descriptor"},
    };
    static const char *const plugin_fields[] = {"id", "package", "symbol"};
    const struct ExpectedObjectRow plugins[] = {
        {"arch.aarch64", "ribon.arch.aarch64", "ribon_arch_plugin_descriptor"},
        {"environment.uefi-app", "ribon.environment.uefi-app", "ribon_uefi_app_environment_plugin_descriptor"},
        {"image.elf64", "ribon.image.elf64", "ribon_elf64_image_plugin_descriptor"},
        {profile->protocol_plugin_id, profile->protocol_package, profile->protocol_symbol},
    };
    static const char *const selection_fields[] = {"id", "kind", "reserved"};
    const struct ExpectedObjectRow selections[] = {
        {"image.elf64", "image-format", ""},
        {profile->protocol_plugin_id, "boot-protocol", ""},
    };
    static const char *const image_keys[] = {"format", "recipe", "artifact"};
    static const char *const evidence_keys[] = {"class", "claim"};
    static const char *const limit_keys[] = {
        "max_memory_regions", "max_load_segments", "max_components", "max_retries",
        "max_input_bytes", "max_handoff_bytes", "arena_bytes", "operation_deadline_ms",
    };
    size_t image;
    size_t evidence;
    size_t limits;
    size_t selections_array;
    size_t cursor;
    if (profile == 0 ||
        !object_has_exact_keys(document, 0u, top_keys, sizeof(top_keys) / sizeof(top_keys[0])) ||
        !expect_u64(document, 0u, "schema_version", 1u) ||
        !expect_string(document, 0u, "product_id", profile->product_id) ||
        !expect_string(document, 0u, "product_kind", "bootloader") ||
        !expect_string(document, 0u, "target_id", profile->target_id) ||
        !expect_string(document, 0u, "architecture", "aarch64") ||
        !expect_string(document, 0u, "environment", "uefi") ||
        !expect_string(document, 0u, "port", "qemu-virt-aarch64") ||
        !expect_string(document, 0u, "mode", "normal") ||
        !expect_u64(document, 0u, "max_plugins", 16u) ||
        !expect_string_array(
            document, 0u, "boot_protocols", &profile->boot_protocol, 1u) ||
        !expect_string_array(document, 0u, "policies", (const char *const[]){"normal"}, 1u) ||
        !expect_string_array(document, 0u, "required_capabilities", capabilities, 12u) ||
        !expect_string_array(document, 0u, "allowed_capabilities", capabilities, 12u) ||
        !expect_object_array(document, 0u, "services", services_fields, services, 6u) ||
        !expect_object_array(document, 0u, "plugins", plugin_fields, plugins, 4u) ||
        !object_value(document, 0u, "image", &image) ||
        !object_has_exact_keys(document, image, image_keys, 3u) ||
        !expect_string(document, image, "format", "pe-coff") ||
        !expect_string(document, image, "recipe", "uefi-esp-read-only-config") ||
        !expect_string(document, image, "artifact", "BOOTAA64.EFI") ||
        !object_value(document, 0u, "evidence", &evidence) ||
        !object_has_exact_keys(document, evidence, evidence_keys, 2u) ||
        !expect_string(document, evidence, "class", "qemu-smoke") ||
        !expect_string(document, evidence, "claim", profile->evidence_claim) ||
        !object_value(document, 0u, "limits", &limits) ||
        !object_has_exact_keys(document, limits, limit_keys, 8u) ||
        !expect_u64(document, limits, "max_memory_regions", 256u) ||
        !expect_u64(document, limits, "max_load_segments", 32u) ||
        !expect_u64(document, limits, "max_components", 32u) ||
        !expect_u64(document, limits, "max_retries", 2u) ||
        !expect_u64(document, limits, "max_input_bytes", 67108864u) ||
        !expect_u64(document, limits, "max_handoff_bytes", 65536u) ||
        !expect_u64(document, limits, "arena_bytes", 262144u) ||
        !expect_u64(document, limits, "operation_deadline_ms", 30000u) ||
        !object_value(document, 0u, "service_selections", &selections_array) ||
        document->tokens[selections_array].kind != JSON_TOKEN_ARRAY ||
        document->tokens[selections_array].count != 0u ||
        !object_value(document, 0u, "plugin_selections", &selections_array) ||
        document->tokens[selections_array].kind != JSON_TOKEN_ARRAY ||
        document->tokens[selections_array].count != 2u) {
        return 0;
    }
    cursor = selections_array + 1u;
    for (size_t row = 0u; row < 2u; ++row) {
        static const char *const exact_fields[] = {"id", "kind"};
        (void)selection_fields;
        if (!object_has_exact_keys(document, cursor, exact_fields, 2u) ||
            !expect_string(document, cursor, "id", selections[row].first) ||
            !expect_string(document, cursor, "kind", selections[row].second)) {
            return 0;
        }
        cursor = document->tokens[cursor].after;
    }
    return 1;
}

static unsigned char *read_file(const char *path, size_t *size_out) {
    FILE *stream = fopen(path, "rb");
    unsigned char *bytes;
    long length;
    if (stream == NULL || fseek(stream, 0, SEEK_END) != 0 ||
        (length = ftell(stream)) <= 0 || (unsigned long)length > MANIFEST_BYTE_LIMIT ||
        fseek(stream, 0, SEEK_SET) != 0) {
        if (stream != NULL) {
            fclose(stream);
        }
        return NULL;
    }
    bytes = malloc((size_t)length);
    if (bytes == NULL || fread(bytes, 1u, (size_t)length, stream) != (size_t)length ||
        fclose(stream) != 0) {
        free(bytes);
        return NULL;
    }
    *size_out = (size_t)length;
    return bytes;
}

static int write_registry_source(
    FILE *output,
    const uint8_t digest[32],
    const struct Aarch64UefiProductProfile *profile) {
    fputs("/* Generated by tools/generate_plugin_registry.py; do not edit. */\n"
          "#include <Ribon/plugin/registry.h>\n"
          "#include <Ribon/security/key_policy.h>\n"
          "#include <Ribon/security/protected_state.h>\n"
          "#include <Ribon/security/signature.h>\n"
          "#include <Ribon/network/recovery.h>\n"
          "#include <Ribon/update/storage.h>\n\n\n"
          "extern const struct RibonPluginDescriptor ribon_arch_plugin_descriptor;\n"
          "extern const struct RibonPluginDescriptor ribon_uefi_app_environment_plugin_descriptor;\n"
          "extern const struct RibonPluginDescriptor ribon_elf64_image_plugin_descriptor;\n",
          output);
    fprintf(output, "extern const struct RibonPluginDescriptor %s;\n",
            profile->protocol_symbol);
    fputs(
          "extern const struct RibonServiceDescriptor ribon_port_diagnostic_sink_service_descriptor;\n"
          "extern const struct RibonServiceDescriptor ribon_uefi_app_boot_source_service_descriptor;\n"
          "extern const struct RibonServiceDescriptor ribon_uefi_app_environment_quiesce_service_descriptor;\n"
          "extern const struct RibonServiceDescriptor ribon_uefi_app_monotonic_timer_service_descriptor;\n"
          "extern const struct RibonServiceDescriptor ribon_uefi_app_persistent_metadata_service_descriptor;\n"
          "extern const struct RibonServiceDescriptor ribon_uefi_app_storage_flush_service_descriptor;\n\n\n"
          "static const uint8_t generated_product_source_digest[32] = {\n    ", output);
    for (size_t index = 0u; index < 32u; ++index) {
        fprintf(output, "0x%02xu%s", digest[index], index + 1u == 32u ? "\n" : ", ");
    }
    fputs("};\n\n\n"
          "const struct RibonKeyPolicyStore *ribon_generated_key_policy_store(void) {\n"
          "    return 0;\n"
          "}\n\n\n"
          "const struct RibonProtectedStateProductBinding *\n"
          "ribon_generated_protected_state_binding(void) {\n"
          "    return 0;\n"
          "}\n\n\n"
          "const struct RibonUpdateStorageProductBinding *\n"
          "ribon_generated_update_storage_binding(void) {\n"
          "    return 0;\n"
          "}\n\n\n"
          "const struct RibonRecoveryNetworkProductBinding *\n"
          "ribon_generated_recovery_network_binding(void) {\n"
          "    return 0;\n"
          "}\n\n\n"
          "static const struct RibonPluginDescriptor *const generated_plugins[] = {\n"
          "    &ribon_arch_plugin_descriptor,\n"
          "    &ribon_uefi_app_environment_plugin_descriptor,\n"
          "    &ribon_elf64_image_plugin_descriptor,\n",
          output);
    fprintf(output, "    &%s,\n", profile->protocol_symbol);
    fputs(
          "};\n\n"
          "static const struct RibonPluginRegistry generated_registry = {\n"
          "    .size = sizeof(generated_registry),\n"
          "    .abi_version = RIBON_CORE_ABI_VERSION,\n"
          "    .plugins = generated_plugins,\n"
          "    .plugin_count = (uint32_t)(sizeof(generated_plugins) / sizeof(generated_plugins[0])),\n"
          "};\n\n"
          "static const struct RibonServiceDescriptor *const generated_services[] = {\n"
          "    &ribon_port_diagnostic_sink_service_descriptor,\n"
          "    &ribon_uefi_app_boot_source_service_descriptor,\n"
          "    &ribon_uefi_app_environment_quiesce_service_descriptor,\n"
          "    &ribon_uefi_app_monotonic_timer_service_descriptor,\n"
          "    &ribon_uefi_app_persistent_metadata_service_descriptor,\n"
          "    &ribon_uefi_app_storage_flush_service_descriptor,\n"
          "};\n\n\n\n"
          "static const struct RibonPluginSelection generated_plugin_selections[] = {\n"
          "    { .kind = RIBON_PLUGIN_KIND_IMAGE_FORMAT, .id = \"image.elf64\" },\n",
          output);
    fprintf(output,
          "    { .kind = RIBON_PLUGIN_KIND_BOOT_PROTOCOL, .id = \"%s\" },\n",
          profile->protocol_plugin_id);
    fputs(
          "};\n\n"
          "static const struct RibonServiceDirectory generated_service_directory = {\n"
          "    .size = sizeof(generated_service_directory),\n"
          "    .abi_version = RIBON_SERVICE_DIRECTORY_ABI_VERSION,\n"
          "    .services = generated_services,\n"
          "    .service_count = 6u,\n"
          "};\n\n"
          "static const struct RibonProductDescriptor generated_product = {\n"
          "    .magic = RIBON_PRODUCT_DESCRIPTOR_MAGIC,\n"
          "    .size = sizeof(generated_product),\n"
          "    .abi_version = RIBON_CORE_ABI_VERSION,\n",
          output);
    fprintf(output, "    .id = \"%s\",\n", profile->product_id);
    fputs(
          "    .kind = RIBON_PRODUCT_KIND_BOOTLOADER,\n"
          "    .architecture_mask = RIBON_ARCH_MASK_AARCH64,\n"
          "    .environment_mask = RIBON_ENV_MASK_UEFI,\n"
          "    .personality_mask = 0u,\n"
          "    .mode_mask = RIBON_MODE_MASK(RIBON_MODE_NORMAL),\n"
          "    .max_plugins = 16u,\n"
          "    .required_capabilities =\n"
          "        RIBON_CAP_ARCHITECTURE |\n"
          "        RIBON_CAP_BOOT_CONFIRMATION |\n"
          "        RIBON_CAP_BOOT_PROTOCOL |\n"
          "        RIBON_CAP_BOOT_SOURCE_READ |\n"
          "        RIBON_CAP_DIAGNOSTIC_SINK |\n"
          "        RIBON_CAP_ENTRY_CONTRACT |\n"
          "        RIBON_CAP_ENVIRONMENT_QUIESCE |\n"
          "        RIBON_CAP_HANDOFF |\n"
          "        RIBON_CAP_IMAGE_ELF64 |\n"
          "        RIBON_CAP_MONOTONIC_TIMER |\n"
          "        RIBON_CAP_PERSISTENT_METADATA |\n"
          "        RIBON_CAP_STORAGE_FLUSH,\n"
          "    .allowed_capabilities =\n"
          "        RIBON_CAP_ARCHITECTURE |\n"
          "        RIBON_CAP_BOOT_CONFIRMATION |\n"
          "        RIBON_CAP_BOOT_PROTOCOL |\n"
          "        RIBON_CAP_BOOT_SOURCE_READ |\n"
          "        RIBON_CAP_DIAGNOSTIC_SINK |\n"
          "        RIBON_CAP_ENTRY_CONTRACT |\n"
          "        RIBON_CAP_ENVIRONMENT_QUIESCE |\n"
          "        RIBON_CAP_HANDOFF |\n"
          "        RIBON_CAP_IMAGE_ELF64 |\n"
          "        RIBON_CAP_MONOTONIC_TIMER |\n"
          "        RIBON_CAP_PERSISTENT_METADATA |\n"
          "        RIBON_CAP_STORAGE_FLUSH,\n"
          "    .service_selections = 0,\n"
          "    .service_selection_count = 0u,\n"
          "    .plugin_selections = generated_plugin_selections,\n"
          "    .plugin_selection_count = 2u,\n"
          "    .limits = {\n"
          "        .max_memory_regions = 256u,\n"
          "        .max_load_segments = 32u,\n"
          "        .max_components = 32u,\n"
          "        .max_retries = 2u,\n"
          "        .max_input_bytes = 67108864ull,\n"
          "        .max_handoff_bytes = 65536ull,\n"
          "        .arena_bytes = 262144ull,\n"
          "        .operation_deadline_ms = 30000u,\n"
          "    },\n"
          "};\n\n"
          "const struct RibonPluginRegistry *ribon_generated_plugin_registry(void) {\n"
          "    return &generated_registry;\n"
          "}\n\n"
          "const struct RibonProductDescriptor *ribon_generated_product_descriptor(void) {\n"
          "    return &generated_product;\n"
          "}\n\n"
          "const uint8_t *ribon_generated_product_source_digest(void) {\n"
          "    return generated_product_source_digest;\n"
          "}\n\n"
          "const struct RibonSignatureProvider *ribon_generated_signature_provider(void) {\n"
          "    return 0;\n"
          "}\n\n"
          "const struct RibonServiceDirectory *ribon_generated_service_directory(void) {\n"
          "    return &generated_service_directory;\n"
          "}\n\n",
          output);
    return ferror(output) == 0;
}

static void digest_hex(const uint8_t digest[32], char out[65]) {
    static const char digits[] = "0123456789abcdef";
    for (size_t index = 0u; index < 32u; ++index) {
        out[index * 2u] = digits[digest[index] >> 4u];
        out[index * 2u + 1u] = digits[digest[index] & 0x0fu];
    }
    out[64] = '\0';
}

static int write_registry_report(
    FILE *output,
    const char *manifest,
    const uint8_t digest[32],
    const struct Aarch64UefiProductProfile *profile) {
    const char *name = strrchr(manifest, '/');
    char hex[65];
    name = name == NULL ? manifest : name + 1;
    digest_hex(digest, hex);
    fprintf(output,
        "{\n"
        "  \"allowed_capabilities\": [\n"
        "    \"ARCHITECTURE\",\n"
        "    \"BOOT_CONFIRMATION\",\n"
        "    \"BOOT_PROTOCOL\",\n"
        "    \"BOOT_SOURCE_READ\",\n"
        "    \"DIAGNOSTIC_SINK\",\n"
        "    \"ENTRY_CONTRACT\",\n"
        "    \"ENVIRONMENT_QUIESCE\",\n"
        "    \"HANDOFF\",\n"
        "    \"IMAGE_ELF64\",\n"
        "    \"MONOTONIC_TIMER\",\n"
        "    \"PERSISTENT_METADATA\",\n"
        "    \"STORAGE_FLUSH\"\n"
        "  ],\n"
        "  \"architecture\": \"aarch64\",\n"
        "  \"boot_module_bundle\": null,\n"
        "  \"environment\": \"uefi\",\n"
        "  \"evidence\": {\n"
        "    \"claim\": \"%s\",\n"
        "    \"class\": \"qemu-smoke\"\n"
        "  },\n"
        "  \"firmware_personality\": null,\n"
        "  \"image\": {\n"
        "    \"artifact\": \"BOOTAA64.EFI\",\n"
        "    \"format\": \"pe-coff\",\n"
        "    \"recipe\": \"uefi-esp-read-only-config\"\n"
        "  },\n"
        "  \"key_policy\": null,\n"
        "  \"key_policy_digest_sha256\": null,\n"
        "  \"mode\": \"normal\",\n"
        "  \"packages\": [\n"
        "    \"ribon.arch.aarch64\",\n"
        "    \"ribon.environment.uefi-app\",\n"
        "    \"ribon.image.elf64\",\n"
        "    \"%s\"\n"
        "  ],\n"
        "  \"payload\": null,\n"
        "  \"plugin_selections\": [\n"
        "    {\n"
        "      \"id\": \"image.elf64\",\n"
        "      \"kind\": \"image-format\"\n"
        "    },\n"
        "    {\n"
        "      \"id\": \"%s\",\n"
        "      \"kind\": \"boot-protocol\"\n"
        "    }\n"
        "  ],\n"
        "  \"plugins\": [\n"
        "    \"arch.aarch64\",\n"
        "    \"environment.uefi-app\",\n"
        "    \"image.elf64\",\n"
        "    \"%s\"\n"
        "  ],\n"
        "  \"port\": \"qemu-virt-aarch64\",\n"
        "  \"product_id\": \"%s\",\n"
        "  \"product_kind\": \"bootloader\",\n"
        "  \"protected_state_domain_digests_sha256\": null,\n"
        "  \"protected_state_provider\": null,\n"
        "  \"recovery_network\": null,\n"
        "  \"required_capabilities\": [\n"
        "    \"ARCHITECTURE\",\n"
        "    \"BOOT_CONFIRMATION\",\n"
        "    \"BOOT_PROTOCOL\",\n"
        "    \"BOOT_SOURCE_READ\",\n"
        "    \"DIAGNOSTIC_SINK\",\n"
        "    \"ENTRY_CONTRACT\",\n"
        "    \"ENVIRONMENT_QUIESCE\",\n"
        "    \"HANDOFF\",\n"
        "    \"IMAGE_ELF64\",\n"
        "    \"MONOTONIC_TIMER\",\n"
        "    \"PERSISTENT_METADATA\",\n"
        "    \"STORAGE_FLUSH\"\n"
        "  ],\n"
        "  \"ribos_policy\": null,\n"
        "  \"service_selections\": [],\n"
        "  \"services\": [\n"
        "    {\n"
        "      \"id\": \"service.port.diagnostic-sink\",\n"
        "      \"kind\": \"diagnostic-sink\",\n"
        "      \"symbol\": \"ribon_port_diagnostic_sink_service_descriptor\"\n"
        "    },\n"
        "    {\n"
        "      \"id\": \"service.uefi-app.boot-source\",\n"
        "      \"kind\": \"boot-source\",\n"
        "      \"symbol\": \"ribon_uefi_app_boot_source_service_descriptor\"\n"
        "    },\n"
        "    {\n"
        "      \"id\": \"service.uefi-app.environment-quiesce\",\n"
        "      \"kind\": \"environment-quiesce\",\n"
        "      \"symbol\": \"ribon_uefi_app_environment_quiesce_service_descriptor\"\n"
        "    },\n"
        "    {\n"
        "      \"id\": \"service.uefi-app.monotonic-timer\",\n"
        "      \"kind\": \"monotonic-timer\",\n"
        "      \"symbol\": \"ribon_uefi_app_monotonic_timer_service_descriptor\"\n"
        "    },\n"
        "    {\n"
        "      \"id\": \"service.uefi-app.persistent-metadata\",\n"
        "      \"kind\": \"persistent-metadata\",\n"
        "      \"symbol\": \"ribon_uefi_app_persistent_metadata_service_descriptor\"\n"
        "    },\n"
        "    {\n"
        "      \"id\": \"service.uefi-app.storage-flush\",\n"
        "      \"kind\": \"storage-flush\",\n"
        "      \"symbol\": \"ribon_uefi_app_storage_flush_service_descriptor\"\n"
        "    }\n"
        "  ],\n"
        "  \"signature_provider\": null,\n"
        "  \"source_manifest\": \"%s\",\n"
        "  \"source_manifest_sha256\": \"%s\",\n"
        "  \"target_id\": \"%s\",\n"
        "  \"update_storage\": null\n"
        "}\n",
        profile->evidence_claim,
        profile->protocol_package,
        profile->protocol_plugin_id,
        profile->protocol_plugin_id,
        profile->product_id,
        name,
        hex,
        profile->target_id);
    return ferror(output) == 0;
}

static int generate_registry(const char *manifest, const char *source_path, const char *report_path) {
    size_t size = 0u;
    unsigned char *bytes = read_file(manifest, &size);
    struct JsonDocument document;
    uint8_t digest[32];
    const struct Aarch64UefiProductProfile *profile = 0;
    FILE *source;
    FILE *report;
    int ok;
    if (bytes != NULL && parse_manifest((const char *)bytes, size, &document)) {
        if (expect_string(&document, 0u, "product_id", fixture_profile.product_id)) {
            profile = &fixture_profile;
        } else if (expect_string(
                       &document, 0u, "product_id", direct_fdt_profile.product_id)) {
            profile = &direct_fdt_profile;
        }
    }
    if (bytes == NULL || profile == 0 || !validate_manifest(&document, profile)) {
        fprintf(stderr, "RIBON-AARCH64-UEFI-GEN-FAIL malformed-product-manifest\n");
        free(bytes);
        return 1;
    }
    ribos_artifact_sha256(bytes, size, digest);
    source = fopen(source_path, "wb");
    report = fopen(report_path, "wb");
    if (source == NULL || report == NULL) {
        fprintf(stderr, "RIBON-AARCH64-UEFI-GEN-FAIL output-open errno=%d\n", errno);
        if (source != NULL) {
            fclose(source);
        }
        if (report != NULL) {
            fclose(report);
        }
        free(bytes);
        return 1;
    }
    ok = write_registry_source(source, digest, profile) &&
         write_registry_report(report, manifest, digest, profile) &&
         fclose(source) == 0 && fclose(report) == 0;
    free(bytes);
    return ok ? 0 : 1;
}

static int write_exact_file(const char *path, const void *bytes, size_t size) {
    FILE *stream = fopen(path, "wb");
    int ok;
    if (stream == NULL) {
        return 0;
    }
    ok = fwrite(bytes, 1u, size, stream) == size;
    ok = fclose(stream) == 0 && ok;
    return ok;
}

static void write_u16(unsigned char *bytes, size_t offset, uint16_t value) {
    bytes[offset] = (unsigned char)value;
    bytes[offset + 1u] = (unsigned char)(value >> 8u);
}

static void write_u32(unsigned char *bytes, size_t offset, uint32_t value) {
    for (size_t index = 0u; index < 4u; ++index) {
        bytes[offset + index] = (unsigned char)(value >> (index * 8u));
    }
}

static void write_u64(unsigned char *bytes, size_t offset, uint64_t value) {
    for (size_t index = 0u; index < 8u; ++index) {
        bytes[offset + index] = (unsigned char)(value >> (index * 8u));
    }
}

static uint32_t encode_mov_wide(uint32_t base, unsigned reg, unsigned immediate, unsigned shift) {
    return base | ((shift / 16u) << 21u) | (immediate << 5u) | reg;
}

static uint32_t encode_imm19(uint32_t base, size_t instruction, size_t target) {
    const int64_t delta = (int64_t)target - (int64_t)instruction;
    return base | (((uint32_t)(delta / 4) & 0x7ffffu) << 5u);
}

static uint32_t encode_branch(size_t instruction, size_t target) {
    const int64_t delta = (int64_t)target - (int64_t)instruction;
    return 0x14000000u | ((uint32_t)(delta / 4) & 0x03ffffffu);
}

static size_t append_serial_code(unsigned char *out, const char *message) {
    const size_t message_offset = 13u * 4u;
    const uint32_t words[] = {
        encode_mov_wide(0xd2800000u, 0u, 0x0000u, 0u),
        encode_mov_wide(0xf2800000u, 0u, 0x0900u, 16u),
        0x10000000u | ((((uint32_t)(message_offset - 8u)) & 3u) << 29u) |
            (((uint32_t)(message_offset - 8u) >> 2u) << 5u) | 1u,
        0x39400022u,
        0x91000421u,
        encode_imm19(0x34000002u, 5u * 4u, 11u * 4u),
        0xb9401803u,
        0x36000000u | (5u << 19u) | ((((9u - 7u) & 0x3fffu)) << 5u) | 3u,
        encode_branch(8u * 4u, 6u * 4u),
        0xb9000002u,
        encode_branch(10u * 4u, 3u * 4u),
        0xd503205fu,
        encode_branch(12u * 4u, 11u * 4u),
    };
    size_t cursor = 0u;
    for (size_t index = 0u; index < sizeof(words) / sizeof(words[0]); ++index) {
        write_u32(out, cursor, words[index]);
        cursor += 4u;
    }
    memcpy(out + cursor, message, strlen(message) + 1u);
    return cursor + strlen(message) + 1u;
}

static size_t build_entry_code(unsigned char *out) {
    unsigned char success[256];
    unsigned char failure[256];
    const size_t success_size = append_serial_code(success, "PARUS-FIXTURE-ENTRY-OK\r\n");
    const size_t failure_size = append_serial_code(failure, "PARUS-FIXTURE-ENTRY-ABI-FAIL\r\n");
    const size_t padded_success = (success_size + 3u) & ~3u;
    const size_t failure_offset = 12u + padded_success;
    write_u32(out, 0u, encode_imm19(0xb4000000u, 0u, failure_offset));
    write_u32(out, 4u, 0xf100001fu | (1u << 10u) | (1u << 5u));
    write_u32(out, 8u, encode_imm19(0x54000001u, 8u, failure_offset));
    memcpy(out + 12u, success, success_size);
    memset(out + 12u + success_size, 0, padded_success - success_size);
    memcpy(out + failure_offset, failure, failure_size);
    return failure_offset + failure_size;
}

static int generate_elf_fixture(const char *path) {
    static const unsigned char ident[16] = {
        0x7fu, 'E', 'L', 'F', 2u, 1u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    };
    static const char provenance[] = "RIBON-FIXTURE-PAYLOAD-V1";
    unsigned char bytes[FIXTURE_BYTE_CAPACITY] = {0};
    unsigned char code[1024];
    const size_t code_size = build_entry_code(code);
    const size_t segment_bytes = (code_size + 15u) & ~15u;
    const size_t image_size = 0x1000u + segment_bytes;
    const size_t total_size = image_size + sizeof(provenance) - 1u;
    if (total_size > sizeof(bytes)) {
        return 1;
    }
    memcpy(bytes, ident, sizeof(ident));
    write_u16(bytes, 16u, 2u);
    write_u16(bytes, 18u, 183u);
    write_u32(bytes, 20u, 1u);
    write_u64(bytes, 24u, 0x41000000ull);
    write_u64(bytes, 32u, 64u);
    write_u16(bytes, 52u, 64u);
    write_u16(bytes, 54u, 56u);
    write_u16(bytes, 56u, 1u);
    write_u32(bytes, 64u, 1u);
    write_u32(bytes, 68u, 5u);
    write_u64(bytes, 72u, 0x1000u);
    write_u64(bytes, 80u, 0x41000000ull);
    write_u64(bytes, 88u, 0x41000000ull);
    write_u64(bytes, 96u, segment_bytes);
    write_u64(bytes, 104u, 0x1000u);
    write_u64(bytes, 112u, 0x1000u);
    memcpy(bytes + 0x1000u, code, code_size);
    memcpy(bytes + image_size, provenance, sizeof(provenance) - 1u);
    return write_exact_file(path, bytes, total_size) ? 0 : 1;
}

static int generate_boot_config(const char *path) {
    static const char config[] =
        "version=1\n"
        "entry=primary\n"
        "priority=100\n"
        "protocol=protocol.luca\n"
        "image=image.elf64\n"
        "kernel=/RIBON/PAYLOAD.ELF\n"
        "init_image=/RIBON/INIT.IMG\n"
        "end\n";
    return write_exact_file(path, config, sizeof(config) - 1u) ? 0 : 1;
}

static int generate_direct_fdt_boot_config(const char *path) {
    static const char config[] =
        "version=1\n"
        "entry=primary\n"
        "priority=100\n"
        "protocol=protocol.luca-direct-fdt-dev\n"
        "image=image.elf64\n"
        "kernel=/RIBON/LUCA.ELF\n"
        "init_image=/RIBON/WORLD.PKG\n"
        "end\n";
    return write_exact_file(path, config, sizeof(config) - 1u) ? 0 : 1;
}

static int generate_init_image(const char *path) {
    static const char prefix[] = "RIBON-OPAQUE-INITIAL-IMAGE-V1\n";
    unsigned char bytes[4096] = {0};
    memcpy(bytes, prefix, sizeof(prefix) - 1u);
    return write_exact_file(path, bytes, sizeof(bytes)) ? 0 : 1;
}

static void usage(const char *program) {
    fprintf(stderr,
        "usage: %s registry MANIFEST OUTPUT_C REPORT_JSON\n"
        "       %s boot-config OUTPUT\n"
        "       %s luca-direct-fdt-config OUTPUT\n"
        "       %s elf-fixture OUTPUT\n"
        "       %s init-image OUTPUT\n",
        program, program, program, program, program);
}

int main(int argc, char **argv) {
    if (argc == 6 && strcmp(argv[1], "registry") == 0 &&
        strcmp(argv[2], "--manifest") == 0) {
        return generate_registry(argv[3], argv[4], argv[5]);
    }
    if (argc == 3 && strcmp(argv[1], "boot-config") == 0) {
        return generate_boot_config(argv[2]);
    }
    if (argc == 3 && strcmp(argv[1], "luca-direct-fdt-config") == 0) {
        return generate_direct_fdt_boot_config(argv[2]);
    }
    if (argc == 3 && strcmp(argv[1], "elf-fixture") == 0) {
        return generate_elf_fixture(argv[2]);
    }
    if (argc == 3 && strcmp(argv[1], "init-image") == 0) {
        return generate_init_image(argv[2]);
    }
    usage(argv[0]);
    return 2;
}
