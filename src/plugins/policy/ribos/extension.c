#include <Ribon/policy/ribos_extension.h>

#include <string.h>

#define RIBON_RIBOS_SCHEMA_MAGIC "RBSCHM1"
#define RIBON_RIBOS_SCHEMA_MAGIC_SIZE 7u

struct RibonRibosSchemaReader {
    const uint8_t *bytes;
    size_t size;
    size_t offset;
};

static int
ribon_ribos_extension_text_is_present(const char *text)
{
    return text != NULL && text[0] != '\0';
}

static int
ribon_ribos_extension_digest_equal(const uint8_t *left, const uint8_t *right)
{
    uint8_t difference = 0u;
    uint32_t index;

    if (left == NULL || right == NULL) {
        return 0;
    }
    for (index = 0u; index < RIBOS_SCHEMA_DIGEST_BYTES; ++index) {
        difference |= (uint8_t)(left[index] ^ right[index]);
    }
    return difference == 0u;
}

static int
ribon_ribos_extension_reserved_is_zero(const uint64_t reserved[4])
{
    return reserved != NULL && reserved[0] == 0u && reserved[1] == 0u &&
           reserved[2] == 0u && reserved[3] == 0u;
}

static int
ribon_ribos_schema_read_bytes(
    struct RibonRibosSchemaReader *reader,
    const void *expected,
    size_t size)
{
    if (reader == NULL || expected == NULL || reader->offset > reader->size ||
        size > reader->size - reader->offset ||
        memcmp(reader->bytes + reader->offset, expected, size) != 0) {
        return 0;
    }
    reader->offset += size;
    return 1;
}

static int
ribon_ribos_schema_read_u16(
    struct RibonRibosSchemaReader *reader,
    uint16_t expected)
{
    const uint8_t bytes[2] = {
        (uint8_t)expected,
        (uint8_t)(expected >> 8),
    };

    return ribon_ribos_schema_read_bytes(reader, bytes, sizeof(bytes));
}

static int
ribon_ribos_schema_read_u32(
    struct RibonRibosSchemaReader *reader,
    uint32_t expected)
{
    const uint8_t bytes[4] = {
        (uint8_t)expected,
        (uint8_t)(expected >> 8),
        (uint8_t)(expected >> 16),
        (uint8_t)(expected >> 24),
    };

    return ribon_ribos_schema_read_bytes(reader, bytes, sizeof(bytes));
}

static int
ribon_ribos_schema_read_text(
    struct RibonRibosSchemaReader *reader,
    const char *expected)
{
    const size_t length = expected == NULL ? 0u : strlen(expected);

    return length <= UINT16_MAX &&
           ribon_ribos_schema_read_u16(reader, (uint16_t)length) &&
           (length == 0u ||
            ribon_ribos_schema_read_bytes(reader, expected, length));
}

static int
ribon_ribos_schema_artifact_matches(
    const RibosProductSchema *schema,
    const uint8_t *artifact,
    size_t artifact_size)
{
    struct RibonRibosSchemaReader reader = {artifact, artifact_size, 0u};
    size_t index;

    if (schema == NULL || artifact == NULL ||
        !ribon_ribos_schema_read_bytes(
            &reader, RIBON_RIBOS_SCHEMA_MAGIC, RIBON_RIBOS_SCHEMA_MAGIC_SIZE) ||
        !ribon_ribos_schema_read_u16(&reader, schema->format_major) ||
        !ribon_ribos_schema_read_u16(&reader, schema->format_minor) ||
        !ribon_ribos_schema_read_text(&reader, schema->product_id) ||
        !ribon_ribos_schema_read_text(&reader, schema->policy_context_type) ||
        !ribon_ribos_schema_read_text(&reader, schema->policy_action_type) ||
        !ribon_ribos_schema_read_text(&reader, schema->policy_error_type) ||
        schema->type_count > UINT32_MAX ||
        !ribon_ribos_schema_read_u32(&reader, (uint32_t)schema->type_count)) {
        return 0;
    }
    for (index = 0u; index < schema->type_count; ++index) {
        const RibosSchemaType *type = &schema->types[index];
        if (!ribon_ribos_schema_read_u32(&reader, type->stable_id) ||
            !ribon_ribos_schema_read_u32(&reader, (uint32_t)type->type_class) ||
            !ribon_ribos_schema_read_text(&reader, type->name) ||
            !ribon_ribos_schema_read_u32(&reader, (uint32_t)type->ownership)) {
            return 0;
        }
    }
    if (schema->member_count > UINT32_MAX ||
        !ribon_ribos_schema_read_u32(&reader, (uint32_t)schema->member_count)) {
        return 0;
    }
    for (index = 0u; index < schema->member_count; ++index) {
        const RibosSchemaMember *member = &schema->members[index];
        if (!ribon_ribos_schema_read_u32(&reader, member->stable_id) ||
            !ribon_ribos_schema_read_text(&reader, member->owner_type) ||
            !ribon_ribos_schema_read_text(&reader, member->name) ||
            !ribon_ribos_schema_read_text(&reader, member->result_type) ||
            !ribon_ribos_schema_read_text(
                &reader, member->collection_element_type) ||
            !ribon_ribos_schema_read_u32(&reader, member->collection_bound)) {
            return 0;
        }
    }
    if (schema->helper_count > UINT32_MAX ||
        !ribon_ribos_schema_read_u32(&reader, (uint32_t)schema->helper_count)) {
        return 0;
    }
    for (index = 0u; index < schema->helper_count; ++index) {
        const RibosSchemaHelper *helper = &schema->helpers[index];
        size_t parameter;
        if (!ribon_ribos_schema_read_u32(&reader, helper->stable_id) ||
            !ribon_ribos_schema_read_text(&reader, helper->path) ||
            !ribon_ribos_schema_read_u32(&reader, helper->capabilities) ||
            !ribon_ribos_schema_read_text(&reader, helper->result_type) ||
            !ribon_ribos_schema_read_text(&reader, helper->error_type) ||
            helper->parameter_count > UINT32_MAX ||
            !ribon_ribos_schema_read_u32(
                &reader, (uint32_t)helper->parameter_count)) {
            return 0;
        }
        for (parameter = 0u; parameter < helper->parameter_count; ++parameter) {
            if (!ribon_ribos_schema_read_text(
                    &reader, helper->parameters[parameter].name) ||
                !ribon_ribos_schema_read_text(
                    &reader, helper->parameters[parameter].type) ||
                !ribon_ribos_schema_read_u32(
                    &reader, (uint32_t)helper->parameters[parameter].mode)) {
                return 0;
            }
        }
        if (!ribon_ribos_schema_read_u32(&reader, helper->flags) ||
            !ribon_ribos_schema_read_u32(
                &reader, helper->transition_parameter)) {
            return 0;
        }
    }
    if (schema->handoff_field_count > UINT32_MAX ||
        !ribon_ribos_schema_read_u32(
            &reader, (uint32_t)schema->handoff_field_count)) {
        return 0;
    }
    for (index = 0u; index < schema->handoff_field_count; ++index) {
        const RibosSchemaHandoffField *field = &schema->handoff_fields[index];
        if (!ribon_ribos_schema_read_u32(&reader, field->stable_id) ||
            !ribon_ribos_schema_read_text(&reader, field->key) ||
            !ribon_ribos_schema_read_text(&reader, field->value_type)) {
            return 0;
        }
    }
    return reader.offset == reader.size;
}

int
ribon_ribos_extension_validate_v1(
    const struct RibonRibosExtensionDescriptor *extension)
{
    uint8_t schema_digest[RIBOS_SCHEMA_DIGEST_BYTES];
    uint8_t helper_digest[RIBOS_SCHEMA_DIGEST_BYTES];
    uint32_t index;

    if (extension == NULL) {
        return RIBON_RIBOS_EXTENSION_STATUS_BAD_ARGUMENT;
    }
    if (extension->size != sizeof(*extension) ||
        extension->abi_version != RIBON_RIBOS_EXTENSION_ABI_VERSION ||
        !ribon_ribos_extension_reserved_is_zero(extension->reserved)) {
        return RIBON_RIBOS_EXTENSION_STATUS_BAD_ABI;
    }
    if (!ribon_ribos_extension_text_is_present(extension->package_id) ||
        extension->schema == NULL || extension->helper_contract == NULL ||
        extension->routes == NULL || extension->route_count == 0u ||
        extension->route_count != extension->schema->helper_count ||
        extension->route_count != extension->helper_contract->binding_count ||
        extension->selected_phase >= 64u ||
        extension->granted_ribos_capabilities == 0u) {
        return RIBON_RIBOS_EXTENSION_STATUS_BAD_ARGUMENT;
    }
    if (ribos_schema_validate(extension->schema) != RIBOS_SCHEMA_OK ||
        ribos_schema_compute_identity(extension->schema, schema_digest) !=
            RIBOS_SCHEMA_OK) {
        return RIBON_RIBOS_EXTENSION_STATUS_BAD_SCHEMA;
    }
    if (!ribon_ribos_extension_digest_equal(
            schema_digest, extension->schema_digest)) {
        return RIBON_RIBOS_EXTENSION_STATUS_DIGEST_MISMATCH;
    }
    if (ribos_vm_helper_contract_validate_v1(extension->helper_contract) !=
            RIBOS_VM_STATUS_OK ||
        ribos_vm_helper_contract_compute_identity_v1(
            extension->helper_contract, helper_digest) != RIBOS_VM_STATUS_OK) {
        return RIBON_RIBOS_EXTENSION_STATUS_BAD_HELPER_CONTRACT;
    }
    if (!ribon_ribos_extension_digest_equal(
            helper_digest, extension->helper_execution_digest) ||
        !ribon_ribos_extension_digest_equal(
            helper_digest, extension->helper_contract->digest)) {
        return RIBON_RIBOS_EXTENSION_STATUS_DIGEST_MISMATCH;
    }
    if (extension->helper_call_budget < extension->route_count) {
        return RIBON_RIBOS_EXTENSION_STATUS_BUDGET_EXCEEDED;
    }
    for (index = 0u; index < extension->route_count; ++index) {
        const RibosSchemaHelper *helper = &extension->schema->helpers[index];
        const RibosVmHelperBinding *binding =
            &extension->helper_contract->bindings[index];
        const RibosVmHelperExecutionDescriptor *execution = &binding->execution;
        const struct RibonRibosHelperRoute *route = &extension->routes[index];
        const int schema_transition =
            (helper->flags & RIBOS_SCHEMA_HELPER_TYPESTATE_TRANSITION) != 0u;
        const int execution_transition =
            execution->handle_transition != RIBOS_VM_HANDLE_TRANSITION_NONE;

        if (helper->stable_id != execution->stable_id ||
            helper->stable_id != route->stable_id || route->invoke == NULL ||
            binding->invoke == NULL ||
            (index != 0u &&
             extension->routes[index - 1u].stable_id >= route->stable_id) ||
            (route->service_kind == RIBON_RIBOS_NO_SERVICE_KIND &&
             (route->service_id != NULL ||
              route->required_ribon_capabilities != 0u)) ||
            (route->service_kind != RIBON_RIBOS_NO_SERVICE_KIND &&
             (route->service_id == NULL ||
              route->required_ribon_capabilities == 0u))) {
            return RIBON_RIBOS_EXTENSION_STATUS_BAD_ROUTE;
        }
        if (helper->capabilities != execution->required_capabilities ||
            (helper->capabilities &
             ~extension->granted_ribos_capabilities) != 0u) {
            return RIBON_RIBOS_EXTENSION_STATUS_CAPABILITY_WIDENING;
        }
        if (schema_transition != execution_transition ||
            (schema_transition &&
             helper->transition_parameter != execution->transition_parameter) ||
            (((helper->flags &
               RIBOS_SCHEMA_HELPER_TERMINAL_BOOT_ACTION) != 0u) !=
             (execution->effect == RIBOS_VM_HELPER_EFFECT_TERMINAL)) ||
            (execution->allowed_phase_mask &
             (UINT64_C(1) << extension->selected_phase)) == 0u) {
            return RIBON_RIBOS_EXTENSION_STATUS_TYPESTATE_MISMATCH;
        }
    }
    return RIBON_RIBOS_EXTENSION_STATUS_OK;
}

int
ribon_ribos_extension_schema_size_v1(
    const struct RibonRibosExtensionDescriptor *extension,
    size_t *required_size)
{
    if (required_size == NULL ||
        ribon_ribos_extension_validate_v1(extension) !=
            RIBON_RIBOS_EXTENSION_STATUS_OK) {
        return RIBON_RIBOS_EXTENSION_STATUS_BAD_ARGUMENT;
    }
    return ribos_schema_encode(extension->schema, NULL, 0u, required_size) ==
            RIBOS_SCHEMA_OK ?
        RIBON_RIBOS_EXTENSION_STATUS_OK :
        RIBON_RIBOS_EXTENSION_STATUS_BAD_SCHEMA;
}

int
ribon_ribos_extension_schema_write_v1(
    const struct RibonRibosExtensionDescriptor *extension,
    uint8_t *output,
    size_t output_capacity,
    size_t *written_size)
{
    if (output == NULL || written_size == NULL ||
        ribon_ribos_extension_validate_v1(extension) !=
            RIBON_RIBOS_EXTENSION_STATUS_OK) {
        return RIBON_RIBOS_EXTENSION_STATUS_BAD_ARGUMENT;
    }
    return ribos_schema_encode(
               extension->schema, output, output_capacity, written_size) ==
            RIBOS_SCHEMA_OK ?
        RIBON_RIBOS_EXTENSION_STATUS_OK :
        RIBON_RIBOS_EXTENSION_STATUS_SCHEMA_ARTIFACT;
}

int
ribon_ribos_extension_schema_read_v1(
    const struct RibonRibosExtensionDescriptor *extension,
    const uint8_t *artifact,
    size_t artifact_size)
{
    if (artifact == NULL ||
        ribon_ribos_extension_validate_v1(extension) !=
            RIBON_RIBOS_EXTENSION_STATUS_OK) {
        return RIBON_RIBOS_EXTENSION_STATUS_BAD_ARGUMENT;
    }
    return ribon_ribos_schema_artifact_matches(
               extension->schema, artifact, artifact_size) ?
        RIBON_RIBOS_EXTENSION_STATUS_OK :
        RIBON_RIBOS_EXTENSION_STATUS_SCHEMA_ARTIFACT;
}
