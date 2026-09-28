#include <Ribon/arch/aarch64/entry_state.h>
#include <Ribon/arch/ops.h>

#include <stddef.h>

#define RIBON_AARCH64_PAGE_SIZE UINT64_C(4096)
#define RIBON_AARCH64_L2_BLOCK UINT64_C(0x200000)
#define RIBON_AARCH64_L1_SPAN UINT64_C(0x40000000)
#define RIBON_AARCH64_ADDRESS_LIMIT (UINT64_C(1) << 39u)
#define RIBON_AARCH64_ADDRESS_MASK UINT64_C(0x0000FFFFFFFFF000)
#define RIBON_AARCH64_DESC_VALID (UINT64_C(1) << 0u)
#define RIBON_AARCH64_DESC_TABLE (UINT64_C(1) << 1u)
#define RIBON_AARCH64_DESC_AF (UINT64_C(1) << 10u)
#define RIBON_AARCH64_DESC_INNER_SHAREABLE (UINT64_C(3) << 8u)
#define RIBON_AARCH64_DESC_PXN (UINT64_C(1) << 53u)
#define RIBON_AARCH64_DESC_UXN (UINT64_C(1) << 54u)
#define RIBON_AARCH64_SCTLR_M (UINT64_C(1) << 0u)
#define RIBON_AARCH64_SCTLR_EL1_BRIDGE UINT64_C(0x0000000030D00805)
#define RIBON_AARCH64_MAIR_EL1_BRIDGE UINT64_C(0x00000000000000FF)

static uint64_t align_down(uint64_t value, uint64_t alignment) {
    return value & ~(alignment - 1u);
}

static int align_up(uint64_t value, uint64_t alignment, uint64_t *out) {
    if (out == 0 || value > UINT64_MAX - (alignment - 1u)) {
        return 0;
    }
    *out = align_down(value + alignment - 1u, alignment);
    return 1;
}

static int valid_kind(enum RibonAarch64BridgeMemoryKind kind) {
    return kind == RIBON_AARCH64_BRIDGE_MEMORY_NORMAL ||
           kind == RIBON_AARCH64_BRIDGE_MEMORY_DEVICE;
}

int ribon_aarch64_bridge_range_add(
    struct RibonAarch64BridgeRangeSet *set,
    uint64_t base,
    uint64_t length,
    enum RibonAarch64BridgeMemoryKind kind) {
    uint64_t end;
    uint64_t aligned_base;
    uint64_t aligned_end;
    uint32_t insert;
    if (set == 0 || length == 0u || !valid_kind(kind) ||
        base > UINT64_MAX - length) {
        return RIBON_ARCH_OPERATION_BAD_ARGUMENT;
    }
    end = base + length;
    aligned_base = align_down(base, RIBON_AARCH64_PAGE_SIZE);
    if (!align_up(end, RIBON_AARCH64_PAGE_SIZE, &aligned_end) ||
        aligned_end <= aligned_base || aligned_end > RIBON_AARCH64_ADDRESS_LIMIT) {
        return RIBON_ARCH_OPERATION_INVALID_PAYLOAD;
    }
    insert = 0u;
    while (insert < set->count && set->ranges[insert].base < aligned_base) {
        ++insert;
    }
    if (insert != 0u) {
        struct RibonAarch64BridgeRange *previous = &set->ranges[insert - 1u];
        const uint64_t previous_end = previous->base + previous->length;
        if (aligned_base <= previous_end) {
            if (previous->kind != kind) {
                return RIBON_ARCH_OPERATION_INVALID_PAYLOAD;
            }
            if (aligned_end > previous_end) {
                previous->length = aligned_end - previous->base;
            }
            while (insert < set->count &&
                   set->ranges[insert].base <= previous->base + previous->length) {
                const uint64_t next_end =
                    set->ranges[insert].base + set->ranges[insert].length;
                if (set->ranges[insert].kind != kind) {
                    return RIBON_ARCH_OPERATION_INVALID_PAYLOAD;
                }
                if (next_end > previous->base + previous->length) {
                    previous->length = next_end - previous->base;
                }
                for (uint32_t move = insert + 1u; move < set->count; ++move) {
                    set->ranges[move - 1u] = set->ranges[move];
                }
                --set->count;
            }
            return RIBON_ARCH_OPERATION_OK;
        }
    }
    if (insert < set->count && aligned_end >= set->ranges[insert].base) {
        struct RibonAarch64BridgeRange *next = &set->ranges[insert];
        const uint64_t next_end = next->base + next->length;
        if (next->kind != kind) {
            return RIBON_ARCH_OPERATION_INVALID_PAYLOAD;
        }
        next->base = aligned_base;
        if (aligned_end > next_end) {
            next->length = aligned_end - next->base;
        } else {
            next->length = next_end - aligned_base;
        }
        return RIBON_ARCH_OPERATION_OK;
    }
    if (set->count == RIBON_AARCH64_BRIDGE_RANGE_LIMIT) {
        return RIBON_ARCH_OPERATION_INVALID_PAYLOAD;
    }
    for (uint32_t move = set->count; move > insert; --move) {
        set->ranges[move] = set->ranges[move - 1u];
    }
    set->ranges[insert] = (struct RibonAarch64BridgeRange){
        .base = aligned_base,
        .length = aligned_end - aligned_base,
        .kind = kind,
    };
    ++set->count;
    return RIBON_ARCH_OPERATION_OK;
}

int ribon_aarch64_entry_state_from_registers(
    uint64_t current_el_raw,
    uint64_t sctlr,
    uint64_t tcr,
    uint64_t mair,
    uint64_t ttbr0,
    uint64_t ttbr1,
    uint64_t vbar,
    uint64_t sp,
    uint64_t daif,
    struct RibonAarch64EntryState *out) {
    const uint32_t current_el = (uint32_t)((current_el_raw >> 2u) & 3u);
    if (out == 0 || (current_el != 1u && current_el != 2u)) {
        return RIBON_ARCH_OPERATION_BAD_ARGUMENT;
    }
    *out = (struct RibonAarch64EntryState){
        .size = sizeof(*out),
        .abi_version = RIBON_AARCH64_ENTRY_STATE_ABI_VERSION,
        .current_el = current_el,
        .translation_enabled = (sctlr & RIBON_AARCH64_SCTLR_M) != 0u,
        .current_el_raw = current_el_raw,
        .sctlr = sctlr,
        .tcr = tcr,
        .mair = mair,
        .ttbr0 = ttbr0,
        .ttbr1 = current_el == 1u ? ttbr1 : 0u,
        .vbar = vbar,
        .sp = sp,
        .daif = daif,
    };
    return RIBON_ARCH_OPERATION_OK;
}

#if defined(__aarch64__)
int ribon_aarch64_entry_state_capture(struct RibonAarch64EntryState *out) {
    uint64_t current_el_raw;
    uint64_t sctlr;
    uint64_t tcr;
    uint64_t mair;
    uint64_t ttbr0;
    uint64_t ttbr1 = 0u;
    uint64_t vbar;
    uint64_t sp;
    uint64_t daif;
    __asm__ __volatile__("mrs %0, CurrentEL" : "=r"(current_el_raw));
    __asm__ __volatile__("mov %0, sp" : "=r"(sp));
    __asm__ __volatile__("mrs %0, daif" : "=r"(daif));
    if (current_el_raw == 4u) {
        __asm__ __volatile__("mrs %0, sctlr_el1" : "=r"(sctlr));
        __asm__ __volatile__("mrs %0, tcr_el1" : "=r"(tcr));
        __asm__ __volatile__("mrs %0, mair_el1" : "=r"(mair));
        __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(ttbr0));
        __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(ttbr1));
        __asm__ __volatile__("mrs %0, vbar_el1" : "=r"(vbar));
    } else if (current_el_raw == 8u) {
        __asm__ __volatile__("mrs %0, sctlr_el2" : "=r"(sctlr));
        __asm__ __volatile__("mrs %0, tcr_el2" : "=r"(tcr));
        __asm__ __volatile__("mrs %0, mair_el2" : "=r"(mair));
        __asm__ __volatile__("mrs %0, ttbr0_el2" : "=r"(ttbr0));
        __asm__ __volatile__("mrs %0, vbar_el2" : "=r"(vbar));
    } else {
        return RIBON_ARCH_OPERATION_UNSUPPORTED;
    }
    return ribon_aarch64_entry_state_from_registers(
        current_el_raw, sctlr, tcr, mair, ttbr0, ttbr1, vbar, sp, daif, out);
}

int ribon_aarch64_entry_address_is_identity(
    const struct RibonAarch64EntryState *state,
    uint64_t address) {
    uint64_t par;
    if (state == 0 || state->size != sizeof(*state) ||
        state->abi_version != RIBON_AARCH64_ENTRY_STATE_ABI_VERSION ||
        (state->current_el != 1u && state->current_el != 2u)) {
        return 0;
    }
    if (state->translation_enabled == 0u) {
        return 1;
    }
    if (state->current_el == 1u) {
        __asm__ __volatile__("at s1e1r, %1; isb; mrs %0, par_el1"
                             : "=r"(par) : "r"(address) : "memory");
    } else {
        __asm__ __volatile__("at s1e2r, %1; isb; mrs %0, par_el1"
                             : "=r"(par) : "r"(address) : "memory");
    }
    return (par & 1u) == 0u &&
           (par & RIBON_AARCH64_ADDRESS_MASK) ==
               (address & RIBON_AARCH64_ADDRESS_MASK);
}
#else
int ribon_aarch64_entry_state_capture(struct RibonAarch64EntryState *out) {
    (void)out;
    return RIBON_ARCH_OPERATION_UNSUPPORTED;
}

int ribon_aarch64_entry_address_is_identity(
    const struct RibonAarch64EntryState *state,
    uint64_t address) {
    (void)state;
    (void)address;
    return 0;
}
#endif

static uint64_t bridge_table_descriptor(uint64_t address) {
    return (address & RIBON_AARCH64_ADDRESS_MASK) |
           RIBON_AARCH64_DESC_VALID | RIBON_AARCH64_DESC_TABLE;
}

static uint64_t bridge_block_descriptor(
    uint64_t address,
    enum RibonAarch64BridgeMemoryKind kind) {
    uint64_t descriptor = (address & RIBON_AARCH64_ADDRESS_MASK) |
                          RIBON_AARCH64_DESC_AF |
                          RIBON_AARCH64_DESC_VALID;
    if (kind == RIBON_AARCH64_BRIDGE_MEMORY_NORMAL) {
        descriptor |= RIBON_AARCH64_DESC_INNER_SHAREABLE;
    } else {
        descriptor |= (UINT64_C(1) << 2u) |
                      RIBON_AARCH64_DESC_PXN |
                      RIBON_AARCH64_DESC_UXN;
    }
    return descriptor;
}

static void zero_tables(uint64_t *tables) {
    for (uint32_t index = 0u;
         index < RIBON_AARCH64_BRIDGE_TABLE_BYTES / sizeof(uint64_t);
         ++index) {
        tables[index] = 0u;
    }
}

static uint64_t entry_pa_range_encoding(
    const struct RibonAarch64EntryState *entry) {
    uint64_t encoding = entry->current_el == 1u ?
        ((entry->tcr >> 32u) & 7u) : ((entry->tcr >> 16u) & 7u);
    /*
     * The bridge must not advertise a wider PA range than the firmware's
     * active regime. Encodings above 48-bit are deliberately clamped because
     * this v1 bridge accepts only addresses below 512 GiB.
     */
    return encoding <= 5u ? encoding : 5u;
}

int ribon_aarch64_prepare_post_exit_bridge(
    const struct RibonAarch64BridgeRequest *request,
    const struct RibonAarch64EntryState *entry,
    uint64_t stack_top,
    void (*continuation)(void *context),
    void *context,
    struct RibonAarch64PostExitTransition *out) {
    uint64_t *tables;
    uint16_t l1_table_page[512];
    uint32_t table_pages = 1u;
    uint64_t previous_end = 0u;
    if (out != 0) {
        *out = (struct RibonAarch64PostExitTransition){0};
    }
    if (request == 0 || entry == 0 || out == 0 || continuation == 0 ||
        context == 0 || request->size != sizeof(*request) ||
        request->abi_version != RIBON_AARCH64_ENTRY_STATE_ABI_VERSION ||
        request->ranges == 0 || request->range_count == 0u ||
        request->range_count > RIBON_AARCH64_BRIDGE_RANGE_LIMIT ||
        request->table_buffer == 0 ||
        request->table_buffer_size < RIBON_AARCH64_BRIDGE_TABLE_BYTES ||
        ((uint64_t)(uintptr_t)request->table_buffer & 4095u) != 0u ||
        entry->size != sizeof(*entry) ||
        entry->abi_version != RIBON_AARCH64_ENTRY_STATE_ABI_VERSION ||
        (entry->current_el != 1u && entry->current_el != 2u) ||
        stack_top == 0u || (stack_top & 15u) != 0u) {
        return RIBON_ARCH_OPERATION_BAD_ARGUMENT;
    }
    for (uint32_t index = 0u; index < 512u; ++index) {
        l1_table_page[index] = 0u;
    }
    for (uint32_t index = 0u; index < request->range_count; ++index) {
        const struct RibonAarch64BridgeRange *range = &request->ranges[index];
        const uint64_t end = range->base + range->length;
        if (!valid_kind(range->kind) || range->length == 0u ||
            (range->base & 4095u) != 0u || (range->length & 4095u) != 0u ||
            range->base > UINT64_MAX - range->length ||
            end > RIBON_AARCH64_ADDRESS_LIMIT ||
            (index != 0u && range->base < previous_end)) {
            return RIBON_ARCH_OPERATION_INVALID_PAYLOAD;
        }
        previous_end = end;
        for (uint64_t address = align_down(range->base, RIBON_AARCH64_L2_BLOCK);
             address < end; address += RIBON_AARCH64_L2_BLOCK) {
            const uint32_t l1 = (uint32_t)(address / RIBON_AARCH64_L1_SPAN);
            if (l1_table_page[l1] == 0u) {
                if (table_pages == 9u) {
                    return RIBON_ARCH_OPERATION_INVALID_PAYLOAD;
                }
                l1_table_page[l1] = (uint16_t)table_pages++;
            }
        }
    }
    tables = (uint64_t *)request->table_buffer;
    zero_tables(tables);
    for (uint32_t l1 = 0u; l1 < 512u; ++l1) {
        if (l1_table_page[l1] != 0u) {
            tables[l1] = bridge_table_descriptor(
                (uint64_t)(uintptr_t)request->table_buffer +
                (uint64_t)l1_table_page[l1] * RIBON_AARCH64_PAGE_SIZE);
        }
    }
    for (uint32_t index = 0u; index < request->range_count; ++index) {
        const struct RibonAarch64BridgeRange *range = &request->ranges[index];
        const uint64_t end = range->base + range->length;
        for (uint64_t address = align_down(range->base, RIBON_AARCH64_L2_BLOCK);
             address < end; address += RIBON_AARCH64_L2_BLOCK) {
            const uint32_t l1 = (uint32_t)(address / RIBON_AARCH64_L1_SPAN);
            const uint32_t l2 = (uint32_t)((address >> 21u) & 511u);
            uint64_t *l2_table = tables +
                (uint64_t)l1_table_page[l1] * 512u;
            const uint64_t descriptor =
                bridge_block_descriptor(address, range->kind);
            if (l2_table[l2] != 0u && l2_table[l2] != descriptor) {
                return RIBON_ARCH_OPERATION_INVALID_PAYLOAD;
            }
            l2_table[l2] = descriptor;
        }
    }
    out->translation_root = (uint64_t)(uintptr_t)request->table_buffer;
    out->tcr_el1 = UINT64_C(25) | (UINT64_C(25) << 16u) |
                   (UINT64_C(1) << 8u) | (UINT64_C(1) << 10u) |
                   (UINT64_C(3) << 12u) | (UINT64_C(1) << 23u) |
                   (entry_pa_range_encoding(entry) << 32u);
    out->mair_el1 = RIBON_AARCH64_MAIR_EL1_BRIDGE;
    out->sctlr_el1 = RIBON_AARCH64_SCTLR_EL1_BRIDGE;
    out->stack_top = stack_top;
    out->continuation = continuation;
    out->context = context;
    out->source_el = entry->current_el;
    out->mapped_l1_tables = table_pages - 1u;
    return RIBON_ARCH_OPERATION_OK;
}
