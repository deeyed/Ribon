#include <Ribon/arch/aarch64/entry_state.h>
#include <Ribon/arch/ops.h>

#include <stdio.h>

static _Alignas(4096) unsigned char
    bridge_tables[RIBON_AARCH64_BRIDGE_TABLE_BYTES];

static void continuation(void *context) {
    (void)context;
}

static int check_entry_models(void) {
    struct RibonAarch64EntryState state;
    if (ribon_aarch64_entry_state_from_registers(
            4u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, &state) !=
            RIBON_ARCH_OPERATION_OK ||
        state.current_el != 1u || state.translation_enabled == 0u ||
        state.ttbr1 != 5u || state.tcr != 2u || state.mair != 3u) {
        return 0;
    }
    if (ribon_aarch64_entry_state_from_registers(
            8u, 0u, 12u, 13u, 14u, UINT64_MAX, 16u, 17u, 18u,
            &state) != RIBON_ARCH_OPERATION_OK ||
        state.current_el != 2u || state.translation_enabled != 0u ||
        state.ttbr0 != 14u || state.ttbr1 != 0u || state.vbar != 16u) {
        return 0;
    }
    return ribon_aarch64_entry_state_from_registers(
               12u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, &state) ==
           RIBON_ARCH_OPERATION_BAD_ARGUMENT;
}

static int check_range_set(void) {
    struct RibonAarch64BridgeRangeSet set = {0};
    if (ribon_aarch64_bridge_range_add(
            &set, UINT64_C(0x40000123), 64u,
            RIBON_AARCH64_BRIDGE_MEMORY_NORMAL) != RIBON_ARCH_OPERATION_OK ||
        ribon_aarch64_bridge_range_add(
            &set, UINT64_C(0x40001000), 4096u,
            RIBON_AARCH64_BRIDGE_MEMORY_NORMAL) != RIBON_ARCH_OPERATION_OK ||
        set.count != 1u || set.ranges[0].base != UINT64_C(0x40000000) ||
        set.ranges[0].length != 8192u) {
        return 0;
    }
    return ribon_aarch64_bridge_range_add(
               &set, UINT64_C(0x40000000), 4096u,
               RIBON_AARCH64_BRIDGE_MEMORY_DEVICE) ==
           RIBON_ARCH_OPERATION_INVALID_PAYLOAD;
}

static int check_bridge(void) {
    const struct RibonAarch64BridgeRange valid_ranges[] = {
        {UINT64_C(0x09000000), 4096u, RIBON_AARCH64_BRIDGE_MEMORY_DEVICE},
        {UINT64_C(0x40000000), 8192u, RIBON_AARCH64_BRIDGE_MEMORY_NORMAL},
    };
    struct RibonAarch64EntryState entry;
    struct RibonAarch64PostExitTransition transition;
    struct RibonAarch64BridgeRequest request = {
        .size = sizeof(request),
        .abi_version = RIBON_AARCH64_ENTRY_STATE_ABI_VERSION,
        .ranges = valid_ranges,
        .range_count = 2u,
        .table_buffer = bridge_tables,
        .table_buffer_size = sizeof(bridge_tables),
    };
    if (ribon_aarch64_entry_state_from_registers(
            8u, 1u, 2u, 3u, 4u, 0u, 5u, 6u, 7u, &entry) !=
            RIBON_ARCH_OPERATION_OK ||
        ribon_aarch64_prepare_post_exit_bridge(
            &request, &entry, UINT64_C(0x40010000), continuation,
            &request, &transition) != RIBON_ARCH_OPERATION_OK ||
        transition.source_el != 2u || transition.translation_root !=
            (uint64_t)(uintptr_t)bridge_tables ||
        transition.mapped_l1_tables != 2u ||
        (((uint64_t *)bridge_tables)[0] & 3u) != 3u ||
        (((uint64_t *)bridge_tables)[1] & 3u) != 3u) {
        return 0;
    }
    {
        const uint64_t *device_l2 =
            (const uint64_t *)(uintptr_t)(((uint64_t *)bridge_tables)[0] &
                                          UINT64_C(0x0000FFFFFFFFF000));
        const uint64_t *normal_l2 =
            (const uint64_t *)(uintptr_t)(((uint64_t *)bridge_tables)[1] &
                                          UINT64_C(0x0000FFFFFFFFF000));
        if ((device_l2[(UINT64_C(0x09000000) >> 21u) & 511u] & 1u) == 0u ||
            (device_l2[(UINT64_C(0x09200000) >> 21u) & 511u] & 1u) != 0u ||
            (normal_l2[0] & 1u) == 0u || (normal_l2[1] & 1u) != 0u) {
            return 0;
        }
    }
    {
        const struct RibonAarch64BridgeRange overlapping[] = {
            {UINT64_C(0x40000000), 8192u, RIBON_AARCH64_BRIDGE_MEMORY_NORMAL},
            {UINT64_C(0x40001000), 4096u, RIBON_AARCH64_BRIDGE_MEMORY_NORMAL},
        };
        request.ranges = overlapping;
        if (ribon_aarch64_prepare_post_exit_bridge(
                &request, &entry, UINT64_C(0x40010000), continuation,
                &request, &transition) != RIBON_ARCH_OPERATION_INVALID_PAYLOAD) {
            return 0;
        }
    }
    {
        const struct RibonAarch64BridgeRange unaligned[] = {
            {UINT64_C(0x40000001), 4096u, RIBON_AARCH64_BRIDGE_MEMORY_NORMAL},
        };
        request.ranges = unaligned;
        request.range_count = 1u;
        if (ribon_aarch64_prepare_post_exit_bridge(
                &request, &entry, UINT64_C(0x40010000), continuation,
                &request, &transition) != RIBON_ARCH_OPERATION_INVALID_PAYLOAD) {
            return 0;
        }
    }
    return 1;
}

int main(void) {
    if (!check_entry_models() || !check_range_set() || !check_bridge()) {
        fputs("aarch64_entry_state_tests: contract failed\n", stderr);
        return 1;
    }
    puts("RIBON-AARCH64-ENTRY-STATE-OK el1=modeled el2=modeled bridge=bounded malformed=rejected");
    return 0;
}
