#include "ribos/host/artifact_emitter.h"

#include "internal.h"
#include "ribos/ir/analysis.h"

#include <string.h>

typedef struct RibosArtifactImport {
    uint32_t stable_id;
    uint32_t capabilities;
    uint32_t call_site_count;
} RibosArtifactImport;

typedef struct RibosArtifactSectionPlan {
    RibosArtifactSectionKind kind;
    uint32_t row_size;
    uint32_t count;
    size_t offset;
    size_t length;
} RibosArtifactSectionPlan;

typedef struct RibosArtifactEmitContext {
    RibosIrModuleView module;
    RibosIrResourceClosure *resources;
    RibosIrResourceSummary resource_summary;
    RibosArtifactEmitOptions options;
    RibosArtifactImport imports[RIBOS_ARTIFACT_MAX_HELPER_IMPORTS];
    size_t import_count;
    RibosArtifactSectionPlan
        sections[RIBOS_ARTIFACT_SECTION_KIND_COUNT];
    size_t section_count;
    uint32_t entry_function;
    size_t payload_length;
    size_t total_length;
} RibosArtifactEmitContext;

static int
ribos_artifact_count_u32(size_t count, uint32_t *result)
{
    if (result == NULL || count > UINT32_MAX) {
        return 0;
    }
    *result = (uint32_t)count;
    return 1;
}

static size_t
ribos_artifact_bounded_name_length(const char name[64])
{
    size_t length;

    for (length = 0; length < 64; ++length) {
        if (name[length] == '\0') {
            return length;
        }
    }
    return SIZE_MAX;
}

static int
ribos_artifact_signature_is_valid(
    const RibosArtifactSignature *signature)
{
    if (signature->algorithm == RIBOS_ARTIFACT_SIGNATURE_NONE) {
        return signature->key_id == NULL &&
            signature->key_id_length == 0 &&
            signature->signature == NULL &&
            signature->signature_length == 0;
    }
    return signature->algorithm ==
            RIBOS_ARTIFACT_SIGNATURE_ED25519 &&
        signature->key_id != NULL &&
        signature->key_id_length > 0 &&
        signature->key_id_length <=
            RIBOS_ARTIFACT_MAX_KEY_ID_BYTES &&
        signature->signature != NULL &&
        signature->signature_length ==
            RIBOS_ARTIFACT_ED25519_SIGNATURE_BYTES;
}

static RibosArtifactStatus
ribos_artifact_collect_imports(RibosArtifactEmitContext *context)
{
    size_t index;

    for (index = 0;
         index < context->module.helper_call_count;
         ++index) {
        const RibosIrHelperCallSite *site =
            &context->module.helper_calls[index];
        size_t position = 0;

        while (position < context->import_count &&
               context->imports[position].stable_id <
                   site->helper_stable_id) {
            ++position;
        }
        if (position < context->import_count &&
            context->imports[position].stable_id ==
                site->helper_stable_id) {
            if (context->imports[position].capabilities !=
                    site->capabilities ||
                context->imports[position].call_site_count ==
                    UINT32_MAX) {
                return RIBOS_ARTIFACT_INVALID_POLICY_IR;
            }
            ++context->imports[position].call_site_count;
            continue;
        }
        if (context->import_count ==
            RIBOS_ARTIFACT_MAX_HELPER_IMPORTS) {
            return RIBOS_ARTIFACT_CAPACITY_EXCEEDED;
        }
        memmove(
            &context->imports[position + 1],
            &context->imports[position],
            (context->import_count - position) *
                sizeof(context->imports[0]));
        context->imports[position] = (RibosArtifactImport){
            .stable_id = site->helper_stable_id,
            .capabilities = site->capabilities,
            .call_site_count = 1,
        };
        ++context->import_count;
    }
    return RIBOS_ARTIFACT_OK;
}

static RibosArtifactStatus
ribos_artifact_select_entry(RibosArtifactEmitContext *context)
{
    size_t index;
    size_t policy_count = 0;

    for (index = 0; index < context->module.function_count; ++index) {
        if ((context->module.functions[index].flags &
             RIBOS_IR_FUNCTION_POLICY) != 0) {
            context->entry_function = (uint32_t)index;
            ++policy_count;
        }
    }
    return policy_count == 1 ?
        RIBOS_ARTIFACT_OK : RIBOS_ARTIFACT_INVALID_POLICY_IR;
}

static RibosArtifactStatus
ribos_artifact_add_section(
    RibosArtifactEmitContext *context,
    RibosArtifactSectionKind kind,
    uint32_t row_size,
    size_t count,
    size_t *cursor)
{
    RibosArtifactSectionPlan *section;
    uint32_t count_u32;

    if (context->section_count + 1 != (size_t)kind ||
        context->section_count >=
            RIBOS_ARTIFACT_SECTION_KIND_COUNT ||
        !ribos_artifact_count_u32(count, &count_u32) ||
        !ribos_artifact_size_align(*cursor, 8, cursor)) {
        return RIBOS_ARTIFACT_CAPACITY_EXCEEDED;
    }
    section = &context->sections[kind];
    *section = (RibosArtifactSectionPlan){
        .kind = kind,
        .row_size = row_size,
        .count = count_u32,
        .offset = *cursor,
    };
    if (!ribos_artifact_size_multiply(
            count,
            row_size,
            &section->length) ||
        !ribos_artifact_size_add(
            *cursor,
            section->length,
            cursor)) {
        return RIBOS_ARTIFACT_CAPACITY_EXCEEDED;
    }
    ++context->section_count;
    return RIBOS_ARTIFACT_OK;
}

static RibosArtifactStatus
ribos_artifact_plan_sections(RibosArtifactEmitContext *context)
{
    size_t directory_length;
    size_t cursor;
    RibosArtifactStatus status;

    context->section_count =
        context->options.include_source_map ? 13 : 12;
    if (!ribos_artifact_size_multiply(
            context->section_count,
            RIBOS_ARTIFACT_SECTION_DESCRIPTOR_BYTES,
            &directory_length) ||
        !ribos_artifact_size_add(
            RIBOS_ARTIFACT_PAYLOAD_HEADER_BYTES,
            directory_length,
            &cursor)) {
        return RIBOS_ARTIFACT_CAPACITY_EXCEEDED;
    }
    context->section_count = 0;
#define RIBOS_ADD_SECTION(section_kind, row_bytes, item_count) \
    do { \
        status = ribos_artifact_add_section( \
            context, \
            section_kind, \
            row_bytes, \
            item_count, \
            &cursor); \
        if (status != RIBOS_ARTIFACT_OK) { \
            return status; \
        } \
    } while (0)

    RIBOS_ADD_SECTION(
        RIBOS_ARTIFACT_SECTION_TYPES,
        RIBOS_ARTIFACT_TYPE_ROW_BYTES,
        context->module.type_count);
    RIBOS_ADD_SECTION(
        RIBOS_ARTIFACT_SECTION_SHAPES,
        RIBOS_ARTIFACT_SHAPE_ROW_BYTES,
        context->module.shape_count);
    RIBOS_ADD_SECTION(
        RIBOS_ARTIFACT_SECTION_CONSTANTS,
        RIBOS_ARTIFACT_CONSTANT_ROW_BYTES,
        context->module.constant_count);
    RIBOS_ADD_SECTION(
        RIBOS_ARTIFACT_SECTION_CONSTANT_BYTES,
        1,
        context->module.constant_byte_count);
    RIBOS_ADD_SECTION(
        RIBOS_ARTIFACT_SECTION_FUNCTIONS,
        RIBOS_ARTIFACT_FUNCTION_ROW_BYTES,
        context->module.function_count);
    RIBOS_ADD_SECTION(
        RIBOS_ARTIFACT_SECTION_BLOCKS,
        RIBOS_ARTIFACT_BLOCK_ROW_BYTES,
        context->module.block_count);
    RIBOS_ADD_SECTION(
        RIBOS_ARTIFACT_SECTION_LOOPS,
        RIBOS_ARTIFACT_LOOP_ROW_BYTES,
        context->module.loop_count);
    RIBOS_ADD_SECTION(
        RIBOS_ARTIFACT_SECTION_SLOTS,
        RIBOS_ARTIFACT_SLOT_ROW_BYTES,
        context->module.slot_count);
    RIBOS_ADD_SECTION(
        RIBOS_ARTIFACT_SECTION_INSTRUCTIONS,
        RIBOS_ARTIFACT_INSTRUCTION_ROW_BYTES,
        context->module.instruction_count);
    RIBOS_ADD_SECTION(
        RIBOS_ARTIFACT_SECTION_OPERANDS,
        RIBOS_ARTIFACT_OPERAND_ROW_BYTES,
        context->module.operand_count);
    RIBOS_ADD_SECTION(
        RIBOS_ARTIFACT_SECTION_HELPER_IMPORTS,
        RIBOS_ARTIFACT_HELPER_IMPORT_ROW_BYTES,
        context->import_count);
    RIBOS_ADD_SECTION(
        RIBOS_ARTIFACT_SECTION_HELPER_BOUNDS,
        RIBOS_ARTIFACT_HELPER_BOUND_ROW_BYTES,
        context->resource_summary.helper_bound_count);
    if (context->options.include_source_map) {
        RIBOS_ADD_SECTION(
            RIBOS_ARTIFACT_SECTION_SOURCE_MAPS,
            RIBOS_ARTIFACT_SOURCE_MAP_ROW_BYTES,
            context->module.source_map_count);
    }
#undef RIBOS_ADD_SECTION
    context->payload_length = cursor;
    if (!ribos_artifact_size_add(
            RIBOS_ARTIFACT_ENVELOPE_BYTES,
            context->payload_length,
            &context->total_length) ||
        !ribos_artifact_size_add(
            context->total_length,
            context->options.signature.key_id_length,
            &context->total_length) ||
        !ribos_artifact_size_add(
            context->total_length,
            context->options.signature.signature_length,
            &context->total_length) ||
        context->total_length > RIBOS_ARTIFACT_MAX_BYTES) {
        return RIBOS_ARTIFACT_CAPACITY_EXCEEDED;
    }
    return RIBOS_ARTIFACT_OK;
}

static RibosArtifactStatus
ribos_artifact_prepare_context(
    const RibosIrModule *module,
    const RibosArtifactEmitOptions *options,
    RibosArtifactEmitContext *context)
{
    RibosArtifactStatus status;
    RibosIrStatus ir_status;

    memset(context, 0, sizeof(*context));
    if (options != NULL) {
        context->options = *options;
    }
    if (context->options.include_source_map > 1) {
        return RIBOS_ARTIFACT_INVALID_ARGUMENT;
    }
    if (!ribos_artifact_signature_is_valid(
            &context->options.signature)) {
        return RIBOS_ARTIFACT_INVALID_SIGNATURE_ENVELOPE;
    }
    if (ribos_ir_validate_v1(module) != RIBOS_IR_OK ||
        ribos_ir_module_view(module, &context->module) != RIBOS_IR_OK) {
        return RIBOS_ARTIFACT_INVALID_POLICY_IR;
    }
    context->resources = ribos_ir_resource_closure_create(
        ribos_ir_module_allocator(module));
    if (context->resources == NULL) {
        return RIBOS_ARTIFACT_RESOURCE_CLOSURE_FAILED;
    }
    ir_status = ribos_ir_analyze_resources_v1(
        module,
        context->resources);
    if (ir_status == RIBOS_IR_OK) {
        ir_status = ribos_ir_enforce_resource_budgets_v1(
            module,
            context->resources);
    }
    if (ir_status != RIBOS_IR_OK ||
        ribos_ir_resource_summary(
            context->resources,
            &context->resource_summary) != RIBOS_IR_OK ||
        context->module.slot_count >
            RIBOS_ARTIFACT_MAX_VIRTUAL_REGISTERS) {
        return RIBOS_ARTIFACT_RESOURCE_CLOSURE_FAILED;
    }
    status = ribos_artifact_select_entry(context);
    if (status == RIBOS_ARTIFACT_OK) {
        status = ribos_artifact_collect_imports(context);
    }
    if (status == RIBOS_ARTIFACT_OK) {
        status = ribos_artifact_plan_sections(context);
    }
    return status;
}

static RibosBytecodeOpcode
ribos_artifact_opcode(RibosIrOpcode opcode)
{
    switch (opcode) {
    case RIBOS_IR_OP_PARAMETER:
        return RIBOS_BC_PARAMETER;
    case RIBOS_IR_OP_CONST_UNIT:
        return RIBOS_BC_CONST_UNIT;
    case RIBOS_IR_OP_CONST_BOOL:
        return RIBOS_BC_CONST_BOOL;
    case RIBOS_IR_OP_CONST_INTEGER:
        return RIBOS_BC_CONST_INTEGER;
    case RIBOS_IR_OP_CONST_STRING:
        return RIBOS_BC_CONST_STRING;
    case RIBOS_IR_OP_CONST_SYMBOL:
        return RIBOS_BC_CONST_SYMBOL;
    case RIBOS_IR_OP_MOVE:
        return RIBOS_BC_MOVE;
    case RIBOS_IR_OP_CHECKED_UNARY:
        return RIBOS_BC_CHECKED_UNARY;
    case RIBOS_IR_OP_CHECKED_BINARY:
        return RIBOS_BC_CHECKED_BINARY;
    case RIBOS_IR_OP_BUILD_LIST:
        return RIBOS_BC_BUILD_LIST;
    case RIBOS_IR_OP_BUILD_MAP:
        return RIBOS_BC_BUILD_MAP;
    case RIBOS_IR_OP_BUILD_STRUCT:
        return RIBOS_BC_BUILD_STRUCT;
    case RIBOS_IR_OP_BUILD_VARIANT:
        return RIBOS_BC_BUILD_VARIANT;
    case RIBOS_IR_OP_MEMBER:
        return RIBOS_BC_MEMBER;
    case RIBOS_IR_OP_INDEX:
        return RIBOS_BC_INDEX;
    case RIBOS_IR_OP_COLLECTION_LENGTH:
        return RIBOS_BC_COLLECTION_LENGTH;
    case RIBOS_IR_OP_VARIANT_TAG:
        return RIBOS_BC_VARIANT_TAG;
    case RIBOS_IR_OP_VARIANT_PAYLOAD:
        return RIBOS_BC_VARIANT_PAYLOAD;
    case RIBOS_IR_OP_CALL_DIRECT:
        return RIBOS_BC_CALL_DIRECT;
    case RIBOS_IR_OP_CALL_HELPER:
        return RIBOS_BC_CALL_HELPER;
    case RIBOS_IR_OP_JUMP:
        return RIBOS_BC_JUMP;
    case RIBOS_IR_OP_BRANCH:
        return RIBOS_BC_BRANCH;
    case RIBOS_IR_OP_RETURN:
        return RIBOS_BC_RETURN;
    case RIBOS_IR_OP_TRAP:
        return RIBOS_BC_TRAP;
    default:
        return 0;
    }
}

static uint16_t
ribos_artifact_type_kind(RibosIrTypeKind kind)
{
    switch (kind) {
    case RIBOS_IR_TYPE_ERROR:
        return RIBOS_BC_TYPE_ERROR;
    case RIBOS_IR_TYPE_UNKNOWN:
        return RIBOS_BC_TYPE_UNKNOWN;
    case RIBOS_IR_TYPE_UNIT:
        return RIBOS_BC_TYPE_UNIT;
    case RIBOS_IR_TYPE_BOOL:
        return RIBOS_BC_TYPE_BOOL;
    case RIBOS_IR_TYPE_UNSIGNED:
        return RIBOS_BC_TYPE_UNSIGNED;
    case RIBOS_IR_TYPE_SIGNED:
        return RIBOS_BC_TYPE_SIGNED;
    case RIBOS_IR_TYPE_STRING_LITERAL:
        return RIBOS_BC_TYPE_STRING_LITERAL;
    case RIBOS_IR_TYPE_NAMED:
        return RIBOS_BC_TYPE_NAMED;
    case RIBOS_IR_TYPE_ARRAY:
        return RIBOS_BC_TYPE_ARRAY;
    case RIBOS_IR_TYPE_LIST:
        return RIBOS_BC_TYPE_LIST;
    case RIBOS_IR_TYPE_FROZEN_MAP:
        return RIBOS_BC_TYPE_FROZEN_MAP;
    case RIBOS_IR_TYPE_DICT:
        return RIBOS_BC_TYPE_DICT;
    case RIBOS_IR_TYPE_OPTION:
        return RIBOS_BC_TYPE_OPTION;
    case RIBOS_IR_TYPE_RESULT:
        return RIBOS_BC_TYPE_RESULT;
    case RIBOS_IR_TYPE_STRUCT:
        return RIBOS_BC_TYPE_STRUCT;
    case RIBOS_IR_TYPE_ENUM:
        return RIBOS_BC_TYPE_ENUM;
    default:
        return UINT16_MAX;
    }
}

static uint32_t
ribos_artifact_storage_kind(RibosIrStorageKind kind)
{
    switch (kind) {
    case RIBOS_IR_STORAGE_SCALAR:
        return RIBOS_BC_STORAGE_SCALAR;
    case RIBOS_IR_STORAGE_OPAQUE:
        return RIBOS_BC_STORAGE_OPAQUE;
    case RIBOS_IR_STORAGE_INLINE_ARRAY:
        return RIBOS_BC_STORAGE_INLINE_ARRAY;
    case RIBOS_IR_STORAGE_INLINE_LIST:
        return RIBOS_BC_STORAGE_INLINE_LIST;
    case RIBOS_IR_STORAGE_SORTED_MAP:
        return RIBOS_BC_STORAGE_SORTED_MAP;
    case RIBOS_IR_STORAGE_TAGGED_UNION:
        return RIBOS_BC_STORAGE_TAGGED_UNION;
    case RIBOS_IR_STORAGE_INLINE_STRUCT:
        return RIBOS_BC_STORAGE_INLINE_STRUCT;
    default:
        return UINT32_MAX;
    }
}

static uint32_t
ribos_artifact_shape_kind(RibosIrShapeKind kind)
{
    switch (kind) {
    case RIBOS_IR_SHAPE_STRUCT_FIELD:
        return RIBOS_BC_SHAPE_STRUCT_FIELD;
    case RIBOS_IR_SHAPE_ENUM_VARIANT:
        return RIBOS_BC_SHAPE_ENUM_VARIANT;
    case RIBOS_IR_SHAPE_ENUM_PAYLOAD:
        return RIBOS_BC_SHAPE_ENUM_PAYLOAD;
    default:
        return UINT32_MAX;
    }
}

static uint16_t
ribos_artifact_constant_kind(RibosIrConstantKind kind)
{
    switch (kind) {
    case RIBOS_IR_CONSTANT_STRING:
        return RIBOS_BC_CONSTANT_STRING;
    case RIBOS_IR_CONSTANT_SYMBOL:
        return RIBOS_BC_CONSTANT_SYMBOL;
    default:
        return UINT16_MAX;
    }
}

static uint32_t
ribos_artifact_checked_operator(uint32_t target)
{
    switch ((RibosIrCheckedOperator)target) {
    case RIBOS_IR_CHECK_NOT:
        return RIBOS_BC_CHECK_NOT;
    case RIBOS_IR_CHECK_EQUAL:
        return RIBOS_BC_CHECK_EQUAL;
    case RIBOS_IR_CHECK_NOT_EQUAL:
        return RIBOS_BC_CHECK_NOT_EQUAL;
    case RIBOS_IR_CHECK_LESS:
        return RIBOS_BC_CHECK_LESS;
    case RIBOS_IR_CHECK_LESS_EQUAL:
        return RIBOS_BC_CHECK_LESS_EQUAL;
    case RIBOS_IR_CHECK_GREATER:
        return RIBOS_BC_CHECK_GREATER;
    case RIBOS_IR_CHECK_GREATER_EQUAL:
        return RIBOS_BC_CHECK_GREATER_EQUAL;
    case RIBOS_IR_CHECK_IN:
        return RIBOS_BC_CHECK_IN;
    case RIBOS_IR_CHECK_NOT_IN:
        return RIBOS_BC_CHECK_NOT_IN;
    case RIBOS_IR_CHECK_BIT_OR:
        return RIBOS_BC_CHECK_BIT_OR;
    case RIBOS_IR_CHECK_BIT_XOR:
        return RIBOS_BC_CHECK_BIT_XOR;
    case RIBOS_IR_CHECK_BIT_AND:
        return RIBOS_BC_CHECK_BIT_AND;
    case RIBOS_IR_CHECK_SHIFT_LEFT:
        return RIBOS_BC_CHECK_SHIFT_LEFT;
    case RIBOS_IR_CHECK_SHIFT_RIGHT:
        return RIBOS_BC_CHECK_SHIFT_RIGHT;
    case RIBOS_IR_CHECK_ADD:
        return RIBOS_BC_CHECK_ADD;
    case RIBOS_IR_CHECK_SUBTRACT:
        return RIBOS_BC_CHECK_SUBTRACT;
    case RIBOS_IR_CHECK_MULTIPLY:
        return RIBOS_BC_CHECK_MULTIPLY;
    case RIBOS_IR_CHECK_DIVIDE:
        return RIBOS_BC_CHECK_DIVIDE;
    case RIBOS_IR_CHECK_REMAINDER:
        return RIBOS_BC_CHECK_REMAINDER;
    case RIBOS_IR_CHECK_POSITIVE:
        return RIBOS_BC_CHECK_POSITIVE;
    case RIBOS_IR_CHECK_NEGATIVE:
        return RIBOS_BC_CHECK_NEGATIVE;
    case RIBOS_IR_CHECK_BIT_NOT:
        return RIBOS_BC_CHECK_BIT_NOT;
    default:
        return UINT32_MAX;
    }
}

static uint32_t
ribos_artifact_function_flags(uint32_t flags)
{
    uint32_t wire = 0;

    if ((flags & ~(RIBOS_IR_FUNCTION_POLICY |
            RIBOS_IR_FUNCTION_PURE)) != 0) {
        return UINT32_MAX;
    }
    if ((flags & RIBOS_IR_FUNCTION_POLICY) != 0) {
        wire |= RIBOS_BC_FUNCTION_POLICY;
    }
    if ((flags & RIBOS_IR_FUNCTION_PURE) != 0) {
        wire |= RIBOS_BC_FUNCTION_PURE;
    }
    return wire;
}

static uint32_t
ribos_artifact_slot_flags(uint32_t flags)
{
    uint32_t wire = 0;

    if ((flags & ~(RIBOS_IR_SLOT_PARAMETER |
            RIBOS_IR_SLOT_BINDING |
            RIBOS_IR_SLOT_MUTABLE)) != 0) {
        return UINT32_MAX;
    }
    if ((flags & RIBOS_IR_SLOT_PARAMETER) != 0) {
        wire |= RIBOS_BC_SLOT_PARAMETER;
    }
    if ((flags & RIBOS_IR_SLOT_BINDING) != 0) {
        wire |= RIBOS_BC_SLOT_BINDING;
    }
    if ((flags & RIBOS_IR_SLOT_MUTABLE) != 0) {
        wire |= RIBOS_BC_SLOT_MUTABLE;
    }
    return wire;
}

static uint32_t
ribos_artifact_block_flags(uint32_t flags)
{
    if ((flags & ~RIBOS_IR_BLOCK_ENTRY) != 0) {
        return UINT32_MAX;
    }
    return (flags & RIBOS_IR_BLOCK_ENTRY) != 0 ?
        RIBOS_BC_BLOCK_ENTRY : 0;
}

static uint32_t
ribos_artifact_terminal_mask(uint32_t mask)
{
    uint32_t wire = 0;

    if ((mask & ~(RIBOS_IR_TERMINAL_RETURN |
            RIBOS_IR_TERMINAL_TRAP)) != 0) {
        return UINT32_MAX;
    }
    if ((mask & RIBOS_IR_TERMINAL_RETURN) != 0) {
        wire |= RIBOS_BC_TERMINAL_RETURN;
    }
    if ((mask & RIBOS_IR_TERMINAL_TRAP) != 0) {
        wire |= RIBOS_BC_TERMINAL_TRAP;
    }
    return wire;
}

static void
ribos_artifact_write_payload_header(
    const RibosArtifactEmitContext *context,
    RibosArtifactWriter *writer,
    size_t payload_base)
{
    const RibosIrFunction *entry =
        &context->module.functions[context->entry_function];
    const RibosIrFunctionResource *resource =
        ribos_ir_resource_function(
            context->resources,
            context->entry_function);
    size_t directory_length;

    if (!ribos_artifact_size_multiply(
            context->section_count,
            RIBOS_ARTIFACT_SECTION_DESCRIPTOR_BYTES,
            &directory_length)) {
        writer->failed = 1;
        return;
    }

    ribos_artifact_writer_bytes(
        writer,
        payload_base + RIBOS_PAYLOAD_MAGIC_OFFSET,
        (const uint8_t *)RIBOS_ARTIFACT_PAYLOAD_MAGIC,
        RIBOS_ARTIFACT_MAGIC_BYTES);
    ribos_artifact_writer_u16(
        writer,
        payload_base + RIBOS_PAYLOAD_VM_MAJOR_OFFSET,
        RIBOS_VM_ABI_V1_MAJOR);
    ribos_artifact_writer_u16(
        writer,
        payload_base + RIBOS_PAYLOAD_VM_MINOR_OFFSET,
        RIBOS_VM_ABI_V1_MINOR);
    ribos_artifact_writer_u16(
        writer,
        payload_base + RIBOS_PAYLOAD_ISA_MAJOR_OFFSET,
        RIBOS_BYTECODE_ISA_V1_MAJOR);
    ribos_artifact_writer_u16(
        writer,
        payload_base + RIBOS_PAYLOAD_ISA_MINOR_OFFSET,
        RIBOS_BYTECODE_ISA_V1_MINOR);
    ribos_artifact_writer_u32(
        writer,
        payload_base + RIBOS_PAYLOAD_HEADER_BYTES_OFFSET,
        RIBOS_ARTIFACT_PAYLOAD_HEADER_BYTES);
    ribos_artifact_writer_u32(
        writer,
        payload_base + RIBOS_PAYLOAD_FLAGS_OFFSET,
        context->options.include_source_map ?
            RIBOS_ARTIFACT_HAS_SOURCE_MAP : 0);
    ribos_artifact_writer_u32(
        writer,
        payload_base + RIBOS_PAYLOAD_SECTION_COUNT_OFFSET,
        (uint32_t)context->section_count);
    ribos_artifact_writer_u32(
        writer,
        payload_base + RIBOS_PAYLOAD_ENTRY_FUNCTION_OFFSET,
        context->entry_function);
    ribos_artifact_writer_u32(
        writer,
        payload_base + RIBOS_PAYLOAD_REGISTER_COUNT_OFFSET,
        (uint32_t)context->module.slot_count);
    ribos_artifact_writer_u32(
        writer,
        payload_base + RIBOS_PAYLOAD_SLOT_COUNT_OFFSET,
        (uint32_t)context->module.slot_count);
    ribos_artifact_writer_u32(
        writer,
        payload_base + RIBOS_PAYLOAD_DECLARED_CAPS_OFFSET,
        entry->declared_capabilities);
    ribos_artifact_writer_u32(
        writer,
        payload_base + RIBOS_PAYLOAD_REQUIRED_CAPS_OFFSET,
        entry->required_capabilities);
    ribos_artifact_writer_u64(
        writer,
        payload_base + RIBOS_PAYLOAD_INSTRUCTION_BUDGET_OFFSET,
        entry->declared_instruction_budget);
    ribos_artifact_writer_u64(
        writer,
        payload_base + RIBOS_PAYLOAD_INSTRUCTION_UPPER_OFFSET,
        resource->instruction_upper_bound);
    ribos_artifact_writer_u64(
        writer,
        payload_base + RIBOS_PAYLOAD_HELPER_BUDGET_OFFSET,
        entry->declared_helper_budget);
    ribos_artifact_writer_u64(
        writer,
        payload_base + RIBOS_PAYLOAD_HELPER_UPPER_OFFSET,
        resource->helper_call_upper_bound);
    ribos_artifact_writer_u64(
        writer,
        payload_base + RIBOS_PAYLOAD_STACK_BYTES_OFFSET,
        resource->maximum_stack_bytes);
    ribos_artifact_writer_u32(
        writer,
        payload_base + RIBOS_PAYLOAD_CALL_DEPTH_OFFSET,
        resource->maximum_call_depth);
    ribos_artifact_writer_bytes(
        writer,
        payload_base + RIBOS_PAYLOAD_SCHEMA_DIGEST_OFFSET,
        context->module.schema_digest,
        RIBOS_SCHEMA_DIGEST_BYTES);
    ribos_artifact_writer_u64(
        writer,
        payload_base + RIBOS_PAYLOAD_DIRECTORY_OFFSET_OFFSET,
        RIBOS_ARTIFACT_PAYLOAD_HEADER_BYTES);
    ribos_artifact_writer_u64(
        writer,
        payload_base + RIBOS_PAYLOAD_DIRECTORY_LENGTH_OFFSET,
        directory_length);
    ribos_artifact_writer_u64(
        writer,
        payload_base + RIBOS_PAYLOAD_LENGTH_OFFSET,
        context->payload_length);
}

static void
ribos_artifact_write_directory(
    const RibosArtifactEmitContext *context,
    RibosArtifactWriter *writer,
    size_t payload_base)
{
    size_t kind;

    for (kind = 1; kind <= context->section_count; ++kind) {
        const RibosArtifactSectionPlan *section =
            &context->sections[kind];
        size_t descriptor =
            payload_base +
            RIBOS_ARTIFACT_PAYLOAD_HEADER_BYTES +
            (kind - 1) *
                RIBOS_ARTIFACT_SECTION_DESCRIPTOR_BYTES;

        ribos_artifact_writer_u16(
            writer,
            descriptor + RIBOS_SECTION_KIND_OFFSET,
            (uint16_t)section->kind);
        ribos_artifact_writer_u32(
            writer,
            descriptor + RIBOS_SECTION_ROW_SIZE_OFFSET,
            section->row_size);
        ribos_artifact_writer_u64(
            writer,
            descriptor + RIBOS_SECTION_DATA_OFFSET_OFFSET,
            section->offset);
        ribos_artifact_writer_u64(
            writer,
            descriptor + RIBOS_SECTION_DATA_LENGTH_OFFSET,
            section->length);
        ribos_artifact_writer_u32(
            writer,
            descriptor + RIBOS_SECTION_COUNT_OFFSET,
            section->count);
    }
}

static void
ribos_artifact_write_types(
    const RibosArtifactEmitContext *context,
    RibosArtifactWriter *writer,
    size_t payload_base)
{
    const RibosArtifactSectionPlan *section =
        &context->sections[RIBOS_ARTIFACT_SECTION_TYPES];
    size_t index;

    for (index = 0; index < context->module.type_count; ++index) {
        const RibosIrType *type = &context->module.types[index];
        const RibosIrTypeLayout *layout =
            ribos_ir_resource_type_layout(
                context->resources,
                (uint32_t)index);
        uint16_t wire_kind = ribos_artifact_type_kind(type->kind);
        uint32_t wire_storage = layout == NULL ?
            UINT32_MAX :
            ribos_artifact_storage_kind(layout->storage_kind);
        size_t name_length =
            ribos_artifact_bounded_name_length(type->name);
        size_t row =
            payload_base + section->offset +
            index * section->row_size;

        if (layout == NULL || wire_kind == UINT16_MAX ||
            wire_storage == UINT32_MAX || name_length == SIZE_MAX ||
            name_length > 63) {
            writer->failed = 1;
            return;
        }
        ribos_artifact_writer_u32(writer, row, type->id);
        ribos_artifact_writer_u16(
            writer, row + 4, wire_kind);
        ribos_artifact_writer_u16(writer, row + 6, type->bits);
        ribos_artifact_writer_u32(writer, row + 8, type->first_type);
        ribos_artifact_writer_u32(writer, row + 12, type->second_type);
        ribos_artifact_writer_u32(writer, row + 16, type->bound);
        ribos_artifact_writer_u32(writer, row + 20, type->shape_start);
        ribos_artifact_writer_u32(writer, row + 24, type->shape_count);
        ribos_artifact_writer_u32(writer, row + 28, type->abi_size);
        ribos_artifact_writer_u32(
            writer, row + 32, type->abi_alignment);
        ribos_artifact_writer_u32(
            writer,
            row + 36,
            wire_storage);
        ribos_artifact_writer_u32(
            writer, row + 40, layout->byte_size);
        ribos_artifact_writer_u32(
            writer, row + 44, layout->element_stride);
        ribos_artifact_writer_u32(
            writer, row + 48, layout->payload_offset);
        ribos_artifact_writer_u32(
            writer, row + 52, layout->capacity);
        ribos_artifact_writer_u32(
            writer, row + 56, (uint32_t)name_length);
        ribos_artifact_writer_bytes(
            writer,
            row + 60,
            (const uint8_t *)type->name,
            name_length);
    }
}

static void
ribos_artifact_write_shapes(
    const RibosArtifactEmitContext *context,
    RibosArtifactWriter *writer,
    size_t payload_base)
{
    const RibosArtifactSectionPlan *section =
        &context->sections[RIBOS_ARTIFACT_SECTION_SHAPES];
    size_t index;

    for (index = 0; index < context->module.shape_count; ++index) {
        const RibosIrShape *shape = &context->module.shapes[index];
        uint32_t wire_kind =
            ribos_artifact_shape_kind(shape->kind);
        size_t row =
            payload_base + section->offset +
            index * section->row_size;

        if (wire_kind == UINT32_MAX) {
            writer->failed = 1;
            return;
        }
        ribos_artifact_writer_u32(writer, row, shape->id);
        ribos_artifact_writer_u32(
            writer, row + 4, wire_kind);
        ribos_artifact_writer_u32(
            writer, row + 8, shape->owner_type);
        ribos_artifact_writer_u32(
            writer, row + 12, shape->variant_tag);
        ribos_artifact_writer_u32(writer, row + 16, shape->ordinal);
        ribos_artifact_writer_u32(
            writer, row + 20, shape->value_type);
    }
}

static void
ribos_artifact_write_constants(
    const RibosArtifactEmitContext *context,
    RibosArtifactWriter *writer,
    size_t payload_base)
{
    const RibosArtifactSectionPlan *section =
        &context->sections[RIBOS_ARTIFACT_SECTION_CONSTANTS];
    const RibosArtifactSectionPlan *bytes =
        &context->sections[
            RIBOS_ARTIFACT_SECTION_CONSTANT_BYTES];
    size_t index;

    for (index = 0; index < context->module.constant_count; ++index) {
        const RibosIrConstant *constant =
            &context->module.constants[index];
        uint16_t wire_kind =
            ribos_artifact_constant_kind(constant->kind);
        size_t row =
            payload_base + section->offset +
            index * section->row_size;

        if (wire_kind == UINT16_MAX) {
            writer->failed = 1;
            return;
        }
        ribos_artifact_writer_u32(writer, row, constant->id);
        ribos_artifact_writer_u16(
            writer, row + 4, wire_kind);
        ribos_artifact_writer_u32(
            writer, row + 8, constant->byte_offset);
        ribos_artifact_writer_u32(
            writer, row + 12, constant->byte_length);
        ribos_artifact_writer_u64(
            writer, row + 16, constant->stable_hash);
    }
    ribos_artifact_writer_bytes(
        writer,
        payload_base + bytes->offset,
        context->module.constant_bytes,
        context->module.constant_byte_count);
}

static void
ribos_artifact_write_functions(
    const RibosArtifactEmitContext *context,
    RibosArtifactWriter *writer,
    size_t payload_base)
{
    const RibosArtifactSectionPlan *section =
        &context->sections[RIBOS_ARTIFACT_SECTION_FUNCTIONS];
    size_t index;

    for (index = 0; index < context->module.function_count; ++index) {
        const RibosIrFunction *function =
            &context->module.functions[index];
        const RibosIrFunctionResource *resource =
            ribos_ir_resource_function(
                context->resources,
                (uint32_t)index);
        uint32_t wire_flags =
            ribos_artifact_function_flags(function->flags);
        uint32_t wire_terminal = resource == NULL ?
            UINT32_MAX :
            ribos_artifact_terminal_mask(resource->terminal_mask);
        size_t row =
            payload_base + section->offset +
            index * section->row_size;

        if (resource == NULL || wire_flags == UINT32_MAX ||
            wire_terminal == UINT32_MAX) {
            writer->failed = 1;
            return;
        }
        ribos_artifact_writer_u32(writer, row, function->id);
        ribos_artifact_writer_u32(writer, row + 4, wire_flags);
        ribos_artifact_writer_u32(
            writer, row + 8, function->return_type);
        ribos_artifact_writer_u32(
            writer, row + 12, function->entry_block);
        ribos_artifact_writer_u32(
            writer, row + 16, function->first_block);
        ribos_artifact_writer_u32(
            writer, row + 20, function->block_count);
        ribos_artifact_writer_u32(
            writer, row + 24, function->first_slot);
        ribos_artifact_writer_u32(
            writer, row + 28, function->slot_count);
        ribos_artifact_writer_u32(
            writer, row + 32, function->parameter_start);
        ribos_artifact_writer_u32(
            writer, row + 36, function->parameter_count);
        ribos_artifact_writer_u32(
            writer, row + 40, function->declared_capabilities);
        ribos_artifact_writer_u32(
            writer, row + 44, function->required_capabilities);
        ribos_artifact_writer_u64(
            writer,
            row + 48,
            function->declared_instruction_budget);
        ribos_artifact_writer_u64(
            writer,
            row + 56,
            resource->instruction_upper_bound);
        ribos_artifact_writer_u64(
            writer,
            row + 64,
            function->declared_helper_budget);
        ribos_artifact_writer_u64(
            writer,
            row + 72,
            resource->helper_call_upper_bound);
        ribos_artifact_writer_u64(
            writer,
            row + 80,
            resource->maximum_stack_bytes);
        ribos_artifact_writer_u32(
            writer, row + 88, resource->frame_bytes);
        ribos_artifact_writer_u32(
            writer, row + 92, resource->maximum_call_depth);
        ribos_artifact_writer_u32(
            writer, row + 96, wire_terminal);
    }
}

static void
ribos_artifact_write_blocks_and_loops(
    const RibosArtifactEmitContext *context,
    RibosArtifactWriter *writer,
    size_t payload_base)
{
    const RibosArtifactSectionPlan *blocks =
        &context->sections[RIBOS_ARTIFACT_SECTION_BLOCKS];
    const RibosArtifactSectionPlan *loops =
        &context->sections[RIBOS_ARTIFACT_SECTION_LOOPS];
    size_t index;

    for (index = 0; index < context->module.block_count; ++index) {
        const RibosIrBlock *block = &context->module.blocks[index];
        uint32_t wire_flags =
            ribos_artifact_block_flags(block->flags);
        size_t row =
            payload_base + blocks->offset +
            index * blocks->row_size;

        if (wire_flags == UINT32_MAX) {
            writer->failed = 1;
            return;
        }
        ribos_artifact_writer_u32(writer, row, block->id);
        ribos_artifact_writer_u32(
            writer, row + 4, block->function_id);
        ribos_artifact_writer_u32(
            writer, row + 8, block->first_instruction);
        ribos_artifact_writer_u32(
            writer, row + 12, block->last_instruction);
        ribos_artifact_writer_u32(
            writer, row + 16, block->instruction_count);
        ribos_artifact_writer_u32(
            writer, row + 20, block->parameter_start);
        ribos_artifact_writer_u32(
            writer, row + 24, block->parameter_count);
        ribos_artifact_writer_u32(writer, row + 28, wire_flags);
    }
    for (index = 0; index < context->module.loop_count; ++index) {
        const RibosIrLoop *loop = &context->module.loops[index];
        size_t row =
            payload_base + loops->offset +
            index * loops->row_size;

        ribos_artifact_writer_u32(writer, row, loop->id);
        ribos_artifact_writer_u32(
            writer, row + 4, loop->function_id);
        ribos_artifact_writer_u32(
            writer, row + 8, loop->header_block);
        ribos_artifact_writer_u32(
            writer, row + 12, loop->body_block);
        ribos_artifact_writer_u32(
            writer, row + 16, loop->exit_block);
        ribos_artifact_writer_u32(
            writer, row + 20, loop->latch_block);
        ribos_artifact_writer_u32(
            writer, row + 24, loop->trip_count);
        ribos_artifact_writer_u32(
            writer,
            row + 28,
            context->options.include_source_map ?
                loop->source_map_id :
                RIBOS_ARTIFACT_INVALID_ID);
    }
}

static void
ribos_artifact_write_slots(
    const RibosArtifactEmitContext *context,
    RibosArtifactWriter *writer,
    size_t payload_base)
{
    const RibosArtifactSectionPlan *section =
        &context->sections[RIBOS_ARTIFACT_SECTION_SLOTS];
    size_t index;

    for (index = 0; index < context->module.slot_count; ++index) {
        const RibosIrSlot *slot = &context->module.slots[index];
        const RibosIrSlotLayout *layout =
            ribos_ir_resource_slot(
                context->resources,
                (uint32_t)index);
        uint32_t wire_flags =
            ribos_artifact_slot_flags(slot->flags);
        size_t row =
            payload_base + section->offset +
            index * section->row_size;

        if (layout == NULL || wire_flags == UINT32_MAX) {
            writer->failed = 1;
            return;
        }
        ribos_artifact_writer_u32(writer, row, slot->id);
        ribos_artifact_writer_u32(
            writer, row + 4, slot->function_id);
        ribos_artifact_writer_u32(writer, row + 8, slot->type_id);
        ribos_artifact_writer_u32(
            writer, row + 12, layout->frame_offset);
        ribos_artifact_writer_u32(
            writer, row + 16, layout->byte_size);
        ribos_artifact_writer_u32(
            writer, row + 20, layout->alignment);
        ribos_artifact_writer_u32(
            writer,
            row + 24,
            context->options.include_source_map ?
                slot->source_map_id :
                RIBOS_ARTIFACT_INVALID_ID);
        ribos_artifact_writer_u32(writer, row + 28, wire_flags);
    }
}

static void
ribos_artifact_write_instructions_and_operands(
    const RibosArtifactEmitContext *context,
    RibosArtifactWriter *writer,
    size_t payload_base)
{
    const RibosArtifactSectionPlan *instructions =
        &context->sections[
            RIBOS_ARTIFACT_SECTION_INSTRUCTIONS];
    const RibosArtifactSectionPlan *operands =
        &context->sections[RIBOS_ARTIFACT_SECTION_OPERANDS];
    size_t index;

    for (index = 0;
         index < context->module.instruction_count;
         ++index) {
        const RibosIrInstruction *instruction =
            &context->module.instructions[index];
        RibosBytecodeOpcode opcode =
            ribos_artifact_opcode(instruction->opcode);
        uint32_t target = instruction->target;
        int invalid_target = 0;
        size_t row =
            payload_base + instructions->offset +
            index * instructions->row_size;

        if (opcode == RIBOS_BC_CHECKED_UNARY ||
            opcode == RIBOS_BC_CHECKED_BINARY) {
            target = ribos_artifact_checked_operator(target);
            invalid_target = target == UINT32_MAX;
        }
        if (opcode == 0 || invalid_target ||
            instruction->operand_count > UINT16_MAX) {
            writer->failed = 1;
            return;
        }
        ribos_artifact_writer_u8(writer, row, (uint8_t)opcode);
        ribos_artifact_writer_u16(
            writer, row + 2, (uint16_t)instruction->operand_count);
        ribos_artifact_writer_u32(writer, row + 4, instruction->id);
        ribos_artifact_writer_u32(
            writer, row + 8, instruction->block_id);
        ribos_artifact_writer_u32(
            writer, row + 12, instruction->result_slot);
        ribos_artifact_writer_u32(
            writer, row + 16, instruction->operand_start);
        ribos_artifact_writer_u32(
            writer, row + 20, target);
        ribos_artifact_writer_u32(
            writer, row + 24, instruction->alternate);
        ribos_artifact_writer_u32(
            writer,
            row + 28,
            context->options.include_source_map ?
                instruction->source_map_id :
                RIBOS_ARTIFACT_INVALID_ID);
        ribos_artifact_writer_u32(
            writer, row + 32, instruction->next_in_block);
        ribos_artifact_writer_u64(
            writer, row + 40, instruction->immediate);
    }
    for (index = 0; index < context->module.operand_count; ++index) {
        ribos_artifact_writer_u32(
            writer,
            payload_base + operands->offset +
                index * operands->row_size,
            context->module.operands[index]);
    }
}

static void
ribos_artifact_write_helpers(
    const RibosArtifactEmitContext *context,
    RibosArtifactWriter *writer,
    size_t payload_base)
{
    const RibosArtifactSectionPlan *imports =
        &context->sections[
            RIBOS_ARTIFACT_SECTION_HELPER_IMPORTS];
    const RibosArtifactSectionPlan *bounds =
        &context->sections[
            RIBOS_ARTIFACT_SECTION_HELPER_BOUNDS];
    size_t index;

    for (index = 0; index < context->import_count; ++index) {
        size_t row =
            payload_base + imports->offset +
            index * imports->row_size;

        ribos_artifact_writer_u32(
            writer, row, context->imports[index].stable_id);
        ribos_artifact_writer_u32(
            writer,
            row + 4,
            context->imports[index].capabilities);
        ribos_artifact_writer_u32(
            writer,
            row + 8,
            context->imports[index].call_site_count);
    }
    for (index = 0;
         index < context->resource_summary.helper_bound_count;
         ++index) {
        const RibosIrHelperBound *bound =
            ribos_ir_resource_helper_bound(
                context->resources,
                index);
        size_t row =
            payload_base + bounds->offset +
            index * bounds->row_size;

        if (bound == NULL) {
            writer->failed = 1;
            return;
        }
        ribos_artifact_writer_u32(
            writer, row, bound->function_id);
        ribos_artifact_writer_u32(
            writer, row + 4, bound->helper_stable_id);
        ribos_artifact_writer_u64(
            writer, row + 8, bound->call_upper_bound);
    }
}

static void
ribos_artifact_write_source_maps(
    const RibosArtifactEmitContext *context,
    RibosArtifactWriter *writer,
    size_t payload_base)
{
    const RibosArtifactSectionPlan *section;
    size_t index;

    if (!context->options.include_source_map) {
        return;
    }
    section = &context->sections[
        RIBOS_ARTIFACT_SECTION_SOURCE_MAPS];
    for (index = 0; index < context->module.source_map_count; ++index) {
        const RibosIrSourceMap *source =
            &context->module.source_maps[index];
        size_t row =
            payload_base + section->offset +
            index * section->row_size;

        ribos_artifact_writer_u32(writer, row, source->id);
        ribos_artifact_writer_u32(
            writer, row + 4, source->ast_node_id);
        ribos_artifact_writer_u64(
            writer, row + 8, source->start_byte);
        ribos_artifact_writer_u64(
            writer, row + 16, source->end_byte);
        ribos_artifact_writer_u32(
            writer, row + 24, source->start_line);
        ribos_artifact_writer_u32(
            writer, row + 28, source->start_column);
        ribos_artifact_writer_u32(
            writer, row + 32, source->end_line);
        ribos_artifact_writer_u32(
            writer, row + 36, source->end_column);
    }
}

static void
ribos_artifact_write_envelope(
    const RibosArtifactEmitContext *context,
    RibosArtifactWriter *writer,
    const uint8_t hash[RIBOS_SCHEMA_DIGEST_BYTES])
{
    const RibosArtifactSignature *signature =
        &context->options.signature;
    size_t key_id_offset;
    size_t signature_offset;

    if (!ribos_artifact_size_add(
            RIBOS_ARTIFACT_ENVELOPE_BYTES,
            context->payload_length,
            &key_id_offset) ||
        !ribos_artifact_size_add(
            key_id_offset,
            signature->key_id_length,
            &signature_offset)) {
        writer->failed = 1;
        return;
    }

    ribos_artifact_writer_bytes(
        writer,
        RIBOS_ENVELOPE_MAGIC_OFFSET,
        (const uint8_t *)RIBOS_ARTIFACT_ENVELOPE_MAGIC,
        RIBOS_ARTIFACT_MAGIC_BYTES);
    ribos_artifact_writer_u16(
        writer,
        RIBOS_ENVELOPE_MAJOR_OFFSET,
        RIBOS_ARTIFACT_ENVELOPE_V1_MAJOR);
    ribos_artifact_writer_u16(
        writer,
        RIBOS_ENVELOPE_MINOR_OFFSET,
        RIBOS_ARTIFACT_ENVELOPE_V1_MINOR);
    ribos_artifact_writer_u32(
        writer,
        RIBOS_ENVELOPE_HEADER_BYTES_OFFSET,
        RIBOS_ARTIFACT_ENVELOPE_BYTES);
    ribos_artifact_writer_u32(
        writer,
        RIBOS_ENVELOPE_FLAGS_OFFSET,
        signature->algorithm == RIBOS_ARTIFACT_SIGNATURE_NONE ?
            0 : RIBOS_ARTIFACT_ENVELOPE_SIGNED);
    ribos_artifact_writer_u16(
        writer,
        RIBOS_ENVELOPE_HASH_ALGORITHM_OFFSET,
        RIBOS_ARTIFACT_HASH_SHA256);
    ribos_artifact_writer_u16(
        writer,
        RIBOS_ENVELOPE_SIGNATURE_ALGORITHM_OFFSET,
        (uint16_t)signature->algorithm);
    ribos_artifact_writer_u64(
        writer,
        RIBOS_ENVELOPE_PAYLOAD_OFFSET_OFFSET,
        RIBOS_ARTIFACT_ENVELOPE_BYTES);
    ribos_artifact_writer_u64(
        writer,
        RIBOS_ENVELOPE_PAYLOAD_LENGTH_OFFSET,
        context->payload_length);
    ribos_artifact_writer_u64(
        writer,
        RIBOS_ENVELOPE_KEY_ID_OFFSET_OFFSET,
        key_id_offset);
    ribos_artifact_writer_u32(
        writer,
        RIBOS_ENVELOPE_KEY_ID_LENGTH_OFFSET,
        (uint32_t)signature->key_id_length);
    ribos_artifact_writer_u32(
        writer,
        RIBOS_ENVELOPE_SIGNATURE_LENGTH_OFFSET,
        (uint32_t)signature->signature_length);
    ribos_artifact_writer_u64(
        writer,
        RIBOS_ENVELOPE_SIGNATURE_OFFSET_OFFSET,
        signature_offset);
    ribos_artifact_writer_u64(
        writer,
        RIBOS_ENVELOPE_TOTAL_LENGTH_OFFSET,
        context->total_length);
    ribos_artifact_writer_bytes(
        writer,
        RIBOS_ENVELOPE_HASH_OFFSET,
        hash,
        RIBOS_SCHEMA_DIGEST_BYTES);
    ribos_artifact_writer_bytes(
        writer,
        key_id_offset,
        signature->key_id,
        signature->key_id_length);
    ribos_artifact_writer_bytes(
        writer,
        signature_offset,
        signature->signature,
        signature->signature_length);
}

RibosArtifactStatus
ribos_artifact_emit_v1(
    const RibosIrModule *module,
    const RibosArtifactEmitOptions *options,
    uint8_t *output,
    size_t output_capacity,
    size_t *required_size)
{
    RibosArtifactEmitContext context;
    RibosArtifactWriter writer;
    RibosArtifactStatus status;
    uint8_t hash[RIBOS_SCHEMA_DIGEST_BYTES];
    size_t payload_base = RIBOS_ARTIFACT_ENVELOPE_BYTES;

    if (module == NULL || required_size == NULL ||
        (output == NULL && output_capacity != 0)) {
        return RIBOS_ARTIFACT_INVALID_ARGUMENT;
    }
    status = ribos_artifact_prepare_context(
        module,
        options,
        &context);
    if (status != RIBOS_ARTIFACT_OK) {
        ribos_ir_resource_closure_destroy(context.resources);
        return status;
    }
    *required_size = context.total_length;
    if (output == NULL) {
        ribos_ir_resource_closure_destroy(context.resources);
        return RIBOS_ARTIFACT_OK;
    }
    if (output_capacity < context.total_length) {
        ribos_ir_resource_closure_destroy(context.resources);
        return RIBOS_ARTIFACT_CAPACITY_EXCEEDED;
    }
    memset(output, 0, context.total_length);
    writer = (RibosArtifactWriter){
        .output = output,
        .capacity = context.total_length,
    };
    ribos_artifact_write_payload_header(
        &context, &writer, payload_base);
    ribos_artifact_write_directory(
        &context, &writer, payload_base);
    ribos_artifact_write_types(&context, &writer, payload_base);
    ribos_artifact_write_shapes(&context, &writer, payload_base);
    ribos_artifact_write_constants(&context, &writer, payload_base);
    ribos_artifact_write_functions(&context, &writer, payload_base);
    ribos_artifact_write_blocks_and_loops(
        &context, &writer, payload_base);
    ribos_artifact_write_slots(&context, &writer, payload_base);
    ribos_artifact_write_instructions_and_operands(
        &context, &writer, payload_base);
    ribos_artifact_write_helpers(&context, &writer, payload_base);
    ribos_artifact_write_source_maps(
        &context, &writer, payload_base);
    if (!writer.failed) {
        ribos_artifact_sha256(
            output + payload_base,
            context.payload_length,
            hash);
        ribos_artifact_write_envelope(&context, &writer, hash);
    }
    ribos_ir_resource_closure_destroy(context.resources);
    return writer.failed ?
        RIBOS_ARTIFACT_CAPACITY_EXCEEDED : RIBOS_ARTIFACT_OK;
}
