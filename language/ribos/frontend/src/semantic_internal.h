#ifndef RIBOS_FRONTEND_SEMANTIC_INTERNAL_H
#define RIBOS_FRONTEND_SEMANTIC_INTERNAL_H

#include "parser_internal.h"

#define RIBOS_MAX_TYPES 256u
#define RIBOS_MAX_FUNCTIONS 64u
#define RIBOS_MAX_PARAMETERS 16u
#define RIBOS_MAX_AGGREGATE_MEMBERS 64u
#define RIBOS_MAX_ENUM_VARIANTS 63u
#define RIBOS_MAX_LOCALS 256u
#define RIBOS_MAX_SCOPE_DEPTH 64u

typedef enum RibosTypeKind {
    RIBOS_TYPE_ERROR = 0,
    RIBOS_TYPE_UNKNOWN,
    RIBOS_TYPE_UNIT,
    RIBOS_TYPE_BOOL,
    RIBOS_TYPE_UNSIGNED,
    RIBOS_TYPE_SIGNED,
    RIBOS_TYPE_STRING_LITERAL,
    RIBOS_TYPE_NAMED,
    RIBOS_TYPE_ARRAY,
    RIBOS_TYPE_LIST,
    RIBOS_TYPE_FROZEN_MAP,
    RIBOS_TYPE_DICT,
    RIBOS_TYPE_OPTION,
    RIBOS_TYPE_RESULT
} RibosTypeKind;

typedef struct RibosType {
    RibosTypeKind kind;
    const char *name;
    size_t name_length;
    uint32_t first;
    uint32_t second;
    uint32_t bound;
    uint8_t bits;
    RibosAstNode *declaration;
} RibosType;

typedef struct RibosParameterInfo {
    Token *name;
    uint32_t type;
} RibosParameterInfo;

typedef struct RibosStaticCost {
    uint64_t helpers;
    uint64_t calls[RIBOS_MAX_FUNCTIONS];
} RibosStaticCost;

typedef struct RibosFunctionInfo {
    RibosAstNode *declaration;
    Token *name;
    uint32_t return_type;
    RibosParameterInfo parameters[RIBOS_MAX_PARAMETERS];
    size_t parameter_count;
    uint32_t declared_capabilities;
    uint32_t direct_capabilities;
    uint32_t required_capabilities;
    uint64_t instruction_budget;
    uint64_t helper_budget;
    size_t helper_call_sites;
    RibosStaticCost direct_cost;
    uint64_t total_helper_upper_bound;
    uint32_t max_call_depth;
    unsigned is_policy : 1;
    unsigned is_pure : 1;
    unsigned visiting : 1;
    unsigned visited : 1;
} RibosFunctionInfo;

typedef struct RibosLocal {
    Token *name;
    uint32_t type;
    uint32_t depth;
    unsigned mutable_binding : 1;
} RibosLocal;

typedef struct RibosSemanticContext {
    Parser *parser;
    const RibosProductSchema *schema;
    RibosIrModule *ir_module;
    RibosCompileSummary *summary;
    RibosCompileDiagnostic *diagnostic;
    RibosCompileStatus status;
    RibosType types[RIBOS_MAX_TYPES];
    size_t type_count;
    RibosFunctionInfo functions[RIBOS_MAX_FUNCTIONS];
    size_t function_count;
    RibosLocal locals[RIBOS_MAX_LOCALS];
    size_t local_count;
    uint32_t scope_depth;
    uint32_t max_scope_depth;
    RibosFunctionInfo *function;
} RibosSemanticContext;

RibosCompileStatus ribos_lower_policy_ir(
    RibosSemanticContext *context,
    RibosIrModule *module);

#endif
