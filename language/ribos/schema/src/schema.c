#include "ribos/schema/schema.h"

#include <string.h>

#define RIBOS_SCHEMA_MAGIC "RBSCHM1"
#define RIBOS_SCHEMA_MAGIC_BYTES 7u

typedef struct RibosSha256 RibosSha256;

typedef struct RibosSchemaWriter {
    uint8_t *output;
    size_t capacity;
    size_t size;
    int overflow;
    RibosSha256 *hash;
} RibosSchemaWriter;

struct RibosSha256 {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t block[64];
    size_t block_size;
};

static const RibosSchemaType ribos_reference_types[] = {
    {1, RIBOS_SCHEMA_TYPE_FACT, "BootContext"},
    {2, RIBOS_SCHEMA_TYPE_VALUE, "BootAction"},
    {3, RIBOS_SCHEMA_TYPE_ENUM, "BootError"},
    {4, RIBOS_SCHEMA_TYPE_ENUM, "BoardRevision"},
    {5, RIBOS_SCHEMA_TYPE_ENUM, "Profile"},
    {6, RIBOS_SCHEMA_TYPE_ENUM, "Device"},
    {7, RIBOS_SCHEMA_TYPE_OPAQUE_HANDLE, "Slot"},
    {8, RIBOS_SCHEMA_TYPE_ENUM, "Channel"},
    {9, RIBOS_SCHEMA_TYPE_ENUM, "Core"},
    {10, RIBOS_SCHEMA_TYPE_OPAQUE_HANDLE, "Image"},
    {11, RIBOS_SCHEMA_TYPE_OPAQUE_HANDLE, "VerifiedImage"},
    {12, RIBOS_SCHEMA_TYPE_ENUM, "RecoveryReason"},
    {13, RIBOS_SCHEMA_TYPE_ENUM, "Diagnostic"},
    {14, RIBOS_SCHEMA_TYPE_ENUM, "HandoffKey"},
    {15, RIBOS_SCHEMA_TYPE_VALUE, "ImageId"},
    {16, RIBOS_SCHEMA_TYPE_OPAQUE_HANDLE, "UpdateReceipt"},
    {17, RIBOS_SCHEMA_TYPE_ENUM, "Capability"},
    {18, RIBOS_SCHEMA_TYPE_ENUM, "DeviceError"},
    {19, RIBOS_SCHEMA_TYPE_ENUM, "NetworkError"},
    {20, RIBOS_SCHEMA_TYPE_ENUM, "UpdateError"},
    {21, RIBOS_SCHEMA_TYPE_ENUM, "VerifyError"},
    {22, RIBOS_SCHEMA_TYPE_FACT, "BoardFacts"},
    {23, RIBOS_SCHEMA_TYPE_FACT, "PowerFacts"},
    {24, RIBOS_SCHEMA_TYPE_FACT, "SystemFacts"},
    {25, RIBOS_SCHEMA_TYPE_VALUE, "DeviceSet"},
    {26, RIBOS_SCHEMA_TYPE_OPAQUE_HANDLE, "Manifest"},
};

static const RibosSchemaMember ribos_reference_members[] = {
    {1, "BootContext", "board", "BoardFacts", NULL, 0},
    {2, "BootContext", "power", "PowerFacts", NULL, 0},
    {3, "BootContext", "system", "SystemFacts", NULL, 0},
    {4, "BootContext", "devices", NULL, "Device", 32},
    {5, "BootContext", "recovery_requested", "bool", NULL, 0},
    {6, "BoardFacts", "revision", "BoardRevision", NULL, 0},
    {7, "PowerFacts", "battery_percent", "u8", NULL, 0},
    {8, "SystemFacts", "on_ground", "bool", NULL, 0},
};

static const RibosSchemaHelper ribos_reference_helpers[] = {
    {
        1, "device.init", RIBOS_CAPABILITY_DEVICE, "Unit", "BootError",
        {{"device", "Device"}, {"profile", "Profile"}}, 2,
    },
    {
        2, "slot.selected", RIBOS_CAPABILITY_INSPECT, "Slot", NULL,
        {{NULL, NULL}}, 0,
    },
    {
        3, "slot.active", RIBOS_CAPABILITY_INSPECT, "Slot", NULL,
        {{NULL, NULL}}, 0,
    },
    {
        4, "slot.inactive", RIBOS_CAPABILITY_INSPECT, "Slot", NULL,
        {{NULL, NULL}}, 0,
    },
    {
        5, "slot.failures", RIBOS_CAPABILITY_INSPECT, "u32", NULL,
        {{"slot", "Slot"}}, 1,
    },
    {
        6, "slot.previous_good", RIBOS_CAPABILITY_STATE, "Slot", "BootError",
        {{NULL, NULL}}, 0,
    },
    {
        7, "slot.mark_bad", RIBOS_CAPABILITY_STATE, "Unit", "BootError",
        {{"slot", "Slot"}}, 1,
    },
    {
        8, "slot.image", RIBOS_CAPABILITY_INSPECT, "Image", "BootError",
        {{"slot", "Slot"}}, 1,
    },
    {
        9, "slot.mark_trial", RIBOS_CAPABILITY_STATE, "Unit", "BootError",
        {
            {"receipt", "UpdateReceipt"},
            {"attempts", "u32"},
            {"commit", "bool"},
        }, 3,
    },
    {
        10, "image.require", RIBOS_CAPABILITY_INSPECT, "Image", "BootError",
        {{"id", "ImageId"}}, 1,
    },
    {
        11, "image.verify", RIBOS_CAPABILITY_INSPECT, "VerifiedImage",
        "BootError", {{"image", "Image"}}, 1,
    },
    {
        12, "core.start", RIBOS_CAPABILITY_DEVICE, "Unit", "BootError",
        {{"core", "Core"}, {"image", "VerifiedImage"}}, 2,
    },
    {
        13, "handoff.set", RIBOS_CAPABILITY_HANDOFF, "Unit", "BootError",
        {{"key", "HandoffKey"}, {"value", "*"}}, 2,
    },
    {
        14, "ota.available", RIBOS_CAPABILITY_NETWORK, "bool", NULL,
        {{"channel", "Channel"}}, 1,
    },
    {
        15, "ota.install",
        RIBOS_CAPABILITY_NETWORK | RIBOS_CAPABILITY_FLASH,
        "UpdateReceipt", "BootError",
        {{"channel", "Channel"}, {"slot", "Slot"}}, 2,
    },
    {
        16, "network.ready", RIBOS_CAPABILITY_NETWORK, "bool", NULL,
        {{NULL, NULL}}, 0,
    },
    {
        17, "network.fetch_signed_manifest", RIBOS_CAPABILITY_NETWORK,
        "Manifest", "BootError", {{"channel", "Channel"}}, 1,
    },
    {
        18, "power.safe", RIBOS_CAPABILITY_INSPECT, "bool", NULL,
        {{NULL, NULL}}, 0,
    },
    {
        19, "diagnostic.note", RIBOS_CAPABILITY_DIAGNOSTIC, "Unit",
        "BootError", {{"message", "*"}}, 1,
    },
    {
        20, "diagnostic.record_verify_error",
        RIBOS_CAPABILITY_DIAGNOSTIC, "Unit", "BootError",
        {{"error", "*"}}, 1,
    },
    {
        21, "boot.slot", RIBOS_CAPABILITY_BOOT, "BootAction", NULL,
        {{"slot", "Slot"}, {"image", "VerifiedImage"}}, 2,
    },
    {
        22, "boot.recovery", RIBOS_CAPABILITY_BOOT, "BootAction", NULL,
        {{"reason", "RecoveryReason"}}, 1,
    },
};

static const RibosSchemaHandoffField ribos_reference_handoff[] = {
    {1, "BOARD_REVISION", "BoardRevision"},
    {2, "DELOS_READY", "bool"},
    {3, "BOOT_SLOT", "Slot"},
};

static const RibosProductSchema ribos_reference_schema = {
    .format_major = RIBOS_SCHEMA_V1_MAJOR,
    .format_minor = RIBOS_SCHEMA_V1_MINOR,
    .product_id = "ribon.generic.reference.v1",
    .types = ribos_reference_types,
    .type_count =
        sizeof(ribos_reference_types) / sizeof(ribos_reference_types[0]),
    .members = ribos_reference_members,
    .member_count =
        sizeof(ribos_reference_members) / sizeof(ribos_reference_members[0]),
    .helpers = ribos_reference_helpers,
    .helper_count =
        sizeof(ribos_reference_helpers) / sizeof(ribos_reference_helpers[0]),
    .handoff_fields = ribos_reference_handoff,
    .handoff_field_count =
        sizeof(ribos_reference_handoff) / sizeof(ribos_reference_handoff[0]),
};

static uint32_t
ribos_sha_rotate_right(uint32_t value, unsigned shift)
{
    return (value >> shift) | (value << (32u - shift));
}

static void
ribos_sha256_transform(RibosSha256 *context, const uint8_t block[64])
{
    static const uint32_t constants[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };
    uint32_t words[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    size_t index;

    for (index = 0; index < 16; ++index) {
        size_t offset = index * 4;

        words[index] =
            ((uint32_t)block[offset] << 24) |
            ((uint32_t)block[offset + 1] << 16) |
            ((uint32_t)block[offset + 2] << 8) |
            (uint32_t)block[offset + 3];
    }
    for (index = 16; index < 64; ++index) {
        uint32_t left = words[index - 15];
        uint32_t right = words[index - 2];
        uint32_t s0 = ribos_sha_rotate_right(left, 7) ^
            ribos_sha_rotate_right(left, 18) ^ (left >> 3);
        uint32_t s1 = ribos_sha_rotate_right(right, 17) ^
            ribos_sha_rotate_right(right, 19) ^ (right >> 10);

        words[index] = words[index - 16] + s0 +
            words[index - 7] + s1;
    }
    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];
    for (index = 0; index < 64; ++index) {
        uint32_t sum1 = ribos_sha_rotate_right(e, 6) ^
            ribos_sha_rotate_right(e, 11) ^
            ribos_sha_rotate_right(e, 25);
        uint32_t choose = (e & f) ^ ((~e) & g);
        uint32_t temporary1 =
            h + sum1 + choose + constants[index] + words[index];
        uint32_t sum0 = ribos_sha_rotate_right(a, 2) ^
            ribos_sha_rotate_right(a, 13) ^
            ribos_sha_rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temporary2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }
    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

static void
ribos_sha256_initialize(RibosSha256 *context)
{
    *context = (RibosSha256){
        .state = {
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
        },
    };
}

static void
ribos_sha256_update(
    RibosSha256 *context,
    const uint8_t *bytes,
    size_t length)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        context->block[context->block_size++] = bytes[index];
        context->bit_count += 8;
        if (context->block_size == sizeof(context->block)) {
            ribos_sha256_transform(context, context->block);
            context->block_size = 0;
        }
    }
}

static void
ribos_sha256_finish(
    RibosSha256 *context,
    uint8_t digest[RIBOS_SCHEMA_DIGEST_BYTES])
{
    uint64_t bit_count = context->bit_count;
    size_t index;

    context->block[context->block_size++] = 0x80;
    if (context->block_size > 56) {
        while (context->block_size < 64) {
            context->block[context->block_size++] = 0;
        }
        ribos_sha256_transform(context, context->block);
        context->block_size = 0;
    }
    while (context->block_size < 56) {
        context->block[context->block_size++] = 0;
    }
    for (index = 0; index < 8; ++index) {
        context->block[63 - index] = (uint8_t)(bit_count >> (index * 8));
    }
    ribos_sha256_transform(context, context->block);
    for (index = 0; index < 8; ++index) {
        digest[index * 4] = (uint8_t)(context->state[index] >> 24);
        digest[index * 4 + 1] =
            (uint8_t)(context->state[index] >> 16);
        digest[index * 4 + 2] =
            (uint8_t)(context->state[index] >> 8);
        digest[index * 4 + 3] = (uint8_t)context->state[index];
    }
}

static void
ribos_schema_write_bytes(
    RibosSchemaWriter *writer,
    const void *bytes,
    size_t length)
{
    if (SIZE_MAX - writer->size < length) {
        writer->overflow = 1;
        return;
    }
    if (writer->output != NULL) {
        if (writer->size > writer->capacity ||
            length > writer->capacity - writer->size) {
            writer->overflow = 1;
        } else if (length != 0) {
            memcpy(writer->output + writer->size, bytes, length);
        }
    }
    if (writer->hash != NULL && length != 0) {
        ribos_sha256_update(writer->hash, bytes, length);
    }
    writer->size += length;
}

static void
ribos_schema_write_u16(RibosSchemaWriter *writer, uint16_t value)
{
    uint8_t bytes[2] = {
        (uint8_t)value,
        (uint8_t)(value >> 8),
    };

    ribos_schema_write_bytes(writer, bytes, sizeof(bytes));
}

static void
ribos_schema_write_u32(RibosSchemaWriter *writer, uint32_t value)
{
    uint8_t bytes[4] = {
        (uint8_t)value,
        (uint8_t)(value >> 8),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 24),
    };

    ribos_schema_write_bytes(writer, bytes, sizeof(bytes));
}

static void
ribos_schema_write_string(RibosSchemaWriter *writer, const char *text)
{
    size_t length = text == NULL ? 0 : strlen(text);

    if (length > UINT16_MAX) {
        writer->overflow = 1;
        return;
    }
    ribos_schema_write_u16(writer, (uint16_t)length);
    ribos_schema_write_bytes(writer, text, length);
}

static RibosSchemaStatus
ribos_schema_encode_into(
    const RibosProductSchema *schema,
    RibosSchemaWriter *writer)
{
    size_t index;

    if (ribos_schema_validate(schema) != RIBOS_SCHEMA_OK) {
        return RIBOS_SCHEMA_INVALID_DESCRIPTOR;
    }
    ribos_schema_write_bytes(
        writer,
        RIBOS_SCHEMA_MAGIC,
        RIBOS_SCHEMA_MAGIC_BYTES);
    ribos_schema_write_u16(writer, schema->format_major);
    ribos_schema_write_u16(writer, schema->format_minor);
    ribos_schema_write_string(writer, schema->product_id);
    ribos_schema_write_u32(writer, (uint32_t)schema->type_count);
    for (index = 0; index < schema->type_count; ++index) {
        const RibosSchemaType *type = &schema->types[index];

        ribos_schema_write_u32(writer, type->stable_id);
        ribos_schema_write_u32(writer, (uint32_t)type->type_class);
        ribos_schema_write_string(writer, type->name);
    }
    ribos_schema_write_u32(writer, (uint32_t)schema->member_count);
    for (index = 0; index < schema->member_count; ++index) {
        const RibosSchemaMember *member = &schema->members[index];

        ribos_schema_write_u32(writer, member->stable_id);
        ribos_schema_write_string(writer, member->owner_type);
        ribos_schema_write_string(writer, member->name);
        ribos_schema_write_string(writer, member->result_type);
        ribos_schema_write_string(
            writer,
            member->collection_element_type);
        ribos_schema_write_u32(writer, member->collection_bound);
    }
    ribos_schema_write_u32(writer, (uint32_t)schema->helper_count);
    for (index = 0; index < schema->helper_count; ++index) {
        const RibosSchemaHelper *helper = &schema->helpers[index];
        size_t parameter;

        ribos_schema_write_u32(writer, helper->stable_id);
        ribos_schema_write_string(writer, helper->path);
        ribos_schema_write_u32(writer, helper->capabilities);
        ribos_schema_write_string(writer, helper->result_type);
        ribos_schema_write_string(writer, helper->error_type);
        ribos_schema_write_u32(writer, (uint32_t)helper->parameter_count);
        for (parameter = 0;
             parameter < helper->parameter_count;
             ++parameter) {
            ribos_schema_write_string(
                writer,
                helper->parameters[parameter].name);
            ribos_schema_write_string(
                writer,
                helper->parameters[parameter].type);
        }
    }
    ribos_schema_write_u32(writer, (uint32_t)schema->handoff_field_count);
    for (index = 0; index < schema->handoff_field_count; ++index) {
        const RibosSchemaHandoffField *field =
            &schema->handoff_fields[index];

        ribos_schema_write_u32(writer, field->stable_id);
        ribos_schema_write_string(writer, field->key);
        ribos_schema_write_string(writer, field->value_type);
    }
    return writer->overflow ?
        RIBOS_SCHEMA_CAPACITY_EXCEEDED : RIBOS_SCHEMA_OK;
}

static int
ribos_schema_id_is_next(uint32_t previous, uint32_t value)
{
    return value != 0 && value > previous;
}

static int
ribos_schema_text_is_present(const char *text)
{
    return text != NULL && text[0] != '\0';
}

static int
ribos_schema_type_reference_is_valid(
    const RibosProductSchema *schema,
    const char *name,
    int allow_wildcard)
{
    static const char *const builtins[] = {
        "Unit", "bool",
        "u8", "u16", "u32", "u64",
        "i8", "i16", "i32", "i64",
    };
    size_t index;

    if (!ribos_schema_text_is_present(name)) {
        return 0;
    }
    if (allow_wildcard && strcmp(name, "*") == 0) {
        return 1;
    }
    for (index = 0;
         index < sizeof(builtins) / sizeof(builtins[0]);
         ++index) {
        if (strcmp(name, builtins[index]) == 0) {
            return 1;
        }
    }
    for (index = 0; index < schema->type_count; ++index) {
        if (strcmp(name, schema->types[index].name) == 0) {
            return 1;
        }
    }
    return 0;
}

const RibosProductSchema *
ribos_schema_reference_v1(void)
{
    return &ribos_reference_schema;
}

RibosSchemaStatus
ribos_schema_validate(const RibosProductSchema *schema)
{
    uint32_t previous = 0;
    size_t index;

    if (schema == NULL ||
        schema->format_major != RIBOS_SCHEMA_V1_MAJOR ||
        schema->format_minor != RIBOS_SCHEMA_V1_MINOR ||
        !ribos_schema_text_is_present(schema->product_id) ||
        schema->types == NULL ||
        schema->type_count == 0 ||
        (schema->member_count != 0 && schema->members == NULL) ||
        (schema->helper_count != 0 && schema->helpers == NULL) ||
        (schema->handoff_field_count != 0 &&
         schema->handoff_fields == NULL) ||
        schema->type_count > UINT32_MAX ||
        schema->member_count > UINT32_MAX ||
        schema->helper_count > UINT32_MAX ||
        schema->handoff_field_count > UINT32_MAX) {
        return RIBOS_SCHEMA_INVALID_DESCRIPTOR;
    }
    for (index = 0; index < schema->type_count; ++index) {
        const RibosSchemaType *type = &schema->types[index];

        size_t duplicate;

        if (!ribos_schema_id_is_next(previous, type->stable_id) ||
            !ribos_schema_text_is_present(type->name)) {
            return RIBOS_SCHEMA_INVALID_DESCRIPTOR;
        }
        for (duplicate = 0; duplicate < index; ++duplicate) {
            if (strcmp(type->name, schema->types[duplicate].name) == 0) {
                return RIBOS_SCHEMA_INVALID_DESCRIPTOR;
            }
        }
        previous = type->stable_id;
    }
    previous = 0;
    for (index = 0; index < schema->member_count; ++index) {
        const RibosSchemaMember *member = &schema->members[index];
        size_t duplicate;

        if (!ribos_schema_id_is_next(previous, member->stable_id) ||
            !ribos_schema_text_is_present(member->owner_type) ||
            !ribos_schema_text_is_present(member->name) ||
            ((member->result_type == NULL) ==
             (member->collection_element_type == NULL)) ||
            (member->collection_element_type != NULL &&
             member->collection_bound == 0) ||
            !ribos_schema_type_reference_is_valid(
                schema,
                member->owner_type,
                0) ||
            (member->result_type != NULL &&
             !ribos_schema_type_reference_is_valid(
                 schema,
                 member->result_type,
                 0)) ||
            (member->collection_element_type != NULL &&
             !ribos_schema_type_reference_is_valid(
                 schema,
                 member->collection_element_type,
                 0))) {
            return RIBOS_SCHEMA_INVALID_DESCRIPTOR;
        }
        for (duplicate = 0; duplicate < index; ++duplicate) {
            const RibosSchemaMember *previous_member =
                &schema->members[duplicate];

            if (strcmp(
                    member->owner_type,
                    previous_member->owner_type) == 0 &&
                strcmp(member->name, previous_member->name) == 0) {
                return RIBOS_SCHEMA_INVALID_DESCRIPTOR;
            }
        }
        previous = member->stable_id;
    }
    previous = 0;
    for (index = 0; index < schema->helper_count; ++index) {
        const RibosSchemaHelper *helper = &schema->helpers[index];
        size_t parameter;

        if (!ribos_schema_id_is_next(previous, helper->stable_id) ||
            !ribos_schema_text_is_present(helper->path) ||
            !ribos_schema_type_reference_is_valid(
                schema,
                helper->result_type,
                0) ||
            (helper->error_type != NULL &&
             !ribos_schema_type_reference_is_valid(
                 schema,
                 helper->error_type,
                 0)) ||
            helper->parameter_count > RIBOS_SCHEMA_MAX_PARAMETERS ||
            (helper->capabilities &
             ~(RIBOS_CAPABILITY_INSPECT |
               RIBOS_CAPABILITY_DEVICE |
               RIBOS_CAPABILITY_STATE |
               RIBOS_CAPABILITY_NETWORK |
               RIBOS_CAPABILITY_FLASH |
               RIBOS_CAPABILITY_HANDOFF |
               RIBOS_CAPABILITY_BOOT |
               RIBOS_CAPABILITY_DIAGNOSTIC)) != 0) {
            return RIBOS_SCHEMA_INVALID_DESCRIPTOR;
        }
        {
            size_t duplicate;

            for (duplicate = 0; duplicate < index; ++duplicate) {
                if (strcmp(
                        helper->path,
                        schema->helpers[duplicate].path) == 0) {
                    return RIBOS_SCHEMA_INVALID_DESCRIPTOR;
                }
            }
        }
        for (parameter = 0;
             parameter < helper->parameter_count;
             ++parameter) {
            if (!ribos_schema_text_is_present(
                    helper->parameters[parameter].name) ||
                !ribos_schema_type_reference_is_valid(
                    schema,
                    helper->parameters[parameter].type,
                    1)) {
                return RIBOS_SCHEMA_INVALID_DESCRIPTOR;
            }
        }
        previous = helper->stable_id;
    }
    previous = 0;
    for (index = 0; index < schema->handoff_field_count; ++index) {
        const RibosSchemaHandoffField *field =
            &schema->handoff_fields[index];
        size_t duplicate;

        if (!ribos_schema_id_is_next(previous, field->stable_id) ||
            !ribos_schema_text_is_present(field->key) ||
            !ribos_schema_type_reference_is_valid(
                schema,
                field->value_type,
                0)) {
            return RIBOS_SCHEMA_INVALID_DESCRIPTOR;
        }
        for (duplicate = 0; duplicate < index; ++duplicate) {
            if (strcmp(
                    field->key,
                    schema->handoff_fields[duplicate].key) == 0) {
                return RIBOS_SCHEMA_INVALID_DESCRIPTOR;
            }
        }
        previous = field->stable_id;
    }
    return RIBOS_SCHEMA_OK;
}

RibosSchemaStatus
ribos_schema_encode(
    const RibosProductSchema *schema,
    uint8_t *output,
    size_t output_capacity,
    size_t *required_size)
{
    RibosSchemaWriter writer = {
        .output = output,
        .capacity = output_capacity,
    };
    RibosSchemaStatus status;

    if (schema == NULL || required_size == NULL ||
        (output == NULL && output_capacity != 0)) {
        return RIBOS_SCHEMA_INVALID_ARGUMENT;
    }
    status = ribos_schema_encode_into(schema, &writer);
    *required_size = writer.size;
    return status;
}

RibosSchemaStatus
ribos_schema_compute_identity(
    const RibosProductSchema *schema,
    uint8_t digest[RIBOS_SCHEMA_DIGEST_BYTES])
{
    RibosSchemaWriter writer;
    RibosSchemaStatus status;
    RibosSha256 hash;

    if (schema == NULL || digest == NULL) {
        return RIBOS_SCHEMA_INVALID_ARGUMENT;
    }
    ribos_sha256_initialize(&hash);
    writer = (RibosSchemaWriter){
        .hash = &hash,
    };
    status = ribos_schema_encode_into(schema, &writer);
    if (status != RIBOS_SCHEMA_OK || writer.overflow) {
        return status == RIBOS_SCHEMA_OK ?
            RIBOS_SCHEMA_CAPACITY_EXCEEDED : status;
    }
    ribos_sha256_finish(&hash, digest);
    return RIBOS_SCHEMA_OK;
}

const RibosSchemaType *
ribos_schema_find_type(
    const RibosProductSchema *schema,
    const char *name,
    size_t name_length)
{
    size_t index;

    if (schema == NULL || name == NULL) {
        return NULL;
    }
    for (index = 0; index < schema->type_count; ++index) {
        const RibosSchemaType *type = &schema->types[index];

        if (strlen(type->name) == name_length &&
            memcmp(type->name, name, name_length) == 0) {
            return type;
        }
    }
    return NULL;
}

const RibosSchemaHelper *
ribos_schema_find_helper(
    const RibosProductSchema *schema,
    const char *path,
    size_t path_length)
{
    size_t index;

    if (schema == NULL || path == NULL) {
        return NULL;
    }
    for (index = 0; index < schema->helper_count; ++index) {
        const RibosSchemaHelper *helper = &schema->helpers[index];

        if (strlen(helper->path) == path_length &&
            memcmp(helper->path, path, path_length) == 0) {
            return helper;
        }
    }
    return NULL;
}

const RibosSchemaMember *
ribos_schema_find_member(
    const RibosProductSchema *schema,
    const char *owner_type,
    size_t owner_type_length,
    const char *member,
    size_t member_length)
{
    size_t index;

    if (schema == NULL || owner_type == NULL || member == NULL) {
        return NULL;
    }
    for (index = 0; index < schema->member_count; ++index) {
        const RibosSchemaMember *candidate = &schema->members[index];

        if (strlen(candidate->owner_type) == owner_type_length &&
            memcmp(
                candidate->owner_type,
                owner_type,
                owner_type_length) == 0 &&
            strlen(candidate->name) == member_length &&
            memcmp(candidate->name, member, member_length) == 0) {
            return candidate;
        }
    }
    return NULL;
}

const RibosSchemaHandoffField *
ribos_schema_find_handoff_field(
    const RibosProductSchema *schema,
    const char *key,
    size_t key_length)
{
    size_t index;

    if (schema == NULL || key == NULL) {
        return NULL;
    }
    for (index = 0; index < schema->handoff_field_count; ++index) {
        const RibosSchemaHandoffField *field =
            &schema->handoff_fields[index];

        if (strlen(field->key) == key_length &&
            memcmp(field->key, key, key_length) == 0) {
            return field;
        }
    }
    return NULL;
}
