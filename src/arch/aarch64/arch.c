#include <Ribon/arch/entry.h>
#include <Ribon/arch/ops.h>
#include <Ribon/boot/image.h>
#include <Ribon/plugin/descriptor.h>

#define RIBON_AARCH64_PAGE_SIZE 4096ull
#define RIBON_AARCH64_ENTRIES 512u
#define RIBON_AARCH64_L1_BLOCK 0x40000000ull
#define RIBON_AARCH64_L2_BLOCK 0x200000ull
#define RIBON_AARCH64_DESC_VALID (1ull << 0)
#define RIBON_AARCH64_DESC_TABLE (1ull << 1)
#define RIBON_AARCH64_DESC_AF (1ull << 10)
#define RIBON_AARCH64_DESC_INNER_SHAREABLE (3ull << 8)
#define RIBON_AARCH64_ATTR_NORMAL (0ull << 2)
#define RIBON_AARCH64_ADDR_MASK 0x0000FFFFFFFFF000ull
#define RIBON_AARCH64_MAIR_VALUE 0x00000000000004FFull
#define RIBON_AARCH64_TCR_BASE \
    ((uint64_t)16u | (16ull << 16u) | \
     (1ull << 8u) | (1ull << 10u) | (3ull << 12u) | \
     (1ull << 24u) | (1ull << 26u) | (3ull << 28u) | \
     (2ull << 30u))
#define RIBON_AARCH64_TCR_IPS_SHIFT 32u
#define RIBON_AARCH64_HIGH_L3_TABLES 8ull
#define RIBON_AARCH64_DIRECT_HIGH_TABLE_PAGES (5ull + RIBON_AARCH64_HIGH_L3_TABLES)

static const struct RibonArchDescriptor aarch64_arch = {
    .size = sizeof(aarch64_arch),
    .abi_version = RIBON_ARCH_OPS_ABI_VERSION,
    .id = RIBON_ARCHITECTURE_AARCH64,
    .canonical_name = "aarch64",
    .family_name = "aarch",
    .word_bits = 64,
    .physical_address_bits = 48,
    .virtual_address_bits = 48,
    .page_size = 4096,
    .large_page_size = 2097152,
    .kernel_alignment = 2097152,
    .handoff_alignment = 4096,
    .boot_module_alignment = 4096,
    .endian = RIBON_ARCH_ENDIAN_LITTLE,
    .tier = RIBON_ARCH_TIER_PRIMARY,
};

/** @brief 선택된 AArch64 architecture descriptor를 반환한다. */
const struct RibonArchDescriptor *ribon_arch_selected(void) {
    return &aarch64_arch;
}

static uint64_t align_down_u64(uint64_t value, uint64_t alignment) {
    return value & ~(alignment - 1u);
}

static int align_up_u64(uint64_t value, uint64_t alignment, uint64_t *out) {
    if (out == 0 || alignment == 0u || (alignment & (alignment - 1u)) != 0u) {
        return 0;
    }
    if (value > UINT64_MAX - (alignment - 1u)) {
        return 0;
    }
    *out = align_down_u64(value + alignment - 1u, alignment);
    return 1;
}

static uint64_t descriptor_address(uint64_t physical_address) {
    return physical_address & RIBON_AARCH64_ADDR_MASK;
}

static uint64_t table_descriptor(uint64_t physical_address) {
    return descriptor_address(physical_address) |
           RIBON_AARCH64_DESC_VALID |
           RIBON_AARCH64_DESC_TABLE;
}

static uint64_t block_descriptor(uint64_t physical_address) {
    return descriptor_address(physical_address) |
           RIBON_AARCH64_ATTR_NORMAL |
           RIBON_AARCH64_DESC_INNER_SHAREABLE |
           RIBON_AARCH64_DESC_AF |
           RIBON_AARCH64_DESC_VALID;
}

static uint64_t page_descriptor(uint64_t physical_address) {
    return block_descriptor(physical_address) | RIBON_AARCH64_DESC_TABLE;
}

static void zero_u64_table(uint64_t *table, uint64_t entries) {
    for (uint64_t index = 0; index < entries; ++index) {
        table[index] = 0;
    }
}

/** @brief AArch64 data/instruction view를 full-system 경계에서 동기화한다. */
static int aarch64_cache_sync(uint64_t address, uint64_t size) {
    (void)address;
    if (size == 0u) {
        return RIBON_ARCH_OPERATION_BAD_ARGUMENT;
    }
#if defined(__aarch64__)
    __asm__ __volatile__(
        "dsb sy\n"
        "ic iallu\n"
        "dsb sy\n"
        "isb\n"
        :
        :
        : "memory");
#endif
    return RIBON_ARCH_OPERATION_OK;
}

/** @brief AArch64 CPU를 terminal wait loop로 전환한다. */
static _Noreturn void aarch64_halt(void) {
    for (;;) {
#if defined(__aarch64__)
        __asm__ __volatile__("msr daifset, #0xf; wfe");
#endif
    }
}

static const struct RibonArchOps aarch64_ops = {
    .size = sizeof(aarch64_ops),
    .abi_version = RIBON_ARCH_OPS_ABI_VERSION,
    .capabilities =
        RIBON_ARCH_CAP_VALIDATE_PAYLOAD |
        RIBON_ARCH_CAP_CACHE_SYNC |
        RIBON_ARCH_CAP_DIRECT_HIGH_ENTRY |
        RIBON_ARCH_CAP_ENTRY_BRIDGE |
        RIBON_ARCH_CAP_HALT,
    .descriptor = &aarch64_arch,
    .validate_payload = ribon_arch_validate_loaded_payload,
    .cache_sync = aarch64_cache_sync,
    .normalize_privilege = 0,
    .direct_high_page_table_pages = ribon_arch_direct_high_page_table_pages,
    .prepare_direct_high_entry = ribon_arch_prepare_direct_high_entry,
    .enter_kernel = ribon_arch_enter_kernel,
    .halt = aarch64_halt,
    .reset = 0,
};

/** @brief 선택된 AArch64 architecture operation table을 반환한다. */
const struct RibonArchOps *ribon_arch_selected_ops(void) {
    return &aarch64_ops;
}

static int find_runtime_for_virtual_entry(
    const struct RibonLoadedPayload *payload,
    uint64_t virtual_entry,
    uint64_t *runtime_entry_out) {
    if (payload == 0 || payload->segments == 0 || runtime_entry_out == 0) {
        return 0;
    }
    for (uint32_t index = 0; index < payload->segment_count; ++index) {
        const struct RibonLoadSegment *segment = &payload->segments[index];
        uint64_t virtual_end;
        if (segment->memory_size == 0u ||
            segment->virtual_address > UINT64_MAX - segment->memory_size) {
            return 0;
        }
        virtual_end = segment->virtual_address + segment->memory_size;
        if (virtual_entry >= segment->virtual_address && virtual_entry < virtual_end) {
            const uint64_t offset = virtual_entry - segment->virtual_address;
            if (segment->runtime_address > UINT64_MAX - offset) {
                return 0;
            }
            *runtime_entry_out = segment->runtime_address + offset;
            return 1;
        }
    }
    return 0;
}

uint64_t ribon_arch_direct_high_page_table_pages(const struct RibonLoadedPayload *payload) {
    if (payload == 0 ||
        (payload->load_plan_flags & RIBON_LOAD_PLAN_DIRECT_HIGH_ENTRY_CANDIDATE) == 0u ||
        payload->high_entry_virtual_address == 0u ||
        payload->high_entry_load_address == 0u ||
        payload->linked_virtual_end <= payload->high_entry_virtual_address ||
        payload->segment_count == 0u ||
        payload->segments == 0) {
        return 0;
    }
    return RIBON_AARCH64_DIRECT_HIGH_TABLE_PAGES;
}

int ribon_arch_prepare_direct_high_entry(
    const struct RibonLoadedPayload *payload,
    uint64_t page_table_physical_address,
    void *page_table_buffer,
    uint64_t page_table_size,
    struct RibonArchDirectHighHandoff *out) {
    uint64_t high_start;
    uint64_t high_end;
    uint64_t high_l2_start;
    uint64_t high_l2_end;
    uint64_t high_entry_runtime = 0;
    uint64_t high_load_start;
    uint64_t l2_span;
    uint64_t *tables;
    uint64_t *identity_l0;
    uint64_t *identity_l1;
    uint64_t *kernel_l0;
    uint64_t *kernel_l1;
    uint64_t *kernel_l2;
    uint64_t *kernel_l3;
    uint32_t l0_index;
    uint32_t l1_index;
    uint32_t start_l2;

    if (payload == 0 || page_table_buffer == 0 || out == 0) {
        return RIBON_ARCH_DIRECT_HIGH_BAD_ARGUMENT;
    }
    *out = (struct RibonArchDirectHighHandoff){0};
    if (ribon_arch_direct_high_page_table_pages(payload) == 0u ||
        page_table_physical_address == 0u ||
        (page_table_physical_address & (RIBON_AARCH64_PAGE_SIZE - 1u)) != 0u ||
        page_table_size < RIBON_AARCH64_DIRECT_HIGH_TABLE_PAGES * RIBON_AARCH64_PAGE_SIZE ||
        !find_runtime_for_virtual_entry(payload, payload->high_entry_virtual_address, &high_entry_runtime)) {
        return RIBON_ARCH_DIRECT_HIGH_BAD_LAYOUT;
    }

    high_start = align_down_u64(payload->high_entry_virtual_address, RIBON_AARCH64_PAGE_SIZE);
    if (!align_up_u64(payload->linked_virtual_end, RIBON_AARCH64_PAGE_SIZE, &high_end) ||
        high_end <= high_start ||
        payload->high_entry_virtual_address < high_start ||
        high_entry_runtime < payload->high_entry_virtual_address - high_start) {
        return RIBON_ARCH_DIRECT_HIGH_BAD_LAYOUT;
    }
    high_load_start = high_entry_runtime - (payload->high_entry_virtual_address - high_start);
    if ((high_load_start & (RIBON_AARCH64_PAGE_SIZE - 1u)) != 0u ||
        high_load_start > UINT64_MAX - (high_end - high_start)) {
        return RIBON_ARCH_DIRECT_HIGH_BAD_LAYOUT;
    }

    high_l2_start = align_down_u64(high_start, RIBON_AARCH64_L2_BLOCK);
    if (!align_up_u64(high_end, RIBON_AARCH64_L2_BLOCK, &high_l2_end) ||
        high_l2_end <= high_l2_start ||
        (((high_l2_end - 1u) >> 30u) & 0x1ffu) != ((high_l2_start >> 30u) & 0x1ffu)) {
        return RIBON_ARCH_DIRECT_HIGH_BAD_LAYOUT;
    }
    l2_span = (high_l2_end - high_l2_start) >> 21u;
    if (l2_span == 0u || l2_span > RIBON_AARCH64_HIGH_L3_TABLES) {
        return RIBON_ARCH_DIRECT_HIGH_OUT_OF_CAPACITY;
    }

    tables = (uint64_t *)page_table_buffer;
    zero_u64_table(tables, RIBON_AARCH64_DIRECT_HIGH_TABLE_PAGES * RIBON_AARCH64_ENTRIES);

    identity_l0 = tables;
    identity_l1 = tables + RIBON_AARCH64_ENTRIES;
    kernel_l0 = tables + RIBON_AARCH64_ENTRIES * 2u;
    kernel_l1 = tables + RIBON_AARCH64_ENTRIES * 3u;
    kernel_l2 = tables + RIBON_AARCH64_ENTRIES * 4u;
    kernel_l3 = tables + RIBON_AARCH64_ENTRIES * 5u;

    identity_l0[0] = table_descriptor(page_table_physical_address + RIBON_AARCH64_PAGE_SIZE);
    for (uint32_t index = 0; index < 4u; ++index) {
        identity_l1[index] = block_descriptor((uint64_t)index * RIBON_AARCH64_L1_BLOCK);
    }

    l0_index = (uint32_t)((high_start >> 39u) & 0x1ffu);
    l1_index = (uint32_t)((high_start >> 30u) & 0x1ffu);
    start_l2 = (uint32_t)((high_l2_start >> 21u) & 0x1ffu);
    kernel_l0[l0_index] = table_descriptor(page_table_physical_address + RIBON_AARCH64_PAGE_SIZE * 3u);
    kernel_l1[l1_index] = table_descriptor(page_table_physical_address + RIBON_AARCH64_PAGE_SIZE * 4u);
    for (uint64_t index = 0; index < l2_span; ++index) {
        kernel_l2[start_l2 + index] =
            table_descriptor(page_table_physical_address + RIBON_AARCH64_PAGE_SIZE * (5u + index));
    }
    for (uint64_t va = high_start; va < high_end; va += RIBON_AARCH64_PAGE_SIZE) {
        const uint64_t physical = high_load_start + (va - high_start);
        const uint32_t l2_index = (uint32_t)((va >> 21u) & 0x1ffu);
        const uint32_t l3_index = (uint32_t)((va >> 12u) & 0x1ffu);
        const uint32_t table_index = l2_index - start_l2;
        kernel_l3[(uint64_t)table_index * RIBON_AARCH64_ENTRIES + l3_index] =
            page_descriptor(physical);
    }

    out->entry = payload->high_entry_virtual_address;
    out->entry_flags =
        RIBON_KERNEL_ENTRY_FLAG_RPH1 |
        RIBON_KERNEL_ENTRY_FLAG_ENTERED_HIGH |
        RIBON_KERNEL_ENTRY_FLAG_DIRECT_HIGH;
    out->bootstrap0 = page_table_physical_address;
    out->high_entry_load = high_entry_runtime;
    out->high_vaddr_start = high_start;
    out->high_vaddr_end = high_end;
    out->high_load_start = high_load_start;
    out->high_load_end = high_load_start + (high_end - high_start);
    return RIBON_ARCH_DIRECT_HIGH_OK;
}

#if defined(__aarch64__)
static uint64_t read_id_aa64mmfr0_el1(void) {
    uint64_t value = 0;
    __asm__ __volatile__("mrs %0, id_aa64mmfr0_el1" : "=r"(value));
    return value;
}

static uint64_t pa_bits_to_tcr_ips(uint32_t bits) {
    if (bits <= 32u) {
        return 0u;
    }
    if (bits <= 36u) {
        return 1u;
    }
    if (bits <= 40u) {
        return 2u;
    }
    if (bits <= 42u) {
        return 3u;
    }
    if (bits <= 44u) {
        return 4u;
    }
    return 5u;
}

static uint32_t current_pa_bits(void) {
    switch (read_id_aa64mmfr0_el1() & 0xFu) {
    case 0u:
        return 32u;
    case 1u:
        return 36u;
    case 2u:
        return 40u;
    case 3u:
        return 42u;
    case 4u:
        return 44u;
    default:
        return 48u;
    }
}
#endif

_Noreturn void ribon_arch_enter_kernel(
    uint64_t entry,
    uint64_t handoff,
    uint64_t entry_flags,
    uint64_t bootstrap0) {
#if defined(__aarch64__)
    if (bootstrap0 != 0u &&
        (entry_flags & RIBON_KERNEL_ENTRY_FLAG_DIRECT_HIGH) != 0u) {
        const uint64_t ttbr1 = bootstrap0 + RIBON_AARCH64_PAGE_SIZE * 2u;
        const uint64_t tcr =
            RIBON_AARCH64_TCR_BASE |
            (pa_bits_to_tcr_ips(current_pa_bits()) << RIBON_AARCH64_TCR_IPS_SHIFT);
        __asm__ __volatile__(
            "msr daifset, #0xf\n"
            "dsb sy\n"
            "mov x0, %0\n"
            "mov x1, %1\n"
            "mov x16, %2\n"
            "msr mair_el1, %6\n"
            "msr tcr_el1, %5\n"
            "msr ttbr0_el1, %3\n"
            "msr ttbr1_el1, %4\n"
            "isb\n"
            "tlbi vmalle1\n"
            "dsb sy\n"
            "isb\n"
            "mrs x9, sctlr_el1\n"
            "orr x9, x9, #(1 << 0)\n"
            "orr x9, x9, #(1 << 2)\n"
            "orr x9, x9, #(1 << 12)\n"
            "msr sctlr_el1, x9\n"
            "isb\n"
            "br x16\n"
            :
            : "r"(handoff), "r"(entry_flags), "r"(entry), "r"(bootstrap0),
              "r"(ttbr1), "r"(tcr), "r"(RIBON_AARCH64_MAIR_VALUE)
            : "x0", "x1", "x9", "x16", "memory");
    }
    __asm__ __volatile__(
        "msr daifset, #0xf\n"
        "mov x16, %2\n"
        "mov x0, %0\n"
        "mov x1, %1\n"
        "dsb sy\n"
        "ic ialluis\n"
        "dsb sy\n"
        "isb\n"
        "br x16\n"
        :
        : "r"(handoff), "r"(entry_flags), "r"(entry)
        : "x0", "x1", "x16", "memory");
#else
    (void)entry;
    (void)handoff;
    (void)entry_flags;
    (void)bootstrap0;
#endif
    for (;;) {
#if defined(__aarch64__)
        __asm__ __volatile__("wfe");
#endif
    }
}

/** @brief Selected AArch64 architecture plugin descriptor다. */
const struct RibonPluginDescriptor ribon_arch_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_arch_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_ARCHITECTURE,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION,
    .id = "arch.aarch64",
    .provides = RIBON_CAP_ARCHITECTURE,
    .architecture_mask = RIBON_ARCH_MASK_AARCH64,
    .environment_mask = RIBON_ENV_MASK_ALL,
    .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 4096u,
    .input_budget = 4096u,
    .output_budget = 4096u,
    .deadline_ms = 30000u,
    .operations = &aarch64_ops,
    .operations_size = sizeof(aarch64_ops),
    .operations_abi = RIBON_ARCH_OPS_ABI_VERSION,
    .validate_operations = ribon_arch_plugin_operations_are_valid,
};
