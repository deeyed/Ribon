#ifndef RIBOS_IR_IR_H
#define RIBOS_IR_IR_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "ribos/schema/schema.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file ir.h
 * @brief Ribos frontend와 bytecode backend 사이의 Policy IR v1 계약.
 */

#define RIBOS_IR_V1_MAJOR 1u
#define RIBOS_IR_V1_MINOR 1u
#define RIBOS_IR_INVALID_ID UINT32_MAX

/** Policy IR의 안정된 종료 상태다. */
typedef enum RibosIrStatus {
    RIBOS_IR_OK = 0,
    RIBOS_IR_INVALID_ARGUMENT,
    RIBOS_IR_CAPACITY_EXCEEDED,
    RIBOS_IR_INVALID_MODULE,
    RIBOS_IR_LOWERING_FAILED,
    RIBOS_IR_UNBOUNDED_CONTROL_FLOW,
    RIBOS_IR_RESOURCE_EXCEEDED,
    RIBOS_IR_BUDGET_EXCEEDED
} RibosIrStatus;

/** VM bytecode와 독립적인 typed Policy IR opcode다. */
typedef enum RibosIrOpcode {
    RIBOS_IR_OP_PARAMETER = 0,
    RIBOS_IR_OP_CONST_UNIT,
    RIBOS_IR_OP_CONST_BOOL,
    RIBOS_IR_OP_CONST_INTEGER,
    RIBOS_IR_OP_CONST_STRING,
    RIBOS_IR_OP_CONST_SYMBOL,
    RIBOS_IR_OP_MOVE,
    RIBOS_IR_OP_CHECKED_UNARY,
    RIBOS_IR_OP_CHECKED_BINARY,
    RIBOS_IR_OP_BUILD_LIST,
    RIBOS_IR_OP_BUILD_MAP,
    RIBOS_IR_OP_BUILD_STRUCT,
    RIBOS_IR_OP_BUILD_VARIANT,
    RIBOS_IR_OP_MEMBER,
    RIBOS_IR_OP_INDEX,
    RIBOS_IR_OP_COLLECTION_LENGTH,
    RIBOS_IR_OP_VARIANT_TAG,
    RIBOS_IR_OP_VARIANT_PAYLOAD,
    RIBOS_IR_OP_CALL_DIRECT,
    RIBOS_IR_OP_CALL_HELPER,
    RIBOS_IR_OP_JUMP,
    RIBOS_IR_OP_BRANCH,
    RIBOS_IR_OP_RETURN,
    RIBOS_IR_OP_TRAP
} RibosIrOpcode;

/** CHECKED_UNARY/CHECKED_BINARY가 사용하는 frontend 독립 operator ID다. */
typedef enum RibosIrCheckedOperator {
    RIBOS_IR_CHECK_NOT = 1,
    RIBOS_IR_CHECK_EQUAL,
    RIBOS_IR_CHECK_NOT_EQUAL,
    RIBOS_IR_CHECK_LESS,
    RIBOS_IR_CHECK_LESS_EQUAL,
    RIBOS_IR_CHECK_GREATER,
    RIBOS_IR_CHECK_GREATER_EQUAL,
    RIBOS_IR_CHECK_IN,
    RIBOS_IR_CHECK_NOT_IN,
    RIBOS_IR_CHECK_BIT_OR,
    RIBOS_IR_CHECK_BIT_XOR,
    RIBOS_IR_CHECK_BIT_AND,
    RIBOS_IR_CHECK_SHIFT_LEFT,
    RIBOS_IR_CHECK_SHIFT_RIGHT,
    RIBOS_IR_CHECK_ADD,
    RIBOS_IR_CHECK_SUBTRACT,
    RIBOS_IR_CHECK_MULTIPLY,
    RIBOS_IR_CHECK_DIVIDE,
    RIBOS_IR_CHECK_REMAINDER,
    RIBOS_IR_CHECK_POSITIVE,
    RIBOS_IR_CHECK_NEGATIVE,
    RIBOS_IR_CHECK_BIT_NOT
} RibosIrCheckedOperator;

/** Function descriptor의 안정된 의미 비트다. */
typedef enum RibosIrFunctionFlags {
    RIBOS_IR_FUNCTION_POLICY = 1u << 0,
    RIBOS_IR_FUNCTION_PURE = 1u << 1
} RibosIrFunctionFlags;

/** Frontend type table에서 복사한 VM 독립 type 분류다. */
typedef enum RibosIrTypeKind {
    RIBOS_IR_TYPE_ERROR = 0,
    RIBOS_IR_TYPE_UNKNOWN,
    RIBOS_IR_TYPE_UNIT,
    RIBOS_IR_TYPE_BOOL,
    RIBOS_IR_TYPE_UNSIGNED,
    RIBOS_IR_TYPE_SIGNED,
    RIBOS_IR_TYPE_STRING_LITERAL,
    RIBOS_IR_TYPE_NAMED,
    RIBOS_IR_TYPE_ARRAY,
    RIBOS_IR_TYPE_LIST,
    RIBOS_IR_TYPE_FROZEN_MAP,
    RIBOS_IR_TYPE_DICT,
    RIBOS_IR_TYPE_OPTION,
    RIBOS_IR_TYPE_RESULT,
    RIBOS_IR_TYPE_STRUCT,
    RIBOS_IR_TYPE_ENUM
} RibosIrTypeKind;

/** Source와 AST node를 IR operation에 연결하는 half-open range다. */
typedef struct RibosIrSourceMap {
    uint32_t id;
    uint32_t ast_node_id;
    size_t start_byte;
    size_t end_byte;
    uint32_t start_line;
    uint32_t start_column;
    uint32_t end_line;
    uint32_t end_column;
} RibosIrSourceMap;

/** IR module 안에서 stable한 resolved type record다. */
typedef struct RibosIrType {
    uint32_t id;
    RibosIrTypeKind kind;
    uint32_t first_type;
    uint32_t second_type;
    uint32_t bound;
    uint32_t shape_start;
    uint32_t shape_count;
    uint32_t abi_size;
    uint32_t abi_alignment;
    uint8_t bits;
    char name[64];
} RibosIrType;

/** User-defined aggregate shape row의 의미다. */
typedef enum RibosIrShapeKind {
    RIBOS_IR_SHAPE_STRUCT_FIELD = 0,
    RIBOS_IR_SHAPE_ENUM_VARIANT,
    RIBOS_IR_SHAPE_ENUM_PAYLOAD
} RibosIrShapeKind;

/**
 * VM backend가 frontend AST 없이 struct와 enum layout을 해석하는 row다.
 *
 * enum variant row의 value_type은 INVALID_ID다. Payload row의 variant_tag와
 * ordinal은 각각 variant tag와 그 variant 안의 payload index다.
 */
typedef struct RibosIrShape {
    uint32_t id;
    RibosIrShapeKind kind;
    uint32_t owner_type;
    uint32_t variant_tag;
    uint32_t ordinal;
    uint32_t value_type;
    char name[64];
} RibosIrShape;

/** IR constant pool entry의 byte 의미다. */
typedef enum RibosIrConstantKind {
    RIBOS_IR_CONSTANT_STRING = 0,
    RIBOS_IR_CONSTANT_SYMBOL
} RibosIrConstantKind;

/** Source와 독립적으로 보존되는 bounded constant byte slice다. */
typedef struct RibosIrConstant {
    uint32_t id;
    RibosIrConstantKind kind;
    uint32_t byte_offset;
    uint32_t byte_length;
    uint64_t stable_hash;
} RibosIrConstant;

/** Function-owned typed virtual slot이다. */
typedef struct RibosIrSlot {
    uint32_t id;
    uint32_t function_id;
    uint32_t type_id;
    uint32_t source_map_id;
    uint32_t flags;
} RibosIrSlot;

/** Explicit basic block descriptor다. */
typedef struct RibosIrBlock {
    uint32_t id;
    uint32_t function_id;
    uint32_t first_instruction;
    uint32_t last_instruction;
    uint32_t instruction_count;
    uint32_t parameter_start;
    uint32_t parameter_count;
    uint32_t flags;
} RibosIrBlock;

/**
 * Source의 bounded `for` 하나를 CFG back-edge와 결합하는 record다.
 *
 * header는 `body_block`과 `exit_block`으로 분기한다. `latch_block`이 유효하면
 * 그 block은 header로 직접 jump하며 `trip_count`번까지만 재진입할 수 있다.
 * Body가 첫 iteration에서 항상 terminal이면 latch는 `INVALID_ID`다.
 */
typedef struct RibosIrLoop {
    uint32_t id;
    uint32_t function_id;
    uint32_t header_block;
    uint32_t body_block;
    uint32_t exit_block;
    uint32_t latch_block;
    uint32_t trip_count;
    uint32_t source_map_id;
} RibosIrLoop;

/**
 * Policy IR instruction이다.
 *
 * operand_start/count는 module operand table의 연속 slice다. target과 alternate는
 * opcode에 따라 function, helper, block, member 또는 variant stable ID를 가진다.
 */
typedef struct RibosIrInstruction {
    uint32_t id;
    RibosIrOpcode opcode;
    uint32_t block_id;
    uint32_t result_slot;
    uint32_t operand_start;
    uint32_t operand_count;
    uint32_t target;
    uint32_t alternate;
    uint64_t immediate;
    uint32_t source_map_id;
    uint32_t next_in_block;
} RibosIrInstruction;

/** Source helper call과 generated product schema를 결합하는 record다. */
typedef struct RibosIrHelperCallSite {
    uint32_t id;
    uint32_t instruction_id;
    uint32_t helper_stable_id;
    uint32_t capabilities;
    uint32_t result_type;
    uint32_t argument_count;
    uint32_t source_map_id;
} RibosIrHelperCallSite;

/** 한 direct function의 CFG와 budget descriptor다. */
typedef struct RibosIrFunction {
    uint32_t id;
    char name[64];
    uint32_t return_type;
    uint32_t entry_block;
    uint32_t first_block;
    uint32_t block_count;
    uint32_t first_slot;
    uint32_t slot_count;
    uint32_t parameter_start;
    uint32_t parameter_count;
    uint32_t declared_capabilities;
    uint32_t required_capabilities;
    uint64_t declared_instruction_budget;
    uint64_t declared_helper_budget;
    uint64_t helper_call_upper_bound;
    uint32_t maximum_call_depth;
    uint32_t flags;
} RibosIrFunction;

/** Policy IR module의 bounded measurement다. */
typedef struct RibosIrSummary {
    size_t type_count;
    size_t shape_count;
    size_t constant_count;
    size_t constant_byte_count;
    size_t function_count;
    size_t block_count;
    size_t loop_count;
    size_t slot_count;
    size_t instruction_count;
    size_t operand_count;
    size_t source_map_count;
    size_t helper_call_count;
} RibosIrSummary;

/**
 * Host Policy IR storage를 artifact emitter에 read-only로 공개하는 view다.
 *
 * 이 구조는 compiler process 안에서만 유효하며 artifact wire ABI가 아니다.
 * 모든 pointer의 lifetime은 owner `RibosIrModule`과 같다.
 */
typedef struct RibosIrModuleView {
    uint16_t format_major;
    uint16_t format_minor;
    const uint8_t *schema_digest;
    const RibosIrType *types;
    size_t type_count;
    const RibosIrShape *shapes;
    size_t shape_count;
    const RibosIrConstant *constants;
    size_t constant_count;
    const uint8_t *constant_bytes;
    size_t constant_byte_count;
    const RibosIrFunction *functions;
    size_t function_count;
    const RibosIrBlock *blocks;
    size_t block_count;
    const RibosIrLoop *loops;
    size_t loop_count;
    const RibosIrSlot *slots;
    size_t slot_count;
    const RibosIrInstruction *instructions;
    size_t instruction_count;
    const uint32_t *operands;
    size_t operand_count;
    const RibosIrSourceMap *source_maps;
    size_t source_map_count;
    const RibosIrHelperCallSite *helper_calls;
    size_t helper_call_count;
} RibosIrModuleView;

/** 내부 storage를 숨기는 host Policy IR module이다. */
typedef struct RibosIrModule RibosIrModule;

/** 고정 상한 storage를 가진 빈 Policy IR module을 생성한다. */
RibosIrModule *ribos_ir_module_create(void);

/** Policy IR module의 모든 compiler-owned storage를 해제한다. */
void ribos_ir_module_destroy(RibosIrModule *module);

/** Module을 같은 capacity의 빈 v1 module로 되돌린다. */
void ribos_ir_module_reset(RibosIrModule *module);

/** Module의 deterministic count와 bound summary를 반환한다. */
RibosIrStatus ribos_ir_module_summary(
    const RibosIrModule *module,
    RibosIrSummary *summary);

/** Validated 여부와 무관하게 module-owned table의 read-only view를 반환한다. */
RibosIrStatus ribos_ir_module_view(
    const RibosIrModule *module,
    RibosIrModuleView *view);

/** CFG, ID, operand slice와 terminator invariants를 검사한다. */
RibosIrStatus ribos_ir_validate_v1(const RibosIrModule *module);

/** Pointer identity를 포함하지 않는 deterministic text dump를 기록한다. */
RibosIrStatus ribos_ir_dump_v1(
    const RibosIrModule *module,
    FILE *output);

/** Policy IR opcode의 안정된 ASCII spelling을 반환한다. */
const char *ribos_ir_opcode_name(RibosIrOpcode opcode);

#ifdef __cplusplus
}
#endif

#endif
