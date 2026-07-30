#include "ribos/schema/schema.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t expected_reference_identity[RIBOS_SCHEMA_DIGEST_BYTES] = {
    0xda, 0x48, 0xc9, 0x6b, 0x07, 0x39, 0x0e, 0xcb,
    0xad, 0xb6, 0xee, 0xf0, 0x6a, 0xb6, 0xcd, 0xfb,
    0xd0, 0x7b, 0x9a, 0x6d, 0xe1, 0xbb, 0x1a, 0xa8,
    0xe8, 0x76, 0xe1, 0x9f, 0x24, 0x37, 0x8f, 0x52,
};

static int
test_reference_identity(void)
{
    const RibosProductSchema *schema = ribos_schema_reference_v1();
    RibosProductSchema renamed = *schema;
    uint8_t digest[RIBOS_SCHEMA_DIGEST_BYTES];
    uint8_t renamed_digest[RIBOS_SCHEMA_DIGEST_BYTES];
    uint8_t *encoded;
    size_t required_size = 0;
    size_t repeated_size = 0;

    if (ribos_schema_validate(schema) != RIBOS_SCHEMA_OK ||
        ribos_schema_compute_identity(schema, digest) != RIBOS_SCHEMA_OK ||
        memcmp(
            digest,
            expected_reference_identity,
            sizeof(digest)) != 0 ||
        ribos_schema_encode(
            schema,
            NULL,
            0,
            &required_size) != RIBOS_SCHEMA_OK ||
        required_size == 0) {
        return 0;
    }
    encoded = malloc(required_size);
    if (encoded == NULL) {
        return 0;
    }
    if (ribos_schema_encode(
            schema,
            encoded,
            required_size,
            &repeated_size) != RIBOS_SCHEMA_OK ||
        repeated_size != required_size) {
        free(encoded);
        return 0;
    }
    free(encoded);
    renamed.product_id = "ribon.generic.reference.variant";
    if (ribos_schema_compute_identity(
            &renamed,
            renamed_digest) != RIBOS_SCHEMA_OK ||
        memcmp(digest, renamed_digest, sizeof(digest)) == 0) {
        return 0;
    }
    return 1;
}

static int
test_descriptor_and_lookup_rules(void)
{
    const RibosProductSchema *schema = ribos_schema_reference_v1();
    const RibosSchemaType invalid_types[] = {
        {1, RIBOS_SCHEMA_TYPE_VALUE, "First"},
        {1, RIBOS_SCHEMA_TYPE_VALUE, "Duplicate"},
    };
    const RibosProductSchema invalid = {
        .format_major = 1,
        .product_id = "invalid.duplicate-ids",
        .types = invalid_types,
        .type_count = sizeof(invalid_types) / sizeof(invalid_types[0]),
    };
    const RibosSchemaType one_type[] = {
        {1, RIBOS_SCHEMA_TYPE_VALUE, "OnlyType"},
    };
    const RibosSchemaHelper unknown_result[] = {
        {
            1, "invalid.helper", RIBOS_CAPABILITY_INSPECT,
            "MissingType", NULL, {{NULL, NULL}}, 0,
        },
    };
    const RibosProductSchema invalid_reference = {
        .format_major = RIBOS_SCHEMA_V1_MAJOR,
        .format_minor = RIBOS_SCHEMA_V1_MINOR,
        .product_id = "invalid.unknown-type",
        .types = one_type,
        .type_count = sizeof(one_type) / sizeof(one_type[0]),
        .helpers = unknown_result,
        .helper_count =
            sizeof(unknown_result) / sizeof(unknown_result[0]),
    };
    RibosProductSchema unsupported_version = *schema;
    const RibosSchemaHelper *helper =
        ribos_schema_find_helper(schema, "boot.slot", strlen("boot.slot"));
    const RibosSchemaType *type =
        ribos_schema_find_type(
            schema,
            "BootContext",
            strlen("BootContext"));
    const RibosSchemaMember *member = ribos_schema_find_member(
        schema,
        "BootContext",
        strlen("BootContext"),
        "board",
        strlen("board"));
    const RibosSchemaHandoffField *field =
        ribos_schema_find_handoff_field(
            schema,
            "BOOT_SLOT",
            strlen("BOOT_SLOT"));

    unsupported_version.format_major = 2;
    return ribos_schema_validate(&invalid) ==
            RIBOS_SCHEMA_INVALID_DESCRIPTOR &&
        ribos_schema_validate(&invalid_reference) ==
            RIBOS_SCHEMA_INVALID_DESCRIPTOR &&
        ribos_schema_validate(&unsupported_version) ==
            RIBOS_SCHEMA_INVALID_DESCRIPTOR &&
        type != NULL && type->stable_id == 1 &&
        type->type_class == RIBOS_SCHEMA_TYPE_FACT &&
        helper != NULL && helper->stable_id == 21 &&
        member != NULL && member->stable_id == 1 &&
        field != NULL && field->stable_id == 3;
}

int
main(void)
{
    if (!test_reference_identity() ||
        !test_descriptor_and_lookup_rules()) {
        (void)fprintf(stderr, "RIBOS-SCHEMA-TEST-FAIL\n");
        return 1;
    }
    (void)printf(
        "RIBOS-SCHEMA-TEST-OK format=1.0 identity="
        "da48c96b07390ecbadb6eef06ab6cdfbd"
        "07b9a6de1bb1aa8e876e19f24378f52\n");
    return 0;
}
