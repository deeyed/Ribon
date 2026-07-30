#ifndef RIBOS_IR_INTERNAL_H
#define RIBOS_IR_INTERNAL_H

#include "ribos/ir/builder.h"

#define RIBOS_IR_MAX_TYPES 256u
#define RIBOS_IR_MAX_SHAPES 4096u
#define RIBOS_IR_MAX_CONSTANTS 4096u
#define RIBOS_IR_MAX_CONSTANT_BYTES (1024u * 1024u)
#define RIBOS_IR_MAX_FUNCTIONS 64u
#define RIBOS_IR_MAX_BLOCKS 4096u
#define RIBOS_IR_MAX_LOOPS 1024u
#define RIBOS_IR_MAX_SLOTS 16384u
#define RIBOS_IR_MAX_INSTRUCTIONS 65536u
#define RIBOS_IR_MAX_OPERANDS 131072u
#define RIBOS_IR_MAX_SOURCE_MAPS 65536u
#define RIBOS_IR_MAX_HELPER_CALLS 16384u

struct RibosIrModule {
    const RibosAllocator *allocator;
    uint16_t format_major;
    uint16_t format_minor;
    uint8_t schema_digest[RIBOS_SCHEMA_DIGEST_BYTES];
    RibosIrType types[RIBOS_IR_MAX_TYPES];
    size_t type_count;
    RibosIrShape shapes[RIBOS_IR_MAX_SHAPES];
    size_t shape_count;
    RibosIrConstant constants[RIBOS_IR_MAX_CONSTANTS];
    size_t constant_count;
    uint8_t constant_bytes[RIBOS_IR_MAX_CONSTANT_BYTES];
    size_t constant_byte_count;
    RibosIrFunction functions[RIBOS_IR_MAX_FUNCTIONS];
    size_t function_count;
    RibosIrBlock blocks[RIBOS_IR_MAX_BLOCKS];
    size_t block_count;
    RibosIrLoop loops[RIBOS_IR_MAX_LOOPS];
    size_t loop_count;
    RibosIrSlot slots[RIBOS_IR_MAX_SLOTS];
    size_t slot_count;
    RibosIrInstruction instructions[RIBOS_IR_MAX_INSTRUCTIONS];
    size_t instruction_count;
    uint32_t operands[RIBOS_IR_MAX_OPERANDS];
    size_t operand_count;
    RibosIrSourceMap source_maps[RIBOS_IR_MAX_SOURCE_MAPS];
    size_t source_map_count;
    RibosIrHelperCallSite helper_calls[RIBOS_IR_MAX_HELPER_CALLS];
    size_t helper_call_count;
};

#endif
