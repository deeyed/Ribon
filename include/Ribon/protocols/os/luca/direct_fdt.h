#ifndef RIBON_PROTOCOLS_OS_LUCA_DIRECT_FDT_H
#define RIBON_PROTOCOLS_OS_LUCA_DIRECT_FDT_H

#include <stdint.h>

#include <Ribon/core/memory.h>
#include <Ribon/protocol/protocol.h>

/** @brief LUCA direct-FDT가 허용하는 root address-cell 수다. */
#define RIBON_LUCA_DIRECT_FDT_MIN_ADDRESS_CELLS 1u
#define RIBON_LUCA_DIRECT_FDT_MAX_ADDRESS_CELLS 2u

/** @brief 한 direct-FDT에 게시할 merged reservation 수 상한이다. */
#define RIBON_LUCA_DIRECT_FDT_RESERVATION_CAPACITY 64u

/** @brief QEMU virt development image가 runtime normal-memory로 유지하는 창이다. */
#define RIBON_LUCA_DIRECT_FDT_RUNTIME_RAM_START UINT64_C(0x40000000)
#define RIBON_LUCA_DIRECT_FDT_RUNTIME_RAM_END UINT64_C(0x80000000)

/** @brief Kernel, World와 handoff storage가 runtime RAM 창 안에 있는지 검사한다. */
int ribon_luca_direct_fdt_runtime_span_valid(uint64_t base, uint64_t size);

/**
 * @brief Validated source FDT에 kernel/World span과 reservation을 게시한다.
 *
 * Source와 destination은 겹치면 안 된다. Reservation은 half-open range이며
 * builder가 정렬·병합하고 기존 FDT reserve map과 함께 bounded하게 직렬화한다.
 */
int ribon_luca_direct_fdt_build_blob(
    const void *source,
    uint64_t source_capacity,
    uint64_t kernel_start,
    uint64_t kernel_size,
    uint64_t kernel_entry,
    uint64_t initrd_start,
    uint64_t initrd_size,
    const struct RibonMemoryRegion *reservations,
    uint32_t reservation_count,
    void *destination,
    uint64_t destination_capacity,
    uint64_t *output_size);

/** @brief LUCA direct-FDT development protocol handoff serializer다. */
int ribon_luca_build_direct_fdt(
    const struct RibonBootPlan *plan,
    const struct RibonBootEnvironment *environment,
    const struct RibonMutableMemoryMap *normalized_memory_map,
    void *buffer,
    uint64_t capacity,
    struct RibonHandoffArtifact *out);

/** @brief Explicit development-only LUCA direct-FDT protocol plugin이다. */
extern const struct RibonPluginDescriptor
    ribon_luca_direct_fdt_protocol_plugin_descriptor;

#endif
