#include "ribos/schema/schema.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t expected_reference_identity[RIBOS_SCHEMA_DIGEST_BYTES] = {
    0x23, 0x78, 0x98, 0xe5, 0xb4, 0xb7, 0xfd, 0x5f,
    0x8c, 0xcc, 0xf9, 0xed, 0xcf, 0x5d, 0xa5, 0x0f,
    0xb6, 0x69, 0x9f, 0x24, 0xb8, 0x69, 0xe8, 0x78,
    0x63, 0x8f, 0x27, 0x88, 0x50, 0x91, 0xa4, 0xa8,
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
        {
            1, RIBOS_SCHEMA_TYPE_VALUE, "First",
            RIBOS_SCHEMA_OWNERSHIP_COPY,
        },
        {
            1, RIBOS_SCHEMA_TYPE_VALUE, "Duplicate",
            RIBOS_SCHEMA_OWNERSHIP_COPY,
        },
    };
    const RibosProductSchema invalid = {
        .format_major = 1,
        .format_minor = RIBOS_SCHEMA_V1_MINOR,
        .product_id = "invalid.duplicate-ids",
        .policy_context_type = "First",
        .policy_action_type = "First",
        .policy_error_type = "First",
        .types = invalid_types,
        .type_count = sizeof(invalid_types) / sizeof(invalid_types[0]),
    };
    const RibosSchemaType one_type[] = {
        {
            1, RIBOS_SCHEMA_TYPE_VALUE, "OnlyType",
            RIBOS_SCHEMA_OWNERSHIP_COPY,
        },
    };
    const RibosSchemaHelper unknown_result[] = {
        {
            1, "invalid.helper", RIBOS_CAPABILITY_INSPECT,
            "MissingType", NULL,
            {
                {
                    NULL, NULL,
                    RIBOS_SCHEMA_PARAMETER_BORROW,
                },
            },
            0, 0, 0,
        },
    };
    const RibosProductSchema invalid_reference = {
        .format_major = RIBOS_SCHEMA_V1_MAJOR,
        .format_minor = RIBOS_SCHEMA_V1_MINOR,
        .product_id = "invalid.unknown-type",
        .policy_context_type = "OnlyType",
        .policy_action_type = "OnlyType",
        .policy_error_type = "OnlyType",
        .types = one_type,
        .type_count = sizeof(one_type) / sizeof(one_type[0]),
        .helpers = unknown_result,
        .helper_count =
            sizeof(unknown_result) / sizeof(unknown_result[0]),
    };
    RibosProductSchema unsupported_version = *schema;
    const RibosSchemaHelper *helper =
        ribos_schema_find_helper(schema, "boot.slot", strlen("boot.slot"));
    const RibosSchemaHelper *verify_helper =
        ribos_schema_find_helper(
            schema,
            "image.verify",
            strlen("image.verify"));
    const RibosSchemaType *type =
        ribos_schema_find_type(
            schema,
            "BootContext",
            strlen("BootContext"));
    const RibosSchemaType *action_type =
        ribos_schema_find_type(
            schema,
            "BootAction",
            strlen("BootAction"));
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
        helper->flags ==
            (RIBOS_SCHEMA_HELPER_TYPESTATE_TRANSITION |
             RIBOS_SCHEMA_HELPER_TERMINAL_BOOT_ACTION) &&
        helper->parameters[1].mode == RIBOS_SCHEMA_PARAMETER_CONSUME &&
        verify_helper != NULL &&
        verify_helper->flags == RIBOS_SCHEMA_HELPER_TYPESTATE_TRANSITION &&
        verify_helper->parameters[0].mode ==
            RIBOS_SCHEMA_PARAMETER_CONSUME &&
        action_type != NULL &&
        action_type->ownership == RIBOS_SCHEMA_OWNERSHIP_LINEAR &&
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
        "RIBOS-SCHEMA-TEST-OK format=1.1 identity="
        "237898e5b4b7fd5f8cccf9edcf5da50f"
        "b6699f24b869e878638f27885091a4a8\n");
    return 0;
}
