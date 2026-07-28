#include <Ribon/arch/entry.h>
#include <Ribon/arch/ops.h>
#include <Ribon/boot/image.h>
#include <Ribon/plugin/descriptor.h>

#define RIBON_X86_64_PAGE_SIZE 4096ull
#define RIBON_X86_64_ENTRIES 512u
#define RIBON_X86_64_LARGE_2M 0x200000ull
#define RIBON_X86_64_LARGE_1G 0x40000000ull
#define RIBON_X86_64_PTE_PRESENT (1ull << 0)
#define RIBON_X86_64_PTE_WRITE (1ull << 1)
#define RIBON_X86_64_PTE_LARGE (1ull << 7)
#define RIBON_X86_64_ADDR_MASK 0x000FFFFFFFFFF000ull
#define RIBON_X86_64_DIRECT_HIGH_TABLE_PAGES 8ull

static const struct RibonArchDescriptor x86_64_arch = {
    .size = sizeof(x86_64_arch),
    .abi_version = RIBON_ARCH_OPS_ABI_VERSION,
    .id = RIBON_ARCHITECTURE_X86_64,
    .canonical_name = "x86_64",
    .family_name = "x86",
    .word_bits = 64,
    .physical_address_bits = 52,
    .virtual_address_bits = 48,
    .page_size = 4096,
    .large_page_size = 2097152,
    .kernel_alignment = 2097152,
    .handoff_alignment = 4096,
    .boot_module_alignment = 4096,
    .endian = RIBON_ARCH_ENDIAN_LITTLE,
    .tier = RIBON_ARCH_TIER_PRIMARY,
};

/** @brief 선택된 x86_64 architecture descriptor를 반환한다. */
const struct RibonArchDescriptor *ribon_arch_selected(void) {
    return &x86_64_arch;
}

/** @brief Power-of-two 경계 아래의 address를 반환한다. */
static uint64_t align_down_u64(uint64_t value, uint64_t alignment) {
    return value & ~(alignment - 1u);
}

/** @brief Overflow 없이 power-of-two 경계 위 address를 계산한다. */
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

/** @brief Writable present page-table descriptor를 만든다. */
static uint64_t table_descriptor(uint64_t physical_address) {
    return (physical_address & RIBON_X86_64_ADDR_MASK) |
           RIBON_X86_64_PTE_PRESENT |
           RIBON_X86_64_PTE_WRITE;
}

/** @brief 2 MiB 또는 1 GiB large-page descriptor를 만든다. */
static uint64_t large_descriptor(uint64_t physical_address) {
    return table_descriptor(physical_address) | RIBON_X86_64_PTE_LARGE;
}

/** @brief Caller-owned page table entry를 allocation 없이 0으로 만든다. */
static void zero_u64_table(uint64_t *table, uint64_t entries) {
    for (uint64_t index = 0; index < entries; ++index) {
        table[index] = 0;
    }
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

/** @brief x86_64 direct-high bridge가 요구하는 고정 table page 수를 반환한다. */
uint64_t ribon_arch_direct_high_page_table_pages(const struct RibonLoadedPayload *payload) {
    if (payload == 0 ||
        (payload->load_plan_flags & RIBON_LOAD_PLAN_DIRECT_HIGH_ENTRY_CANDIDATE) == 0u ||
        payload->high_entry_virtual_address == 0u ||
        payload->linked_virtual_end <= payload->high_entry_virtual_address ||
        payload->segment_count == 0u ||
        payload->segments == 0) {
        return 0;
    }
    return RIBON_X86_64_DIRECT_HIGH_TABLE_PAGES;
}

int ribon_arch_prepare_direct_high_entry(
    const struct RibonLoadedPayload *payload,
    uint64_t page_table_physical_address,
    void *page_table_buffer,
    uint64_t page_table_size,
    struct RibonArchDirectHighHandoff *out) {
    uint64_t high_start;
    uint64_t high_end;
    uint64_t high_entry_runtime = 0;
    uint64_t high_load_start;
    uint64_t high_load_end;
    uint64_t *tables;
    uint64_t *pml4;
    uint64_t *low_pdpt;
    uint64_t *high_pdpt;
    uint64_t *high_pd;
    uint64_t low_pd_physical[4];
    uint32_t pml4_index;
    uint32_t pdpt_index;
    uint32_t pd_index;

    if (payload == 0 || page_table_buffer == 0 || out == 0) {
        return RIBON_ARCH_DIRECT_HIGH_BAD_ARGUMENT;
    }
    *out = (struct RibonArchDirectHighHandoff){0};
    if (ribon_arch_direct_high_page_table_pages(payload) == 0u ||
        page_table_physical_address == 0u ||
        (page_table_physical_address & (RIBON_X86_64_PAGE_SIZE - 1u)) != 0u ||
        page_table_size < RIBON_X86_64_DIRECT_HIGH_TABLE_PAGES * RIBON_X86_64_PAGE_SIZE ||
        !find_runtime_for_virtual_entry(payload, payload->high_entry_virtual_address, &high_entry_runtime)) {
        return RIBON_ARCH_DIRECT_HIGH_BAD_LAYOUT;
    }

    high_start = align_down_u64(payload->high_entry_virtual_address, RIBON_X86_64_LARGE_2M);
    if (!align_up_u64(payload->linked_virtual_end, RIBON_X86_64_LARGE_2M, &high_end) ||
        high_end <= high_start ||
        (((high_end - 1u) >> 30u) & 0x1ffu) != ((high_start >> 30u) & 0x1ffu)) {
        return RIBON_ARCH_DIRECT_HIGH_BAD_LAYOUT;
    }
    if (payload->high_entry_virtual_address < high_start ||
        high_entry_runtime < payload->high_entry_virtual_address - high_start) {
        return RIBON_ARCH_DIRECT_HIGH_BAD_LAYOUT;
    }
    high_load_start = high_entry_runtime - (payload->high_entry_virtual_address - high_start);
    if ((high_load_start & (RIBON_X86_64_LARGE_2M - 1u)) != 0u ||
        high_load_start > UINT64_MAX - (high_end - high_start)) {
        return RIBON_ARCH_DIRECT_HIGH_BAD_LAYOUT;
    }
    high_load_end = high_load_start + (high_end - high_start);

    tables = (uint64_t *)page_table_buffer;
    zero_u64_table(tables, RIBON_X86_64_DIRECT_HIGH_TABLE_PAGES * RIBON_X86_64_ENTRIES);

    pml4 = tables;
    low_pdpt = tables + RIBON_X86_64_ENTRIES;
    high_pdpt = tables + (RIBON_X86_64_ENTRIES * 6u);
    high_pd = tables + (RIBON_X86_64_ENTRIES * 7u);
    low_pd_physical[0] = page_table_physical_address + RIBON_X86_64_PAGE_SIZE * 2u;
    low_pd_physical[1] = page_table_physical_address + RIBON_X86_64_PAGE_SIZE * 3u;
    low_pd_physical[2] = page_table_physical_address + RIBON_X86_64_PAGE_SIZE * 4u;
    low_pd_physical[3] = page_table_physical_address + RIBON_X86_64_PAGE_SIZE * 5u;

    pml4[0] = table_descriptor(page_table_physical_address + RIBON_X86_64_PAGE_SIZE);
    for (uint32_t gb = 0; gb < 4u; ++gb) {
        uint64_t *pd = tables + (RIBON_X86_64_ENTRIES * (2u + gb));
        low_pdpt[gb] = table_descriptor(low_pd_physical[gb]);
        for (uint32_t index = 0; index < RIBON_X86_64_ENTRIES; ++index) {
            pd[index] =
                large_descriptor(((uint64_t)gb * RIBON_X86_64_LARGE_1G) +
                                 ((uint64_t)index * RIBON_X86_64_LARGE_2M));
        }
    }

    pml4_index = (uint32_t)((high_start >> 39u) & 0x1ffu);
    pdpt_index = (uint32_t)((high_start >> 30u) & 0x1ffu);
    pd_index = (uint32_t)((high_start >> 21u) & 0x1ffu);
    pml4[pml4_index] = table_descriptor(page_table_physical_address + RIBON_X86_64_PAGE_SIZE * 6u);
    high_pdpt[pdpt_index] = table_descriptor(page_table_physical_address + RIBON_X86_64_PAGE_SIZE * 7u);
    for (uint64_t va = high_start; va < high_end; va += RIBON_X86_64_LARGE_2M) {
        if (pd_index >= RIBON_X86_64_ENTRIES) {
            return RIBON_ARCH_DIRECT_HIGH_OUT_OF_CAPACITY;
        }
        high_pd[pd_index++] = large_descriptor(high_load_start + (va - high_start));
    }

    out->entry = payload->high_entry_virtual_address;
    out->translation_root = page_table_physical_address;
    out->high_entry_load = high_entry_runtime;
    out->high_vaddr_start = high_start;
    out->high_vaddr_end = high_end;
    out->high_load_start = high_load_start;
    out->high_load_end = high_load_end;
    return RIBON_ARCH_DIRECT_HIGH_OK;
}

_Noreturn void ribon_arch_transfer_prepared(
    const struct RibonPreparedEntry *prepared) {
#if defined(__x86_64__) || defined(_M_X64)
    const uint64_t entry = prepared->invocation.entry_address;
    const uint64_t argument0 = prepared->invocation.arguments[0];
    const uint64_t argument1 = prepared->invocation.arguments[1];
    const uint64_t argument2 = prepared->invocation.arguments[2];
    const uint64_t argument3 = prepared->invocation.arguments[3];
    if (prepared->translation_root != 0u &&
        prepared->invocation.translation ==
            RIBON_ENTRY_TRANSLATION_DIRECT_HIGH_BRIDGE) {
        __asm__ __volatile__(
            "cli\n"
            "cld\n"
            "movq %5, %%cr3\n"
            "movq %0, %%rdi\n"
            "movq %1, %%rsi\n"
            "movq %2, %%rdx\n"
            "movq %3, %%rcx\n"
            "jmpq *%4\n"
            :
            : "r"(argument0), "r"(argument1), "r"(argument2), "r"(argument3),
              "r"(entry), "r"(prepared->translation_root)
            : "rdi", "rsi", "rdx", "rcx", "memory");
    }
    __asm__ __volatile__(
        "cli\n"
        "cld\n"
        "movq %0, %%rdi\n"
        "movq %1, %%rsi\n"
        "movq %2, %%rdx\n"
        "movq %3, %%rcx\n"
        "jmpq *%4\n"
        :
        : "r"(argument0), "r"(argument1), "r"(argument2), "r"(argument3),
          "r"(entry)
        : "rdi", "rsi", "rdx", "rcx", "memory");
#else
    (void)prepared;
#endif
    for (;;) {
#if defined(__x86_64__) || defined(_M_X64)
        __asm__ __volatile__("hlt");
#endif
    }
}

/** @brief x86의 coherent instruction view 앞에 compiler ordering을 고정한다. */
static int x86_64_cache_sync(uint64_t address, uint64_t size) {
    (void)address;
    if (size == 0u) {
        return RIBON_ARCH_OPERATION_BAD_ARGUMENT;
    }
#if defined(__x86_64__) || defined(_M_X64)
    __asm__ __volatile__("mfence" ::: "memory");
#endif
    return RIBON_ARCH_OPERATION_OK;
}

/** @brief x86_64 CPU를 terminal halt loop로 전환한다. */
static _Noreturn void x86_64_halt(void) {
    for (;;) {
#if defined(__x86_64__) || defined(_M_X64)
        __asm__ __volatile__("cli; hlt");
#endif
    }
}

/** @brief x86 TSC를 allocation 없이 읽는다. */
static uint64_t x86_64_monotonic_counter(void) {
#if defined(__x86_64__) || defined(_M_X64)
    uint32_t low;
    uint32_t high;
    __asm__ __volatile__("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32u) | low;
#else
    return 1u;
#endif
}

static const struct RibonArchOps x86_64_ops = {
    .size = sizeof(x86_64_ops),
    .abi_version = RIBON_ARCH_OPS_ABI_VERSION,
    .capabilities =
        RIBON_ARCH_CAP_VALIDATE_PAYLOAD |
        RIBON_ARCH_CAP_CACHE_SYNC |
        RIBON_ARCH_CAP_DIRECT_HIGH_ENTRY |
        RIBON_ARCH_CAP_ENTRY_BRIDGE |
        RIBON_ARCH_CAP_HALT |
        RIBON_ARCH_CAP_MONOTONIC_COUNTER,
    .descriptor = &x86_64_arch,
    .validate_payload = ribon_arch_validate_loaded_payload,
    .cache_sync = x86_64_cache_sync,
    .normalize_privilege = 0,
    .direct_high_page_table_pages = ribon_arch_direct_high_page_table_pages,
    .prepare_direct_high_entry = ribon_arch_prepare_direct_high_entry,
    .prepare_entry = ribon_arch_prepare_entry,
    .transfer_prepared = ribon_arch_transfer_prepared,
    .halt = x86_64_halt,
    .reset = 0,
    .monotonic_counter = x86_64_monotonic_counter,
};

/** @brief 선택된 x86_64 architecture operation table을 반환한다. */
const struct RibonArchOps *ribon_arch_selected_ops(void) {
    return &x86_64_ops;
}

/** @brief Selected x86_64 architecture plugin descriptor다. */
const struct RibonPluginDescriptor ribon_arch_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_arch_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_ARCHITECTURE,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION,
    .id = "arch.x86_64",
    .provides = RIBON_CAP_ARCHITECTURE,
    .architecture_mask = RIBON_ARCH_MASK_X86_64,
    .environment_mask = RIBON_ENV_MASK_ALL,
    .personality_mask = RIBON_PERSONALITY_MASK_ALL,
    .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 4096u,
    .input_budget = 4096u,
    .output_budget = 4096u,
    .deadline_ms = 30000u,
    .operations = &x86_64_ops,
    .operations_size = sizeof(x86_64_ops),
    .operations_abi = RIBON_ARCH_OPS_ABI_VERSION,
    .validate_operations = ribon_arch_plugin_operations_are_valid,
};
